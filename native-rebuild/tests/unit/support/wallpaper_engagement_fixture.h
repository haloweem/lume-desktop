#pragma once
#include "tests/unit/support/account_backend_fixture.h"
#include <set>
namespace lume::account::testing {
class EngagementTransport final : public ITransport {
public:
    FixtureTransport auth;
    std::atomic<bool> holdStats{false}, holdLike{false}, ignoreCancellation{false};
    std::atomic<unsigned> statsRequests{0}, likeRequests{0}, ticketRequests{0};
    std::atomic<bool> holdRefreshResponse{false}, refreshResponseReady{false},
        refreshResponseCancelled{false}, refreshResponseGateExpired{false};
    unsigned wallpaperStatus = 200;
    std::optional<Json> statsOverride;
    bool unsafeTicket = false;
    bool unlimitedMember = false;
    std::atomic<bool> holdGrant{false};
    std::atomic<unsigned> grantRequests{0};
    HttpResponse Send(const HttpRequest& request, const Cancellation& cancel) override {
        if (request.url.find("https://account.lumedesktop.com/api/wallpapers/") != 0) {
            auto response = auth.Send(request, cancel);
            if (request.target == Target::Token && response.status == 200 &&
                request.body.find("grant_type=refresh_token") != std::string::npos && holdRefreshResponse) {
                // The server has consumed the old token; cancellation must not lose this response.
                refreshResponseReady = true;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (holdRefreshResponse && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                refreshResponseGateExpired = holdRefreshResponse.load();
                refreshResponseCancelled = cancel.Cancelled();
            }
            return response;
        }
        cancel.Check();
        {
            std::lock_guard lock(mutex);
            calls.push_back(request);
        }
        const auto principal = request.bearer.empty() ? std::string{} : auth.subject;
        const bool stats = request.url.find("/stats?id=") != request.url.npos;
        const bool like = request.url.find("/like") != request.url.npos;
        const bool grant = request.target == Target::WallpaperGrant;
        if (grant) ++grantRequests;
        if (stats) ++statsRequests;
        if (like) ++likeRequests;
        while ((stats && holdStats) || (like && holdLike) || (grant && holdGrant)) {
            if (!ignoreCancellation) cancel.Check();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!ignoreCancellation) cancel.Check();
        std::lock_guard lock(mutex);
        if (wallpaperStatus != 200) return {wallpaperStatus, "{}", "application/json"};
        if (stats) {
            if (statsOverride) return FixtureTransport::Reply(*statsOverride);
            const auto id = request.url.substr(request.url.find("?id=") + 4);
            return FixtureTransport::Reply(Stats(id, principal));
        }
        if (like) {
            if (principal.empty()) return {401, "{}", "application/json"};
            const auto body = StrictJson(request.body);
            const auto id = body.at("id").get<std::string>();
            if (body.at("liked") == true) likes[id].insert(principal);
            else likes[id].erase(principal);
            return FixtureTransport::Reply(Stats(id, principal));
        }
        if (request.url.find("/grant") != request.url.npos) {
            const auto device = request.headers.find("x-lume-device-id");
            const auto deviceId = device == request.headers.end() ? std::string{} : device->second;
            // Exercise the production contract: a guest device gets ten in total,
            // an account ten a day of its own (guest use does not count), and
            // the server's unlimited allowlist bypasses the member ceiling.
            const bool member = !request.bearer.empty();
            auto& used = member ? memberGrants[request.bearer] : deviceGrants[deviceId];
            if ((member ? !unlimitedMember && used >= MemberWallpaperDailyLimit : used >= GuestWallpaperTotalLimit))
                return {429, "{}", "application/json"};
            ++used;
            ticketId = StrictJson(request.body).at("id").get<std::string>();
            return FixtureTransport::Reply({{"ok", true}, {"downloadUrl", unsafeTicket ?
                "https://evil.invalid/api/wallpapers/download/abcdefghijklmnopqrstuvwxyz" :
                "https://account.lumedesktop.com/api/wallpapers/download/abcdefghijklmnopqrstuvwxyz"}});
        }
        ++ticketRequests;
        ++downloads[ticketId];
        return {302, "", ""};
    }
    std::vector<HttpRequest> Calls() { std::lock_guard lock(mutex); return calls; }
private:
    Json Stats(const std::string& id, const std::string& principal) {
        return {{"ok", true}, {"likes", likes[id].size()}, {"downloads", downloads[id]},
            {"liked", !principal.empty() && likes[id].count(principal) != 0}};
    }
    std::mutex mutex;
    std::vector<HttpRequest> calls;
    std::map<std::string, std::set<std::string>> likes;
    std::map<std::string, unsigned> downloads;
    std::map<std::string, unsigned> deviceGrants, memberGrants;
    std::string ticketId;
};
}
