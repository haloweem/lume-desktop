#include "core/account/backend.h"
#include "tests/unit/support/account_backend_fixture.h"
#include <cstdlib>
#include <iostream>
using namespace lume::account;
namespace {
void Expect(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
template<class F> void Reject(F function, Error code, const char* why) {
    try { function(); } catch (const Failure& error) { Expect(error.code == code, why); return; }
    Expect(false, why);
}
void Unavailable(testing::FixtureTransport& http, Target stage, bool value) {
    if (stage == Target::Userinfo) http.userinfoUnavailable = value;
    if (stage == Target::Device) http.deviceUnavailable = value;
    if (stage == Target::ProfileRead) http.profileUnavailable = value;
}
}
int main() {
    const Grant grant{"fixture-code", "fixture-nonce", "fixture-verifier"};
    Cancellation cancel;
    for (const auto status : {503u, 401u, 403u}) {
        auto http = std::make_shared<testing::FixtureTransport>();
        auto store = std::make_shared<testing::MemoryCredentials>();
        http->rotateRefreshTokens = true;
        Backend backend(http, store, [] { return 1000; });
        backend.Login(grant, cancel);
        const auto requests = http->tokenRequests.load();
        const auto saves = store->saves.load();
        http->jwksStatus = status;
        Reject([&] { backend.Refresh(cancel); }, Error::Service, "JWKS outage reports service error");
        Expect(http->tokenRequests == requests && store->saves == saves &&
            store->Load()->refreshToken == "fixture-refresh-R0" && backend.SignedIn(),
            "JWKS outage must be detected before consuming R0 at the token endpoint");
        http->jwksStatus = 200;
        Backend restarted(http, store, [] { return 1000; });
        Expect(restarted.Restore(cancel).has_value() && store->Load()->refreshToken == "fixture-refresh-R1" &&
            http->rejectedRefreshReuses == 0, "restart retries still-valid R0 after preflight JWKS recovery");
    }
    {
        auto http = std::make_shared<testing::FixtureTransport>();
        auto store = std::make_shared<testing::MemoryCredentials>();
        http->rotateRefreshTokens = true;
        Backend backend(http, store, [] { return 1000; });
        backend.Login(grant, cancel);
        http->jwksUnavailableAfterRefresh = true;
        const auto first = http->Calls().size();
        Expect(backend.Refresh(cancel).id == "fixture-user" && store->Load()->refreshToken == "fixture-refresh-R1",
            "JWKS outage starting during token response cannot discard a verified rotation");
        const auto calls = http->Calls();
        bool keyBeforeToken = false, tokenSeen = false;
        for (auto i = first; i < calls.size(); ++i) {
            if (calls[i].target == Target::Jwks) {
                Expect(!tokenSeen, "no fallible JWKS network lookup after rotation");
                keyBeforeToken = true;
            }
            if (calls[i].target == Target::Token) { Expect(keyBeforeToken, "JWKS precedes refresh POST"); tokenSeen = true; }
        }
        Expect(tokenSeen, "rotation really used token endpoint");
        const auto requests = http->tokenRequests.load();
        Reject([&] { backend.Refresh(cancel); }, Error::Service, "subsequent refresh waits for JWKS recovery");
        Expect(http->tokenRequests == requests && store->Load()->refreshToken == "fixture-refresh-R1",
            "subsequent preflight outage leaves R1 usable");
        http->jwksUnavailableAfterRefresh = false; http->jwksStatus = 200;
        Backend restarted(http, store, [] { return 1000; });
        Expect(restarted.Restore(cancel).has_value() && store->Load()->refreshToken == "fixture-refresh-R2" &&
            http->rejectedRefreshReuses == 0, "R1 restores after a JWKS outage with no revoked token reuse");
    }
    {
        auto http = std::make_shared<testing::FixtureTransport>();
        auto store = std::make_shared<testing::MemoryCredentials>();
        http->rotateRefreshTokens = true;
        Backend backend(http, store, [] { return 1000; });
        backend.Login(grant, cancel);
        const auto saves = store->saves.load();
        http->invalidRefreshIssuer = true;
        Reject([&] { backend.Refresh(cancel); }, Error::InvalidToken, "signed refresh with wrong issuer fails validation");
        Expect(store->saves == saves && !store->Load() && !backend.SignedIn(),
            "rotation is not persisted before JWT signature and identity validation");
    }
    for (const auto stage : {Target::Userinfo, Target::Device, Target::ProfileRead}) {
        for (const auto omitId : {false, true}) for (const auto restart : {false, true}) {
            auto http = std::make_shared<testing::FixtureTransport>();
            auto store = std::make_shared<testing::MemoryCredentials>();
            http->rotateRefreshTokens = true; http->omitRefreshId = omitId;
            Backend backend(http, store, [] { return 1000; });
            const auto profile = backend.Login(grant, cancel);
            Expect(store->Load()->refreshToken == "fixture-refresh-R0", "fixture initial R0");
            Unavailable(*http, stage, true);
            Reject([&] { backend.Refresh(cancel); }, Error::Service, "post-rotation 503 is a service error");
            Expect(store->Load() && store->Load()->refreshToken == "fixture-refresh-R1",
                "validated R1 must survive a later userinfo/device/profile 503");
            Expect(backend.Current() && backend.Current()->id == profile.id, "503 retains last confirmed public profile");
            Unavailable(*http, stage, false);
            if (restart) {
                Backend restarted(http, store, [] { return 1000; });
                Expect(restarted.Restore(cancel).has_value(), "restart restores with R1, not revoked R0");
            } else {
                Expect(backend.Refresh(cancel).id == profile.id, "same worker retries using R1, not revoked R0");
            }
            Expect(store->Load()->refreshToken == "fixture-refresh-R2" && http->rejectedRefreshReuses == 0,
                "strict rotating fixture rejects any reuse; R1 advances to R2");
        }
    }
    {
        auto http = std::make_shared<testing::FixtureTransport>();
        auto store = std::make_shared<testing::MemoryCredentials>();
        http->rotateRefreshTokens = true;
        Backend initial(http, store, [] { return 1000; });
        initial.Login(grant, cancel);
        Backend restoring(http, store, [] { return 1000; });
        http->profileUnavailable = true;
        Reject([&] { restoring.Restore(cancel); }, Error::Service, "restore can fail after rotating R0");
        Expect(store->Load()->refreshToken == "fixture-refresh-R1" && !restoring.SignedIn() && !restoring.Current(),
            "early private rotation is not a successful public restore");
        http->profileUnavailable = false;
        Expect(restoring.Restore(cancel).has_value() && store->Load()->refreshToken == "fixture-refresh-R2",
            "failed restore retries from the rotated credential");
    }
    {
        auto http = std::make_shared<testing::FixtureTransport>();
        auto store = std::make_shared<testing::MemoryCredentials>();
        http->rotateRefreshTokens = true;
        Backend backend(http, store, [] { return 1000; });
        backend.Login(grant, cancel);
        http->deviceInactive = true;
        Reject([&] { backend.Refresh(cancel); }, Error::DeviceInactive, "inactive device still revokes rotated account");
        Expect(!store->Load() && !backend.Current(), "early credential commit must not bypass device invalidation");
        http->deviceInactive = false;
        backend.Login(grant, cancel);
        http->wrongSubject = true;
        Reject([&] { backend.Refresh(cancel); }, Error::InvalidToken, "userinfo mismatch still rejects");
        Expect(!store->Load() && !backend.Current(), "identity mismatch clears even an early committed rotation");
        http->wrongSubject = false;
        backend.Login(grant, cancel); store->failSave = true;
        Reject([&] { backend.Refresh(cancel); }, Error::Storage, "rotated credential save failure is never success");
        Expect(!store->Load() && !backend.Current(), "save failure invalidates local account");
    }
    {
        auto http=std::make_shared<testing::FixtureTransport>();auto store=std::make_shared<testing::MemoryCredentials>();
        http->rotateRefreshTokens=true;Backend backend(http,store,[]{return 1000;});backend.Login(grant,cancel);
        http->rejectDeviceOnce=true;backend.CheckDevice(cancel);
        Expect(backend.SignedIn()&&store->Load()->refreshToken=="fixture-refresh-R1"&&http->rejectedRefreshReuses==0,
            "early access expiry refreshes once before losing login");
    }
    std::cout << "refresh rotation survives downstream failure, retry and restart\n";
}
