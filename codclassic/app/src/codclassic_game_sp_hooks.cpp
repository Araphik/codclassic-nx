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

#include <cstring>
#include <set>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

extern "C" REX_FUNC(__imp__sub_870621A8);  // trap allocation (syscall 14), r3 = size
extern "C" REX_FUNC(__imp__sub_8708F348);
extern "C" REX_FUNC(__imp__sub_870927D8);
extern "C" REX_FUNC(__imp__sub_870917B0);
extern "C" REX_FUNC(__imp__sub_87091AA0);
extern "C" REX_FUNC(__imp__sub_87091928);  // ConnectPaths(ent): undoes that entity's cuts
extern "C" REX_FUNC(__imp__sub_8704B750);  // save loader for one entity's cut chain

REXCVAR_DEFINE_INT32(codclassic_path_cut_limit, 400, "Call of Duty Classic",
                     "Undo the path cuts of an entity that cuts more links than this (0 = no limit)");

namespace {
// lis r11,-30929 ; addi r31,r11,21856  ->  0x872F5560; the pool pointer is at +4000.
constexpr uint32_t kPoolPtr = 0x872F5560u + 4000u;
constexpr uint32_t kOriginalEntries = 2048;
constexpr uint32_t kEntries = 8192;
uint32_t g_pool = 0;  // the pool this file allocated (kEntries entries)
uint32_t g_cap = 0;   // entries the free list has been given so far
uint32_t g_cuts = 0;
std::set<uint32_t> g_neutral;  // gentities whose cuts are neutralised (see sub_87091AA0)

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
  g_neutral.clear();
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

// Path cuts per entity. Dawnville cuts about 3500 links where the 360's pool holds 2048, and two
// entities alone cut 500 and 1104. With that many links gone the friendlies cannot walk to the
// French car at the end of the level: getToNode() loops until Foley, Moody and Elder reach their
// nodes, the car never arrives and the "get in the car" trigger never appears (softlock).
//
// Until the cause of the over-cutting is found, the cuts of an entity that cuts more than
// codclassic_path_cut_limit links are neutralised: its chain of pool entries is kept exactly as the
// game built it (the save game stores the pool entries by index and the loader expects the same
// chains, so removing them would break every save), but the "disconnected" counter of each of its
// links (u8 at link + 2) is decremented, so the AI can use those links again. The counter goes back
// up when the game reconnects the entity (sub_87091928), and the save loader (sub_8704B750), which
// increments the counters of the chains it restores, is followed by the same decrement.
namespace {
constexpr uint32_t kNodes = 0x8710B470;  // path nodes, 116 bytes each (sub_8708D930)

// Adds delta to the disconnect counter of every link cut by `ent`.
void AdjustLinks(uint8_t* base, uint32_t ent, int delta) {
  const uint32_t pool = REX_LOAD_U32(kPoolPtr);
  const uint32_t head = REX_LOAD_U16(ent + 698);
  if (pool == 0 || head == 0) {
    return;
  }
  uint32_t e = head;
  for (uint32_t guard = 0; guard < kEntries; ++guard) {
    const uint32_t entry = pool + 8 * e;
    const uint32_t node = kNodes + 116 * REX_LOAD_U16(entry + 0);
    const uint32_t target = REX_LOAD_U16(entry + 2);
    const uint32_t links = REX_LOAD_U32(node + 88);
    const int32_t count = static_cast<int16_t>(REX_LOAD_U16(node + 86));
    for (int32_t i = count - 1; i >= 0; --i) {
      const uint32_t link = links + 12 * i;
      if (REX_LOAD_U16(link) == target) {
        const int v = static_cast<int>(REX_LOAD_U8(link + 2)) + delta;
        REX_STORE_U8(link + 2, static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)));
        break;
      }
    }
    e = REX_LOAD_U16(entry + 6);
    if (e == head || e == 0) {
      break;
    }
  }
}
}  // namespace

REX_HOOK_RAW(sub_87091AA0) {
  const uint32_t ent = ctx.r3.u32;
  const uint32_t before = g_cuts;
  __imp__sub_87091AA0(ctx, base);
  const uint32_t cut = g_cuts - before;
  if (cut >= 64) {
    auto f = [&](uint32_t off) {
      const uint32_t u = REX_LOAD_U32(ent + off);
      float v;
      std::memcpy(&v, &u, 4);
      return v;
    };
    REXLOG_INFO(
        "codclassic: gentity {:08X} (num {}, bmodel {}, contents {:08X}) cut {} path links (total {}); "
        "abs ({:.0f} {:.0f} {:.0f})-({:.0f} {:.0f} {:.0f}) origin ({:.0f} {:.0f} {:.0f})",
        ent, REX_LOAD_U16(ent + 6), REX_LOAD_U32(ent + 168), REX_LOAD_U32(ent + 196), cut, g_cuts,
        f(200), f(204), f(208), f(212), f(216), f(220), f(224), f(228), f(232));
  }
  const int32_t limit = REXCVAR_GET(codclassic_path_cut_limit);
  if (limit > 0 && cut > static_cast<uint32_t>(limit) && g_neutral.insert(ent).second) {
    AdjustLinks(base, ent, -1);
    REXLOG_WARN("codclassic: gentity {:08X} cut {} path links (limit {}): cuts neutralised", ent,
                cut, limit);
  }
}

// ConnectPaths(ent): restore the counters first so that its own decrement leaves them right.
REX_HOOK_RAW(sub_87091928) {
  const uint32_t ent = ctx.r3.u32;
  if (g_neutral.erase(ent)) {
    AdjustLinks(base, ent, +1);
  }
  __imp__sub_87091928(ctx, base);
}

// Save loader for one entity's chain: it increments the counters of the links it restores.
REX_HOOK_RAW(sub_8704B750) {
  const uint32_t ent = ctx.r3.u32;
  __imp__sub_8704B750(ctx, base);
  if (g_neutral.count(ent)) {
    AdjustLinks(base, ent, -1);
  }
}
