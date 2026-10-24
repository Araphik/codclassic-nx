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

#include <cstdio>
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

// Our own title: an application installed for the port (Sphaira and other forwarder tools give it an
// ID outside the 0100xxxxxxxxxxxx range of Nintendo's titles: e.g. 03DB12780BD84000), or one whose
// control data carries our name. A retail game used for a title override never qualifies.
bool OwnTitle(u64* out_id) {
  u64 id = 0;
  if (R_FAILED(svcGetInfo(&id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)) || id == 0) {
    *out_id = 0;
    return false;
  }
  *out_id = id;
  const bool retail_range = (id >> 56) == 0x01;
  bool named = false;
  if (R_SUCCEEDED(nsInitialize())) {
    auto* data = new NsApplicationControlData();
    u64 size = 0;
    const Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, id, data,
                                                  sizeof(*data), &size);
    if (R_SUCCEEDED(rc) && size >= sizeof(data->nacp)) {
      for (const auto& lang : data->nacp.lang) {
        if (std::strncmp(lang.name, kTitleName, sizeof(lang.name)) == 0) {
          named = true;
          break;
        }
      }
      if (!named) {
        Note("codclassic: title {:016X} is named \"{:.{}s}\"", id, data->nacp.lang[0].name,
             sizeof(data->nacp.lang[0].name));
      }
    } else {
      Note("codclassic: no control data for {:016X} (0x{:X})", id, rc);
    }
    delete data;
    nsExit();
  }
  return named || !retail_range;
}

// The user whose save data is used. Launched with a user already chosen (a NACP that asks for one at
// startup) that user is taken; otherwise the system's "Select a user" screen is shown, as games and
// other ports do. Forwarders made with Sphaira do not ask for a user at startup, so in practice the
// screen comes from here.
bool PickUser(AccountUid* uid) {
  if (R_FAILED(accountInitialize(AccountServiceType_Application))) {
    Note("codclassic: account service unavailable");
    return false;
  }
  bool ok = R_SUCCEEDED(accountGetPreselectedUser(uid)) && accountUidIsValid(uid);
  accountExit();
  if (ok) {
    return true;
  }
  PselUserSelectionSettings settings{};
  const Result rc = pselShowUserSelector(uid, &settings);
  ok = R_SUCCEEDED(rc) && accountUidIsValid(uid);
  if (!ok) {
    Note("codclassic: user selection cancelled or failed (0x{:X})", rc);
  }
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

namespace {
std::filesystem::path PickRoot(const std::filesystem::path& sd_root);
}

// Returns the user data root to use: the mounted save data, or `sd_root` unchanged. What happened
// is also written next to the NRO, in saves_status.txt, to be read without digging through logs.
std::filesystem::path CodclassicUserDataRoot(const std::filesystem::path& sd_root) {
  const std::filesystem::path root = PickRoot(sd_root);
  if (FILE* f = std::fopen((sd_root.parent_path() / "saves_status.txt").string().c_str(), "w")) {
    std::fputs(g_status.c_str(), f);
    std::fputs("\n", f);
    std::fclose(f);
  }
  return root;
}

namespace {
std::filesystem::path PickRoot(const std::filesystem::path& sd_root) {
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
}  // namespace

const std::string& CodclassicSaveDataStatus() { return g_status; }
