#include "core/calendar/calendar_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include <windows.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}
int main() {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr,executable,32768);
    Expect(length != 0 && length < 32768, "resolve test executable directory");
    const auto root = std::filesystem::path(executable).parent_path() / L"calendar-preferences-artifacts" /
        std::to_wstring(GetCurrentProcessId()) / L"\u65e5\u5386\u914d\u7f6e";
    std::filesystem::create_directories(root);
    const auto path = (root / L"\u504f\u597d.json").u8string();
    lume::platform::windows::LocalSettingsFileStore files;
    lume::calendar::CalendarPreferencesStore store(files);
    lume::calendar::Preferences expected;
    expected.enabled = true; expected.theme = lume::calendar::Theme::Split;
    expected.textColor = {0xaabbcc,30}; expected.profiles[1] = {-120,20,999,555,true};
    Expect(store.Save(path,expected), "real Unicode-path calendar save succeeds");
    lume::platform::windows::LocalSettingsFileStore reopenedFiles;
    lume::calendar::CalendarPreferencesStore reopened(reopenedFiles);
    const auto loaded = reopened.Load(path);
    Expect(loaded.source == lume::calendar::PreferencesSource::Main && loaded.value.enabled &&
        loaded.value.theme == lume::calendar::Theme::Split && loaded.value.Active()->x == -120 &&
        loaded.value.Active()->width == 999 && loaded.value.textColor.alpha == 30,
        "fresh file adapter and store reopen persisted Unicode-path preferences");
    std::string previous;
    Expect(files.Read(path,previous), "read original bytes");
    const auto temporary = std::filesystem::u8path(path + ".tmp");
    std::filesystem::create_directory(temporary);
    Expect(!reopened.Save(path,{}), "real temporary write failure reported");
    std::string retained;
    Expect(files.Read(path,retained) && retained == previous, "real failed write preserves primary bytes");
    std::error_code ignored;
    std::filesystem::remove(temporary,ignored);
    Expect(reopened.Save(path,{}), "save succeeds after obstruction removed");
    Expect(files.Read(path + ".bak",retained) && retained == previous, "real backup contains exact previous bytes");
    Expect(files.Write(path,"bad") && files.Write(path + ".corrupt","existing evidence"), "prepare corruption and existing evidence");
    const auto recovery = reopened.Load(path);
    Expect(recovery.source == lume::calendar::PreferencesSource::Backup && recovery.value.enabled &&
        recovery.error == lume::calendar::LoadError::MainInvalid, "real corrupt primary recovers valid backup");
    Expect(files.Read(path + ".corrupt",retained) && retained == "existing evidence" &&
        files.Read(path + ".corrupt.1",retained) && retained == "bad", "real isolation preserves earlier evidence");
    Expect(!files.Rename(path + ".missing",path + ".bak") && files.Read(path + ".bak",retained) &&
        retained == previous, "failed native rename preserves destination");
    Expect(!reopened.Save({},{}), "real store refuses empty path");
    std::filesystem::remove_all(root.parent_path());
}
