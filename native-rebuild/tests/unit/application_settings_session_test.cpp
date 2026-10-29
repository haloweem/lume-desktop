#include "apps/shell/application_settings_session.h"
#include "vendor/nlohmann/json.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace lume::application_settings;
using namespace lume::shell;
using namespace lume::ui;
namespace {
void Check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
struct File final : IPreferencesFile {
    FileRead value{ReadStatus::Loaded, R"({"version":1,"language":"en","balancedMode":false,"autostart":true})"};
    std::atomic<bool> fail{false};
    FileRead Read() override { return value; }
    bool WriteAtomically(const std::string& bytes) override {
        if (fail) return false;
        value = {ReadStatus::Loaded, bytes};
        return true;
    }
};
struct Startup final : IStartupRegistration {
    StartupEntry value;
    std::atomic<int> reads{0}, writes{0};
    bool Read(StartupEntry& out) override { ++reads; out = value; return true; }
    bool Write(const StartupEntry& entry) override { value = entry; ++writes; return true; }
};
struct Transport final : IUpdateTransport {
    std::atomic<int> calls{0};
    std::atomic<bool> entered{false}, exited{false}, release{false}, offline{false}, current{false};
    std::atomic<bool> throwOnFetch{false};
    ManifestResponse Fetch(const Cancellation& cancellation) override {
        ++calls; entered = true; exited = false;
        while (!release && !cancellation.cancelled) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        exited = true;
        if (cancellation.cancelled) return {{}, "update-cancelled"};
        if (throwOnFetch) throw std::runtime_error("transport failure");
        if (offline) return {{}, "update-offline"};
        if (current) return {R"({"version":"1.0.1"})", {}};
        return {R"({"version":"1.0.9","downloadUrl":"https://lumedesktop.com/electron.exe"})", {}};
    }
};
template<class Predicate> bool Pump(Predicate done) {
    const auto start = GetTickCount64();
    while (!done()) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (GetTickCount64() - start > 3000) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}
nlohmann::json Json(const std::wstring& text) { return nlohmann::json::parse(text); }
void VerifyDefaultAutostartBoundary() {
    for (const bool verification : {false, true}) {
        auto file = std::make_shared<File>();
        file->value = {};
        auto startup = std::make_shared<Startup>();
        auto transport = std::make_shared<Transport>();
        ApplicationSettingsOptions options;
        options.verification = verification;
        options.testPorts = {file, startup, transport, L"C:\\Lume\\LumeShell.exe"};
        ApplicationSettingsSession session(GetModuleHandleW(nullptr), options, {}, [](bool) { return true; });
        Check(startup->reads == 0 && startup->writes == 0 && file->value.status == ReadStatus::Missing,
              "会话构造只读配置，不注册或持久化默认值");
        Check(session.Restore() && Pump([&] { return !session.HasWork(); }), "首次异步恢复完成");
        Check(Json(session.State()).at("error") == "" && transport->calls == 0,
              "默认恢复不触发更新网络且没有伪造注册错误");
        if (verification) {
            Check(startup->writes == 0 && file->value.status == ReadStatus::Missing &&
                  !session.Preferences().autostart, "验证模式即使注入可写端口也不尝试默认注册");
        } else {
            Check(startup->writes == 1 && session.Preferences().autostart &&
                  DecodePreferences(file->value.bytes).preferences.autostart,
                  "正常会话首次恢复真实开启并持久化默认值");
        }
        session.JoinShutdown();
    }
    auto disabled = std::make_shared<File>();
    disabled->value = {ReadStatus::Loaded, EncodePreferences({"zh-CN", true, false})};
    auto legacyStartup = std::make_shared<Startup>();
    legacyStartup->value = EnabledStartupEntry(L"\"C:\\Lume\\lume.exe\" -tray-only");
    ApplicationSettingsOptions legacyVerification;
    legacyVerification.verification = true;
    legacyVerification.testPorts = {disabled, legacyStartup, std::make_shared<Transport>(), L"C:\\Lume\\LumeShell.exe"};
    ApplicationSettingsSession legacySession(GetModuleHandleW(nullptr), legacyVerification, {}, [](bool) { return true; });
    Check(legacySession.Restore() && Pump([&] { return !legacySession.HasWork(); }) &&
          legacyStartup->writes == 0 && legacySession.Preferences().autostart &&
          !DecodePreferences(disabled->value.bytes).preferences.autostart,
          "验证模式不删除或迁移旧入口，只显示系统实际状态");
    legacySession.JoinShutdown();
    auto file = std::make_shared<File>();
    file->value = {};
    ApplicationSettingsOptions verification;
    verification.verification = true;
    verification.testPorts.preferencesFile = file;
    ApplicationSettingsSession session(GetModuleHandleW(nullptr), verification, {}, [](bool) { return true; });
    Check(session.Restore() && Pump([&] { return !session.HasWork(); }) &&
          !session.Preferences().autostart && Json(session.State()).at("error") == "" &&
          file->value.status == ReadStatus::Missing, "默认验证适配器保持只读隔离边界");
}
void VerifyRuntimeErrorAtPageBoundary() {
    auto file = std::make_shared<File>();
    file->value = {ReadStatus::Loaded, EncodePreferences({})};
    file->fail = true;
    auto startup = std::make_shared<Startup>();
    auto transport = std::make_shared<Transport>();
    transport->current = true;
    ApplicationSettingsOptions options;
    options.verification = true;
    options.testPorts = {file, startup, transport, L"C:\\Lume\\LumeShell.exe"};
    bool runtime = true;
    int calls = 0;
    bool expectMismatch = false;
    ApplicationSettingsSession session(GetModuleHandleW(nullptr), options, [&](const std::wstring& text) {
        if (expectMismatch) Check(Json(text).at("error") == "balanced-rollback",
                                  "UI completion notifications retain unresolved runtime errors");
    }, [&](bool enabled) {
        if (++calls == 2) return false;
        runtime = enabled;
        return true;
    });
    Check(session.HandlePage({ApplicationSettingsAction::BalancedMode, {}, false}).has_value() &&
          Pump([&] { return !session.HasWork(); }) && !runtime && session.BalancedMode() &&
          Json(session.State()).at("error") == "balanced-rollback", "page fixture has a real no-effect rollback failure");
    file->fail = false;
    expectMismatch = true;
    const auto saving = session.HandlePage({ApplicationSettingsAction::Language, "en"});
    Check(saving && Json(*saving).at("busy") && Json(*saving).at("error") == "balanced-rollback",
          "enqueue cannot temporarily erase an unresolved runtime error");
    Check(Pump([&] { return !session.HasWork(); }) && session.Preferences().language == "en" &&
          Json(session.State()).at("error") == "balanced-rollback", "language completion retains runtime mismatch");
    const auto checking = session.HandlePage({ApplicationSettingsAction::CheckUpdate});
    Check(checking && Json(*checking).at("error") == "balanced-rollback" &&
          Pump([&] { return transport->entered.load(); }), "checking reply retains runtime mismatch");
    const auto concurrent = session.HandlePage({ApplicationSettingsAction::Language, "zh-CN"});
    Check(concurrent && Json(*concurrent).at("error") == "balanced-rollback",
          "busy rejection cannot replace the runtime mismatch");
    transport->release = true;
    Check(Pump([&] { return !session.HasWork(); }) && Json(session.State()).at("update").at("status") == "current" &&
          Json(session.State()).at("error") == "balanced-rollback" && calls == 2,
          "successful read-only update result cannot clear or repair runtime state");
    transport->throwOnFetch = true;
    Check(session.HandlePage({ApplicationSettingsAction::CheckUpdate}).has_value() &&
          Pump([&] { return !session.HasWork(); }) && Json(session.State()).at("error") == "balanced-rollback",
          "an unrelated worker exception cannot mask the unresolved runtime error");
    expectMismatch = false;
    const auto applying = session.HandlePage({ApplicationSettingsAction::BalancedMode, {}, true});
    Check(applying && Json(*applying).at("error") == "balanced-rollback",
          "balanced retry does not claim recovery before the port has completed");
    Check(Pump([&] { return !session.HasWork(); }) && runtime && calls == 3 &&
          Json(session.State()).at("error") == "", "confirmed explicit balanced reapply clears the UI error");
}
void VerifyPreferenceSaveOnExit() {
    for (const auto action : {ApplicationSettingsAction::Language, ApplicationSettingsAction::BalancedMode,
                              ApplicationSettingsAction::Autostart}) {
        auto file = std::make_shared<File>();
        file->value = {ReadStatus::Loaded, EncodePreferences({"zh-CN", false, true})};
        auto startup = std::make_shared<Startup>();
        startup->value = EnabledStartupEntry(*StartupCommand(L"C:/Lume/LumeShell.exe"));
        ApplicationSettingsOptions options; options.verification = true;
        options.testPorts = {file, startup, std::make_shared<Transport>(), L"C:/Lume/LumeShell.exe"};
        std::atomic<bool> entered{false}, release{false};
        int notifications = 0;
        ApplicationSettingsSession session(GetModuleHandleW(nullptr), options,
            [&](const std::wstring&) { ++notifications; }, [&](bool enabled) {
                if (enabled) { entered = true; while (!release) std::this_thread::yield(); }
                return true;
            });
        Check(session.HandlePage({action, "en", action == ApplicationSettingsAction::BalancedMode}).has_value(),
              "setting accepted immediately before exit");
        if (action == ApplicationSettingsAction::BalancedMode)
            Check(Pump([&] { return entered.load(); }), "balanced save is deterministically in flight at exit");
        session.Shutdown();
        std::thread finishRuntime([&] { Sleep(20); release = true; });
        session.FlushPreferences();
        finishRuntime.join();
        Check(!session.HasWork(), "accepted preference finishes before its runtime dependency is stopped");
        session.JoinShutdown();
        const auto saved = DecodePreferences(file->value.bytes);
        Check(saved.error.empty() && notifications == 0, "exit save commits without late UI notifications");
        Check(action != ApplicationSettingsAction::Language || saved.preferences.language == "en",
              "language accepted before exit survives restart");
        Check(action != ApplicationSettingsAction::BalancedMode || saved.preferences.balancedMode,
              "in-flight balanced change survives restart");
        Check(action != ApplicationSettingsAction::Autostart || !saved.preferences.autostart,
              "unchecked autostart accepted before exit survives restart");
        ApplicationSettingsSession restored(GetModuleHandleW(nullptr), options, {}, [](bool) { return true; });
        Check(restored.Preferences().language == saved.preferences.language &&
              restored.Preferences().balancedMode == saved.preferences.balancedMode &&
              restored.Preferences().autostart == saved.preferences.autostart,
              "new settings session reads all final preferences");
    }
}
}
int main() {
    VerifyPreferenceSaveOnExit();
    VerifyDefaultAutostartBoundary();
    VerifyRuntimeErrorAtPageBoundary();
    auto file = std::make_shared<File>();
    auto startup = std::make_shared<Startup>();
    auto transport = std::make_shared<Transport>();
    ApplicationSettingsOptions options;
    options.verification = true;
    options.testPorts = {file, startup, transport, L"C:\\Lume Native\\LumeShell.exe"};
    startup->value = EnabledStartupEntry(*StartupCommand(options.testPorts.executablePath));
    const auto owner = GetCurrentThreadId();
    std::atomic<int> balancedCalls{0};
    std::atomic<bool> balancedEntered{false}, balancedRelease{true};
    bool runtime = true;
    int notifications = 0;
    std::uint64_t lastRevision = 0;
    ApplicationSettingsSession session(GetModuleHandleW(nullptr), options, [&](const std::wstring& text) {
        Check(GetCurrentThreadId() == owner, "Notify executes only on owning UI thread");
        const auto json = Json(text);
        Check(json.at("revision").get<std::uint64_t>() > lastRevision, "notifications have increasing revisions");
        lastRevision = json.at("revision").get<std::uint64_t>();
        ++notifications;
    }, [&](bool enabled) {
        Check(GetCurrentThreadId() != owner, "Restore and balanced changes never wait on the UI thread");
        ++balancedCalls; balancedEntered = true;
        while (!balancedRelease) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        runtime = enabled;
        return true;
    });
    Check(session.Preferences().language == "en" && !session.BalancedMode() &&
          session.Preferences().autostart && balancedCalls == 0, "构造阶段保留保存的意图但不触碰运行时");
    Check(transport->calls == 0 && startup->writes == 0, "construction never checks updates or writes Run");
    Check(startup->reads == 0 && notifications == 0, "constructor only loads the file and never calls UI");
    Check(session.Restore(), "restore queues independent worker");
    Check(Pump([&] { return !session.HasWork(); }) && balancedCalls == 1 && !runtime &&
          startup->reads > 0 && session.Preferences().autostart && startup->writes == 0 && transport->calls == 0,
          "restore reads real Run state and applies balanced preferences without any registration or network writes");

    balancedEntered = false; balancedRelease = false;
    const auto start = GetTickCount64();
    Check(session.HandlePage({ApplicationSettingsAction::BalancedMode, {}, true}).has_value(), "balanced action accepted");
    Check(GetTickCount64() - start < 100, "UI does not wait on a slow wallpaper port");
    Check(Pump([&] { return balancedEntered.load(); }), "worker reaches runtime port");
    Check(Json(session.State()).at("busy") && !session.BalancedMode(), "pending change preserves confirmed snapshot");
    Check(session.HandlePage({ApplicationSettingsAction::Language, "zh-CN"}).has_value() &&
          session.Preferences().language == "en", "concurrent mutation cannot overtake in-flight balanced transaction");
    balancedRelease = true;
    Check(Pump([&] { return !session.HasWork(); }) && session.BalancedMode() && runtime, "worker completion publishes confirmed value");

    Check(session.HandlePage({ApplicationSettingsAction::CheckUpdate}).has_value(), "manual update check accepted");
    Check(Pump([&] { return transport->entered.load(); }), "manual check executes asynchronously");
    Check(session.HandlePage({ApplicationSettingsAction::CheckUpdate}).has_value() && transport->calls == 1,
          "concurrent manual checks coalesce without duplicate requests");
    transport->release = true;
    Check(Pump([&] { return !session.HasWork(); }) &&
          Json(session.State()).at("update").at("status") == "incompatible", "old Electron release is never downloadable");
    Check(session.HandlePage({ApplicationSettingsAction::DownloadUpdate}).has_value(), "download action is explicitly rejected by backend");
    Check(Pump([&] { return !session.HasWork(); }) && transport->calls == 1 &&
          Json(session.State()).at("update").at("status") == "incompatible", "no download network request for incompatible source");
    Check(session.HandlePage({ApplicationSettingsAction::InstallUpdate}).has_value(), "install action handled without execution");
    Check(Pump([&] { return !session.HasWork(); }) &&
          Json(session.State()).at("update").at("status") == "incompatible", "never pretend an installation started");
    transport->offline = true;
    Check(session.HandlePage({ApplicationSettingsAction::CheckUpdate}).has_value(), "offline retry accepted");
    Check(Pump([&] { return !session.HasWork(); }) &&
          Json(session.State()).at("error") == "update-offline", "offline error reaches UI truthfully");
    transport->entered = false; transport->release = false;
    Check(session.HandlePage({ApplicationSettingsAction::CheckUpdate}).has_value(), "start a cancellable request");
    Check(Pump([&] { return transport->entered.load(); }), "request entered before shutdown");
    const auto count = notifications;
    const auto shutdownStart = GetTickCount64();
    session.Shutdown();
    Check(GetTickCount64() - shutdownStart < 100, "shutdown does not wait for network");
    Check(Pump([&] { return !session.HasWork(); }) && transport->exited && notifications == count,
          "shutdown cancels worker and discards stale notifications");
    Check(!Json(session.State()).at("busy").get<bool>(), "cancelled snapshot stops reporting busy when the worker really exits");
    Check(!session.HandlePage({ApplicationSettingsAction::Autostart, {}, true}), "stopped session refuses mutations");
    session.Shutdown();
    session.JoinShutdown();
    session.JoinShutdown();
    Check(!session.HasWork(), "joined worker has really stopped");

    std::atomic<bool> portEntered{false}, actorStopped{false};
    std::atomic<int> portCalls{0};
    int lateNotifications = 0;
    ApplicationSettingsSession stopping(GetModuleHandleW(nullptr), options,
        [&](const std::wstring&) { ++lateNotifications; }, [&](bool) {
            ++portCalls;
            portEntered = true;
            while (!actorStopped) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return false;
        });
    Check(stopping.Restore() && Pump([&] { return portEntered.load(); }), "restore port blocks only its own worker");
    const auto beforeCancel = lateNotifications;
    const auto cancelStart = GetTickCount64();
    stopping.Shutdown();
    Check(GetTickCount64() - cancelStart < 100 && stopping.HasWork(),
          "cancel does not hide or detach a still-executing wallpaper port");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    Check(stopping.HasWork(), "HasWork stays true throughout an outstanding runtime call");
    actorStopped = true;
    stopping.JoinShutdown();
    Check(!stopping.HasWork() && portCalls == 1 && lateNotifications == beforeCancel,
          "cancel actor then join stops every dependency call before wallpaper destruction");
    transport->entered = false;
    transport->exited = false;
    transport->release = false;
    {
        ApplicationSettingsSession destruction(GetModuleHandleW(nullptr), options, {}, [](bool) { return true; });
        Check(destruction.HandlePage({ApplicationSettingsAction::CheckUpdate}).has_value() &&
              Pump([&] { return transport->entered.load(); }), "destructor cancellation fixture entered fetch");
    }
    Check(transport->exited, "destruction joins a cancellable fetch without needing a UI message pump");
    std::cout << "application settings session passed\n";
}
