#include "platform/windows/local_settings_file_store.h"
#include "core/settings/json_settings_codec.h"
#include "core/settings/settings_store.h"
#include <windows.h>
#include <filesystem>
#include <cstdlib>
#include <iostream>

void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"lume-config-test-" + std::to_wstring(GetCurrentProcessId())) / L"中文";
    std::filesystem::create_directories(root);
    const auto path = (root / "preferences.json").u8string();
    lume::platform::windows::LocalSettingsFileStore files;
    lume::settings::JsonSettingsCodec codec;
    lume::settings::SettingsStore store(files, codec);
    lume::settings::SettingsSnapshot value;
    for (const auto* style : {"mond", "insideDream", "welfare"}) {
        value.clock.style = style;
        value.clock.enabled = !value.clock.enabled;
        Expect(store.Save(path, value), "repeated atomic saves succeed on a Unicode path");
        const auto loaded = store.Load(path);
        Expect(loaded.clock.style == style && loaded.clock.enabled == value.clock.enabled, "real persisted values reload");
    }
    std::string previous;
    Expect(files.Read(path, previous), "read saved preferences");
    Expect(!files.Rename(path + ".missing", path), "missing source rename fails");
    std::string retained;
    Expect(files.Read(path, retained) && retained == previous, "failed rename never deletes destination");
    retained="unchanged";
    Expect(!files.ReadBounded(path,previous.size()-1,retained)&&retained=="unchanged","bounded read rejects oversize without modifying output");
    Expect(files.ReadBounded(path,previous.size(),retained)&&retained==previous,"bounded read accepts exact limit on Unicode path");
    Expect(files.Write(path+".empty","")&&files.ReadBounded(path+".empty",0,retained)&&retained.empty(),"zero-byte files remain readable within zero limit");
    Expect(!files.Write({}, "value") && !files.Rename(path, {}) && !files.Exists({}),
           "empty configuration path cannot write into the working directory");
    const auto appData = root / "appdata";
    std::filesystem::create_directories(appData);
    SetEnvironmentVariableW(L"LOCALAPPDATA", appData.c_str());
    using lume::platform::windows::GetWallpaperRootPathFor;
    const auto program = root / "program";
    std::filesystem::create_directories(program);
    const auto wallpaperRoot = GetWallpaperRootPathFor(program.wstring());
    Expect(wallpaperRoot == (program / "Wallpapers").u8string(),
           "wallpaper storage sits beside the installed program");
    Expect(std::filesystem::is_directory(std::filesystem::u8path(wallpaperRoot)),
           "wallpaper storage root is created independently of clock preferences");
    // A library from an earlier version in the per-user folder moves over once, whole.
    const auto legacy = appData / "Lume" / "Wallpapers";
    std::filesystem::create_directories(legacy / "media");
    Expect(files.Write((legacy / "library.json").u8string(), "{\"items\":[]}") &&
           files.Write((legacy / "media" / "a.apk").u8string(), "media"), "prepare an old library");
    const auto moved = root / "moved";
    std::filesystem::create_directories(moved);
    std::string media;
    Expect(GetWallpaperRootPathFor(moved.wstring()) == (moved / "Wallpapers").u8string() &&
           files.Read((moved / "Wallpapers" / "media" / "a.apk").u8string(), media) && media == "media" &&
           !std::filesystem::exists(legacy / "library.json"), "old library moved beside the program");
    Expect(GetWallpaperRootPathFor(moved.wstring()) == (moved / "Wallpapers").u8string(),
           "a moved library is not moved again");
    // A program folder that cannot hold the library keeps the per-user folder.
    const auto blocked = root / "blocked";
    std::filesystem::create_directories(blocked);
    Expect(files.Write((blocked / "Wallpapers").u8string(), "occupied"), "block the program-side folder");
    Expect(GetWallpaperRootPathFor(blocked.wstring()) == legacy.u8string(),
           "unwritable program folder falls back to the per-user folder");
    const auto profile = root / "profile";
    std::filesystem::create_directories(profile);
    Expect(files.Write((profile / "Lume").u8string(), "occupied"), "prepare blocked preferences directory");
    SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str());
    // The per-user folder is only a fallback now: data normally lives beside the
    // program, so a blocked profile must not throw and must not be used.
    const auto preferences = lume::platform::windows::GetUiPreferencesPath();
    const auto profilePrefix = std::filesystem::u8path(profile.u8string() + "\\").u8string();
    const std::string suffix = "ui-preferences.json";
    const bool endsWith = preferences.size() >= suffix.size() &&
        preferences.compare(preferences.size() - suffix.size(), suffix.size(), suffix) == 0;
    Expect(!preferences.empty() && endsWith && preferences.rfind(profilePrefix, 0) != 0,
           "blocked preferences directory falls back to the program-side data folder");
    std::filesystem::remove_all(root.parent_path());
}
