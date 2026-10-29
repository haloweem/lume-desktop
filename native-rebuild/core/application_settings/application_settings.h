#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lume::application_settings {
inline constexpr char CurrentVersion[] = "1.0.8";
inline constexpr char NativeRuntime[] = "native-win32";
inline constexpr char NativeChannel[] = "native-verification";
inline constexpr char ManifestUrl[] = "https://lumedesktop.com/updates.json";

struct Preferences {
    std::string language = "zh-CN";
    bool balancedMode = true;
    bool autostart = true;
    // Empty follows the installation folder; a value overrides the wallpaper
    // library root for future downloads (read at start-up).
    std::string wallpaperDirectory;
};
enum class ReadStatus { Missing, Loaded, Failed };
struct FileRead {
    ReadStatus status = ReadStatus::Missing;
    std::string bytes;
};
class IPreferencesFile {
public:
    virtual ~IPreferencesFile() = default;
    virtual FileRead Read() = 0;
    virtual bool WriteAtomically(const std::string& bytes) = 0;
};
// 回滚时保留注册表值的类型和完整原始字节。
struct StartupEntry {
    bool exists = false;
    std::uint32_t type = 0;
    std::vector<std::uint8_t> bytes;
};
class IStartupRegistration {
public:
    virtual ~IStartupRegistration() = default;
    virtual bool Read(StartupEntry& entry) = 0;
    virtual bool Write(const StartupEntry& entry) = 0;
};
struct Cancellation {
    std::atomic<bool> cancelled{false};
};
struct ManifestResponse {
    std::string body;
    std::string error;
};
class IUpdateTransport {
public:
    virtual ~IUpdateTransport() = default;
    virtual ManifestResponse Fetch(const Cancellation& cancellation) = 0;
};
struct UpdateState {
    std::string status = "idle";
    std::string version;
    int progress = 0;
};
struct Snapshot {
    std::uint64_t revision = 0;
    Preferences preferences;
    // Resolved for display only; filled by the host before serializing.
    std::string dataDirectory;
    std::string wallpaperDirectory;
    bool busy = false;
    std::string error;
    UpdateState update;
};
struct ManifestResult {
    UpdateState update;
    std::string error;
    struct Installer {
        std::string url, sha256;
        std::uint64_t size = 0;
    } installer;
};
struct PreparedUpdate { std::wstring path; std::string error; };
class IUpdateInstaller {
public:
    virtual ~IUpdateInstaller() = default;
    virtual PreparedUpdate Download(const ManifestResult&, const Cancellation&, const std::function<void(int)>& progress) = 0;
    virtual std::string Launch(const PreparedUpdate&, const ManifestResult&, const std::wstring& installDirectory) = 0;
};
struct DecodedPreferences {
    Preferences preferences;
    std::string error;
};
DecodedPreferences DecodePreferences(std::string_view bytes) noexcept;
std::string EncodePreferences(const Preferences& preferences);
std::optional<int> CompareVersions(std::string_view left, std::string_view right) noexcept;
ManifestResult EvaluateManifest(std::string_view body, std::string_view current = CurrentVersion) noexcept;
bool IsOfficialUpdateUrl(std::string_view value) noexcept;
std::optional<std::wstring> StartupCommand(std::wstring_view executable) noexcept;
StartupEntry EnabledStartupEntry(std::wstring_view command);
bool SameStartupEntry(const StartupEntry& left, const StartupEntry& right) noexcept;
enum class StartupEntryKind { Missing, Current, Legacy, OtherInstallation, Foreign };
StartupEntryKind ClassifyStartupEntry(const StartupEntry& entry, std::wstring_view executable) noexcept;
std::wstring SerializeSnapshot(const Snapshot& snapshot);

class Service final {
public:
    Service(IPreferencesFile& file, IStartupRegistration& startup, std::wstring executable,
            std::function<bool(bool)> applyBalancedMode, const Cancellation* cancellation = nullptr,
            bool restoreStartup = true);
    bool LoadPreferences();
    bool Load();
    bool Restore();
    bool SetLanguage(const std::string& language);
    bool SetBalancedMode(bool enabled);
    bool SetAutostart(bool enabled);
    bool SetWallpaperDirectory(const std::string& directory);
    bool BeginCheck();
    void FinishCheck(const ManifestResult& result);
    void UnsupportedUpdate();
    void UpdateProgress(UpdateState update, std::string error = {});
    void CancelCheck();
    const Snapshot& State() const noexcept;
private:
    bool Save(const Preferences& preferences, bool saveAutostart = false, bool restoring = false);
    bool ApplyAutostart(bool enabled, bool restoring);
    bool Ready();
    bool ReadStartup(StartupEntry& entry);
    void ObserveStartup(const StartupEntry& entry);
    bool RollbackStartup(const StartupEntry& entry, const StartupEntry& expected);
    bool ApplyBalanced(bool enabled) noexcept;
    void RefreshError();
    void Fail(const char* error);
    IPreferencesFile& file_;
    IStartupRegistration& startup_;
    std::wstring executable_;
    std::optional<std::wstring> command_;
    std::function<bool(bool)> applyBalancedMode_;
    Snapshot state_;
    const Cancellation* cancellation_ = nullptr;
    std::string storageError_;
    std::string operationError_;
    std::string balancedRuntimeError_;
    std::string startupRuntimeError_;
    bool autostartIntent_ = true;
    bool initializeAutostart_ = false;
    bool restoreStartup_ = true;
    bool restored_ = false;
    bool writable_ = false;
    bool compatibleUpdate_ = false;
};
}
