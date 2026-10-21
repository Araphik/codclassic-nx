#include <rex/platform.h>
#include <rex/platform/dynlib.h>

static_assert(REX_PLATFORM_LINUX || REX_PLATFORM_MAC, "This file is POSIX-only");

#include <dlfcn.h>
#include <string>
#include <system_error>

namespace rex::platform {

DynamicLibrary::~DynamicLibrary() {
  Close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept : handle_(other.handle_) {
  other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

bool DynamicLibrary::Load(const std::filesystem::path& path, SymbolResolution mode) {
  Close();
  int flags = (mode == SymbolResolution::kImmediate) ? RTLD_NOW : RTLD_LAZY;
  handle_ = dlopen(path.c_str(), flags);
  if (!handle_ && !path.has_extension() && !path.has_parent_path()) {
    // Module names are registered platform-neutrally (e.g. "game_mod"); on
    // Windows LoadLibrary appends ".dll", so mirror that with the ELF/Mach-O
    // naming convention, preferring a library next to the host executable.
#if defined(__APPLE__)
    const char* suffix = ".dylib";
#else
    const char* suffix = ".so";
#endif
    std::string file = "lib" + path.string() + suffix;
    std::error_code ec;
    auto exe_dir = std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
    if (!ec) {
      handle_ = dlopen((exe_dir / file).c_str(), flags);
    }
    if (!handle_) {
      handle_ = dlopen(file.c_str(), flags);
    }
  }
  return handle_ != nullptr;
}

void DynamicLibrary::Close() {
  if (handle_) {
    dlclose(handle_);
    handle_ = nullptr;
  }
}

void* DynamicLibrary::GetRawSymbol(const char* name) const {
  if (!handle_)
    return nullptr;
  return dlsym(handle_, name);
}

}  // namespace rex::platform
