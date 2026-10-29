#include "apps/shell/music_provider_account_session.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>

using namespace lume::music::auth;
using namespace lume::shell;
using namespace std::chrono_literals;
namespace {
void Require(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class F> bool Await(F predicate) {
    const auto end = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}
Json Account(MusicProviderAccountSession& session, Provider provider = Provider::Netease) {
    return Json::parse(session.State())["accounts"][static_cast<std::size_t>(provider)];
}
struct Fixture {
    std::atomic<int> loads{0}, saves{0}, clears{0}, identity{1}, shape{0};
    std::atomic<bool> holdLoad{false}, entered{false}, release{false}, authorizing{false}, clearFails{false};
    MusicProviderAccountOptions Options() {
        MusicProviderAccountOptions options;
        options.verification = true;
        options.pollInterval = 5s;
        options.load = [&](Provider provider) {
            ++loads;
            const auto secrets = Json{{"token", "synthetic-secret"}, {"provider", Name(provider)},
                {"identity", identity.load()}};
            if (holdLoad.exchange(false)) {
                entered = true;
                Require(Await([&] { return release.load(); }), "blocked credential read released");
            }
            if (shape == 1) return Json::array({"synthetic-secret"});
            if (shape == 2) return Json("synthetic-secret");
            if (shape == 3) return Json::object();
            if (shape == 4) throw std::runtime_error("synthetic-private-path");
            return secrets;
        };
        options.save = [&](Provider, const Json&) { ++saves; };
        options.clear = [&](Provider) {
            ++clears;
            if (clearFails) throw std::runtime_error("synthetic-private-path");
        };
        options.execute = [&](Provider, Operation, Json&, const Cancelled&) {
            Reply reply;
            if (authorizing) {
                reply.status = "authorizing";
                reply.phase = "waiting";
                reply.qrText = "https://fixture.invalid/qr";
            } else {
                reply.status = "signed-in";
                reply.userId = std::to_string(identity.load());
                reply.displayName = "Fixture";
            }
            return reply;
        };
        return options;
    }
};
void Login(MusicProviderAccountSession& session, std::uint64_t request = 1,
           Provider provider = Provider::Netease) {
    session.HandlePage({"refresh", provider, request});
    Require(Await([&] { return Account(session, provider)["status"] == "signed-in"; }), "fixture signed in");
}
void Lifecycle() {
    Fixture fixture;
    MusicProviderAccountSession session(fixture.Options());
    Require(!session.AcquireCredentials(Provider::Netease) &&
        !session.AcquireCredentials(static_cast<Provider>(99)) && fixture.loads == 0,
        "signed-out and invalid providers never read credentials");
    Login(session);
    Login(session, 1, Provider::Kugou);
    const auto old = session.AcquireCredentials(Provider::Netease);
    const auto other = session.AcquireCredentials(Provider::Kugou);
    Require(old && other && session.IsCurrent(*old) && session.IsCurrent(*other),
        "independent providers receive current leases");
    Require(old->credentials["token"] == "synthetic-secret", "secrets are native-only lease data");
    auto forged = *old;
    forged.userId = "forged";
    Require(!session.IsCurrent(forged), "user mismatch rejected");
    forged = *old; ++forged.generation;
    Require(!session.IsCurrent(forged), "generation mismatch rejected");
    forged = *old; forged.provider = static_cast<Provider>(99);
    Require(!session.IsCurrent(forged), "invalid provider rejected");
    session.CancelActive();
    Require(session.IsCurrent(*old), "hiding an idle signed-in account does not revoke its lease");
    fixture.authorizing = true;
    session.HandlePage({"begin", Provider::Netease, 2});
    Require(!session.IsCurrent(*old) && !session.AcquireCredentials(Provider::Netease),
        "begin revokes lease immediately");
    Require(Await([&] { return Account(session)["status"] == "authorizing"; }), "fixture authorizing");
    session.CancelActive();
    const auto restored = session.AcquireCredentials(Provider::Netease);
    Require(restored && session.IsCurrent(*restored) && !session.IsCurrent(*old) &&
        session.IsCurrent(*other), "cancel restores identity under a new provider-scoped generation");
    fixture.authorizing = false;
    fixture.identity = 2;
    Login(session, 3);
    const auto switched = session.AcquireCredentials(Provider::Netease);
    Require(switched && switched->userId == "2" && !session.IsCurrent(*restored) &&
        session.IsCurrent(*other), "switch invalidates old account lease only");
    session.HandlePage({"logout", Provider::Netease, 4});
    Require(!session.IsCurrent(*switched) && !session.AcquireCredentials(Provider::Netease),
        "accepted logout revokes leases before clear completes");
    Require(Await([&] { return Account(session)["status"] == "signed-out"; }), "logout committed");
    Require(fixture.clears == 1 && session.IsCurrent(*other), "logout remains provider scoped");
    const auto publicState = session.State();
    Require(publicState.find(L"synthetic-secret") == std::wstring::npos &&
        publicState.find(L"credentials") == std::wstring::npos, "state excludes lease secrets");
    if (const auto notification = session.DrainNotification())
        Require(notification->find(L"synthetic-secret") == std::wstring::npos, "notification excludes secrets");
    const auto loads = fixture.loads.load();
    session.Shutdown();
    Require(!session.IsCurrent(*other) && !session.AcquireCredentials(Provider::Kugou) &&
        loads == fixture.loads && !session.DrainNotification(), "shutdown revokes without further credential reads");
}
void ReadRaces() {
    for (const auto* action : {"cancel", "logout", "switch", "shutdown"}) {
        Fixture fixture;
        MusicProviderAccountSession session(fixture.Options());
        Login(session);
        fixture.holdLoad = true;
        auto acquisition = std::async(std::launch::async, [&] {
            return session.AcquireCredentials(Provider::Netease);
        });
        Require(Await([&] { return fixture.entered.load(); }), "lease disk read entered");
        if (std::string_view(action) == "shutdown") session.Shutdown();
        else if (std::string_view(action) == "switch") {
            fixture.identity = 2;
            Login(session, 2);
        } else session.HandlePage({action, Provider::Netease, 2});
        fixture.release = true;
        Require(!acquisition.get(), "cancel/logout/switch/shutdown rejects an in-flight stale credential copy");
        session.Shutdown();
    }
}
void BadStorage() {
    Fixture fixture;
    MusicProviderAccountSession session(fixture.Options());
    Login(session);
    for (int shape = 1; shape <= 4; ++shape) {
        fixture.shape = shape;
        Require(!session.AcquireCredentials(Provider::Netease),
            "malformed or empty credential document cannot become a lease");
    }
    fixture.shape = 0;
    const auto lease = session.AcquireCredentials(Provider::Netease);
    Require(lease.has_value(), "valid credentials restored");
    fixture.clearFails = true;
    session.HandlePage({"logout", Provider::Netease, 2});
    Require(Await([&] { return Account(session)["status"] == "error"; }), "fixture clear failure reported");
    Require(!session.IsCurrent(*lease) && !session.AcquireCredentials(Provider::Netease),
        "failed logout never reauthorizes the old lease");
    Require(session.State().find(L"synthetic-private-path") == std::wstring::npos,
        "storage exception details never enter page state");
    fixture.clearFails = false;
    session.HandlePage({"logout", Provider::Netease, 3});
    Require(Await([&] { return Account(session)["status"] == "signed-out"; }), "logout retry succeeds");
}
}
int main() {
    Lifecycle();
    ReadRaces();
    BadStorage();
    std::cout << "music credential lease isolated regressions passed\n";
}
