#include "platform/windows/local_settings_file_store.h"

#include <windows.h>

#include <filesystem>
#include <iterator>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace lume::platform::windows {

bool LocalSettingsFileStore::ReadBounded(const std::string& path,std::size_t limit,std::string& out) const {
    if(path.empty())return false;
    std::ifstream input(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
    if(!input)return false;
    const auto length=input.tellg();
    if(length<0||static_cast<std::uint64_t>(length)>limit)return false;
    std::string value(static_cast<std::size_t>(length),'\0');
    input.seekg(0);
    if(!input.read(value.data(),static_cast<std::streamsize>(value.size()))||input.peek()!=std::char_traits<char>::eof())return false;
    out=std::move(value);return true;
}

bool LocalSettingsFileStore::Read(const std::string& path, std::string& out) const {
    if (path.empty()) return false;
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (!input) return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    out = buffer.str();
    return true;
}

bool LocalSettingsFileStore::Exists(const std::string& path) const {
    if (path.empty()) return false;
    std::error_code error;
    return std::filesystem::exists(std::filesystem::u8path(path), error);
}

bool LocalSettingsFileStore::Write(const std::string& path, const std::string& value) {
    if (path.empty() || value.size() > MAXDWORD) return false;
    const auto target = std::filesystem::u8path(path);
    HANDLE file = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, value.data(), static_cast<DWORD>(value.size()), &written, nullptr) &&
        written == value.size() && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

bool LocalSettingsFileStore::Rename(const std::string& from, const std::string& to) {
    if (from.empty() || to.empty()) return false;
    const auto source = std::filesystem::u8path(from);
    const auto destination = std::filesystem::u8path(to);
    return MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool LocalSettingsFileStore::Remove(const std::string& path) {
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::remove(std::filesystem::u8path(path), error);
    return !error;
}

namespace {
bool Writable(const std::filesystem::path& folder);
// The user's data lives beside the program by default (<install>\Data). A folder
// chosen in Settings is kept in HKCU so it survives the move itself; the
// per-user folder remains the fallback when the install folder is read-only.
constexpr wchar_t DataKey[] = L"Software\\Lume";
constexpr wchar_t DataValue[] = L"DataDirectory";
constexpr wchar_t PendingValue[] = L"PendingDataDirectory";
std::wstring ModuleFolder() {
    wchar_t module[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    if (!length || length >= std::size(module)) return {};
    return std::filesystem::path(module).parent_path().wstring();
}
std::wstring ReadRegistry(const wchar_t* name) {
    wchar_t value[32768]{};
    DWORD size = static_cast<DWORD>(sizeof(value)), type = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, DataKey, name, RRF_RT_REG_SZ, &type, value, &size) != ERROR_SUCCESS)
        return {};
    return value;
}
bool WriteRegistry(const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, DataKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const auto bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const bool ok = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}
void DeleteRegistry(const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, DataKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    RegDeleteValueW(key, name);
    RegCloseKey(key);
}
std::filesystem::path LegacyDataRoot() {
    wchar_t value[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value, static_cast<DWORD>(std::size(value)));
    if (!length || length >= std::size(value)) return {};
    return std::filesystem::path(value) / L"Lume";
}
const std::wstring& DataRootWide() {
    static const std::wstring root = [] {
        // Account credentials and to-dos are private to the Windows user: by default
        // they stay in that user's own folder, never in the shared install folder.
        const auto chosen = ReadRegistry(DataValue);
        if (!chosen.empty() && Writable(chosen)) return chosen;
        return LegacyDataRoot().wstring();
    }();
    return root;
}
bool HasData(const std::filesystem::path& folder) {
    std::error_code error;
    return std::filesystem::exists(folder / L"application-settings.json", error) ||
        std::filesystem::exists(folder / L"account", error) ||
        std::filesystem::exists(folder / L"todo-data", error);
}
bool NeedsMove(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    if (from.empty() || to.empty()) return false;
    if (from == to) return false;
    if (!std::filesystem::exists(from, error) || !HasData(from)) return false;
    return !HasData(to);
}
std::string PathUnderLume(const wchar_t* relative, bool createLeaf) {
    const auto& root = DataRootWide();
    if (root.empty()) return {};
    const std::filesystem::path target = std::filesystem::path(root) / relative;
    std::error_code error;
    std::filesystem::create_directories(createLeaf ? target : target.parent_path(), error);
    if (error) return {};
    const auto wide = target.wstring();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(bytes > 0 ? bytes : 0), '\0');
    if (bytes > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), bytes, nullptr, nullptr);
        result.resize(static_cast<std::size_t>(bytes - 1));
    }
    return result;
}
std::string PreferencesPath(const wchar_t* filename) { return PathUnderLume(filename, false); }
}
std::string GetUiPreferencesPath() { return PreferencesPath(L"ui-preferences.json"); }
std::string GetCalendarPreferencesPath() { return PreferencesPath(L"calendar-preferences.json"); }
std::string GetPerformancePreferencesPath() { return PreferencesPath(L"performance-preferences.json"); }
std::string GetSpectrumPreferencesPath() { return PreferencesPath(L"spectrum-preferences.json"); }
std::string GetAlmanacPreferencesPath() { return PreferencesPath(L"almanac-preferences.json"); }
std::string GetTodayTodoPreferencesPath(){return PreferencesPath(L"today-todo-preferences.json");}
std::string GetWeatherPreferencesPath() { return PreferencesPath(L"weather-preferences.json"); }
std::string GetOrganizerPreferencesPath() { return PreferencesPath(L"organizer-preferences.json"); }
std::string GetRadialPreferencesPath() { return PreferencesPath(L"radial-preferences.json"); }
std::string GetDockPreferencesPath() { return PreferencesPath(L"dock-preferences.json"); }
std::string GetDynamicIslandPreferencesPath() { return PreferencesPath(L"dynamic-island-preferences.json"); }
std::string GetDesktopIconsPreferencesPath() { return PreferencesPath(L"desktop-icons-preferences.json"); }
std::string GetTaskbarPreferencesPath() { return PreferencesPath(L"taskbar-state.json"); }
std::string GetMinimizePreferencesPath() { return PreferencesPath(L"minimize-animation-preferences.json"); }
std::string GetMusicPreferencesPath() { return PreferencesPath(L"music-preferences.json"); }
std::string GetMusicBackgroundRootPath() { return PathUnderLume(L"MusicBackground", true); }
std::string GetMusicBackgroundPreferencesPath() {
    const auto root = GetMusicBackgroundRootPath();
    return root.empty() ? std::string{} : (std::filesystem::u8path(root) / "music-background.json").u8string();
}
namespace {
std::string Utf8Path(const std::filesystem::path& path) {
    const auto wide = path.wstring();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(bytes > 0 ? bytes : 0), '\0');
    if (bytes > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), bytes, nullptr, nullptr);
        result.resize(static_cast<std::size_t>(bytes - 1));
    }
    return result;
}
bool Writable(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error) return false;
    const auto probe = folder / L".lume-write-probe";
    const HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}
// Moves a library from the old per-user folder. Same volume: one rename.
// Across volumes the copy is staged beside the target and only renamed into
// place once complete, so an interrupted move leaves the old library in use.
bool MoveLibrary(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    if (std::filesystem::exists(to, error) && std::filesystem::is_empty(to, error)) std::filesystem::remove(to, error);
    error.clear();
    std::filesystem::rename(from, to, error);
    if (!error) return true;
    const auto staging = std::filesystem::path(to).concat(L".moving");
    std::filesystem::remove_all(staging, error);
    error.clear();
    std::filesystem::copy(from, staging, std::filesystem::copy_options::recursive, error);
    if (!error) std::filesystem::rename(staging, to, error);
    if (error) { std::filesystem::remove_all(staging, error); return false; }
    std::filesystem::remove_all(from, error);
    return true;
}
}  // namespace

// Wallpapers live beside the installed program, on the drive the user chose,
// not under %LOCALAPPDATA% on C:. If that folder cannot be written (a protected
// install location) the per-user folder is used instead. A library still in the
// per-user folder from an earlier version is moved over once.
std::string GetWallpaperRootPath() {
    wchar_t module[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    if (!length || length >= std::size(module)) return PathUnderLume(L"Wallpapers", true);
    return GetWallpaperRootPathFor(std::filesystem::path(module).parent_path().wstring());
}
std::string GetWallpaperRootPathFor(const std::wstring& programFolder) {
    return GetWallpaperRootPathFor(programFolder, {});
}
std::string GetWallpaperRootPath(const std::string& preferred) {
    wchar_t module[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    if (!length || length >= std::size(module)) return PathUnderLume(L"Wallpapers", true);
    return GetWallpaperRootPathFor(std::filesystem::path(module).parent_path().wstring(), preferred);
}
std::string GetWallpaperRootPathFor(const std::wstring& programFolder, const std::string& preferred) {
    // A directory the user picked in Settings wins, as long as it can be written.
    if (!preferred.empty()) {
        const auto chosen = std::filesystem::u8path(preferred);
        if (chosen.is_absolute() && Writable(chosen)) return Utf8Path(chosen);
    }
    const auto legacy = Utf8Path(LegacyDataRoot() / L"Wallpapers");
    const auto target = std::filesystem::path(programFolder) / L"Wallpapers";
    if (!Writable(target)) return legacy.empty() ? PathUnderLume(L"Wallpapers", true) : legacy;
    std::error_code error;
    const auto old = std::filesystem::u8path(legacy);
    if (!legacy.empty() && std::filesystem::exists(old / "library.json", error) &&
        !std::filesystem::exists(target / "library.json", error) &&
        !std::filesystem::equivalent(old, target, error) && !MoveLibrary(old, target))
        return legacy;
    return Utf8Path(target);
}
std::string GetUserDataRootPath() {
    const auto& root = DataRootWide();
    if (root.empty()) return {};
    return Utf8Path(std::filesystem::path(root));
}
bool SetDataDirectoryChoice(const std::string& directory) {
    if (directory.empty()) {
        DeleteRegistry(PendingValue);
        DeleteRegistry(DataValue);
        return true;
    }
    const auto chosen = std::filesystem::u8path(directory);
    if (!chosen.is_absolute() || !Writable(chosen)) return false;
    return WriteRegistry(PendingValue, chosen.wstring());
}
std::string PendingDataDirectory() {
    const auto pending = ReadRegistry(PendingValue);
    return pending.empty() ? std::string{} : Utf8Path(std::filesystem::path(pending));
}
bool ApplyPendingDataMove(std::string& error) {
    error.clear();
    const auto pending = ReadRegistry(PendingValue);
    const auto effective = ReadRegistry(DataValue);
    const auto current = effective.empty() ? LegacyDataRoot() : std::filesystem::path(effective);
    std::error_code ignored;
    if (!pending.empty()) {
        const std::filesystem::path target(pending);
        if (target != current && NeedsMove(current, target) &&
            !MoveLibrary(current, target)) {
            error = "data-move";
            DeleteRegistry(PendingValue);
            WriteRegistry(DataValue, current.wstring());
            return false;
        }
        WriteRegistry(DataValue, pending);
        DeleteRegistry(PendingValue);
        return true;
    }
    // Without a choice from Settings the data stays where it is: no automatic move.
    return true;
}
std::string GetApplicationSettingsPath() { return PreferencesPath(L"application-settings.json"); }
std::string GetDiagnosticsPath() { return PreferencesPath(L"diagnostics.json"); }
std::string GetAccountRootPath() { return PathUnderLume(L"Account", true); }

}  // namespace lume::platform::windows
