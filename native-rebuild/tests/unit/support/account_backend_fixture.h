#pragma once
#include "core/account/ports.h"
#include "tests/unit/support/account_signed_fixture.h"
#include <mutex>
#include <thread>
namespace lume::account::testing {
inline Bytes Png() { return Unbase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8_x8AAwMCAO-aXx8AAAAASUVORK5CYII"); }
class MemoryCredentials final : public ICredentials {
public:
    std::optional<Credentials> Load() override { std::lock_guard lock(mutex); return saved; }
    void Save(const Credentials& value) override {
        std::lock_guard lock(mutex);
        if (failSave) throw Failure(Error::Storage);
        saved = value; ++saves;
    }
    void Clear() override {
        std::lock_guard lock(mutex);
        if (failClear) throw Failure(Error::Storage);
        saved.reset(); ++clears;
    }
    std::string DeviceId() override { return "fixture-device-0123456789abcdef"; }
    void ReserveWallpaperDownload(std::int64_t day, std::string_view subject, unsigned limit) override {
        std::lock_guard lock(mutex);
        if (failSave) throw Failure(Error::Storage);
        if (day > quota.day) quota = WallpaperQuota{day, 0, {}, quota.guest};
        if (subject.empty() ? quota.guest >= limit : quota.accounts[std::string(subject)] >= limit)
            throw Failure(Error::WallpaperQuota);
        ++quota.total;
        if (subject.empty()) ++quota.guest; else ++quota.accounts[std::string(subject)];
    }
    WallpaperQuota quota;
    void ReleaseWallpaperDownload(std::int64_t day, std::string_view subject) override {
        std::lock_guard lock(mutex);if(failSave)throw Failure(Error::Storage);
        if(subject.empty()&&quota.guest)--quota.guest;
        if(day!=quota.day||!quota.total)return;
        if(!subject.empty()){const auto found=quota.accounts.find(std::string(subject));
            if(found==quota.accounts.end()||!found->second)return;
            if(!--found->second)quota.accounts.erase(found);}
        --quota.total;
    }
    std::optional<Credentials> saved;
    std::mutex mutex;
    std::atomic<bool> failSave{false}, failClear{false};
    std::atomic<int> saves{0}, clears{0};
};
class FixtureTransport final : public ITransport {
public:
    HttpResponse Send(const HttpRequest& request, const Cancellation& cancel) override {
        cancel.Check();
        {
            std::lock_guard lock(mutex); calls.push_back(request);
        }
        if (request.target == Target::Token) {
            ++tokenRequests;
            while (hold && (!cancel.Cancelled() || ignoreCancellation)) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (!ignoreCancellation) cancel.Check();
            if (invalidGrant) return {400, R"({"error":"invalid_grant","error_description":"SECRET"})", "application/json"};
            bool refresh = request.body.find("grant_type=refresh_token") != std::string::npos;
            std::string refreshToken = "fixture-refresh";
            if (rotateRefreshTokens) {
                std::lock_guard lock(mutex);
                if (refresh) {
                    if (request.body != "grant_type=refresh_token&client_id=" + std::string(ClientId) +
                        "&refresh_token=" + UrlEncode(currentRefresh)) {
                        ++rejectedRefreshReuses;
                        return {400, R"({"error":"invalid_grant"})", "application/json"};
                    }
                    ++refreshRotation;
                } else refreshRotation = 0;
                currentRefresh = "fixture-refresh-R" + std::to_string(refreshRotation);
                refreshToken = currentRefresh;
            }
            auto claims = SignedFixture::Claims(now);
            claims["sub"] = subject;
            if (refresh && invalidRefreshIssuer) claims["iss"] = "https://other.example";
            if (refresh) claims.erase("nonce"); else claims["nonce"] = nonce;
            Json tokens{{"access_token", "fixture-access"}, {"refresh_token", refreshToken},
                {"expires_in", 3600}, {"token_type", "Bearer"}};
            if (!refresh || !omitRefreshId) tokens["id_token"] = signing.Sign(claims);
            if (refresh && jwksUnavailableAfterRefresh) jwksStatus = 503;
            return Reply(tokens);
        }
        switch (request.target) {
        case Target::Discovery:
            return Reply({{"issuer", Issuer}, {"authorization_endpoint", std::string(Issuer) + "/login/oauth/authorize"},
                {"token_endpoint", std::string(Issuer) + "/api/login/oauth/access_token"},
                {"jwks_uri", std::string(Issuer) + "/.well-known/jwks"},
                {"userinfo_endpoint", std::string(Issuer) + "/api/userinfo"}});
        case Target::Jwks:
            if (jwksStatus != 200) return {jwksStatus.load(), "{}", "application/json"};
            return Reply(signing.jwks);
        case Target::Userinfo:
            if (userinfoUnavailable) return {503, "{}", "application/json"};
            return Reply({{"sub", wrongSubject ? "other-user" : subject}});
        case Target::Device:
            if (rejectDeviceOnce.exchange(false)) return {401, "{}", "application/json"};
            if (request.method == "GET") {
                ++deviceChecks;
                while (holdDevice && (!cancel.Cancelled() || ignoreCancellation))
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if (!ignoreCancellation) cancel.Check();
            }
            if (deviceUnavailable) return {503, "{}", "application/json"};
            return Reply({{"ok", true}, {"active", !deviceInactive}});
        case Target::ProfileRead: {
            ++profileReads;
            while (holdProfileRead && !cancel.Cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            cancel.Check();
            if (profileUnavailable) return {503, "{}", "application/json"};
            Json profile{{"owner", "fixture-org"}, {"name", "fixture-name"},
                {"id", wrongAccount ? "other-user" : (accountId.empty() ? subject : accountId)}, {"displayName", displayName}, {"email", email}, {"avatar", avatarUrl},
                {"properties", {{"lume_vip", "true"}, {"lume_vip_expires_at", "2099-01-01T00:00:00.000Z"}}}};
            return Reply({{"status", "ok"}, {"data", profile}});
        }
        case Target::ProfileWrite: {
            ++profileWrites;
            auto body = StrictJson(request.body);
            if (body.contains("displayName")) displayName = body["displayName"].get<std::string>();
            if (body.contains("avatar")) avatarUrl = body["avatar"].get<std::string>();
            return Reply({{"status", "ok"}, {"data", "Affected"}});
        }
        case Target::AvatarTicket:
            return Reply({{"ok", true}, {"uploadUrl", unsafeUpload ? "https://evil.example/x?sig=x" :
                "https://fixture-123.cos.ap-shanghai.myqcloud.com/lume-avatars/u/x.png?sig=fixture"},
                {"avatarUrl", "https://static.lumedesktop.com/lume-avatars/u/x.png"}});
        case Target::AvatarPut: return {avatarPutStatus.load(), "", ""};
        case Target::AvatarGet: {
            auto bytes = Png(); return {200, std::string(bytes.begin(), bytes.end()), "image/png"};
        }
        default: throw Failure(Error::InvalidTarget);
        }
    }
    std::vector<HttpRequest> Calls() { std::lock_guard lock(mutex); return calls; }
    static HttpResponse Reply(const Json& j) { return {200, j.dump(), "application/json"}; }
    SignedFixture signing;
    std::mutex mutex;
    std::vector<HttpRequest> calls;
    std::atomic<int> tokenRequests{0};
    std::atomic<int> rejectedRefreshReuses{0};
    std::atomic<int> deviceChecks{0}, profileReads{0}, profileWrites{0};
    std::atomic<unsigned> avatarPutStatus{200}, jwksStatus{200};
    std::atomic<bool> rejectDeviceOnce{false};
    std::atomic<bool> hold{false}, ignoreCancellation{false}, invalidGrant{false}, wrongSubject{false},
        wrongAccount{false}, deviceInactive{false}, deviceUnavailable{false}, unsafeUpload{false}, omitRefreshId{false},
        rotateRefreshTokens{false}, userinfoUnavailable{false}, profileUnavailable{false},
        holdDevice{false}, holdProfileRead{false}, jwksUnavailableAfterRefresh{false},
        invalidRefreshIssuer{false};
    std::int64_t now = 1000;
    std::string nonce = "fixture-nonce", displayName = "Fixture User", avatarUrl, email;
    std::string subject = "fixture-user", accountId;
    unsigned refreshRotation = 0;
    std::string currentRefresh;
};
}
