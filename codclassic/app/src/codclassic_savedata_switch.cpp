// Saves in the console's own save data when the port runs as an installed title.
//
// From the Homebrew Menu the NRO runs under whatever game was used for the title override, so its
// files stay on the SD card (sdmc:/switch/codclassic/codclassic/). When it is installed as its own
// title (a forwarder made with Sphaira, or an NSP) it has an application ID of its own, and then the
// user data (cache: = config, last.svg, current.psw, and the CoDSaves content packages) goes into
// that title's account save data, where DBI, Checkpoint, JKSV and the system's save data management
// can see, back up and restore it.
//
// The title is recognised by the name in its control data ("Call of Duty Classic", the NRO's NACP
// name, which forwarders copy). Any other title is somebody else's game: its saves are never touched.
// The first time, the saves already on the SD card are copied into the save data.
//
// Horizon save data only keeps what was committed: every change the guest makes there
// (rex::filesystem::NotifyHostWrite: a file written and closed, created, deleted, renamed, a save
// container closed) is committed right away.

#include <switch.h>

#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>

#include <rex/filesystem/host_write_hook.h>
#include <rex/logging.h>
#include <fmt/format.h>

namespace {

constexpr const char* kTitleName = "Call of Duty Classic";
constexpr const char* kDevice = "codsave";
constexpr s64 kSaveSize = 64ll << 20;     // the saves take a few MB
constexpr s64 kJournalSize = 32ll << 20;  // largest single commit (a save game is under 1 MB)

std::mutex g_commit_mutex;
// OnConfigurePaths runs before logging is set up: messages are kept and logged later.
std::string g_status;

template <typename... Args>
void Note(fmt::format_string<Args...> f, Args&&... args) {
  if (!g_status.empty()) g_status += "\n";
  g_status += fmt::format(f, std::forward<Args>(args)...);
}

void CommitOnWrite(const std::filesystem::path& changed) {
  const std::string p = changed.string();
  if (p.compare(0, std::strlen(kDevice) + 1, std::string(kDevice) + ":") != 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_commit_mutex);
  const Result rc = fsdevCommitDevice(kDevice);
  if (R_FAILED(rc)) {
    // A file still open for writing in the save data makes the commit fail; the change is committed
    // with the next one, when that file is closed.
    REXLOG_WARN("codclassic: save data commit after {} failed (0x{:X})", p, rc);
  }
}

bool OwnTitle(u64* out_id) {
  u64 id = 0;
  if (R_FAILED(svcGetInfo(&id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)) || id == 0) {
    return false;
  }
  if (R_FAILED(nsInitialize())) {
    return false;
  }
  auto* data = new NsApplicationControlData();
  u64 size = 0;
  bool own = false;
  if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, id, data,
                                              sizeof(*data), &size)) &&
      size >= sizeof(data->nacp)) {
    NacpLanguageEntry* entry = nullptr;
    if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &entry)) && entry) {
      own = std::strncmp(entry->name, kTitleName, sizeof(entry->name)) == 0;
    }
    if (!own) {
      // Any language entry with the name counts (forwarder tools may fill only some of them).
      for (const auto& lang : data->nacp.lang) {
        if (std::strncmp(lang.name, kTitleName, sizeof(lang.name)) == 0) {
          own = true;
          break;
        }
      }
    }
  }
  delete data;
  nsExit();
  *out_id = id;
  return own;
}

bool PickUser(AccountUid* uid) {
  if (R_FAILED(accountInitialize(AccountServiceType_Application))) {
    return false;
  }
  bool ok = R_SUCCEEDED(accountGetPreselectedUser(uid)) && accountUidIsValid(uid);
  if (!ok) {
    ok = R_SUCCEEDED(accountGetLastOpenedUser(uid)) && accountUidIsValid(uid);
  }
  accountExit();
  return ok;
}

bool OpenOrCreate(FsFileSystem* fs, u64 app_id, AccountUid uid) {
  if (R_SUCCEEDED(fsOpen_SaveData(fs, app_id, uid))) {
    return true;
  }
  FsSaveDataAttribute attr{};
  attr.application_id = app_id;
  attr.uid = uid;
  attr.save_data_type = FsSaveDataType_Account;
  FsSaveDataCreationInfo info{};
  info.save_data_size = kSaveSize;
  info.journal_size = kJournalSize;
  info.available_size = 0x4000;
  info.owner_id = app_id;
  info.save_data_space_id = FsSaveDataSpaceId_User;
  FsSaveDataMetaInfo meta{};
  const Result rc = fsCreateSaveDataFileSystem(&attr, &info, &meta);
  if (R_FAILED(rc)) {
    Note("codclassic: could not create the save data of {:016X} (0x{:X})", app_id, rc);
    return false;
  }
  return R_SUCCEEDED(fsOpen_SaveData(fs, app_id, uid));
}

// Copies the user data that was on the SD card into an empty save data, once.
void Migrate(const std::filesystem::path& sd_root, const std::filesystem::path& save_root) {
  std::error_code ec;
  if (!std::filesystem::is_directory(sd_root, ec)) {
    return;
  }
  if (std::filesystem::directory_iterator(save_root, ec) != std::filesystem::directory_iterator()) {
    return;  // the save data already has something: never overwrite it
  }
  size_t copied = 0;
  for (const char* sub : {"cache_partition", "B13EBABEBABEBABE", "achievements"}) {
    const auto from = sd_root / sub;
    if (!std::filesystem::exists(from, ec)) {
      continue;
    }
    std::filesystem::copy(from, save_root / sub,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::skip_existing,
                          ec);
    if (ec) {
      Note("codclassic: copying {} into the save data failed: {}", from.string(),
                  ec.message());
    } else {
      ++copied;
    }
  }
  if (copied) {
    fsdevCommitDevice(kDevice);
    Note("codclassic: copied the saves from {} into the save data", sd_root.string());
  }
}

}  // namespace

// Returns the user data root to use: the mounted save data, or `sd_root` unchanged.
std::filesystem::path CodclassicUserDataRoot(const std::filesystem::path& sd_root) {
  u64 app_id = 0;
  if (!OwnTitle(&app_id)) {
    Note("codclassic: running under title {:016X} (not an installed codclassic): saves on SD",
                app_id);
    return sd_root;
  }
  AccountUid uid{};
  if (!PickUser(&uid)) {
    Note("codclassic: installed title {:016X} but no user selected: saves on SD", app_id);
    return sd_root;
  }
  FsFileSystem fs;
  if (!OpenOrCreate(&fs, app_id, uid)) {
    return sd_root;
  }
  if (fsdevMountDevice(kDevice, fs) < 0) {
    fsFsClose(&fs);
    Note("codclassic: could not mount the save data: saves on SD");
    return sd_root;
  }
  const std::filesystem::path save_root = std::string(kDevice) + ":/";
  Migrate(sd_root, save_root);
  rex::filesystem::SetHostWriteHook(CommitOnWrite);
  Note("codclassic: saves in the save data of {:016X} (user {:016X}{:016X})", app_id,
              uid.uid[1], uid.uid[0]);
  return save_root;
}

const std::string& CodclassicSaveDataStatus() { return g_status; }
