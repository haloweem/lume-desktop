#include <iostream>
#include "core/account/backend.h"
#include "tests/unit/support/account_backend_fixture.h"
#include <cstdlib>
using namespace lume::account;
void Expect(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
template<class F> void Reject(F f, Error code, const char* why) {
    try { f(); } catch (const Failure& e) { Expect(e.code == code, why); return; }
    Expect(false, why);
}
// The server stamps every response with its own time (Date header).
class DatedTransport final : public ITransport {
public:
    std::shared_ptr<testing::FixtureTransport> inner = std::make_shared<testing::FixtureTransport>();
    HttpResponse Send(const HttpRequest& request, const Cancellation& cancel) override {
        auto response = inner->Send(request, cancel);
        response.date = 1000;
        return response;
    }
};
int main() {
    // A PC clock ten minutes slow or two hours fast must still sign in: token
    // times are checked against the server's clock, not this PC's.
    for (const std::int64_t pc : {1000 - 600, 1000 + 7200}) {
        auto dated = std::make_shared<DatedTransport>();
        Backend skewed(dated, std::make_shared<testing::MemoryCredentials>(), [pc] { return pc; });
        Cancellation skewCancel;
        const auto signedIn = skewed.Login({"fixture-code", "fixture-nonce", "fixture-verifier"}, skewCancel);
        Expect(skewed.SignedIn() && signedIn.id == "fixture-user", "login with a skewed PC clock");
    }
    // Email sign-up accounts and odd display names must still sign in.
    struct NameCase { std::string displayName, email, expected; };
    for (const auto& item : {NameCase{"", "newuser@qq.com", "newuser"}, NameCase{"bad\nname", "x@y.com", "x"},
                             NameCase{std::string(300, 'n'), "", "fixture-name"}, NameCase{"", "", "fixture-name"}}) {
        auto transport = std::make_shared<testing::FixtureTransport>();
        transport->displayName = item.displayName; transport->email = item.email;
        Backend named(transport, std::make_shared<testing::MemoryCredentials>(), [] { return 1000; });
        Cancellation nameCancel;
        const auto signedIn = named.Login({"fixture-code", "fixture-nonce", "fixture-verifier"}, nameCancel);
        Expect(named.SignedIn() && signedIn.displayName == item.expected, "email sign-up and odd display names sign in");
    }
    Expect(ValidTarget(Target::WallpaperStats, "GET",
        "https://account.lumedesktop.com/api/wallpapers/stats?id=official-a", false),
        "anonymous public wallpaper stats have a narrowly allowed native destination");
    Expect(!ValidTarget(Target::WallpaperStats, "GET",
        "https://account.lumedesktop.com/api/wallpapers/stats?id=official-a&token=x", true),
        "wallpaper stats reject additional query credentials");
    auto http = std::make_shared<testing::FixtureTransport>();
    auto store = std::make_shared<testing::MemoryCredentials>();
    Backend backend(http, store, [] { return 1000; });
    Cancellation cancel;
    const Grant grant{"fixture-code", "fixture-nonce", "fixture-verifier"};
    auto profile = backend.Login(grant, cancel);
    Expect(backend.SignedIn() && profile.id == "fixture-user" && profile.vip && profile.avatarUrl.empty(), "verified login profile and VIP");
    Expect(store->Load().has_value(), "login persisted before success");
    auto calls = http->Calls();
    bool registerDevice = false, userinfo = false;
    for (const auto& request : calls) {
        if (request.target == Target::Device) {
            registerDevice = request.method == "POST" && request.headers.at("x-lume-device-id") == store->DeviceId() &&
                request.body == "{}" && request.bearer == "fixture-access";
        }
        if (request.target == Target::Userinfo) userinfo = true;
    }
    Expect(registerDevice && userinfo, "real login device registration and userinfo chain");
    profile = backend.UpdateName("  New Name  ", cancel);
    Expect(profile.displayName == "New Name", "native name update refreshed");
    bool updateShape = false;
    for (const auto& request : http->Calls()) if (request.target == Target::ProfileWrite) {
        const auto j = StrictJson(request.body);
        updateShape = request.url == std::string(Issuer) + "/api/update-user?userId=fixture-user&owner=fixture-org&columns=displayName" &&
            j == Json{{"id", "fixture-user"}, {"owner", "fixture-org"}, {"name", "fixture-name"}, {"displayName", "New Name"}};
    }
    Expect(updateShape, "legacy Casdoor ownership body and query");
    const auto count = http->Calls().size();
    Reject([&] { backend.UpdateName(std::string(33, 'n'), cancel); }, Error::InvalidName, "name validated before HTTP");
    Expect(http->Calls().size() == count, "invalid name no remote side effect");
    profile = backend.UploadAvatar({testing::Png(), "image/png"}, cancel);
    Expect(profile.avatarUrl.find("data:image/png;base64,") == 0, "avatar displayed as bounded data URL");
    std::vector<Target> avatarChain;
    for (const auto& r : http->Calls()) {
        if (r.target == Target::AvatarTicket || r.target == Target::AvatarPut) {
            avatarChain.push_back(r.target);
            if (r.target == Target::AvatarPut) Expect(r.bearer.empty() && !r.body.empty(), "signed PUT never bearer");
        }
    }
    Expect(avatarChain == std::vector<Target>{Target::AvatarTicket, Target::AvatarPut}, "real upload ticket then PUT chain");
    for (const auto status : {401u, 403u, 503u}) {
        http->avatarPutStatus = status;
        const auto previous = backend.Current();
        const auto credentials = store->Load();
        const auto beforeUpload = http->Calls().size();
        bool uploadRejected = false;
        try { backend.UploadAvatar({testing::Png(), "image/png"}, cancel); }
        catch (const Failure& error) { uploadRejected = ErrorCode(error.code) == "avatar-upload-failed"; }
        Expect(uploadRejected, "COS authorization/service failure must map to avatar-upload-failed, not account expiry");
        Expect(backend.SignedIn() && backend.Current()->avatarUrl == previous->avatarUrl &&
            store->Load() && store->Load()->refreshToken == credentials->refreshToken,
            "non-Bearer upload failure preserves login, credentials and last avatar");
        const auto afterUpload = http->Calls();
        for (auto i = beforeUpload; i < afterUpload.size(); ++i)
            Expect(afterUpload[i].target != Target::ProfileWrite, "failed PUT never updates avatar profile");
    }
    http->avatarPutStatus = 200;
    Expect(!backend.UploadAvatar({testing::Png(), "image/png"}, cancel).avatarUrl.empty(),
        "upload can retry after an expired signed PUT without reauthentication");
    http->unsafeUpload = true;
    Reject([&] { backend.UploadAvatar({testing::Png(), "image/png"}, cancel); }, Error::InvalidTarget, "ticket cannot authorize arbitrary host");
    http->unsafeUpload = false;
    http->omitRefreshId = true;
    Expect(backend.Refresh(cancel).id == profile.id, "OIDC refresh without new ID token uses bound userinfo");
    http->omitRefreshId = false;
    Backend restored(http, store, [] { return 1000; });
    Expect(restored.Restore(cancel).has_value(), "restore refreshes protected credential");
    const auto before = http->Calls().size();
    restored.CheckDevice(cancel);
    Expect(http->Calls().size() > before, "device check wired");
    http->deviceInactive = true;
    Reject([&] { restored.CheckDevice(cancel); }, Error::DeviceInactive, "inactive device logout");
    Expect(!restored.SignedIn() && !store->Load(), "device invalidation deletes credentials");
    http->deviceInactive = false;
    store->failSave = true;
    Reject([&] { restored.Login(grant, cancel); }, Error::Storage, "save failure not login success");
    Expect(!restored.SignedIn(), "failed save leaves no signed-in backend");
    store->failSave = false; http->wrongSubject = true;
    Reject([&] { restored.Login(grant, cancel); }, Error::InvalidToken, "userinfo subject mismatch");
    http->wrongSubject = false; http->wrongAccount = true;
    Reject([&] { restored.Login(grant, cancel); }, Error::InvalidToken, "profile subject mismatch");
    http->wrongAccount = false;
    restored.Login(grant, cancel); http->invalidGrant = true;
    Reject([&] { restored.Refresh(cancel); }, Error::AuthRequired, "refresh rejection clears local account");
    Expect(!restored.SignedIn() && !store->Load(), "invalid_grant not successful refresh");
    auto casdoor = std::make_shared<testing::FixtureTransport>();
    casdoor->subject = casdoor->accountId = "991070cc-b62c-4c55-920e-358732c22a1e";
    Backend casdoorBackend(casdoor, store, [] { return 1000; });
    Expect(casdoorBackend.Login(grant, cancel).id == casdoor->accountId,
        "Casdoor Subject=user.Id UUID, distinct from owner/name");
    casdoor->subject = "fixture-org/fixture-name";
    Reject([&] { casdoorBackend.Login(grant, cancel); }, Error::InvalidToken,
        "owner/name cannot substitute for verified UUID subject even with matching userinfo");
    casdoor->subject = casdoor->accountId;
    casdoor->displayName = std::string(257, 'A');
    // A display name the page cannot show falls back; it never blocks sign-in.
    Expect(casdoorBackend.Login(grant, cancel).displayName == "fixture-name", "overlong display name falls back to the account name");
    casdoor->displayName = "A\tB";
    Expect(casdoorBackend.Login(grant, cancel).displayName == "fixture-name", "control characters in the display name fall back");
    std::cout << "account production backend with isolated signed transport passed\n";
}
