// Rendering without 4x MSAA predicated tiling.
//
// The game renders its scene into a 4x MSAA EDRAM render target. 1280x720 at 4x (32-bit color + 32-bit
// depth) is 29.5 MB and EDRAM holds 10 MB, so it uses Direct3D predicated tiling: the screen is split
// into three 1280x256 bands (1280 * 256 * 8 bytes * 4 samples = exactly 10 MB) and every draw of the
// frame is replayed once per band. The Switch port pays for that three times over: the command
// processor translates every draw three times (about 1400 draws per frame instead of 470), the host
// GPU runs the vertex work three times, and every pixel is rendered and resolved at 4 samples. The
// profiler shows it as 3 resolves per frame covering one screen in total and a scissor of a third of
// the screen per draw.
//
// With codclassic_msaa = false (the default) the game gets one band covering the whole screen and
// single-sample render targets, which fit in EDRAM (3.5 MB color + 3.5 MB depth):
//
//   sub_82191730  renderer init: writes the three tile rects at 0x82FAE8B4 (x1, y1, x2, y2 each)
//                 and then calls
//   sub_82191138  creates the tile render target and depth surface with
//                 sub_82216F38(width, height, format, multisample = 2 (4 samples), params), sized by
//   sub_82191078  the largest tile rect;
//   sub_8221BE40  D3DDevice_BeginTiling(device, flags, count = 3, rects = 0x82FAE8B4, ...), called each
//                 frame from sub_82194120 and sub_82194458.
//
// Hooks: before sub_82191138 the three rects are made full screen; its two surface creations get
// multisample 0; BeginTiling gets one rect. The resolve at the end of tiling then copies one
// full-screen band instead of three thirds.

#include "codclassic_pch.h"

#include <algorithm>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(codclassic_msaa, false, "Call of Duty Classic",
                    "Original 4x MSAA with predicated tiling (three passes per frame; slow on Switch)");

namespace {
constexpr uint32_t kTileRects = 0x82FAE8B4;  // 3 x {x1, y1, x2, y2}
constexpr uint32_t kTileCount = 3;
constexpr uint32_t kFullHeight = 720;
}  // namespace

extern "C" REX_FUNC(__imp__sub_82191138);
extern "C" REX_FUNC(__imp__sub_82216F38);
extern "C" REX_FUNC(__imp__sub_8221BE40);

REX_HOOK_RAW(sub_82191138) {
  if (!REXCVAR_GET(codclassic_msaa)) {
    // Width as the game chose it (1280, or 960 for 4:3), full height, the same rect three times so
    // that every reader of the table (sub_82191078 takes the largest) sees one full-screen tile.
    const uint32_t width = REX_LOAD_U32(kTileRects + 8);
    for (uint32_t i = 0; i < kTileCount; ++i) {
      const uint32_t r = kTileRects + 16 * i;
      REX_STORE_U32(r + 0, 0);
      REX_STORE_U32(r + 4, 0);
      REX_STORE_U32(r + 8, width);
      REX_STORE_U32(r + 12, kFullHeight);
    }
    REXLOG_INFO("codclassic: rendering {}x{} without MSAA tiling (codclassic_msaa = false)", width,
                kFullHeight);
  }
  __imp__sub_82191138(ctx, base);
}

REX_HOOK_RAW(sub_82216F38) {
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  if (!REXCVAR_GET(codclassic_msaa) && (lr == 0x82191238 || lr == 0x8219126C) &&
      ctx.r6.u32 == 2) {
    ctx.r6.u64 = 0;  // D3DMULTISAMPLE_NONE for the scene color and depth surfaces
  }
  __imp__sub_82216F38(ctx, base);
}

REX_HOOK_RAW(sub_8221BE40) {
  if (!REXCVAR_GET(codclassic_msaa) && ctx.r6.u32 == kTileRects && ctx.r5.u32 == kTileCount) {
    ctx.r5.u64 = 1;
  }
  __imp__sub_8221BE40(ctx, base);
}

// Hunk size.
//
// Com_InitHunkMemory (sub_82078FE8) reads com_hunkMegs through Cvar_Get(name, "128", CVAR_LATCH) and
// allocates (integer << 20) bytes with malloc. Dawnville loaded from a save (death or autosave)
// failed with "Hunk_ReallocateTempMemory: failed on 72213715": low permanent + temp + high came to
// 134217729 bytes, one byte over 128 MB (log 043). The Switch build takes a little more hunk than the
// 360 one (the path disconnect pool is 64 KB instead of 16 KB, codclassic_game_sp_hooks.cpp), and the
// margin was already gone. The guest has memory to spare: 160 MB.
REXCVAR_DEFINE_INT32(codclassic_hunk_megs, 160, "Call of Duty Classic",
                     "Size of the engine hunk in MB (the 360 uses 128)");

extern "C" REX_FUNC(__imp__sub_8207FA78);  // Cvar_Get(name, default, flags) -> cvar_t*

REX_HOOK_RAW(sub_8207FA78) {
  constexpr uint32_t kHunkMegsName = 0x82004738;  // "com_hunkMegs"
  const bool hunk = ctx.r3.u32 == kHunkMegsName;
  __imp__sub_8207FA78(ctx, base);
  if (hunk && ctx.r3.u32 != 0) {
    const int32_t megs = std::max<int32_t>(REXCVAR_GET(codclassic_hunk_megs), 128);
    if (static_cast<int32_t>(REX_LOAD_U32(ctx.r3.u32 + 32)) < megs) {
      REX_STORE_U32(ctx.r3.u32 + 32, static_cast<uint32_t>(megs));  // cvar_t::integer
      REXLOG_INFO("codclassic: hunk {} MB", megs);
    }
  }
}
