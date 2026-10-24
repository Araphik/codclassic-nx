#pragma once

#include <filesystem>

namespace rex::filesystem {

// Called after the guest changed something on a host folder through a HostPathDevice: a file that
// was written and closed, a file or folder created, deleted or renamed, a save container closed.
// The path is the host path that changed. Lets the app make the change durable where the host needs
// it, e.g. commit a Horizon save data file system (changes there are lost until fsdevCommitDevice).
using HostWriteHook = void (*)(const std::filesystem::path& changed);

void SetHostWriteHook(HostWriteHook hook);
void NotifyHostWrite(const std::filesystem::path& changed);

}  // namespace rex::filesystem
