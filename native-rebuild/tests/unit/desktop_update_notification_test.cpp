#include "apps/shell/desktop_application.h"
#include "core/update_notification/update_notification.h"
#include <atomic>
#include <filesystem>
#include <iostream>
#include <type_traits>
#include <utility>

namespace {
using namespace lume;
bool passed = true;
void Check(bool value, const char* message) {
    if (!value) { passed = false; std::cerr << "FAIL: " << message << '\n'; }
}
struct File final : application_settings::IPreferencesFile {
    application_settings::FileRead value;
    int writes = 0;
    application_settings::FileRead Read() override { return value; }
    bool WriteAtomically(const std::string& bytes) override {
        ++writes; value = {application_settings::ReadStatus::Loaded, bytes}; return true;
    }
};
struct Startup final : application_settings::IStartupRegistration {
    bool Read(application_settings::StartupEntry& entry) override { entry = {}; return true; }
    bool Write(const application_settings::StartupEntry&) override { Check(false, "no real startup writes"); return false; }
};
struct Transport final : application_settings::IUpdateTransport {
    std::atomic<int> calls{0};
    application_settings::ManifestResponse Fetch(const application_settings::Cancellation&) override {
        ++calls; return {R"({"version":"1.0.9"})", {}};
    }
};
shell::DesktopApplication* active = nullptr;
ULONGLONG deadline = 0;
bool delivered = false;
std::shared_ptr<File> activeLedger;
ULONGLONG completedAt = 0;
HWND originalForeground = nullptr;
void CALLBACK Poll(HWND, UINT, UINT_PTR, DWORD) {
    if (!active) return;
    Check(!active->ManagerHandle(), "background checks never create or restore the manager");
    Check(!FindWindowW(L"LumeNativeUpdateNotice", nullptr), "background checks never create native update popups");
    Check(GetForegroundWindow() == originalForeground, "background checks preserve the foreground application");
    if (activeLedger && activeLedger->writes && !completedAt) completedAt = GetTickCount64();
    // Keep pumping after completion to catch delayed page-ready or presenter work.
    if (completedAt && GetTickCount64() - completedAt >= 300) delivered = true;
    if (delivered || GetTickCount64() >= deadline) {
        Check(delivered, "automatic update completes and persists without opening any UI");
        active->Exit();
    }
}
template<class T, class = void> struct HasNotifications : std::false_type {};
template<class T> struct HasNotifications<T, std::void_t<decltype(std::declval<T&>().updateNotification)>> : std::true_type {};
template<class Options> void Verify(const std::filesystem::path& root, bool inApp) {
    if constexpr (!HasNotifications<Options>::value) {
        Check(false, "desktop composition must own update notification options and session");
    } else {
        Options options; options.verification = true; options.startHidden = true;
        options.clockPreferencesPath = (root / "clock.json").u8string();
        options.calendarPreferencesPath = (root / "calendar.json").u8string();
        options.performancePreferencesPath = (root / "performance.json").u8string();
        options.almanacPreferencesPath = (root / "almanac.json").u8string();
        options.weatherPreferencesPath = (root / "weather.json").u8string();
        options.organizerPreferencesPath = (root / "organizer.json").u8string();
        options.desktopIconsPreferencesPath = (root / "icons.json").u8string();
        options.taskbarPaths.stateFile = (root / "taskbar.json").u8string();
        options.minimizePreferencesPath = (root / "minimize.json").u8string();
        options.wallpaperOptions.root = (root / "wallpapers").u8string();
        options.accountOptions.storageDirectory = (root / "account").u8string();
        auto settings = std::make_shared<File>();
        settings->value = {application_settings::ReadStatus::Loaded,
            application_settings::EncodePreferences({"en", false, false})};
        auto transport = std::make_shared<Transport>();
        options.applicationSettings.testPorts = {settings, std::make_shared<Startup>(), transport, L"C:\\Lume\\LumeShell.exe"};
        auto ledger = std::make_shared<File>();
        options.updateNotification.statePath = (root / "never-write-production.json").u8string();
        options.updateNotification.testPorts.file = ledger;
        options.updateNotification.testPorts.now = [] { return update_notification::ClockSample{1790726400000LL, 20260930}; };
        int shown = 0, closed = 0;
        bool checking = false;
        options.onMessage = [&](const std::wstring& text) {
            if (text.find(L"\"status\":\"checking\"") != text.npos) checking = true;
            if (text.find(L"\"type\":\"app-update-notice\"") != text.npos) {
                Check(false, "automatic result must not be posted as a modal notice");
                ++shown;
            }
        };
        if (!inApp) options.updateNotification.testPorts.show = [&](const platform::windows::UpdateNotice& notice) {
            ++shown;
            (void)notice;
            Check(false, "automatic result must not reach a native presenter");
            return true;
        };
        options.updateNotification.testPorts.shutdownPresenter = [&] { ++closed; };
        int result = 1;
        {
            shell::DesktopApplication app(GetModuleHandleW(nullptr), std::move(options));
            active = &app; activeLedger = ledger; completedAt = 0;
            originalForeground = GetForegroundWindow();
            delivered = false; deadline = GetTickCount64() + 20000;
            const auto timer = SetTimer(nullptr, 0, 25, Poll);
            Check(timer != 0, "isolated lifecycle timer created");
            result = app.Run();
            KillTimer(nullptr, timer); active = nullptr; activeLedger.reset();
        }
        Check(result == 0 && checking && shown == 0 && closed == 1 && transport->calls == 1 && ledger->writes == 1,
            "silent update lifecycle persists once and shuts down once");
        Check(!std::filesystem::exists(root / "never-write-production.json"), "verification uses only injected ledger");
        MSG quit{}; Check(!PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE), "composition consumes its own quit");
    }
}
}
int main(int argc, char** argv) {
    Check(argc == 2, "explicit isolated output directory required");
    if (argc == 2) {
        const auto root = std::filesystem::u8path(argv[1]) / std::to_string(GetCurrentProcessId());
        std::filesystem::create_directories(root);
        Verify<shell::DesktopApplicationOptions>(root / "native-fallback", false);
        Verify<shell::DesktopApplicationOptions>(root / "in-app", true);
    }
    if (passed) std::cout << "PASS silent native-fallback and in-app update composition\n";
    return passed ? 0 : 1;
}
