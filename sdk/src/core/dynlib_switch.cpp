/**
 * @file        rex/core/dynlib_switch.cpp
 * @brief       DynamicLibrary backend for Horizon: there is no dynamic loading
 *
 * An NRO cannot load libraries at run time: dlopen does not exist and everything is
 * linked statically. This backend does not fail loudly; it returns "not loaded" for
 * any path, because the SDK's two callers already handle that case:
 *
 *   - gpu_plugin_loader.cpp, which on Switch must be resolved through static
 *     registration (see rex::system::LoadGpuPlugin)
 *   - the optional RenderDoc and SPIRV-Tools loaders, which make no sense on the
 *     console
 *
 * If a third caller that depends on dlopen is ever added, it will show up here as a
 * Load() that returns false, not as a link error.
 */

#include <rex/platform.h>
#include <rex/platform/dynlib.h>

#include <map>
#include <mutex>
#include <string>

static_assert(REX_PLATFORM_SWITCH, "This file is Nintendo Switch-only");

namespace rex::platform {

namespace {

using SymbolTable = std::map<std::string, void*, std::less<>>;

struct StaticRegistry {
  std::mutex mutex;
  std::map<std::string, SymbolTable, std::less<>> libraries;
};

StaticRegistry& Registry() {
  static StaticRegistry registry;
  return registry;
}

// "libfoo.so", "foo.dll", "dir/foo" -> "foo"
std::string LibraryKey(const std::filesystem::path& path) {
  std::string name = path.filename().string();
  for (const char* ext : {".so", ".dll", ".dylib", ".nro"}) {
    const std::string e(ext);
    if (name.size() > e.size() && name.compare(name.size() - e.size(), e.size(), e) == 0) {
      name.resize(name.size() - e.size());
      break;
    }
  }
  if (name.rfind("lib", 0) == 0 && name.size() > 3) {
    std::string stripped = name.substr(3);
    auto& reg = Registry();
    if (reg.libraries.find(name) == reg.libraries.end() &&
        reg.libraries.find(stripped) != reg.libraries.end()) {
      return stripped;
    }
  }
  return name;
}

}  // namespace

void RegisterStaticLibrary(const char* lib_name, const char* symbol, void* address) {
  if (!lib_name || !symbol) {
    return;
  }
  auto& reg = Registry();
  std::lock_guard lock(reg.mutex);
  reg.libraries[lib_name][symbol] = address;
}

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
  (void)mode;
  Close();
  auto& reg = Registry();
  std::lock_guard lock(reg.mutex);
  auto it = reg.libraries.find(LibraryKey(path));
  if (it == reg.libraries.end()) {
    return false;
  }
  // std::map nodes are stable, so the table pointer stays valid.
  handle_ = &it->second;
  return true;
}

void DynamicLibrary::Close() {
  handle_ = nullptr;
}

void* DynamicLibrary::GetRawSymbol(const char* name) const {
  if (!handle_ || !name) {
    return nullptr;
  }
  auto& reg = Registry();
  std::lock_guard lock(reg.mutex);
  const auto* table = static_cast<const SymbolTable*>(handle_);
  auto it = table->find(name);
  return it == table->end() ? nullptr : it->second;
}

}  // namespace rex::platform
