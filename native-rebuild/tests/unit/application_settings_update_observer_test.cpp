#include "apps/shell/application_settings_session.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace lume::application_settings;
using namespace lume::shell;
namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
struct File final : IPreferencesFile {
    FileRead Read() override { return {ReadStatus::Loaded, EncodePreferences({"zh-CN", true, false})}; }
    bool WriteAtomically(const std::string&) override { return true; }
};
struct Startup final : IStartupRegistration {
    bool Read(StartupEntry& entry) override { entry = {}; return true; }
    bool Write(const StartupEntry&) override { Check(false, "observer must never change startup"); return false; }
};
struct Transport final : IUpdateTransport {
    std::atomic<int> calls{0};
    std::atomic<bool> release{false}, current{false}, offline{false}, throws{false};
    ManifestResponse Fetch(const Cancellation& cancellation) override {
        ++calls;
        while (!release && !cancellation.cancelled) Sleep(1);
        if (cancellation.cancelled) return {{}, "update-cancelled"};
        if (throws) throw std::runtime_error("transport exception");
        if (offline) return {{}, "update-offline"};
        return {current ? R"({"version":"1.0.1"})" : R"({"version":"1.0.9"})", {}};
    }
};
template<class Predicate> bool Pump(Predicate done) {
    const auto deadline = GetTickCount64() + 3000;
    while (!done()) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (GetTickCount64() >= deadline) return false;
        Sleep(1);
    }
    return true;
}
using Observer = std::function<void(bool, const ManifestResult&)>;
template<class T, class = void> struct HasObserver : std::false_type {};
template<class T> struct HasObserver<T, std::void_t<
    decltype(std::declval<T&>().SetUpdateObserver(Observer{})),
    decltype(std::declval<T&>().RequestUpdateCheck(false))>> : std::true_type {};

template<class Session> void Verify() {
    if constexpr (!HasObserver<Session>::value) {
        Check(false, "update completion needs a UI-thread result observer and truthful queue acceptance");
    } else {
        auto transport = std::make_shared<Transport>();
        ApplicationSettingsOptions options;
        options.verification = true;
        options.testPorts = {std::make_shared<File>(), std::make_shared<Startup>(), transport, L"C:\\Lume\\LumeShell.exe"};
        std::vector<std::pair<bool, ManifestResult>> observed;
        const auto owner = GetCurrentThreadId();
        Session session(GetModuleHandleW(nullptr), options, {}, [](bool) { return false; });
        session.SetUpdateObserver([&](bool manual, const ManifestResult& result) {
            Check(GetCurrentThreadId() == owner, "observer runs on UI thread");
            observed.emplace_back(manual, result);
        });
        Check(session.Restore(), "restore queued");
        const auto restoring = session.State();
        Check(!session.RequestUpdateCheck(false) && session.State() == restoring, "busy background rejection must not alter settings");
        Check(Pump([&] { return !session.HasWork(); }), "restore settles");
        Check(observed.empty(), "unrelated completions never notify update observer");
        Check(session.RequestUpdateCheck(true), "manual check really queued");
        Check(Pump([&] { return transport->calls == 1; }), "worker fetch started");
        const auto busy = session.State();
        Check(!session.RequestUpdateCheck(true) && !session.RequestUpdateCheck(false) &&
              session.State() == busy, "duplicate requests rejected without duplicate work or false busy errors");
        transport->release = true;
        Check(Pump([&] { return !session.HasWork(); }) && observed.size() == 1, "one result per completed fetch");
        Check(observed[0].first && observed[0].second.update.version == "1.0.9" &&
              observed[0].second.error == "update-incompatible", "manual source and raw manifest result survive unrelated runtime error");
        Check(session.State().find(L"balanced-runtime") != std::wstring::npos, "update observation does not clear runtime failure");
        transport->current = true;
        Check(session.RequestUpdateCheck(false) && Pump([&] { return !session.HasWork(); }), "background check completes");
        Check(observed.size() == 2 && !observed[1].first && observed[1].second.update.status == "current" &&
              observed[1].second.error.empty(), "background result is independent of global settings errors");
        transport->offline = true;
        Check(session.HandlePage({lume::ui::ApplicationSettingsAction::CheckUpdate}).has_value() &&
              Pump([&] { return !session.HasWork(); }), "existing manual page route still works");
        Check(observed.size() == 3 && observed.back().first &&
              observed.back().second.error == "update-offline", "manual page results use the same observer");
        transport->offline = false;
        Check(session.RequestUpdateCheck(false), "lost completion signal fixture queued");
        bool removedSignal = false;
        const auto deadline = GetTickCount64() + 3000;
        while (!removedSignal && GetTickCount64() < deadline) {
            MSG message{};
            removedSignal = PeekMessageW(&message, nullptr, WM_APP + 0x351, WM_APP + 0x351, PM_REMOVE) != 0;
            if (!removedSignal) Sleep(1);
        }
        Check(removedSignal && observed.size() == 3, "drop posted completion before UI dispatch");
        Check(Pump([&] { return !session.HasWork(); }) && observed.size() == 4,
              "UI fallback must deliver a completion even when the posted signal is lost");
        transport->throws = true;
        Check(session.RequestUpdateCheck(true) && Pump([&] { return !session.HasWork(); }) &&
              observed.size() == 5 && observed.back().second.error == "settings-internal",
              "transport exception still completes the observer exactly once");
        transport->throws = false;
        transport->release = false;
        const auto count = transport->calls.load();
        Check(session.RequestUpdateCheck(false) && Pump([&] { return transport->calls > count; }), "cancellation fixture started");
        session.Shutdown();
        session.JoinShutdown();
        Check(observed.size() == 5 && !session.RequestUpdateCheck(true), "shutdown drops late update callbacks");
        session.SetUpdateObserver([](bool, const ManifestResult&) { Check(false, "stopped session cannot reattach observer"); });

        transport->release = true; transport->offline = false;
        std::unique_ptr<Session> destroyed;
        int afterDestruction = 0;
        destroyed = std::make_unique<Session>(GetModuleHandleW(nullptr), options,
            [&](const std::wstring&) { destroyed.reset(); }, [](bool) { return true; });
        destroyed->SetUpdateObserver([&](bool, const ManifestResult&) { ++afterDestruction; });
        Check(destroyed->RequestUpdateCheck(true) && Pump([&] { return !destroyed; }), "notify may synchronously destroy session");
        Check(afterDestruction == 0, "destroyed session must not call the copied observer");

        int observerDestructions = 0;
        destroyed = std::make_unique<Session>(GetModuleHandleW(nullptr), options,
            [](const std::wstring&) {}, [](bool) { return true; });
        destroyed->SetUpdateObserver([&](bool, const ManifestResult&) { ++observerDestructions; destroyed.reset(); });
        Check(destroyed->RequestUpdateCheck(true) && Pump([&] { return !destroyed; }) &&
              observerDestructions == 1, "observer may destroy session after a single completion");
    }
}
}
int main() { Verify<ApplicationSettingsSession>(); std::cout << "update result observer passed\n"; }
