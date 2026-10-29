#pragma once

#include "core/settings/file_store.h"

#include <string>

namespace lume::platform::windows {

class LocalSettingsFileStore final : public settings::IFileStore {
public:
    bool Read(const std::string& path, std::string& out) const override;
    bool ReadBounded(const std::string& path,std::size_t limit,std::string& out) const override;
    bool Exists(const std::string& path) const override;
    bool Write(const std::string& path, const std::string& value) override;
    bool Rename(const std::string& from, const std::string& to) override;
    bool Remove(const std::string& path) override;
};

std::string GetUiPreferencesPath();
std::string GetCalendarPreferencesPath();
std::string GetPerformancePreferencesPath();
std::string GetSpectrumPreferencesPath();
std::string GetAlmanacPreferencesPath();
std::string GetTodayTodoPreferencesPath();
std::string GetWeatherPreferencesPath();
std::string GetOrganizerPreferencesPath();
std::string GetRadialPreferencesPath();
std::string GetDockPreferencesPath();
std::string GetDynamicIslandPreferencesPath();
std::string GetDesktopIconsPreferencesPath();
std::string GetTaskbarPreferencesPath();
std::string GetMinimizePreferencesPath();
std::string GetMusicPreferencesPath();
std::string GetMusicBackgroundRootPath();
std::string GetMusicBackgroundPreferencesPath();
std::string GetWallpaperRootPath();
// Same, honouring a directory chosen in Settings (empty follows the program).
std::string GetWallpaperRootPath(const std::string& preferred);
// The wallpaper root for a program installed in programFolder (exposed for tests).
std::string GetWallpaperRootPathFor(const std::wstring& programFolder);
// Same, but an explicitly chosen directory wins when it can be written.
std::string GetWallpaperRootPathFor(const std::wstring& programFolder, const std::string& preferred);
// %LOCALAPPDATA%\Lume by default, or <install>\Data beside the program when that
// chosen in Settings.
std::string GetUserDataRootPath();
// Stores a chosen data folder for the next launch (empty restores the default).
bool SetDataDirectoryChoice(const std::string& directory);
std::string PendingDataDirectory();
// Moves the data folder to a pending choice before anything reads it; on failure
// the previous folder stays in use and error explains why.
bool ApplyPendingDataMove(std::string& error);
std::string GetApplicationSettingsPath();
std::string GetDiagnosticsPath();
std::string GetAccountRootPath();

}  // namespace lume::platform::windows
