// Guards around game functions that crash on the Switch port.
//
// sub_8218C578 - appends one surface's triangle indices to the dynamic index buffer of the
// current draw batch (called per surface from sub_8218C700 <- sub_8218C8D0 <- sub_8218CA70):
//
//   surf  = *(rec + 4)                 rec = r3, one entry of the sorted draw list
//   src   = *(surf + 32)               u16 indices of the surface
//   n     = *(u16*)(surf + 16)         index count, copied 6 at a time (n / 6 * 12 bytes)
//   dst   = *(0x82FA4204) + 2 * *(0x82FAE600)    mapped index buffer + indices already queued
//
// Three crashes in play (logs 004, 017, 023), minutes apart and at different spots, all here: a read
// of src or a write to dst landed on guest memory that is not committed at all (0x22344B98,
// 0xB5EFAC2C, 0x90FE63C8). Until the source of the stale pointer is found, a surface whose source
// or destination is not committed memory is skipped for that frame (one missing draw instead of a
// crash), and the details are logged so the cause can be traced.

#include "codclassic_pch.h"

#include <atomic>
#include <cstring>
#include <string_view>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#if REX_PLATFORM_SWITCH
#include "guest_memory_switch.h"
#endif

namespace {

#if REX_PLATFORM_SWITCH
// Mirrors the Switch exception handler (exception_handler_switch.cpp): an access to the guest window
// is recoverable if the page is mapped/watched (READ or WRITE: emulated or faulted in), or if it is
// guest physical memory not committed yet (committed on first touch). It is fatal only when the page
// exists with no access at all, or has no backing outside physical memory. Guest heap metadata is not
// used: each physical mirror (0xA0000000, 0xC0000000, 0xE0000000) keeps its own page table, and memory
// allocated through one mirror looks free through another, which made the first version of this guard
// reject valid surfaces and corrupt geometry.
bool Accessible(uint8_t* base, uint32_t address, bool write) {
  if (address < 0x1000) {
    return false;
  }
  auto* heap = REX_KERNEL_MEMORY()->LookupHeap(address);
  const bool physical = heap && heap->heap_type() == rex::memory::HeapType::kGuestPhysical;
  // Only the plain virtual heaps (below 0x80000000). The image heaps hold the renderer's draw list
  // (0x831Cxxxx) and their metadata does not say "committed" for it: checking them rejected every
  // world surface (log 037: 262145 skipped, broken geometry in the second mission).
  if (heap && !physical && address < 0x80000000u) {
    // Virtual heaps (0x00000000-0x7FFFFFFF, 0x8xxxxxxx image) have reliable page metadata, unlike the
    // physical mirrors: the page must be committed, and writable for a write.
    rex::memory::HeapAllocationInfo info{};
    if (!heap->QueryRegionInfo(address & ~0xFFFu, &info) ||
        !(info.state & rex::memory::kMemoryAllocationCommit)) {
      return false;
    }
    if (write && !(info.protect & rex::memory::kMemoryProtectWrite)) {
      return false;
    }
  }
  // Outside physical memory, a page with no backing at all faults fatally. The logical permissions
  // below say nothing about that (a page never committed has no protection entry and reads as
  // accessible): log 042, write of guest 0x90B3F000 from this function, in the 0x90000000 heap.
  if (!physical && !RexGmShadowFor(reinterpret_cast<uint64_t>(base + address))) {
    return false;
  }
  size_t length = 0x1000;
  RexGmAccess access = REX_GM_NONE;
  if (RexGmQueryProtect(base + address, &length, &access)) {
    if (access == REX_GM_NONE) {
      return false;
    }
    // A read-only page outside physical memory is really read-only: only physical memory has
    // write watching behind it (log 036: write to 0x55F379E0 passed the read check and was fatal).
    return !write || access == REX_GM_WRITE || physical;
  }
  return physical;
}
#else
bool Accessible(uint8_t*, uint32_t address, bool) { return address >= 0x1000; }
#endif

bool Range(uint8_t* base, uint32_t address, uint32_t length, bool write = false) {
  const uint64_t last = uint64_t(address) + (length ? length - 1 : 0);
  if (last > 0xFFFFFFFFull) {
    return false;
  }
  for (uint64_t page = address & ~0xFFFull; page <= (last & ~0xFFFull); page += 0x1000) {
    if (!Accessible(base, static_cast<uint32_t>(page < address ? address : page), write)) {
      return false;
    }
  }
  return true;
}

std::atomic<uint32_t> g_skipped{0};

}  // namespace

// sub_8207C390 - Com_Printf(fmt, ...). The retail build formats the message into a 4 KB buffer
// on its stack and then drops it (the console output was compiled out). Let it format, then copy
// the text into our log: menu, script, save/load and Com_Error messages all go through here.
extern "C" REX_FUNC(__imp__sub_8207C390);

namespace {
std::atomic<uint32_t> g_printed{0};
}

REX_HOOK_RAW(sub_8207C390) {
  const uint32_t sp = ctx.r1.u32;
  __imp__sub_8207C390(ctx, base);
  // stwu r1,-4208(r1); buffer at 96(r1). The frame is popped but nothing has reused it yet.
  const uint32_t buffer = sp - 4208 + 96;
  const char* text = reinterpret_cast<const char*>(base + buffer);
  size_t len = 0;
  while (len < 4095 && text[len]) {
    ++len;
  }
  while (len && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
    --len;
  }
  if (!len) {
    return;
  }
  const uint32_t k = g_printed.fetch_add(1, std::memory_order_relaxed);
  if (k < 50000 || (k & 1023) == 0) {
    REXLOG_INFO("[cod] {}", std::string_view(text, len));
  }
}

extern "C" REX_FUNC(__imp__sub_8218C578);

namespace {
// Reads through the shadow alias (always mapped while the backing exists), so a page whose window
// mapping was removed between the guard's check and the copy cannot fault. Falls back to the window
// (and the exception handler) where there is no backing yet, e.g. physical memory never touched.
#if REX_PLATFORM_SWITCH
struct ShadowReader {
  uint8_t* base;
  uint32_t page = 0xFFFFFFFFu;
  uint8_t* host = nullptr;  // shadow address of `page`, or null
  const uint8_t* At(uint32_t address) {
    const uint32_t p = address & ~0xFFFu;
    if (p != page) {
      page = p;
      host = static_cast<uint8_t*>(RexGmShadowFor(reinterpret_cast<uint64_t>(base + p)));
    }
    return host ? host + (address - p) : nullptr;
  }
  uint32_t U32(uint32_t address) {
    if ((address & 0xFFFu) <= 0xFFCu) {
      if (const uint8_t* h = At(address)) {
        uint32_t v;
        std::memcpy(&v, h, 4);
        return __builtin_bswap32(v);
      }
    }
    return REX_LOAD_U32(address);
  }
  uint16_t U16(uint32_t address) {
    if ((address & 0xFFFu) <= 0xFFEu) {
      if (const uint8_t* h = At(address)) {
        uint16_t v;
        std::memcpy(&v, h, 2);
        return __builtin_bswap16(v);
      }
    }
    return REX_LOAD_U16(address);
  }
};
#else
struct ShadowReader {
  uint8_t* base;
  uint32_t U32(uint32_t address) { return REX_LOAD_U32(address); }
  uint16_t U16(uint32_t address) { return REX_LOAD_U16(address); }
};
#endif
}  // namespace

// The function itself is reimplemented here (same bookkeeping and copy), reading the record, the
// surface and its indices through the shadow alias. The guard checks alone were not enough: on a save
// load the surface memory can lose its window mapping while the renderer still walks last frame's
// draw list, between the check and the copy (log 048: read of 0x0700B04C inside the original after
// the guard passed).
REX_HOOK_RAW(sub_8218C578) {
  constexpr uint32_t kBufferPtr = 0x82FA4204;  // mapped dynamic index buffer
  constexpr uint32_t kMinVertex = kBufferPtr + 20556;
  constexpr uint32_t kBatch = 0x82FAE600;      // +0 indices queued, +264 vertex range end
  ShadowReader rd{base};
  const uint32_t rec = ctx.r3.u32;
  uint32_t surf = 0, src = 0, dst = 0, n = 0, queued = 0;
  bool rec_ok = Range(base, rec, 12);
  if (rec_ok) {
    surf = rd.U32(rec + 4);
    rec_ok = Range(base, surf, 36);
  }
  bool src_ok = false, dst_ok = false;
  uint32_t bytes = 0;
  if (rec_ok) {
    n = rd.U16(surf + 16);
    src = rd.U32(surf + 32);
    queued = REX_LOAD_U32(kBatch);
    dst = REX_LOAD_U32(kBufferPtr) + 2 * queued;
    bytes = n / 6 * 12;
    // The copy loop runs n / 6 times through bdnz: with n < 6 the counter starts at 0 and the loop
    // runs 2^32 times, writing until it leaves mapped memory (log 032: write of guest 0x301047D7).
    // Index data is u16, so an odd source or destination is garbage too.
    if (n < 6) {
      return;  // nothing to queue; a stale surface record
    }
    src_ok = (src & 1) == 0 && Range(base, src, bytes);
    dst_ok = (dst & 1) == 0 && Range(base, dst, bytes, /*write=*/true);
  }
  if (!(rec_ok && src_ok && dst_ok)) {
    const uint32_t k = g_skipped.fetch_add(1, std::memory_order_relaxed);
    if (k < 64 || (k & (k - 1)) == 0) {
      REXLOG_WARN(
          "codclassic: guard sub_8218C578 #{} lr={:08X} rec={:08X} surf={:08X} n={} src={:08X}{} "
          "dst={:08X}{} queued={}",
          k + 1, static_cast<uint32_t>(ctx.lr), rec, surf, n, src, src_ok ? "" : " (BAD)", dst,
          dst_ok ? "" : " (BAD)", queued);
    }
    if (!rec_ok || !dst_ok) {
      return;  // nothing can be written safely
    }
  }
  // Bookkeeping: lowest first vertex and end of the vertex range of the batch.
  const uint32_t first = rd.U32(rec + 8);
  const uint32_t end = first + rd.U16(surf + 18);
  const uint32_t range_end = REX_LOAD_U32(kBatch + 264);
  if (range_end == 0) {
    REX_STORE_U32(kMinVertex, first);
    REX_STORE_U32(kBatch + 264, end);
  } else {
    if (REX_LOAD_U32(kMinVertex) > first) {
      REX_STORE_U32(kMinVertex, first);
    }
    if (range_end < end) {
      REX_STORE_U32(kBatch + 264, end);
    }
  }
  REX_STORE_U32(kBatch, queued + n);
  // Each u32 holds two u16 indices; both get `first` added (rlwinm r3,r4,16 ; or r9,r3,r4).
  const uint32_t add = ((first << 16) & 0xFFFF0000u) | first;
  if (!src_ok) {
    // Bad source: degenerate triangles (every index the surface's first vertex) keep the batch
    // consistent and draw nothing.
    const uint32_t word = ((first & 0xFFFF) << 16) | (first & 0xFFFF);
    for (uint32_t i = 0; i < bytes; i += 4) {
      REX_STORE_U32(dst + i, word);
    }
    return;
  }
  for (uint32_t i = 0; i < bytes; i += 4) {
    REX_STORE_U32(dst + i, rd.U32(src + i) + add);
  }
}

// "Continue" from the main menu (loadgame_continue, sub_82095170).
//
// With no save loaded this session it falls back to g_lastSaveGame = "cache\last.svg": the full copy
// of the last save that SaveGame (sub_8207E758) writes to the cache: partition. sub_82094BB8 reads it
// fine and runs "spmap cache\last.svg". The spmap handler (sub_820944A8) then prefixes "save/" to any
// name that does not start with "save/" or "save\" and reads "save/cache\last.svg" instead: in the save
// package that file is a 23-byte text pointer ("autosave/training.svg"), whose 5th byte was taken as
// a length and the rest as the map name ("Can't find map maps/ave/training.svg"), and the menu stayed
// put. Names under cache\ are already complete, so at those two prefix checks they count as prefixed.
//
// sub_8208C180 is Q_stricmpn(a, b, n).
extern "C" REX_FUNC(__imp__sub_8208C180);

REX_HOOK_RAW(sub_8208C180) {
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  if ((lr == 0x820945B8 || lr == 0x820945D4) && ctx.r5.u32 == 5) {
    const char* name = reinterpret_cast<const char*>(base + ctx.r3.u32);
    const bool under_cache = (name[0] | 0x20) == 'c' && (name[1] | 0x20) == 'a' &&
                             (name[2] | 0x20) == 'c' && (name[3] | 0x20) == 'h' &&
                             (name[4] | 0x20) == 'e' && (name[5] == '\\' || name[5] == '/');
    if (under_cache) {
      REXLOG_INFO("codclassic: spmap '{}' read as is (no save/ prefix)", name);
      ctx.r3.u64 = 0;
      return;
    }
  }
  __imp__sub_8208C180(ctx, base);
}
