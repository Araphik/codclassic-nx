// Nintendo Switch: the guest DLL modules (*.xdl) are linked statically into
// the NRO. Each one's entry points were renamed at compile time (see
// CMakeLists.txt); publish them under the module's shared_lib_name so the
// kernel's LoadUserModule -> DynamicLibrary::Load() finds them.

#include <rex/platform/dynlib.h>

namespace rex {
struct PPCImageInfo;
namespace runtime {
class IModuleRegistrar;
}
}  // namespace rex

#define CODCLASSIC_MODULE(lib)                                                        \
  extern "C" void ReXModule_Register__##lib(rex::runtime::IModuleRegistrar*);         \
  extern "C" const rex::PPCImageInfo* ReXModule_GetImageInfo__##lib();

CODCLASSIC_MODULE(codclassic_x360shared)
CODCLASSIC_MODULE(codclassic_game_sp)
CODCLASSIC_MODULE(codclassic_cgame_sp)
CODCLASSIC_MODULE(codclassic_ui_sp)

#undef CODCLASSIC_MODULE

namespace {

#define CODCLASSIC_PUBLISH(lib)                                                       \
  rex::platform::RegisterStaticLibrary(#lib, "ReXModule_Register",                    \
                                       reinterpret_cast<void*>(&ReXModule_Register__##lib)); \
  rex::platform::RegisterStaticLibrary(#lib, "ReXModule_GetImageInfo",                \
                                       reinterpret_cast<void*>(&ReXModule_GetImageInfo__##lib));

// Runs before main(): long before the kernel loads any module.
struct PublishStaticModules {
  PublishStaticModules() {
    CODCLASSIC_PUBLISH(codclassic_x360shared)
    CODCLASSIC_PUBLISH(codclassic_game_sp)
    CODCLASSIC_PUBLISH(codclassic_cgame_sp)
    CODCLASSIC_PUBLISH(codclassic_ui_sp)
  }
} g_publish_static_modules;

#undef CODCLASSIC_PUBLISH

}  // namespace
