// Hooks inside game_sp.xdl (compiled into the codclassic_game_sp module library: the module's
// symbols are made local when it is linked into the NRO, so a hook has to live in the module).
//
// Path disconnects (log 040, loading dawnville, the fourth mission):
//   "COM ERROR - Max number of disconnected paths exceeded"
// Entities with "disconnectpaths" (tanks, script brush models) cut every path link whose sight trace
// they block. Each cut takes an entry from a pool of 2048 (a doubly linked free list of 8-byte
// entries: +0 link index, +2 node, +4 prev, +6 next; entry 0 is the list head), allocated and
// initialised by sub_8708F348; sub_870917B0 takes one, and fails with that error when the list is
// empty; sub_87091AA0 does it for one entity over all nodes and links.
//
// The pool is made four times larger. Two functions initialise it, and both allocate it only
// when the pointer is still 0: sub_8708F348 and an inlined copy at the start of sub_870927D8 (the
// map's path setup), each with 2048 hardcoded. So the hooks allocate it first, 8192 entries, the
// originals then build their 2048-entry free list in it, and when that list runs out the hook on
// sub_870917B0 links the next 2048 spare entries into it (same layout: head.next = first, entries
// chained, last.next = 0, head.prev = last). Cuts are logged per entity, so the next logs show
// whether dawnville really needs more than 2048 or a trace is wrong.

#include "codclassic_pch.h"

#include <algorithm>

#include <rex/hook.h>
#include <rex/logging.h>

extern "C" REX_FUNC(__imp__sub_870621A8);  // trap allocation (syscall 14), r3 = size
extern "C" REX_FUNC(__imp__sub_8708F348);
extern "C" REX_FUNC(__imp__sub_870927D8);
extern "C" REX_FUNC(__imp__sub_870917B0);
extern "C" REX_FUNC(__imp__sub_87091AA0);

namespace {
// lis r11,-30929 ; addi r31,r11,21856  ->  0x872F5560; the pool pointer is at +4000.
constexpr uint32_t kPoolPtr = 0x872F5560u + 4000u;
constexpr uint32_t kOriginalEntries = 2048;
constexpr uint32_t kEntries = 8192;
uint32_t g_pool = 0;  // the pool this file allocated (kEntries entries)
uint32_t g_cap = 0;   // entries the free list has been given so far
uint32_t g_cuts = 0;

// Before either original initialiser: allocate the large pool if there is none yet.
void PreparePool(PPCContext& ctx, uint8_t* base) {
  uint32_t pool = REX_LOAD_U32(kPoolPtr);
  if (pool == 0) {
    const PPCContext saved = ctx;
    ctx.r3.u64 = kEntries * 8;
    __imp__sub_870621A8(ctx, base);
    pool = ctx.r3.u32;
    ctx = saved;
    REX_STORE_U32(kPoolPtr, pool);
    g_pool = pool;
    REXLOG_INFO("codclassic: path disconnect pool {} entries at {:08X}", kEntries, pool);
  }
  // The original builds a free list of the first 2048 entries.
  g_cap = kOriginalEntries;
  g_cuts = 0;
}
}  // namespace

// Links entries [g_cap, kEntries) into the free list right after the head, whatever state the list
// is in. The save loader (sub_8704B750, G_LoadGame) takes entries with its own inlined copy of
// sub_870917B0 that does not go through the hook and does not check for an empty list: restoring
// dawnville's 3500 cuts from a save emptied the 2048-entry list and then took the head itself,
// corrupting the list (second death: crash reading 0x0700B04C or a hang, logs 048/049). So the whole
// pool is handed to the list as soon as an initialiser returns.
void SpliceRemaining(uint8_t* base, uint32_t pool) {
  if (pool == 0 || pool != g_pool || g_cap >= kEntries) {
    return;
  }
  const uint32_t first = g_cap;
  const uint32_t last = kEntries - 1;
  const uint32_t old_next = REX_LOAD_U16(pool + 6);
  for (uint32_t i = first; i <= last; ++i) {
    const uint32_t e = pool + 8 * i;
    REX_STORE_U16(e + 4, static_cast<uint16_t>(i == first ? 0 : i - 1));
    REX_STORE_U16(e + 6, static_cast<uint16_t>(i == last ? old_next : i + 1));
  }
  REX_STORE_U16(pool + 8 * old_next + 4, static_cast<uint16_t>(last));  // old_next may be 0 (head)
  REX_STORE_U16(pool + 6, static_cast<uint16_t>(first));
  g_cap = kEntries;
}

REX_HOOK_RAW(sub_8708F348) {
  PreparePool(ctx, base);
  __imp__sub_8708F348(ctx, base);
  SpliceRemaining(base, REX_LOAD_U32(kPoolPtr));
}

REX_HOOK_RAW(sub_870927D8) {
  PreparePool(ctx, base);
  __imp__sub_870927D8(ctx, base);
  SpliceRemaining(base, REX_LOAD_U32(kPoolPtr));
}

REX_HOOK_RAW(sub_870917B0) {
  const uint32_t pool = REX_LOAD_U32(kPoolPtr);
  if (pool != 0 && pool == g_pool && REX_LOAD_U16(pool + 6) == 0 && g_cap < kEntries) {
    // Ran out while an initialiser is still running (dawnville cuts 3500 links during map setup):
    // hand over the rest now.
    SpliceRemaining(base, pool);
    REXLOG_WARN("codclassic: path disconnect pool extended to {} entries ({} cuts so far)", g_cap,
                g_cuts);
  }
  ++g_cuts;
  __imp__sub_870917B0(ctx, base);
}

REX_HOOK_RAW(sub_87091AA0) {
  const uint32_t ent = ctx.r3.u32;
  const uint32_t before = g_cuts;
  __imp__sub_87091AA0(ctx, base);
  if (g_cuts - before >= 64) {
    REXLOG_INFO("codclassic: gentity {:08X} disconnected {} path links (total {})", ent,
                g_cuts - before, g_cuts);
  }
}
