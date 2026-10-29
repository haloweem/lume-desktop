#include "apps/shell/account_session.h"
#include "core/account/backend.h"
#include "tests/unit/support/account_backend_fixture.h"
#include "tests/unit/support/wallpaper_engagement_fixture.h"
#include <objbase.h>
#include <cstdlib>
#include <iostream>
using namespace lume;
using namespace lume::account;
namespace {
void Expect(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
void Pump(unsigned milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 2, QS_ALLINPUT);
    } while (GetTickCount64() < end);
}
template<class F> void Until(F done, const char* why) {
    for (int i = 0; i < 400 && !done(); ++i) Pump(10);
    Expect(done(), why);
}
std::string Param(const std::string& url, const std::string& key) {
    auto start = url.find(key + "=");
    Expect(start != std::string::npos, "fixture authorization parameter");
    start += key.size() + 1;
    return url.substr(start, url.find('&', start) - start);
}
struct Harness {
    std::shared_ptr<testing::FixtureTransport> http = std::make_shared<testing::FixtureTransport>();
    std::shared_ptr<testing::MemoryCredentials> store = std::make_shared<testing::MemoryCredentials>();
    std::shared_ptr<std::atomic<std::int64_t>> now = std::make_shared<std::atomic<std::int64_t>>(1000);
    std::vector<Json> states;
    std::string authorization;
    std::unique_ptr<shell::AccountSession> session;
    Harness() {
        http->rotateRefreshTokens = true;
        shell::AccountSessionOptions options;
        options.verification = true; options.transport = http; options.credentials = store;
        options.clock = [clock = now] { return clock->load(); };
        options.openBrowser = [this](std::wstring_view url) {
            authorization = Utf8(url); http->nonce = Param(authorization, "nonce"); return true;
        };
        const auto uiThread = GetCurrentThreadId();
        session = std::make_unique<shell::AccountSession>(GetModuleHandleW(nullptr), options,
            [this, uiThread](const std::wstring& text) {
                Expect(GetCurrentThreadId() == uiThread, "background result stays on UI thread");
                auto j = StrictJson(Utf8(text), 3 * MaxAvatarBytes);
                Expect((j["status"] == "signed-in") == !j["profile"].is_null(), "background keeps public schema");
                if (!states.empty()) Expect(j["revision"] > states.back()["revision"], "background revisions monotonic");
                states.push_back(std::move(j));
            });
        session->HandlePage({ui::AccountAction::Login, {}});
        Expect(session->HandleCallback(Wide("lume://login?code=fixture-code&state=" + Param(authorization, "state"))), "fixture login");
        Until([&] { return State()["status"] == "signed-in" && !Busy(); }, "fixture login completed");
    }
    ~Harness() {
        http->hold = false; http->holdDevice = false; http->holdProfileRead = false;
        session->Shutdown();
    }
    Json State() const { return StrictJson(Utf8(session->State()), 3 * MaxAvatarBytes); }
    bool Busy() const { return State()["busy"].get<bool>(); }
    void HoldDevice(bool advance = true) {
        http->holdDevice = true;
        const auto before = http->deviceChecks.load();
        if (advance) *now += 61;
        Until([&] { return http->deviceChecks > before; }, "background device GET entered");
    }
};
}
int main() {
    Expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "STA");
    {
        Harness h;
        const auto notifications = h.states.size();
        h.HoldDevice();
        Expect(!h.Busy() && h.State()["status"] == "signed-in" && h.states.size() == notifications,
            "slow background check must not publish busy or disable editing/logout");
        const auto reads = h.http->profileReads.load();
        h.http->holdProfileRead = true;
        h.session->HandlePage({ui::AccountAction::UpdateName, "Queued Name"});
        Expect(h.Busy(), "save is accepted and visible while queued behind background");
        h.http->holdDevice = false;
        Until([&] { return h.http->profileReads > reads; }, "queued save actually starts");
        Pump(40);
        Expect(h.Busy() && h.http->profileWrites == 0, "background completion cannot clear a pending foreground busy flag");
        h.http->holdProfileRead = false;
        Until([&] { return !h.Busy(); }, "queued name save completes");
        Expect(h.State()["profile"]["displayName"] == "Queued Name" && h.http->profileWrites == 1,
            "background check does not silently drop the user save");
        h.HoldDevice();
        Expect(!h.Busy(), "next background check remains quiet");
        const auto requests = h.http->tokenRequests.load();
        h.http->hold = true;
        h.session->HandlePage({ui::AccountAction::Refresh, {}});
        Expect(h.Busy(), "refresh accepted behind device check");
        h.http->holdDevice = false;
        Until([&] { return h.http->tokenRequests > requests; }, "queued refresh actually starts");
        Pump(40); Expect(h.Busy(), "background result does not finish foreground refresh");
        h.http->hold = false;
        Until([&] { return !h.Busy(); }, "queued refresh completes");
        Expect(h.store->Load()->refreshToken == "fixture-refresh-R1" && h.http->rejectedRefreshReuses == 0,
            "queued refresh completes real fixture rotation");
        h.HoldDevice(); h.http->ignoreCancellation = true;
        const auto start = GetTickCount64();
        h.session->HandlePage({ui::AccountAction::Logout, {}});
        Expect(GetTickCount64() - start < 300 && h.State()["status"] == "signed-out" && !h.store->Load(),
            "logout immediately clears account without waiting for a slow device check");
        const auto revision = h.State()["revision"].get<std::uint64_t>();
        h.http->holdDevice = false;
        Until([&] { return !h.session->HasWork(); }, "cancelled device and local logout drain");
        for (const auto& state : h.states) if (state["revision"].get<std::uint64_t>() > revision)
            Expect(state["status"] == "signed-out", "late background generation cannot resurrect logout");
    }
    {
        Harness h;
        // Automatic background refresh has committed R1 before the device GET stalls.
        *h.now = 4550; h.http->now = 4550;
        h.HoldDevice(false);
        Expect(!h.Busy() && h.store->Load()->refreshToken == "fixture-refresh-R1",
            "private background rotation does not lock the UI");
        h.session->HandlePage({ui::AccountAction::Refresh, {}});
        h.session->HandlePage({ui::AccountAction::Logout, {}});
        Expect(!h.store->Load() && h.State()["status"] == "signed-out", "logout erases an already committed R1");
        h.http->holdDevice = false;
        Until([&] { return !h.session->HasWork(); }, "rotating background cancellation drains");
        Expect(!h.store->Load() && h.http->tokenRequests == 2,
            "logout cancels queued foreground refresh and never rewrites R1");
    }
    {
        Harness h;
        h.HoldDevice(); h.http->deviceInactive = true;
        h.session->HandlePage({ui::AccountAction::UpdateName, "Must Not Save"});
        h.http->holdDevice = false;
        Until([&] { return h.State()["status"] == "signed-out" && !h.Busy(); }, "device loss cancels queued user work");
        Expect(h.State()["error"] == "device-inactive" && !h.store->Load() && h.http->profileWrites == 0,
            "device invalidation advances the session generation and preserves its failure reason");
    }
    {
        auto http=std::make_shared<testing::EngagementTransport>();
        auto store=std::make_shared<testing::MemoryCredentials>();
        auto now=std::make_shared<std::atomic<std::int64_t>>(1000);
        const auto clock=[now]{return now->load();};http->auth.rotateRefreshTokens=true;
        Backend seed(http,store,clock);Cancellation cancel;seed.Login({"fixture-code","fixture-nonce","fixture-verifier"},cancel);
        shell::AccountSessionOptions options;options.verification=true;options.transport=http;options.credentials=store;options.clock=clock;
        http->auth.jwksStatus=503;
        shell::AccountSession session(GetModuleHandleW(nullptr),options,{});session.Restore();
        Until([&]{return StrictJson(Utf8(session.State()))["error"]=="service-unavailable";},
            "startup outage is reported without deleting retained credentials");
        Expect(store->Load()&&http->auth.tokenRequests==1&&!session.SignedIn(),"failed restoration never fabricates authentication");
        http->auth.jwksStatus=200;*now=1006;
        Until([&]{return session.SignedIn();},"transient startup outage automatically recovers without restarting or logging in again");
        Expect(store->Load()->refreshToken=="fixture-refresh-R1","restoration retry uses valid retained credential");
        *now=4550;http->auth.now=4550;http->holdRefreshResponse=true;
        Until([&]{return http->refreshResponseReady.load();},"normal shutdown fixture holds a consumed refresh token response");
        const auto start=GetTickCount64();session.Shutdown();
        Expect(GetTickCount64()-start<500,"normal shutdown stays bounded while refresh response finishes privately");
        http->holdRefreshResponse=false;
        Until([&]{const auto saved=store->Load();return saved&&saved->refreshToken=="fixture-refresh-R2";},
            "normal shutdown durably saves validated rotated credentials instead of deleting login");
        Expect(!http->refreshResponseCancelled&&!http->refreshResponseGateExpired,"normal shutdown does not cancel consumed rotation response");
        Backend restarted(http,store,clock);
        Expect(restarted.Restore(cancel).has_value()&&http->auth.rejectedRefreshReuses==0,"next launch recovers from the saved rotation without revoked refresh reuse");
    }
    {
        Harness h;
        h.http->deviceUnavailable=true;const auto before=h.http->deviceChecks.load();*h.now+=61;
        Until([&]{return h.http->deviceChecks>before&&h.State()["error"]=="service-unavailable";},
            "temporary device service outage is visible");
        Expect(h.session->SignedIn()&&h.store->Load(),"temporary device outage preserves verified login");
        h.http->deviceUnavailable=false;*h.now+=61;
        Until([&]{return h.State()["error"]=="";},"device check recovers automatically without login");
    }
    CoUninitialize();
    std::cout << "quiet device checks, queued user work and rotating logout races passed\n";
}
