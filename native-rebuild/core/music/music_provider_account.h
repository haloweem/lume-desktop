#pragma once

#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lume::music::auth {

using Json = nlohmann::json;
using Cancelled = std::function<bool()>;
enum class Provider { Netease, QQ, Kugou };
enum class Operation { Begin, Poll, Refresh };
inline const char* Name(Provider provider) {
    switch (provider) {
    case Provider::Netease: return "netease";
    case Provider::QQ: return "qq";
    case Provider::Kugou: return "kugou";
    }
    return "";
}
inline void CheckCancelled(const Cancelled& cancelled) {
    if (cancelled && cancelled()) throw std::runtime_error("cancelled");
}

struct HttpRequest {
    Provider provider = Provider::Netease;
    std::string method = "GET";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    std::size_t maxResponse = 256 * 1024;
};
struct HttpResponse {
    unsigned status = 0;
    std::string body;
    std::string contentType;
    std::vector<std::string> cookies;
    std::string location;
};
using Exchange = std::function<HttpResponse(const HttpRequest&, const Cancelled&)>;

inline bool PublicImageUrl(std::string_view url) {
    if (url.empty()) return true;
    if (url.size() > 2048 || url.substr(0, 8) != "https://" || url.find('\\') != url.npos ||
        std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c >= 127; })) return false;
    // Keep these origins aligned with musicProviderImage and the page CSP.
    for (const std::string_view host : {"y.qq.com", "y.gtimg.cn", "q1.qlogo.cn", "q2.qlogo.cn", "thirdqq.qlogo.cn",
        "p1.music.126.net", "p2.music.126.net", "p3.music.126.net", "p4.music.126.net",
        "imge.kugou.com", "imgessl.kugou.com"}) {
        if (url.substr(8, host.size()) != host) continue;
        auto suffix = url.substr(8 + host.size());
        if (suffix.substr(0, 4) == ":443") suffix.remove_prefix(4);
        if (suffix.empty() || suffix.front() == '/' || suffix.front() == '?' || suffix.front() == '#') return true;
    }
    return false;
}

struct Membership {
    std::string kind;
    std::string label;
    std::int64_t level = 0;
    bool active = false;
    std::string iconUrl;
    std::int64_t expiresAt = 0;  // Unix milliseconds; zero means unknown.
};
inline void to_json(Json& json, const Membership& membership) {
    json = {{"kind", membership.kind}, {"label", membership.label}, {"level", membership.level},
        {"active", membership.active}, {"iconUrl", membership.iconUrl}, {"expiresAt", membership.expiresAt}};
}

// Only these public fields may cross the page bridge. Session secrets never do.
struct Reply {
    std::string status = "signed-out";
    std::string phase;
    std::string userId;
    std::string displayName;
    std::string qrImage;
    std::string qrText;
    std::string error;
    std::string avatarUrl = {};
    std::vector<Membership> memberships = {};
};
using Execute = std::function<Reply(Provider, Operation, Json&, const Cancelled&)>;

}  // namespace lume::music::auth
