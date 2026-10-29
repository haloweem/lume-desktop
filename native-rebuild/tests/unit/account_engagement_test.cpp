#include "apps/shell/account_session.h"
#include "core/account/backend.h"
#include "tests/unit/support/wallpaper_engagement_fixture.h"
#include "platform/windows/account/credentials.h"
#include "unique_test_directory.h"
#include <cstdlib>
#include <iostream>
using namespace lume;
using namespace lume::account;
namespace {
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
template<class F> void Reject(F run, Error expected, const char* message) {
    try { run(); } catch (const Failure& failure) { Expect(failure.code == expected, message); return; }
    Expect(false, message);
}
template<class F> bool WaitFor(F ready) {
    for (unsigned i = 0; i < 400 && !ready(); ++i) {
        MSG event{};
        while (PeekMessageW(&event, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&event); DispatchMessageW(&event);
        }
        Sleep(5);
    }
    return ready();
}
template<class F> void Until(F ready, const char* message) {
    Expect(WaitFor(ready), message);
}
bool SelectionCancelDuringRefreshRotation() {
    auto http = std::make_shared<testing::EngagementTransport>();
    auto store = std::make_shared<testing::MemoryCredentials>();
    auto now = std::make_shared<std::atomic<std::int64_t>>(1000);
    const auto clock = [now] { return now->load(); };
    http->auth.rotateRefreshTokens = true;
    Backend seed(http, store, clock);
    Cancellation cancel;
    seed.Login({"fixture-code", "fixture-nonce", "fixture-verifier"}, cancel);
    shell::AccountSessionOptions options;
    options.verification = true; options.transport = http; options.credentials = store; options.clock = clock;
    shell::AccountSession session(GetModuleHandleW(nullptr), options, {});
    session.Restore();
    Until([&] { return session.SignedIn(); }, "rotation regression: restore completes");
    Expect(store->Load() && store->Load()->refreshToken == "fixture-refresh-R1",
        "rotation regression: restore persists first rotation");
    *now = 4541; http->auth.now = 4541;
    http->holdRefreshResponse = true;
    session.HandleWallpaper({ui::WallpaperAction::Stats, "official-a"});
    Until([&] { return http->refreshResponseReady.load(); },
        "rotation regression: refresh response held after old token consumed");
    session.HandleWallpaper({ui::WallpaperAction::EndStats});
    session.HandleWallpaper({ui::WallpaperAction::Stats, "official-b"});
    http->holdRefreshResponse = false;
    const bool ready = WaitFor([&] {
        const auto state = StrictJson(Utf8(session.WallpaperState()));
        return state["id"] == "official-b" && state["status"] == "ready" && state["busy"] == false;
    });
    const auto saved = store->Load();
    const bool passed = ready && session.SignedIn() && saved &&
        saved->refreshToken == "fixture-refresh-R2" && http->auth.rejectedRefreshReuses == 0 &&
        !http->refreshResponseCancelled && !http->refreshResponseGateExpired;
    if (!passed)
        std::cerr << "FAIL selection cancellation during refresh: nextReady=" << ready
            << " signedIn=" << session.SignedIn() << " rotatedCredentialsSaved="
            << (saved && saved->refreshToken == "fixture-refresh-R2")
            << " rejectedRefreshReuses=" << http->auth.rejectedRefreshReuses.load()
            << " renewalCancelled=" << http->refreshResponseCancelled.load()
            << " gateExpired=" << http->refreshResponseGateExpired.load() << '\n';
    session.Shutdown();
    return passed;
}
bool GuestAuthTimeoutRestartsPendingStats() {
    auto http = std::make_shared<testing::EngagementTransport>();
    auto store = std::make_shared<testing::MemoryCredentials>();
    auto now = std::make_shared<std::atomic<std::int64_t>>(1000);
    shell::AccountSessionOptions options;
    options.verification = true; options.transport = http; options.credentials = store;
    options.clock = [now] { return now->load(); };
    options.openBrowser = [](std::wstring_view) { return true; };
    shell::AccountSession session(GetModuleHandleW(nullptr), options, {});
    session.Restore();
    Until([&] { return !session.HasWork(); }, "timeout regression: guest restored");
    session.HandlePage({ui::AccountAction::Login, {}});
    Expect(StrictJson(Utf8(session.State()))["status"] == "authorizing",
        "timeout regression: login started");
    http->holdStats = true;
    session.HandleWallpaper({ui::WallpaperAction::Stats, "official-a"});
    Until([&] { return http->statsRequests == 1; }, "timeout regression: selected stats pending");
    *now = 1001 + LoginTransactionSeconds;
    Until([&] { return StrictJson(Utf8(session.State()))["error"] == "callback-expired"; },
        "timeout regression: login expires while selected stats pending");
    http->holdStats = false;
    const bool ready = WaitFor([&] {
        const auto state = StrictJson(Utf8(session.WallpaperState()));
        return state["id"] == "official-a" && state["status"] == "ready" &&
            state["busy"] == false && !session.HasWork();
    });
    const auto state = StrictJson(Utf8(session.WallpaperState()));
    bool anonymous = true;
    for (const auto& request : http->Calls()) anonymous = anonymous && request.bearer.empty();
    const bool passed = ready && http->statsRequests >= 2 && anonymous && !session.SignedIn() &&
        !store->Load() && state["canLike"] == false && state["liked"] == false &&
        state["likes"] == 0 && state["downloads"] == 0;
    if (!passed)
        std::cerr << "FAIL guest auth timeout: selectedStatus=" << state["status"]
            << " statsRequests=" << http->statsRequests.load() << " hasWork=" << session.HasWork()
            << " anonymous=" << anonymous << '\n';
    session.Shutdown();
    return passed;
}
}
int main() {
    auto http = std::make_shared<testing::EngagementTransport>();
    auto store = std::make_shared<testing::MemoryCredentials>();
    Backend backend(http, store, [] { return 1000; });
    Cancellation cancel;
    const Grant grant{"fixture-code", "fixture-nonce", "fixture-verifier"};
    auto stats = backend.WallpaperStats("official-a", cancel);
    Expect(stats.likes == 0 && stats.downloads == 0 && !stats.liked, "guest can read real zero totals");
    Reject([&] { backend.SetWallpaperLike("official-a", true, cancel); }, Error::AuthRequired,
        "guest cannot write likes");
    const auto beforeInvalid = http->Calls().size();
    Reject([&] { backend.WallpaperStats("../x", cancel); }, Error::InvalidCommand, "invalid IDs rejected");
    Expect(http->Calls().size() == beforeInvalid, "invalid ID has no remote request");
    backend.Login(grant, cancel);
    stats = backend.SetWallpaperLike("official-a", true, cancel);
    Expect(stats.likes == 1 && stats.liked, "like uses signed-in identity");
    Expect(backend.SetWallpaperLike("official-a", true, cancel).likes == 1, "desired likes are idempotent");
    backend.Logout();
    stats = backend.WallpaperStats("official-a", cancel);
    Expect(stats.likes == 1 && !stats.liked, "public totals do not inherit signed-out liking");
    http->auth.subject = "second-user";
    backend.Login(grant, cancel);
    Expect(!backend.WallpaperStats("official-a", cancel).liked, "another account has independent liked state");
    Expect(backend.SetWallpaperLike("official-a", true, cancel).likes == 2, "aggregate is shared across accounts");
    Expect(backend.SetWallpaperLike("official-a", false, cancel).likes == 1, "unlike removes only this account");
    for (const auto& value : {Json(-1), Json(0.5), Json(9007199254740992ULL), Json("1")}) {
        http->statsOverride = Json{{"ok", true}, {"likes", value}, {"downloads", 0}, {"liked", false}};
        Reject([&] { backend.WallpaperStats("official-a", cancel); }, Error::InvalidResponse, "invalid totals rejected");
    }
    http->statsOverride.reset();
    const std::string source = "https://static.lumedesktop.com/lume/wallpapers/official/videos/forest.mp4";
    backend.RecordWallpaperDownload("official-a", source, cancel);
    Expect(backend.WallpaperStats("official-a", cancel).downloads == 1, "completed download ticket records public count");
    for (const auto& request : http->Calls()) {
        if (request.url.find("/download/") != request.url.npos)
            Expect(request.bearer.empty() && request.method == "GET", "ticket metadata never sends Bearer or streams media");
        Expect(request.url.find("static.lumedesktop.com") == request.url.npos, "account transport never downloads media");
    }
    http->unsafeTicket = true;
    const auto tickets = http->ticketRequests.load();
    Reject([&] { backend.RecordWallpaperDownload("official-a", source, cancel); }, Error::InvalidTarget, "unsafe ticket rejected");
    Expect(http->ticketRequests == tickets, "untrusted ticket never requested");
    http->unsafeTicket = false;
    Reject([&] { backend.RecordWallpaperDownload("official-a", "https://evil.invalid/file", cancel); },
        Error::InvalidTarget, "non-catalog download source rejected");
    backend.Logout();
    backend.RecordWallpaperDownload("official-a", source, cancel);
    Expect(backend.WallpaperStats("official-a", cancel).downloads == 2, "guest completed downloads record without login");
    http->wallpaperStatus = 429;
    bool distinctQuota = false;
    try { backend.RecordWallpaperDownload("official-a", source, cancel); }
    catch (const Failure& failure) { distinctQuota = ErrorCode(failure.code) == "wallpaper-download-quota"; }
    Expect(distinctQuota, "server recording quota has a distinct non-account public error");
    http->wallpaperStatus = 200;
    backend.Login(grant, cancel);

    shell::AccountSessionOptions options;
    options.verification = true; options.transport = http; options.credentials = store;
    options.clock = [] { return 1000; };
    const auto uiThread = GetCurrentThreadId();
    std::vector<Json> events;
    shell::AccountSession session(GetModuleHandleW(nullptr), options, [&](const std::wstring& text) {
        Expect(GetCurrentThreadId() == uiThread, "engagement notifications are UI-thread only");
        Expect(text.find(L"fixture-access") == text.npos && text.find(L"fixture-refresh") == text.npos,
            "engagement never exposes tokens");
        events.push_back(StrictJson(Utf8(text)));
    });
    auto engagement = [&] { return StrictJson(Utf8(session.WallpaperState())); };
    session.Restore();
    Until([&] { return session.SignedIn(); }, "native account restored");
    session.HandleWallpaper({ui::WallpaperAction::Stats, "official-a"});
    Until([&] { return engagement()["status"] == "ready"; }, "stats completion delivered");
    Expect(StrictJson(Utf8(session.State()))["busy"] == false,
        "wallpaper stats never lock account login/profile operations");
    Expect(engagement()["canLike"] == true && engagement()["likes"] == 1 && engagement()["downloads"] == 2,
        "sidebar state has public counts and native account eligibility");
    session.HandleWallpaper({ui::WallpaperAction::Like, "official-a", 0, 0, true});
    Until([&] { return engagement()["status"] == "ready" && !engagement()["busy"].get<bool>(); }, "like completes");
    Expect(engagement()["liked"] == true && engagement()["likes"] == 2, "like confirmed by server response");
    const auto queuedLikes = http->likeRequests.load();
    session.HandleWallpaper({ui::WallpaperAction::Like, "another-wallpaper", 0, 0, true});
    Expect(http->likeRequests == queuedLikes, "like cannot target another selected wallpaper");
    http->holdStats = true; http->ignoreCancellation = true;
    const auto reads = http->statsRequests.load();
    session.HandleWallpaper({ui::WallpaperAction::Stats, "official-b"});
    Until([&] { return http->statsRequests > reads; }, "held selected request started");
    session.HandleWallpaper({ui::WallpaperAction::Stats, "official-c"});
    Expect(engagement()["id"] == "official-c" && engagement()["likes"].is_null(), "new selection clears old totals");
    session.HandlePage({ui::AccountAction::Logout, {}});
    const auto logoutRevision = engagement()["revision"].get<std::uint64_t>();
    Expect(engagement()["liked"] == false && engagement()["canLike"] == false, "logout immediately clears account state");
    http->holdStats = false;
    Until([&] { return engagement()["status"] == "ready" && !session.HasWork(); }, "guest stats and logout queue drain");
    for (const auto& event : events) {
        if (event["type"] == "wallpaper-engagement-state" && event["revision"].get<std::uint64_t>() > logoutRevision)
            Expect(event["canLike"] == false && event["liked"] == false, "late member response cannot reappear");
    }
    const auto likesBeforeGuest = http->likeRequests.load();
    session.HandleWallpaper({ui::WallpaperAction::Like, "official-c", 0, 0, true});
    Expect(http->likeRequests == likesBeforeGuest, "guest like has no HTTP side effect");
    session.HandleWallpaper({ui::WallpaperAction::EndStats});
    Expect(engagement()["status"] == "idle" && engagement()["id"] == "" && !session.HasWork(),
        "leaving detail releases selected work");
    http->wallpaperStatus = 429;
    const auto notices = events.size();
    session.RecordWallpaperDownload("official-a", source);
    Until([&] { return !session.HasWork(); }, "failed background recording releases keepalive");
    bool quotaNotice = false;
    for (auto i = notices; i < events.size(); ++i) {
        const auto& event = events[i];
        if (event["type"] == "wallpaper-engagement-notice")
            quotaNotice = event["id"] == "official-a" && event["error"] == "WALLPAPER_DOWNLOAD_STATS_QUOTA";
    }
    Expect(quotaNotice && engagement()["status"] == "idle",
        "recording quota is reported even when detail is closed, without changing media state");
    http->wallpaperStatus = 200;
    session.Shutdown();
    backend.Login(grant, cancel);
    std::vector<Json> invalidatedEvents;
    shell::AccountSession invalidated(GetModuleHandleW(nullptr), options, [&](const std::wstring& text) {
        invalidatedEvents.push_back(StrictJson(Utf8(text)));
    });
    invalidated.Restore();
    Until([&] { return invalidated.SignedIn(); }, "member recording fixture restored");
    http->wallpaperStatus = 401;
    invalidated.RecordWallpaperDownload("official-a", source);
    Until([&] { return !invalidated.HasWork(); }, "invalidated recording releases account and work");
    bool expiredRecordingNotice = false;
    for (const auto& event : invalidatedEvents) {
        if (event["type"] == "wallpaper-engagement-notice")
            expiredRecordingNotice = event["error"] == "WALLPAPER_DOWNLOAD_STATS_FAILED";
    }
    Expect(expiredRecordingNotice && !invalidated.SignedIn() && !store->Load(),
        "account-invalidating recording failure still reports media-independent notice");
    invalidated.Shutdown();
    http->wallpaperStatus = 200;
    unsigned browserRequests = 0;
    options.openBrowser = [&](std::wstring_view) { ++browserRequests; return true; };
    shell::AccountSession guest(GetModuleHandleW(nullptr), options, {});
    guest.Restore();
    Until([&] { return !guest.HasWork(); }, "empty guest restored");
    guest.HandleWallpaper({ui::WallpaperAction::Stats, "official-a"});
    Until([&] { return StrictJson(Utf8(guest.WallpaperState()))["status"] == "ready"; }, "guest reads totals");
    Expect(StrictJson(Utf8(guest.State()))["busy"] == false, "guest stats leave login unlocked");
    guest.HandlePage({ui::AccountAction::Login, {}});
    Expect(browserRequests == 1 && StrictJson(Utf8(guest.State()))["status"] == "authorizing",
        "guest can start login after reading totals");
    guest.HandlePage({ui::AccountAction::CancelLogin, {}});
    guest.HandleWallpaper({ui::WallpaperAction::EndStats});
    Until([&] { return !guest.HasWork(); }, "guest login cancellation releases ownership");
    guest.Shutdown();
    const bool rotationPassed = SelectionCancelDuringRefreshRotation();
    const bool timeoutPassed = GuestAuthTimeoutRestartsPendingStats();
    if (!rotationPassed || !timeoutPassed) return 1;
    static_assert(GuestWallpaperTotalLimit == 10 && MemberWallpaperDailyLimit == 10);
    {
        // Exercise the production Backend + DPAPI composition, not an in-memory ledger.
        const auto root = UniqueTestDirectory(std::filesystem::path(LUME_ACCOUNT_TEST_ARTIFACTS), "download-dpapi-");
        auto http = std::make_shared<testing::EngagementTransport>();
        auto disk = std::make_shared<platform::windows::account::DpapiCredentials>(root.u8string());
        Backend member(http, disk, [] { return 1000; });
        member.Login(grant, cancel);
        for (unsigned i = 0; i < MemberWallpaperDailyLimit; ++i)
            member.AuthorizeWallpaperDownload("official-member", source, cancel);
        Reject([&] { member.AuthorizeWallpaperDownload("official-eleven", source, cancel); }, Error::WallpaperQuota,
            "production DPAPI admission reaches server and respects ordinary member denial");
        Expect(http->grantRequests == 11 && http->ticketRequests == 10,
            "ordinary member ceiling is enforced by server after local reservation");
        // A server-allowlisted identity can continue beyond ten and after reopening storage.
        http->unlimitedMember = true;
        auto reopened = std::make_shared<platform::windows::account::DpapiCredentials>(root.u8string());
        Backend restored(http, reopened, [] { return 1000; });
        Expect(restored.Restore(cancel).has_value(), "production credentials restore member");
        for (unsigned i = 0; i < 3; ++i)
            restored.AuthorizeWallpaperDownload("official-unlimited", source, cancel);
        Expect(http->ticketRequests == 13, "server allowlist passes beyond ten across DPAPI reopen");
        restored.Logout();
        const auto before = http->grantRequests.load();
        Backend guestAgain(http, disk, [] { return 1000; });
        // Member use never spends the guest total: after logout the guest still has ten.
        for (unsigned i = 0; i < GuestWallpaperTotalLimit; ++i)
            guestAgain.AuthorizeWallpaperDownload("official-guest", source, cancel);
        Expect(http->grantRequests == before + GuestWallpaperTotalLimit, "guest total is independent of member use");
        Reject([&] { guestAgain.AuthorizeWallpaperDownload("official-guest", source, cancel); }, Error::WallpaperQuota,
            "production DPAPI ledger stops the eleventh guest download");
        Expect(http->grantRequests == before + GuestWallpaperTotalLimit, "exhausted guest is rejected before server request");
        std::filesystem::remove_all(root);
    }
    {
        auto quotaHttp=std::make_shared<testing::EngagementTransport>();
        auto quotaStore=std::make_shared<testing::MemoryCredentials>();
        Backend limited(quotaHttp,quotaStore,[]{return 1000;});
        for(unsigned i=0;i<GuestWallpaperTotalLimit;++i)
            limited.AuthorizeWallpaperDownload("official-guest",source,cancel);
        auto before=quotaHttp->ticketRequests.load();
        Reject([&]{limited.AuthorizeWallpaperDownload("official-c",source,cancel);},Error::WallpaperQuota,
            "guest eleventh download rejected before grant and CDN");
        Expect(quotaHttp->ticketRequests==before,"exhausted guest never consumes another server ticket");
        Backend nextDay(quotaHttp,quotaStore,[]{return 1000+86400*3;});
        Reject([&]{nextDay.AuthorizeWallpaperDownload("official-c",source,cancel);},Error::WallpaperQuota,
            "the guest total does not reset on a later day");
        limited.Login(grant,cancel);
        for(unsigned i=0;i<MemberWallpaperDailyLimit;++i)
            limited.AuthorizeWallpaperDownload("official-member",source,cancel);
        Reject([&]{limited.AuthorizeWallpaperDownload("official-eleven",source,cancel);},Error::WallpaperQuota,
            "signing in after the guest total gives a full ten");
        limited.Logout();
        Backend guestAgain(quotaHttp,quotaStore,[]{return 1000;});
        Reject([&]{guestAgain.AuthorizeWallpaperDownload("official-guest",source,cancel);},Error::WallpaperQuota,
            "logout and backend reconstruction cannot retain member allowance");
        auto failedStore=std::make_shared<testing::MemoryCredentials>(); failedStore->failSave=true;
        Backend diskFailed(quotaHttp,failedStore,[]{return 1000;});before=quotaHttp->ticketRequests.load();
        Reject([&]{diskFailed.AuthorizeWallpaperDownload("official-a",source,cancel);},Error::Storage,
            "quota storage failure cannot authorize bytes");
        Expect(quotaHttp->ticketRequests==before,"failed local reservation never consumes a server ticket");
        auto deniedStore=std::make_shared<testing::MemoryCredentials>();
        Backend serviceFailed(quotaHttp,deniedStore,[]{return 1000;});quotaHttp->wallpaperStatus=503;
        Reject([&]{serviceFailed.AuthorizeWallpaperDownload("official-a",source,cancel);},Error::Service,
            "server authorization failure blocks media");
        Expect(deniedStore->quota.total==0,"server rejection refunds the unused local reservation");
        quotaHttp->wallpaperStatus=200;
    }
    {
        auto quotaHttp=std::make_shared<testing::EngagementTransport>();
        auto quotaStore=std::make_shared<testing::MemoryCredentials>();
        Backend seed(quotaHttp,quotaStore,[]{return 1000;});seed.Login(grant,cancel);
        shell::AccountSessionOptions quotaOptions;quotaOptions.verification=true;quotaOptions.transport=quotaHttp;
        quotaOptions.credentials=quotaStore;quotaOptions.clock=[]{return 1000;};
        shell::AccountSession authorized(GetModuleHandleW(nullptr),quotaOptions,{});authorized.Restore();
        Until([&]{return authorized.SignedIn();},"authorization fixture restores member identity");
        quotaHttp->holdGrant=true;std::string result;unsigned completions=0;
        authorized.AuthorizeWallpaperDownload("official-a",source,[&](std::string error){result=std::move(error);++completions;});
        Until([&]{return quotaHttp->grantRequests==1;},"member authorization is pending");
        authorized.HandlePage({ui::AccountAction::Logout,{}});
        Expect(completions==1&&result=="WALLPAPER_CANCELLED"&&!authorized.SignedIn(),
            "logout immediately revokes pending member download admission");
        quotaHttp->holdGrant=false;Until([&]{return !authorized.HasWork();},"cancelled authorization drains without member resurrection");
        const auto before=quotaHttp->Calls().size();
        authorized.AuthorizeWallpaperDownload("official-b",source,[&](std::string error){result=std::move(error);++completions;});
        Until([&]{return completions==2;},"next download after logout is independently admitted as guest");
        Expect(result.empty(),"failed member admission refunds its local reservation before guest download");
        const auto calls=quotaHttp->Calls();
        for(auto i=before;i<calls.size();++i)Expect(calls[i].bearer.empty(),"post-logout download never carries former member token");
        for(unsigned i=1;i<GuestWallpaperTotalLimit;++i) {
            const auto expected=completions+1;
            authorized.AuthorizeWallpaperDownload("official-guest",source,[&](std::string error){result=std::move(error);++completions;});
            Until([&]{return completions==expected;},"remaining post-logout guest admission settles");
            Expect(result.empty(),"ten guest wallpapers are allowed after cancelled member admission");
        }
        const auto expected=completions+1;
        authorized.AuthorizeWallpaperDownload("official-eleven",source,[&](std::string error){result=std::move(error);++completions;});
        Until([&]{return completions==expected;},"eleventh guest admission settles");
        Expect(result=="WALLPAPER_DOWNLOAD_QUOTA","post-logout identity has its ten-download total even after member login");
        authorized.Shutdown();
    }
    std::cout << "wallpaper account/public engagement, counting, isolation and races passed\n";
}
