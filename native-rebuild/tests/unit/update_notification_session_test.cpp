#include "apps/shell/update_notification_session.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace lume::application_settings;
using namespace lume::update_notification;
using lume::shell::UpdateNotificationSession;
using lume::platform::windows::UpdateNotice;
namespace {
void Check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct File final : IPreferencesFile {
    FileRead value;
    bool fail = false;
    int writes = 0;
    FileRead Read() override { return value; }
    bool WriteAtomically(const std::string& bytes) override {
        ++writes; if (fail) return false; value = {ReadStatus::Loaded, bytes}; return true;
    }
};
struct Fixture {
    std::shared_ptr<File> file = std::make_shared<File>();
    ClockSample now{1790726400000LL, 20260930};
    std::vector<UpdateNotice> notices;
    std::vector<bool> requests;
    bool busy = false, failShow = false, visible = false;
    int closed = 0, changed = 0, defaultRequests = 0;
    lume::shell::UpdateNotificationOptions Options() {
        lume::shell::UpdateNotificationOptions options;
        options.verification = true;
        options.testPorts.file = file;
        options.testPorts.now = [this] { return now; };
        options.testPorts.requestCheck = [this](bool manual) {
            if (busy) return false;
            requests.push_back(manual); return true;
        };
        options.testPorts.show = [this](const UpdateNotice& notice) {
            if (failShow) return false;
            notices.push_back(notice); visible = true; return true;
        };
        options.testPorts.shutdownPresenter = [this] { ++closed; visible = false; };
        options.testPorts.hasVisible = [this] { return visible; };
        return options;
    }
    void Tomorrow() { now.localDay = 20261001; now.utcMillis += 86400000; }
};
ManifestResult Newer() { return {{"incompatible", "1.0.9", 0}, "update-incompatible"}; }
void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
void Scheduling() {
    Fixture f;
    UpdateNotificationSession session(nullptr, f.Options(), [&](bool) { ++f.defaultRequests; return true; });
    Check(session.Start() && session.Handle() && session.HasWork(), "start creates hidden timer receiver and requests automatically");
    Check(f.requests == std::vector<bool>{false} && f.defaultRequests == 0, "fake transport overrides production callback");
    session.Tick(); Check(f.requests.size() == 1, "in-flight checks are single flight");
    session.OnCheckCompleted(false, Newer());
    Check(f.notices.empty() && !session.HasVisible() && f.file->writes == 1, "automatic result is consumed silently without a presenter");
    session.Tick(); Check(f.requests.size() == 1, "same day does not recheck");
    f.now.utcMillis += 15 * 60000;session.Tick();Check(f.requests.size() == 2, "running app polls for new releases every fifteen minutes");
    session.OnCheckCompleted(false, Newer());Check(f.notices.empty(), "periodic polls never present a release reminder");
    f.Tomorrow(); session.Tick(); Check(f.requests.size() == 3, "next civil day rechecks without restart");
    session.OnCheckCompleted(false, Newer()); session.RetryPresentation();
    Check(f.notices.empty(), "next civil day and page-ready never replay automatic results");
    session.Shutdown(); Check(!session.HasWork() && !session.Handle() && !session.HasVisible(), "shutdown releases receiver and presenter");
    session.OnCheckCompleted(true, Newer()); session.Tick();
    Check(f.notices.empty() && f.closed == 1 && !session.RequestManual(), "no late delivery or work after shutdown");
}
void BusyAndManual() {
    Fixture f; f.busy = true;
    UpdateNotificationSession session(nullptr, f.Options(), {});
    session.SetLanguage(true);
    Check(session.Start() && f.requests.empty(), "busy settings postpone background work");
    Check(session.RequestManual() && f.requests.empty(), "manual request waits without taking another service's busy state");
    f.busy = false; session.Wake(); Pump();
    Check(f.requests == std::vector<bool>{true}, "queued manual check takes priority");
    session.OnCheckCompleted(true, {{"current", "1.0.1", 0}, {}});
    Check(f.notices.size() == 1 && f.notices.back().version.empty() && f.notices.back().manual &&
          f.notices.back().detail.find(L"No newer version") != std::wstring::npos,
          "manual current uses generic check title and truthful current detail");
    Check(session.RequestManual() && f.requests.size() == 2, "manual request bypasses daily cadence");
    session.OnCheckCompleted(true, {{"error", {}, 0}, "update-offline"});
    Check(f.notices.size() == 2 && f.notices.back().version.empty() && f.notices.back().manual &&
          f.notices.back().detail.find(L"Could not reach") != std::wstring::npos,
          "manual error uses generic check title and an offline detail");
}
void CoalescingAndResume() {
    Fixture f; UpdateNotificationSession session(nullptr, f.Options(), {});
    Check(session.Start() && session.RequestManual() && f.requests.size() == 1, "manual intent merges into background flight");
    session.OnCheckCompleted(false, Newer());
    Check(f.notices.size() == 1 && f.notices.back().manual, "merged completion keeps manual presentation");
    f.Tomorrow();
    SendMessageW(session.Handle(), WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0); Pump();
    Check(f.requests.size() == 2, "resume requests a due next-day check");
}
void PresentationAndStorageFailure() {
    Fixture f; f.failShow = true;
    UpdateNotificationSession session(nullptr, f.Options(), {});
    Check(session.Start() && session.RequestManual(), "start failure fixture"); session.OnCheckCompleted(false, Newer());
    Check(f.file->writes == 0 && f.notices.empty(), "failed display does not persist");
    f.failShow = false; f.now.utcMillis += 60000; session.Tick();
    Check(f.notices.size() == 1 && f.requests.size() == 1, "retry cached result without another fetch");
    f.Tomorrow(); f.file->fail = true; session.RequestManual(); session.OnCheckCompleted(true, Newer());
    Check(f.notices.size() == 2 && !session.Error().empty(), "storage failure is observable after visible reminder");
    f.file->fail = false; f.now.utcMillis += 60000; session.Tick();
    Check(f.notices.size() == 2 && session.Error().empty(), "retry persistence without duplicating the notice");
}
void SilentOffline() {
    Fixture f; UpdateNotificationSession session(nullptr, f.Options(), {});
    Check(session.Start(), "start offline fixture");
    session.OnCheckCompleted(false, {{"error", {}, 0}, "update-offline"});
    session.Tick(); Check(f.notices.empty() && f.requests.size() == 1 && f.file->writes == 0, "background errors stay silent with bounded retry");
    f.now.utcMillis += 15 * 60000; session.Tick();
    Check(f.requests.size() == 2, "offline retry remains possible without consuming day");
}
void Verification(const std::filesystem::path& root) {
    lume::shell::UpdateNotificationOptions options;
    options.verification = true; options.statePath = (root / "must-not-exist.json").u8string();
    int requests = 0;
    UpdateNotificationSession session(nullptr, options, [&](bool) { ++requests; return true; });
    Check(session.Start(), "verification session is constructible");
    session.Tick(); session.RequestManual(); session.OnCheckCompleted(true, Newer());
    Check(requests == 0 && !session.HasVisible() && !session.HasWork() &&
          !std::filesystem::exists(std::filesystem::u8path(options.statePath)),
          "verification does not call default check, show real UI, write state, or keep shell alive");
}
void ReentrantShutdown() {
    Fixture f; UpdateNotificationSession* pointer = nullptr;
    auto options = f.Options();
    options.testPorts.show = [&](const UpdateNotice&) { pointer->Shutdown(); return true; };
    UpdateNotificationSession session(nullptr, options, {});
    pointer = &session;
    Check(session.Start() && session.RequestManual(), "start reentrant fixture");
    session.OnCheckCompleted(false, Newer()); Pump();
    Check(!session.HasWork() && f.file->writes == 0, "presenter shutdown callback prevents late state writes");
}
void ReceiverLifecycle() {
    Fixture f; auto options = f.Options();
    int synchronous = 0;
    UpdateNotificationSession* pointer = nullptr;
    options.testPorts.requestCheck = [&](bool manual) {
        ++synchronous; pointer->OnCheckCompleted(manual, Newer()); return true;
    };
    UpdateNotificationSession session(nullptr, options, {}); pointer = &session;
    Check(session.Start() && synchronous == 1 && f.notices.empty(), "synchronous automatic completion is silent");
    Check(!IsWindowVisible(session.Handle()) && GetParent(session.Handle()) != HWND_MESSAGE,
        "hidden top-level receiver remains eligible for broadcasts");
    f.Tomorrow(); SendMessageW(session.Handle(), WM_TIMECHANGE, 0, 0); Pump();
    Check(synchronous == 2 && f.notices.empty(), "civil time change checks without opening the manager or popup");
    Check(session.RequestManual() && synchronous == 3 && f.notices.back().manual, "synchronous manual completion preserves source");
}
void DestructionAndRetryClock() {
    Fixture f;
    std::unique_ptr<UpdateNotificationSession> session;
    auto options = f.Options();
    options.testPorts.show = [&](const UpdateNotice&) { session.reset(); return true; };
    session = std::make_unique<UpdateNotificationSession>(nullptr, options, UpdateNotificationSession::RequestCheck{});
    Check(session->Start() && session->RequestManual(), "start destructible fixture");
    session->OnCheckCompleted(false, Newer()); Pump();
    Check(!session && f.file->writes == 0 && f.closed == 1, "destroying public owner during display cannot commit ledger");
    Fixture retry;
    UpdateNotificationSession other(nullptr, retry.Options(), {});
    Check(other.Start(), "start retry clock fixture");
    other.OnCheckCompleted(false, {{"error", {}, 0}, "update-offline"});
    retry.now.utcMillis -= 3600000; other.Tick();
    Check(retry.requests.size() == 2, "wall-clock rollback cannot trap retry deadline");
    other.OnCheckCompleted(false, Newer());
    const auto writes = retry.file->writes;
    other.OnCheckCompleted(false, Newer());
    Check(retry.notices.empty() && retry.file->writes == writes, "duplicate late automatic completion is ignored");
}
void FreshManualAndNextDay() {
    Fixture f; f.failShow = true;
    UpdateNotificationSession session(nullptr, f.Options(), {});
    Check(session.Start(), "start manual retry fixture");
    session.OnCheckCompleted(false, Newer());
    f.failShow = false;
    Check(session.RequestManual() && f.requests == std::vector<bool>({false, true}) && f.notices.empty(),
        "new manual intent fetches again without replaying a silent automatic result");
    session.OnCheckCompleted(true, {{"current", "1.0.1", 0}, {}});
    Check(f.notices.size() == 1 && f.notices.back().version.empty(), "fresh current response replaces stale newer metadata");
    f.Tomorrow(); f.failShow = true; session.Tick(); session.OnCheckCompleted(false, Newer());
    f.now.localDay = 20261002; f.now.utcMillis += 86400000; f.failShow = false; session.Tick();
    Check(f.requests.size() == 4 && f.notices.size() == 1, "next natural day refreshes metadata instead of displaying yesterday's failed result");
}
void InAppDelivery() {
    Fixture f;
    auto options = f.Options();
    options.testPorts.show = {};
    bool ready = false;
    int attempts = 0;
    options.showInApp = [&](const Notice& notice) {
        ++attempts;
        Check(notice.kind == NoticeKind::Incompatible && notice.version == "1.0.9",
              "in-app presenter receives classified metadata");
        return ready;
    };
    UpdateNotificationSession session(nullptr, options, {});
    Check(session.Start() && session.RequestManual(), "start in-app delivery");
    session.OnCheckCompleted(false, Newer());
    Check(attempts == 1 && f.file->writes == 0 && !session.HasVisible(),
          "unready page does not consume notice or launch native presenter");
    ready = true;
    session.RetryPresentation();
    Check(attempts == 2 && f.file->writes == 1 && f.requests.size() == 1,
          "page-ready retries pending notice immediately without another check");
    session.RetryPresentation();
    Check(attempts == 2, "delivered notice is not replayed");
    session.Shutdown();
    session.RetryPresentation();
    Check(attempts == 2, "shutdown prevents late page-ready callbacks");
}
}
int main(int argc, char** argv) {
    int failures = 0;
    for (const auto& test : std::vector<std::pair<const char*, void(*)()>>{
        {"scheduling", Scheduling}, {"busy-and-manual", BusyAndManual}, {"coalescing-and-resume", CoalescingAndResume},
        {"presentation-and-storage", PresentationAndStorageFailure}, {"silent-offline", SilentOffline},
        {"reentrant-shutdown", ReentrantShutdown}, {"receiver-lifecycle", ReceiverLifecycle},
        {"destruction-and-retry-clock", DestructionAndRetryClock}, {"fresh-manual-and-next-day", FreshManualAndNextDay},
        {"in-app-delivery", InAppDelivery}}) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    try { Check(argc == 2, "isolated artifact directory required"); Verification(std::filesystem::u8path(argv[1])); std::cout << "PASS verification\n"; }
    catch (const std::exception& error) { ++failures; std::cerr << "FAIL verification: " << error.what() << '\n'; }
    return failures ? 1 : 0;
}
