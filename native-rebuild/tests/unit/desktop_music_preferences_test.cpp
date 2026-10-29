#include "apps/shell/desktop_application.h"
#include "apps/shell/music_preferences_session.h"
#include "platform/windows/local_settings_file_store.h"
#include "vendor/nlohmann/json.hpp"

#include <filesystem>
#include <iostream>

namespace {
using Json = nlohmann::json;

Json LegacyPreferences() {
    return Json{{"version", 1}, {"mode", "gallery"}, {"fontFamily", "Arial"}, {"animationIntensity", "calm"},
        {"lyricsFontColor", "#ff2200"}, {"lyricsKaraokeFillColor", "#00aaff"}};
}
Json SonnetDefaults() {
    return Json{{"lyricsFontColor", "#ffffff"}, {"karaokeFillColor", "#38bdf8"}, {"glowColor", "#60a5fa"}};
}
Json AppliedPreferences() {
    auto value = LegacyPreferences();
    value["version"] = 3;
    value["effectMode"] = "star-river";
    value["sonnetColors"] = Json{{"lyricsFontColor", "#abcdef"}, {"karaokeFillColor", "#123abc"},
        {"glowColor", "#456def"}};
    return value;
}

struct Probe {
    lume::shell::DesktopApplication* app = nullptr;
    ULONGLONG started = 0;
    bool failed = false, opened = false, migrated = false, migratedV2 = false;
    bool applied = false, changed = false, effectChanged = false, reset = false, saved = false;
    void Check(bool value, const char* reason) {
        if (!value) { failed = true; std::cerr << "FAIL: " << reason << '\n'; }
    }
    void Message(const std::wstring& text) {
        const auto json = Json::parse(text);
        if (json.value("type", "") != "music-preferences-state") return;
        const auto request = json.value("request", 0);
        Check(json.value("version", 0) == 1, "preference state envelope stays v1");
        const auto& preferences = json.at("preferences");
        if (request == 0 && !migrated) {
            auto expected = LegacyPreferences();
            expected["version"] = 3; expected["sonnetColors"] = SonnetDefaults(); expected["effectMode"] = "none";
            Check(json.value("status", "") == "ready" && json.value("revision", 0) == 4 && preferences == expected,
                  "native bridge starts with migrated v1 settings, no effect and original Sonnet defaults");
            migrated = true;
        } else if (request == 38) {
            auto expected = AppliedPreferences(); expected["effectMode"] = "none";
            Check(migrated && json.value("status", "") == "ready" && json.value("revision", 0) == 5 &&
                  preferences == expected, "native bridge migrates exact v2 with all Sonnet colors and no effect");
            migratedV2 = true;
        } else if (request >= 30 && request <= 40) {
            Check(false, "invalid effect, legacy smuggling, future version or Sonnet DTO must not produce a response");
        } else if (request == 41) {
            Check(migratedV2 && json.value("status", "") == "ready" && json.value("revision", 0) == 6 &&
                  preferences == AppliedPreferences(), "native bridge saves independent v3 effect and colors");
            applied = true;
        } else if (request == 42) {
            auto expected = AppliedPreferences(); expected["sonnetColors"]["glowColor"] = "#c0ffee";
            Check(applied && json.value("status", "") == "ready" && json.value("revision", 0) == 7 &&
                  preferences == expected, "native bridge Sonnet-only change preserves effect and globals");
            changed = true;
        } else if (request == 45) {
            auto expected = AppliedPreferences(); expected["sonnetColors"]["glowColor"] = "#c0ffee";
            expected["effectMode"] = "rain-window";
            Check(changed && json.value("status", "") == "ready" && json.value("revision", 0) == 8 &&
                  preferences == expected, "native bridge effect-only change preserves renderer and every color");
            effectChanged = true;
        } else if (request == 43) {
            const auto expected = Json{{"version", 3}, {"mode", "partita"}, {"fontFamily", ""}, {"effectMode", "none"},
                {"animationIntensity", "normal"}, {"lyricsFontColor", "#ffffff"},
                {"lyricsKaraokeFillColor", "#ffffff"}, {"sonnetColors", SonnetDefaults()}};
            Check(effectChanged && json.value("status", "") == "ready" && json.value("revision", 0) == 9 &&
                  preferences == expected, "native bridge whole reset restores no effect and original Sonnet defaults");
            reset = true;
        } else if (request == 44) {
            Check(reset && json.value("status", "") == "ready" && json.value("revision", 0) == 10 &&
                  preferences == AppliedPreferences(), "native bridge reapplies v3 effect and colors after reset");
            saved = true;
        }
        if (failed || saved) app->Exit();
    }
    void Tick() {
        if (GetTickCount64() - started > 20000) {
            Check(false, "music preferences integration deadline"); app->Exit(); return;
        }
        if (!opened) { opened = true; app->Open(lume::shell::ManagerPage::Music); }
    }
};
Probe* active = nullptr;
void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) { if (active) active->Tick(); }
}

int main() {
    const auto root = std::filesystem::current_path() /
        ("desktop-music-preferences-fixture-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    if (!std::filesystem::create_directory(root)) return 1;
    lume::platform::windows::LocalSettingsFileStore files;
    const auto path = root / "music.json";
    const auto legacy = Json{{"version", 1}, {"revision", 4}, {"preferences", LegacyPreferences()}}.dump();
    if (!files.Write(path.u8string(), legacy)) return 1;
    lume::shell::DesktopApplicationOptions options;
    options.verification = true;
    options.startHidden = true;
    options.clockPreferencesPath = (root / "clock.json").u8string();
    options.calendarPreferencesPath = (root / "calendar.json").u8string();
    options.applicationSettings.preferencesPath = (root / "application.json").u8string();
    options.accountOptions.storageDirectory = (root / "account").u8string();
    options.wallpaperOptions.root = (root / "wallpapers").u8string();
    options.musicPreferencesPath = path.u8string();
    options.readyScript = LR"JS(
        (() => {
            const webview = window.chrome.webview;
            const preferences = {
                version:3, mode:'gallery', fontFamily:'Arial', animationIntensity:'calm', effectMode:'star-river',
                lyricsFontColor:'#FF2200', lyricsKaraokeFillColor:'#00AAFF',
                sonnetColors:{lyricsFontColor:'#ABCDEF',karaokeFillColor:'#123ABC',glowColor:'#456DEF'}};
            const post = (action, request, revision, value) => webview.postMessage({
                version:1, scope:'music-preferences', action, request, revision,
                ...(value ? {preferences:value} : {})});
            const receive = ({data}) => {
                if (data.type !== 'music-preferences-state' || data.status !== 'ready') return;
                if (data.request === 38) {
                    post('apply', 41, 5, preferences);
                } else if (data.request === 41) {
                    post('apply', 42, 6, {...preferences,
                        sonnetColors:{...preferences.sonnetColors,glowColor:'#C0FFEE'}});
                } else if (data.request === 42) {
                    post('apply', 45, 7, {...preferences, effectMode:'rain-window',
                        sonnetColors:{...preferences.sonnetColors,glowColor:'#C0FFEE'}});
                } else if (data.request === 45) {
                    post('reset', 43, 8);
                } else if (data.request === 43) {
                    post('apply', 44, 9, preferences);
                } else if (data.request === 44) {
                    webview.removeEventListener('message', receive);
                }
            };
            webview.addEventListener('message', receive);
            const {effectMode, ...withoutEffect} = preferences;
            const {sonnetColors, ...v1} = withoutEffect;
            post('apply', 30, 4, {...preferences,effectMode:'rain'});
            post('apply', 31, 4, withoutEffect);
            post('apply', 32, 4, {...preferences,version:2});
            post('apply', 33, 4, {...v1,version:1,effectMode:'none'});
            post('apply', 34, 4, {...preferences,effectMode:null});
            post('apply', 35, 4, {...preferences,version:4});
            post('apply', 39, 4, {...preferences,sonnetColors:{...preferences.sonnetColors,unknown:'#ffffff'}});
            post('apply', 40, 4, {...preferences,sonnetColors:{...preferences.sonnetColors,glowColor:'invalid'}});
            post('apply', 38, 4, {...withoutEffect,version:2});
        })();
    )JS";
    Probe probe;
    options.onMessage = [&](const auto& value) { probe.Message(value); };
    {
        lume::shell::DesktopApplication app(GetModuleHandleW(nullptr), std::move(options));
        probe.app = &app; probe.started = GetTickCount64(); active = &probe;
        const auto timer = SetTimer(nullptr, 0, 250, Tick);
        probe.Check(timer != 0, "integration timer created");
        probe.Check(app.Run() == 0, "isolated shell exited");
        KillTimer(nullptr, timer); active = nullptr;
    }
    probe.Check(probe.migrated && probe.migratedV2 && probe.applied && probe.changed && probe.effectChanged &&
                probe.reset && probe.saved, "WebView v1/v2 migration, apply, independent changes and reset reached native session");
    lume::shell::MusicPreferencesSession reopened(path, files);
    const auto restored = Json::parse(reopened.State());
    probe.Check(restored.value("revision", 0) == 10 && restored.at("preferences") == AppliedPreferences(),
                "fresh session restores v3 effect and all colors from isolated disk");
    std::string bytes;
    const auto persisted = files.ReadBounded(path.u8string(), lume::music::kMusicPreferencesMaxBytes, bytes);
    probe.Check(persisted && Json::parse(bytes) == Json({{"version", 1}, {"revision", 10},
                {"preferences", AppliedPreferences()}}), "persisted envelope stays v1 with canonical v3 preferences");
    const auto savedBytes = bytes;
    const auto resetCommand = lume::ui::DecodeMusicPreferencesCommand(Json{{"version", 1},
        {"scope", "music-preferences"}, {"action", "reset"}, {"request", 46}, {"revision", 10}});
    probe.Check(resetCommand.has_value(), "isolated reset command decodes");
    if (resetCommand && probe.saved) {
        const auto resetState = Json::parse(reopened.HandlePage(*resetCommand));
        probe.Check(resetState["revision"] == 11 && resetState["preferences"] ==
                    lume::music::EncodeMusicPreferences({}), "isolated reset restores exact v3 defaults");
        probe.Check(files.ReadBounded(path.u8string() + ".bak", lume::music::kMusicPreferencesMaxBytes, bytes) &&
                    bytes == savedBytes, "backup retains exact native bridge effect and colors");
        probe.Check(files.Write(path.u8string(), "damaged"), "damage only isolated main for backup recovery");
        lume::shell::MusicPreferencesSession recovered(path, files);
        const auto state = Json::parse(recovered.State());
        probe.Check(state["revision"] == 10 && state["preferences"] == AppliedPreferences(),
                    "isolated backup recovers native bridge effect and all colors");
    }
    if (!probe.failed) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::cout << "PASS: native WebView bridge v1/v2 migration, v3 effect/colors, independent revisions, reset and backup\n";
    }
    return probe.failed ? 1 : 0;
}
