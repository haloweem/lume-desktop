#include "apps/shell/desktop_application.h"
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using lume::shell::DesktopApplication;
struct Probe {
    DesktopApplication* app = nullptr;
    ULONGLONG started = 0;
    bool failed = false, hidden = false, opened = false, settings = false, account = false,
        diagnostics = false, diagnosticsCleared = false;
    std::uint64_t initialDiagnosticsRevision = 0;
    unsigned diagnosticsSnapshots = 0;
    std::filesystem::path diagnosticsPath;
    void Check(bool value, const char* reason) {
        if (!value) { failed = true; std::cerr << "FAIL: " << reason << '\n'; }
    }
    void Message(const std::wstring& text) {
        const auto json = nlohmann::json::parse(text);
        if (json.value("type", "") == "diagnostics-state") {
            diagnostics = true;
            Check(json.value("schemaVersion", 0) == 1, "native diagnostics use the versioned schema");
            Check(json.contains("events") && json.at("events").is_array(), "native diagnostics include bounded events");
            Check(json.contains("health") && json.at("health").is_object(), "native diagnostics include module health");
            Check(json.contains("mismatches") && json.at("mismatches").is_array(), "native diagnostics include current mismatches");
            const auto revision=json.value("revision",0ULL);
            if(diagnosticsSnapshots++==0)initialDiagnosticsRevision=revision;
            else if(revision>initialDiagnosticsRevision)diagnosticsCleared=true;
        }
        if (json.value("type", "") == "account-state") {
            account = true;
            Check(json.value("status", "") == "signed-out", "isolated verification cannot restore real credentials");
            Check(!json.contains("accessToken") && !json.contains("refreshToken"), "only public account snapshot");
        }
        if (json.value("type", "") == "app-settings-state" && json.value("language", "") == "en" &&
            !json.value("busy", true)) {
            settings = true;
            Check(json.value("error", "") == "", "language applies without system or account effects");
        }
        if (settings && account && GetTickCount64()-started>1000) app->Exit();
    }
    void Tick() {
        if (GetTickCount64() - started > 15000) {
            Check(false, "integrated settings/account deadline"); app->Exit(); return;
        }
        if (!opened) {
            Check(!app->ManagerHandle(), "tray-only startup does not create a management WebView");
            std::ifstream recorded(diagnosticsPath);
            nlohmann::json snapshot;
            if(recorded)recorded>>snapshot;
            Check(snapshot.is_object()&&snapshot.value("previous-record","")=="preserved",
                "disabled diagnostics do not read or overwrite previous records during tray-only startup");
            hidden = true;
            opened = true;
            app->Open(lume::shell::ManagerPage::ApplicationSettings);
        }
        if(settings&&account&&GetTickCount64()-started>1000)app->Exit();
    }
};
Probe* active = nullptr;
void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) { if (active) active->Tick(); }
}

int main() {
    const auto root = std::filesystem::current_path() /
        ("desktop-settings-account-fixture-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    lume::shell::DesktopApplicationOptions options;
    options.verification = true;
    options.startHidden = true;
    options.clockPreferencesPath = (root / "clock.json").u8string();
    options.calendarPreferencesPath = (root / "calendar.json").u8string();
    options.applicationSettings.preferencesPath = (root / "application.json").u8string();
    options.diagnosticsPath = (root / "diagnostics.json").u8string();
    {std::ofstream previous(root / "diagnostics.json");previous<<R"({"previous-record":"preserved"})";}
    options.accountOptions.storageDirectory = (root / "account").u8string();
    options.wallpaperOptions.root = (root / "wallpapers").u8string();
    options.readyScript = LR"JS(
        const settingsProbe = setInterval(() => {
            const language = document.querySelector('[data-setting="language"]');
            if (!language || language.disabled || !document.querySelector('.application-settings:not([hidden])')) return;
            clearInterval(settingsProbe);
            if(document.querySelector('[data-settings-section="diagnostics"]'))return;
            document.querySelector('[data-settings-section="user"]').click();
            document.querySelector('[data-settings-section="general"]').click();
            let diagnosticsRevision = null;
            chrome.webview.addEventListener('message', event => {
              if (event.data?.type !== 'diagnostics-state') return;
              if (diagnosticsRevision === null) {
                diagnosticsRevision = event.data.revision;
                chrome.webview.postMessage({version:1,scope:'diagnostics',action:'clear'});
              }
            });
            chrome.webview.postMessage({version:1,scope:'diagnostics',action:'snapshot'});
            chrome.webview.postMessage({version:1,scope:'diagnostics',action:'clear'});
            language.value = 'en';
            language.dispatchEvent(new Event('change', {bubbles:true}));
        }, 50);
    )JS";
    Probe probe;
    probe.diagnosticsPath=root/"diagnostics.json";
    options.onMessage = [&](const auto& value) { probe.Message(value); };
    int result = 1;
    {
        DesktopApplication application(GetModuleHandleW(nullptr), std::move(options));
        probe.app = &application;
        probe.started = GetTickCount64();
        active = &probe;
        const auto timer = SetTimer(nullptr, 0, 250, Tick);
        probe.Check(timer != 0, "test timer created");
        result = application.Run();
        KillTimer(nullptr, timer);
        active = nullptr;
    }
    probe.Check(result == 0 && probe.hidden && probe.settings && probe.account && !probe.diagnostics && !probe.diagnosticsCleared,
        "complete settings/account lifecycle with diagnostics disabled and no diagnostic replies");
    nlohmann::json preferences;
    {
        std::ifstream saved(root / "application.json");
        if (saved) saved >> preferences;
    }
    probe.Check(preferences.value("language", "") == "en", "language persisted in the isolated file");
    nlohmann::json diagnosticStore;
    {
        std::ifstream diagnosticFile(root / "diagnostics.json", std::ios::binary);
        if (diagnosticFile) diagnosticFile >> diagnosticStore;
    }
    probe.Check(diagnosticStore.is_object() && diagnosticStore.value("previous-record", "") == "preserved"&&diagnosticStore.size()==1,
        "disabled snapshot and clear commands preserve the existing diagnostic file after manager shutdown");
    std::error_code cleanupError;
    std::filesystem::remove_all(root,cleanupError);
    probe.Check(!cleanupError,"isolated diagnostics fixture is cleaned up");
    if (!probe.failed) std::cout << "PASS: integrated settings/account snapshots, persistence and tray-only lifecycle\n";
    return probe.failed ? 1 : 0;
}
