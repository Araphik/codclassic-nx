// codclassic - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>
#include <memory>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>

class CodclassicApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<CodclassicApp>(new CodclassicApp(ctx, "codclassic",
        PPCImageConfig));
  }

 protected:
  // Portable layout, the same on PC and Switch:
  //   <executable folder>/game_root/   default.xex, *.xdl, main/*.pk3, shaders/, splash/
  // On the Switch the NRO lives in sdmc:/switch/codclassic/, so the game goes in
  // sdmc:/switch/codclassic/game_root/. --game_data_root still overrides it.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty()) {
      const auto folder = rex::filesystem::GetExecutableFolder();
      std::error_code ec;
      if (!folder.empty() && std::filesystem::is_directory(folder / "game_root", ec)) {
        paths.game_data_root = folder / "game_root";
      }
    }
    user_data_root_ = paths.user_data_root;
  }

  // Settings that used to be passed on the command line
  // (--gpu_plugin=xenos --license_mask=1 [--mnk_mode=true]). Only applied when
  // nobody set them explicitly, so codclassic.toml and arguments still win.
  void OnPostInitLogging() override {
    SetIfDefault("gpu_plugin", "xenos");
    SetIfDefault("license_mask", "1");  // full game, not the XBLA trial
#if REX_PLATFORM_SWITCH
    // Keep every guest thread at 0x3B, the only Horizon priority that is time-sliced. While loading
    // a level the game raises its loader thread with KeSetBasePriorityThread; that reached Horizon as
    // 0x2C, the priority of the SDK's GPU Commands thread, and same-priority threads above 0x3B do not
    // share a core: the loader spun waiting for the GPU and the GPU thread never ran again (the
    // pathfinder briefing hung at 0 fps with one core at 100%).
    SetIfDefault("ignore_thread_priorities", "true");
#endif
#if !REX_PLATFORM_SWITCH
    SetIfDefault("mnk_mode", "true");
#endif
  }

  // Call of Duty Classic keeps its Quake-style home path (config, save games,
  // cache) on the Xbox 360 utility partition, mounted by the title as
  // "cache:". The SDK leaves cache: unmounted on purpose, so give this game a
  // writable host folder for it under the user data directory.
  void OnPostSetup() override {
    auto* fs = runtime() ? runtime()->file_system() : nullptr;
    if (!fs) {
      return;
    }
    std::filesystem::path dir =
        (user_data_root_.empty() ? rex::filesystem::GetExecutableFolder() : user_data_root_) /
        "cache_partition";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    constexpr const char* kMount = "\\Device\\CodClassicCache";
    auto device = std::make_unique<rex::filesystem::HostPathDevice>(kMount, dir,
                                                                     /*read_only=*/false);
    if (!device->Initialize() || !fs->RegisterDevice(std::move(device))) {
      REXLOG_ERROR("codclassic: failed to mount cache: at {}", dir.string());
      return;
    }
    fs->RegisterSymbolicLink("cache:", kMount);
    REXLOG_INFO("codclassic: mounted cache: -> {}", dir.string());
  }

 private:
  static void SetIfDefault(const char* name, const char* value) {
    if (rex::cvar::GetFlagInfo(name) == nullptr || rex::cvar::HasNonDefaultValue(name)) {
      return;
    }
    rex::cvar::SetFlagByName(name, value);
  }

  std::filesystem::path user_data_root_;
};
