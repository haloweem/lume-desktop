#include "core/account/security.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include <algorithm>
#include <map>
#include <set>

namespace lume::account {
namespace {
std::vector<std::uint32_t> Scalars(std::string_view s) {
    std::vector<std::uint32_t> result;
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i++]);
        std::uint32_t cp = c;
        unsigned extra = 0;
        if (c >= 0xc2 && c <= 0xdf) { extra = 1; cp = c & 31; }
        else if (c >= 0xe0 && c <= 0xef) { extra = 2; cp = c & 15; }
        else if (c >= 0xf0 && c <= 0xf4) { extra = 3; cp = c & 7; }
        else if (c >= 0x80) throw Failure(Error::InvalidResponse);
        if (i + extra > s.size()) throw Failure(Error::InvalidResponse);
        for (unsigned j = 0; j < extra; ++j) {
            auto b = static_cast<unsigned char>(s[i++]);
            if ((b & 0xc0) != 0x80) throw Failure(Error::InvalidResponse);
            cp = (cp << 6) | (b & 63);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            throw Failure(Error::InvalidResponse);
        result.push_back(cp);
    }
    return result;
}
void AppendUtf8(std::string& s, std::uint32_t c) {
    if (c < 0x80) s += static_cast<char>(c);
    else if (c < 0x800) {
        s += static_cast<char>(0xc0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 63));
    } else if (c < 0x10000) {
        s += static_cast<char>(0xe0 | (c >> 12)); s += static_cast<char>(0x80 | ((c >> 6) & 63));
        s += static_cast<char>(0x80 | (c & 63));
    } else {
        s += static_cast<char>(0xf0 | (c >> 18)); s += static_cast<char>(0x80 | ((c >> 12) & 63));
        s += static_cast<char>(0x80 | ((c >> 6) & 63)); s += static_cast<char>(0x80 | (c & 63));
    }
}
int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
std::string Decode(std::string_view s) {
    std::string result;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%') {
            if (i + 2 >= s.size() || Hex(s[i + 1]) < 0 || Hex(s[i + 2]) < 0) throw Failure(Error::CallbackInvalid);
            result += static_cast<char>((Hex(s[i + 1]) << 4) | Hex(s[i + 2])); i += 2;
        } else result += s[i] == '+' ? ' ' : s[i];
    }
    try {
        for (auto cp : Scalars(result)) if (cp < 32 || cp == 127) throw Failure(Error::CallbackInvalid);
    } catch (...) { throw Failure(Error::CallbackInvalid); }
    return result;
}
struct Url { std::string_view host, path, query; };
Url ParseHttps(std::string_view s) {
    if (s.size() > 8192 || s.substr(0, 8) != "https://") throw Failure(Error::InvalidTarget);
    for (unsigned char c : s) if (c <= 32 || c >= 127 || c == '\\' || c == '#') throw Failure(Error::InvalidTarget);
    auto slash = s.find('/', 8);
    if (slash == s.npos) throw Failure(Error::InvalidTarget);
    auto host = s.substr(8, slash - 8);
    if (host.empty() || host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") != host.npos ||
        host.front() == '.' || host.back() == '.') throw Failure(Error::InvalidTarget);
    auto question = s.find('?', slash);
    auto path = s.substr(slash, question == s.npos ? s.size() - slash : question - slash);
    if (path.find("..") != path.npos || path.find('%') != path.npos) throw Failure(Error::InvalidTarget);
    return {host, path, question == s.npos ? std::string_view{} : s.substr(question + 1)};
}
bool CosHost(std::string_view host) {
    constexpr std::string_view suffix = ".myqcloud.com";
    if (host.size() <= suffix.size() || host.substr(host.size() - suffix.size()) != suffix) return false;
    auto cos = host.find(".cos.");
    return cos != host.npos && cos > 0 && cos < host.size() - suffix.size() &&
        host.substr(0, cos).find('.') == host.npos &&
        host.substr(cos + 5, host.size() - suffix.size() - cos - 5).find('.') == host.npos;
}
}
std::string ErrorCode(Error e) {
    switch (e) {
    case Error::None: return "";
    case Error::InvalidCommand: return "invalid-command";
    case Error::InvalidName: return "invalid-name";
    case Error::InvalidAvatar: return "invalid-avatar";
    case Error::AvatarTooLarge: return "avatar-too-large";
    case Error::CallbackInvalid: return "callback-invalid";
    case Error::CallbackInactive: return "callback-inactive";
    case Error::CallbackState: return "callback-state";
    case Error::CallbackExpired: return "callback-expired";
    case Error::AuthorizationDenied: return "authorization-denied";
    case Error::InvalidToken: return "invalid-token";
    case Error::TokenExpired: return "token-expired";
    case Error::Network: return "network-unavailable";
    case Error::Service: return "service-unavailable";
    case Error::InvalidResponse: return "invalid-response";
    case Error::Storage: return "storage-failed";
    case Error::CredentialCorrupt: return "credential-corrupt";
    case Error::Cancelled: return "cancelled";
    case Error::AuthRequired: return "auth-required";
    case Error::DeviceInactive: return "device-inactive";
    case Error::Browser: return "browser-failed";
    case Error::LocalLogout: return "local-logout";
    case Error::InvalidTarget: return "invalid-target";
    case Error::ProtocolRegistration: return "protocol-registration-failed";
    case Error::AvatarUploadFailed: return "avatar-upload-failed";
    case Error::WallpaperQuota: return "wallpaper-download-quota";
    }
    return "service-unavailable";
}
Json StrictJson(std::string_view input, std::size_t limit) {
    if (input.empty() || input.size() > limit) throw Failure(Error::InvalidResponse);
    try {
        Scalars(input);
        std::vector<std::set<std::string>> objects;
        return Json::parse(input, [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 24) throw Failure(Error::InvalidResponse);
            if (event == Json::parse_event_t::object_start) objects.emplace_back();
            if (event == Json::parse_event_t::object_end) objects.pop_back();
            if (event == Json::parse_event_t::key && (objects.empty() || !objects.back().insert(value.get<std::string>()).second))
                throw Failure(Error::InvalidResponse);
            return true;
        });
    } catch (...) { throw Failure(Error::InvalidResponse); }
}
std::string Utf8(std::wstring_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        auto c = static_cast<std::uint32_t>(s[i]);
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == s.size() || s[i] < 0xdc00 || s[i] > 0xdfff) throw Failure(Error::InvalidResponse);
            c = 0x10000 + ((c - 0xd800) << 10) + (s[i] - 0xdc00);
        } else if (c >= 0xdc00 && c <= 0xdfff) throw Failure(Error::InvalidResponse);
        if (c > 0x10ffff) throw Failure(Error::InvalidResponse);
        AppendUtf8(out, c);
    }
    return out;
}
std::wstring Wide(std::string_view s) {
    std::wstring out;
    for (auto cp : Scalars(s)) {
        if (cp < 0x10000) out += static_cast<wchar_t>(cp);
        else { cp -= 0x10000; out += static_cast<wchar_t>(0xd800 + (cp >> 10)); out += static_cast<wchar_t>(0xdc00 + (cp & 1023)); }
    }
    return out;
}
std::string NormalizeName(std::string_view s) {
    if (s.size() > 512) throw Failure(Error::InvalidName);
    try {
        auto chars = Scalars(s);
        const auto space = [](std::uint32_t c) { return c == 32 || c == 9 || c == 10 || c == 13 || c == 0xa0 ||
            c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff; };
        while (!chars.empty() && space(chars.back())) chars.pop_back();
        auto first = std::find_if_not(chars.begin(), chars.end(), space);
        chars.erase(chars.begin(), first);
        if (chars.size() < 2 || chars.size() > 32) throw Failure(Error::InvalidName);
        std::string out;
        for (auto c : chars) {
            if (c < 32 || (c >= 0x7f && c <= 0x9f)) throw Failure(Error::InvalidName);
            AppendUtf8(out, c);
        }
        return out;
    } catch (...) { throw Failure(Error::InvalidName); }
}
std::string UrlEncode(std::string_view s) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string Base64(const Bytes& bytes, bool url) {
    const char* chars = url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_" :
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::uint32_t bits = 0; unsigned count = 0;
    for (auto b : bytes) {
        bits = (bits << 8) | b; count += 8;
        while (count >= 6) { count -= 6; out += chars[(bits >> count) & 63]; }
    }
    if (count) out += chars[(bits << (6 - count)) & 63];
    if (!url) while (out.size() % 4) out += '=';
    return out;
}
Bytes Unbase64(std::string_view s) {
    if (s.empty() || s.size() % 4 == 1 || s.size() > 256 * 1024) throw Failure(Error::InvalidToken);
    constexpr std::string_view chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    Bytes out; std::uint32_t bits = 0; unsigned count = 0;
    for (char c : s) {
        auto v = chars.find(c);
        if (v == chars.npos) throw Failure(Error::InvalidToken);
        bits = (bits << 6) | static_cast<unsigned>(v); count += 6;
        if (count >= 8) { count -= 8; out.push_back(static_cast<unsigned char>(bits >> count)); }
    }
    if (count && (bits & ((1u << count) - 1))) throw Failure(Error::InvalidToken);
    return out;
}
std::string PkceChallenge(std::string_view verifier) { return Base64(Digest(verifier)); }
std::string Transaction::Begin(std::int64_t now) {
    Cancel();
    state = Base64(RandomBytes(32)); nonce = Base64(RandomBytes(32)); verifier = Base64(RandomBytes(32));
    expires = now + LoginTransactionSeconds; active = true;
    return std::string(Issuer) + "/login/oauth/authorize?client_id=" + ClientId +
        "&response_type=code&redirect_uri=lume%3A%2F%2Flogin&scope=openid%20profile%20email%20phone%20offline_access" +
        "&state=" + state + "&nonce=" + nonce + "&code_challenge=" + PkceChallenge(verifier) + "&code_challenge_method=S256";
}
void Transaction::Cancel() {
    active = false;
    std::fill(state.begin(), state.end(), '\0'); std::fill(nonce.begin(), nonce.end(), '\0');
    std::fill(verifier.begin(), verifier.end(), '\0');
    state.clear(); nonce.clear(); verifier.clear(); expires = 0;
}
Grant Transaction::Complete(std::string_view uri, std::int64_t now) {
    if (!active) throw Failure(Error::CallbackInactive);
    if (now >= expires) { Cancel(); throw Failure(Error::CallbackExpired); }
    if (uri.size() > 8192 || uri.find_first_of("#\\\r\n\t ") != uri.npos) throw Failure(Error::CallbackInvalid);
    auto q = uri.find('?');
    if (q == uri.npos || (uri.substr(0, q) != CallbackUri && uri.substr(0, q) != "lume://login/"))
        throw Failure(Error::CallbackInvalid);
    std::map<std::string, std::string> params;
    const std::set<std::string> allowed{"code", "state", "iss", "session_state", "error", "error_description", "error_uri"};
    for (auto pos = q + 1; pos <= uri.size();) {
        auto end = uri.find('&', pos); if (end == uri.npos) end = uri.size();
        auto pair = uri.substr(pos, end - pos); auto equals = pair.find('=');
        if (equals == pair.npos) throw Failure(Error::CallbackInvalid);
        auto key = Decode(pair.substr(0, equals)); auto value = Decode(pair.substr(equals + 1));
        if (!allowed.count(key) || !params.emplace(key, value).second || value.size() > 4096)
            throw Failure(Error::CallbackInvalid);
        if (end == uri.size()) break;
        pos = end + 1;
    }
    if (!params.count("state") || params["state"].empty()) throw Failure(Error::CallbackInvalid);
    // Random fixed-length state. Compare every byte before deciding.
    auto received = params["state"];
    unsigned difference = static_cast<unsigned>(received.size() ^ state.size());
    for (std::size_t i = 0; i < state.size(); ++i)
        difference |= static_cast<unsigned char>(state[i]) ^ (i < received.size() ? static_cast<unsigned char>(received[i]) : 0);
    if (difference) throw Failure(Error::CallbackState);
    if (params.count("iss") && params["iss"] != Issuer) throw Failure(Error::CallbackInvalid);
    if (params.count("error")) {
        if (params.count("code")) throw Failure(Error::CallbackInvalid);
        Cancel(); throw Failure(Error::AuthorizationDenied);
    }
    if (!params.count("code") || params["code"].empty() || params.count("error_description") || params.count("error_uri"))
        throw Failure(Error::CallbackInvalid);
    Grant out{params["code"], nonce, verifier}; Cancel(); return out;
}
bool ValidTarget(Target target, std::string_view method, std::string_view url, bool bearer) noexcept {
    try {
        auto u = ParseHttps(url);
        const bool auth = u.host == "auth.lumedesktop.com";
        const bool account = u.host == "account.lumedesktop.com";
        if (bearer && !auth && !account) return false;
        switch (target) {
        case Target::Discovery: return auth && !bearer && method == "GET" && u.path == "/.well-known/openid-configuration" && u.query.empty();
        case Target::Token: return auth && method == "POST" && u.path == "/api/login/oauth/access_token" && u.query.empty();
        case Target::Jwks: return auth && !bearer && method == "GET" && u.path == "/.well-known/jwks" && u.query.empty();
        case Target::Userinfo: return auth && method == "GET" && u.path == "/api/userinfo" && u.query.empty();
        case Target::ProfileRead: return auth && method == "GET" && u.path == "/api/get-account" && u.query.empty();
        case Target::ProfileWrite: return auth && method == "POST" && u.path == "/api/update-user" && !u.query.empty();
        case Target::Device: return account && (method == "GET" || method == "POST") && u.path == "/api/devices/session" && u.query.empty();
        case Target::AvatarTicket: return account && method == "POST" && u.path == "/api/avatar/upload-url" && u.query.empty();
        case Target::AvatarPut: return !bearer && method == "PUT" && CosHost(u.host) && !u.query.empty();
        case Target::AvatarGet: return !bearer && method == "GET" && (auth || account || CosHost(u.host) ||
            u.host == "static.lumedesktop.com" || u.host == "lumedesktop.com");
        case Target::WallpaperStats:
            return account && method == "GET" && u.path == "/api/wallpapers/stats" &&
                u.query.substr(0, 3) == "id=" && wallpaper::IsSafeWallpaperId(u.query.substr(3));
        case Target::WallpaperLike:
            return account && bearer && method == "POST" && u.path == "/api/wallpapers/like" && u.query.empty();
        case Target::WallpaperGrant:
            return account && method == "POST" && u.path == "/api/wallpapers/grant" && u.query.empty();
        case Target::WallpaperTicket: {
            constexpr std::string_view prefix = "/api/wallpapers/download/";
            const auto token = u.path.substr(prefix.size());
            return account && !bearer && method == "GET" && u.query.empty() &&
                u.path.substr(0, prefix.size()) == prefix && token.size() >= 20 && token.size() <= 256 &&
                std::all_of(token.begin(), token.end(), [](unsigned char c) {
                    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_';
                });
        }
        }
    } catch (...) {}
    return false;
}
bool ValidAuthorizationUrl(std::string_view s) noexcept {
    try { auto u = ParseHttps(s); return u.host == "auth.lumedesktop.com" && u.path == "/login/oauth/authorize" && !u.query.empty(); }
    catch (...) { return false; }
}
Error MapHttpError(unsigned status, std::string_view body, Target target) {
    if (status >= 200 && status < 300) return Error::None;
    if (target == Target::WallpaperTicket && status == 302) return Error::None;
    if ((target == Target::WallpaperTicket || target == Target::WallpaperGrant) && status == 429) return Error::WallpaperQuota;
    if (target == Target::WallpaperTicket && (status == 401 || status == 403)) return Error::Service;
    // A signed storage PUT has its own authorization, never the account Bearer.
    if (target == Target::AvatarPut) return Error::AvatarUploadFailed;
    if (target == Target::Discovery || target == Target::Jwks) return Error::Service;
    // A business endpoint's permission failure is not token revocation. Device
    // eviction is represented explicitly by a verified active=false response.
    if (status == 403 && target != Target::Token) return Error::Service;
    if (status == 401 || status == 403) return Error::AuthRequired;
    if (status == 400) {
        try { auto j = StrictJson(body); if (j.value("error", "") == "invalid_grant") return Error::AuthRequired; }
        catch (...) {}
    }
    return Error::Service;
}
}
