#include "platform/windows/music_provider_http.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace lume::music::auth {
namespace {
constexpr std::size_t MaxBody = 1024 * 1024;
// Large catalog listings (a 10,000-song NetEase playlist is ~1.8 MB of track IDs).
constexpr std::size_t MaxResponseBody = 4 * 1024 * 1024;
constexpr std::size_t MaxHeaders = 32 * 1024;
struct Route {
    Provider provider;
    std::string_view method, host, path;
    bool publicImage = false;
};
constexpr Route Routes[] = {
    {Provider::Netease, "POST", "music.163.com", "/weapi/login/qrcode/unikey"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/login/qrcode/client/login"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/nuser/account/get"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/w/nuser/account/get"},
    {Provider::Netease, "POST", "music.163.com", "/api/login/qrcode/unikey"},
    {Provider::Netease, "POST", "music.163.com", "/api/login/qrcode/client/login"},
    {Provider::Netease, "POST", "music.163.com", "/api/nuser/account/get"},
    {Provider::Netease, "POST", "music.163.com", "/api/w/nuser/account/get"},
    {Provider::Netease, "POST", "interfacepc.music.163.com", "/eapi/login/qrcode/unikey"},
    {Provider::Netease, "POST", "interfacepc.music.163.com", "/eapi/login/qrcode/client/login"},
    {Provider::QQ, "GET", "ssl.ptlogin2.qq.com", "/ptqrshow"},
    {Provider::QQ, "GET", "ssl.ptlogin2.qq.com", "/ptqrlogin"},
    {Provider::QQ, "GET", "ssl.ptlogin2.graph.qq.com", "/check_sig"},
    {Provider::QQ, "POST", "graph.qq.com", "/oauth2.0/authorize"},
    {Provider::QQ, "POST", "api.tencentmusic.com", "/tme/trpc/proxy"},
    {Provider::QQ, "POST", "u.y.qq.com", "/cgi-bin/musicu.fcg"},
    {Provider::Kugou, "GET", "login-user.kugou.com", "/v2/qrcode"},
    {Provider::Kugou, "GET", "login-user.kugou.com", "/v2/get_userinfo_qrcode"},
    {Provider::Kugou, "POST", "userservice.kugou.com", "/risk/v2/r_register_dev"},
    {Provider::Kugou, "POST", "gateway.kugou.com", "/v3/get_my_info"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/personalized/playlist"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/user/playlist"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/playlist/list"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/v3/discovery/recommend/songs"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/music-vip-membership/client/vip/info"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/play-record/song/list"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/radio/like"},
    {Provider::Netease, "POST", "music.163.com", "/weapi/playlist/create"},
    {Provider::Netease, "POST", "interfacepc.music.163.com", "/eapi/v6/playlist/detail"},
    {Provider::Netease, "POST", "interfacepc.music.163.com", "/eapi/v3/song/detail"},
    {Provider::Netease, "POST", "interfacepc.music.163.com", "/eapi/cloudsearch/pc"},
    {Provider::Netease, "POST", "interfacepc.music.163.com", "/eapi/song/like/get"},
    {Provider::Netease, "POST", "interfacepc.music.163.com", "/eapi/playlist/manipulate/tracks"},
    {Provider::Netease, "POST", "interfacepc.music.163.com", "/eapi/song/enhance/player/url"},
    {Provider::Netease, "POST", "interfacepc.music.163.com", "/eapi/song/enhance/player/url/v1"},
    {Provider::Netease, "POST", "interfacepc.music.163.com", "/eapi/song/lyric/v1"},
    {Provider::QQ, "GET", "c.y.qq.com", "/splcloud/fcgi-bin/fcg_get_diss_by_tag.fcg"},
    {Provider::QQ, "GET", "c.y.qq.com", "/rsc/fcgi-bin/fcg_user_created_diss"},
    {Provider::QQ, "GET", "c.y.qq.com", "/fav/fcgi-bin/fcg_get_profile_order_asset.fcg"},
    {Provider::Kugou, "GET", "gateway.kugou.com", "/v3/search/song"},
    {Provider::Kugou, "GET", "complexsearch.kugou.com", "/v2/search/song"},
    {Provider::Kugou, "GET", "gateway.kugou.com", "/pubsongs/v2/get_other_list_file_nofilt"},
    {Provider::Kugou, "GET", "gateway.kugou.com", "/v5/url"},
    {Provider::Kugou, "POST", "gateway.kugou.com", "/v7/get_all_list"},
    {Provider::Kugou, "POST", "gateway.kugou.com", "/youth/v1/song/single_card_recommend"},
    {Provider::Kugou, "POST", "gateway.kugou.com", "/v1/album_audio/lite"},
    {Provider::Kugou, "POST", "gateway.kugou.com", "/playhistory/v1/get_songs"},
    {Provider::Kugou, "POST", "gateway.kugou.com", "/cloudlist.service/v5/add_list"},
    {Provider::Kugou, "POST", "gateway.kugou.com", "/cloudlist.service/v6/add_song"},
    {Provider::Kugou, "POST", "gateway.kugou.com", "/v4/delete_songs"},
    {Provider::Kugou, "POST", "gateway.kugou.com", "/v4/get_list_all_file"},
    {Provider::Kugou, "GET", "lyrics.kugou.com", "/v1/search"},
    {Provider::Kugou, "GET", "lyrics.kugou.com", "/download"},
    {Provider::Kugou, "GET", "krcs.kugou.com", "/search"},
    {Provider::Netease, "GET", "p1.music.126.net", "", true},
    {Provider::Netease, "GET", "p2.music.126.net", "", true},
    {Provider::Netease, "GET", "p3.music.126.net", "", true},
    {Provider::Netease, "GET", "p4.music.126.net", "", true},
    {Provider::QQ, "GET", "y.gtimg.cn", "", true},
    {Provider::Kugou, "GET", "imge.kugou.com", "", true},
    {Provider::Kugou, "GET", "imgessl.kugou.com", "", true},
};
[[noreturn]] void Fail() { throw std::runtime_error("network"); }
[[noreturn]] void Invalid() { throw std::runtime_error("invalid-response"); }
void Check(BOOL ok) { if (!ok) Fail(); }
bool Printable(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}
bool Same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return (x >= 'A' && x <= 'Z' ? x + 32 : x) == y;
    });
}
const Route* Target(const HttpRequest& request) {
    const std::string_view url = request.url;
    if (url.size() > 16384 || url.substr(0, 8) != "https://" || !Printable(url) ||
        url.find_first_of(" #\\") != url.npos) return nullptr;
    const auto slash = url.find('/', 8);
    if (slash == url.npos) return nullptr;
    const auto host = url.substr(8, slash - 8);
    const auto query = url.find('?', slash);
    const auto path = url.substr(slash, query == url.npos ? url.size() - slash : query - slash);
    for (const auto& route : Routes) {
        if (route.provider != request.provider || route.method != request.method || route.host != host)
            continue;
        if (route.publicImage ? !path.empty() && path.front() == '/' : route.path == path)
            return &route;
    }
    return nullptr;
}
bool AllowedHeader(const Route& route, std::string_view name, std::string_view value) {
    if (!Printable(value) || value.size() > 16384) return false;
    if (route.provider == Provider::QQ && route.host == "api.tencentmusic.com") {
        if (Same(name, "host")) return value == route.host;
        if (Same(name, "method")) return value == "GetQimei";
        if (Same(name, "service")) return value == "trpc.tme_datasvr.qimeiproxy.QimeiProxy";
        if (Same(name, "appid")) return value == "qimei_qq_android";
        if (Same(name, "sign")) return value.size() == 32 &&
            value.find_first_not_of("0123456789abcdefABCDEF") == value.npos;
        if (Same(name, "timestamp")) return !value.empty() && value.size() <= 16 &&
            value.find_first_not_of("0123456789") == value.npos;
    }
    if (route.provider == Provider::Kugou) {
        if (Same(name, "dfid") || Same(name, "mid")) return !value.empty() && value.size() <= 256 &&
            value.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-") == value.npos;
        if (Same(name, "clienttime")) return !value.empty() && value.size() <= 16 &&
            value.find_first_not_of("0123456789") == value.npos;
        if (Same(name, "kg-clienttimems")) return route.host == "complexsearch.kugou.com" &&
            route.path == "/v2/search/song" && !value.empty() && value.size() <= 16 &&
            value.find_first_not_of("0123456789") == value.npos;
        if (Same(name, "kg-rc") || Same(name, "kg-rec")) return value == "1";
        if (Same(name, "kg-thash")) return value == "5d816a0";
        if (Same(name, "kg-rf")) return value == "B9EDA08A64250DEFFBCADDEE00F8F25F";
        if (Same(name, "kg-tid")) return route.host == "gateway.kugou.com" &&
            route.path == "/v1/album_audio/lite" && value == "255";
    }
    if (Same(name, "cookie")) {
        // A request Cookie contains only pairs, never Set-Cookie routing attributes.
        while (!value.empty()) {
            const auto end = value.find(';');
            auto pair = value.substr(0, end);
            while (!pair.empty() && pair.front() == ' ') pair.remove_prefix(1);
            const auto equals = pair.find('=');
            if (equals == pair.npos || !equals) return false;
            const auto key = pair.substr(0, equals);
            if (key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != key.npos ||
                Same(key, "domain") || Same(key, "path") || Same(key, "secure") ||
                Same(key, "httponly") || Same(key, "samesite") || Same(key, "expires") ||
                Same(key, "max-age")) return false;
            if (end == value.npos) break;
            value.remove_prefix(end + 1);
        }
        return true;
    }
    if (Same(name, "content-type")) return value == "application/json" ||
        value == "application/json;charset=UTF-8" || value == "application/json; charset=UTF-8" ||
        value == "application/x-www-form-urlencoded" ||
        value == "application/x-www-form-urlencoded; charset=UTF-8" ||
        value == "application/octet-stream";
    if (Same(name, "accept-encoding")) return value == "gzip, deflate";
    if (Same(name, "accept")) return value == "*/*" || value == "application/json" || value == "image/png";
    if (Same(name, "user-agent")) return !value.empty() && value.size() <= 512;
    if (Same(name, "x-router")) {
        if (route.provider == Provider::Kugou && route.host == "complexsearch.kugou.com" &&
            route.path == "/v2/search/song") return value == "complexsearch.kugou.com";
        if (route.provider != Provider::Kugou || route.host != "gateway.kugou.com") return false;
        if (route.path == "/v3/get_my_info") return value == "usercenter.kugou.com";
        if (route.path == "/v3/search/song") return value == "complexsearch.kugou.com";
        if (route.path == "/v5/url") return value == "trackercdn.kugou.com";
        if (route.path == "/v1/album_audio/lite") return value == "openapi.kugou.com";
        if (route.path == "/v7/get_all_list" || route.path == "/v4/delete_songs" ||
            route.path == "/v4/get_list_all_file")
            return value == "cloudlist.service.kugou.com";
        return false;
    }
    if (Same(name, "referer") || Same(name, "origin")) {
        switch (route.provider) {
        case Provider::Netease: return value == "https://music.163.com/" || value == "https://music.163.com";
        case Provider::QQ: return value == "https://xui.ptlogin2.qq.com/" ||
            value == "https://graph.qq.com/" || value == "https://y.qq.com/" || value == "https://y.qq.com";
        case Provider::Kugou: return value == "https://www.kugou.com/" || value == "https://www.kugou.com";
        }
    }
    return false;
}
std::wstring Wide(std::string_view ascii) { return {ascii.begin(), ascii.end()}; }
struct Internet {
    HINTERNET value = nullptr;
    ~Internet() { if (value) WinHttpCloseHandle(value); }
};
struct Pending {
    std::mutex mutex;
    std::condition_variable ready;
    DWORD event = 0, error = 0, count = 0;
    std::array<char, 16384> buffer{};
    std::string body;
    std::wstring headers;
};
using Context = std::shared_ptr<Pending>;
void CALLBACK Status(HINTERNET, DWORD_PTR context, DWORD event, void* info, DWORD size) {
    if (!context) return;
    auto* owner = reinterpret_cast<Context*>(context);
    auto state = *owner;
    if (event == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { delete owner; return; }
    {
        std::lock_guard lock(state->mutex);
        if (event == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && info && size >= sizeof(WINHTTP_ASYNC_RESULT))
            state->error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
        else if (event == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
                 event == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
                 event == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            state->event = event;
            state->count = size;
        } else return;
    }
    state->ready.notify_all();
}
using Deadline = std::chrono::steady_clock::time_point;
void CheckTime(const Cancelled& cancelled, Deadline deadline) {
    CheckCancelled(cancelled);
    if (std::chrono::steady_clock::now() >= deadline) Fail();
}
DWORD Await(const Context& state, DWORD event, const Cancelled& cancelled, Deadline deadline) {
    std::unique_lock lock(state->mutex);
    for (;;) {
        // User cancellation code must not execute while the callback mutex is held.
        lock.unlock();
        CheckTime(cancelled, deadline);
        lock.lock();
        if (state->error) Fail();
        if (state->event == event) { state->event = 0; return state->count; }
        state->ready.wait_until(lock, std::min(deadline,
            std::chrono::steady_clock::now() + std::chrono::milliseconds(20)));
    }
}
std::string Header(HINTERNET request, DWORD query, DWORD* index, std::size_t& budget) {
    std::array<wchar_t, 8193> buffer{};
    DWORD bytes = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &bytes, index)) {
        if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) return {};
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) Invalid();
        Fail();
    }
    if (bytes % sizeof(wchar_t)) Invalid();
    const std::size_t length = bytes / sizeof(wchar_t);
    if (length > 8192 || length > budget) Invalid();
    budget -= length;
    std::string value;
    value.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        if (buffer[i] < 32 || buffer[i] > 126) Invalid();
        value.push_back(static_cast<char>(buffer[i]));
    }
    return value;
}
}  // namespace

bool ValidHttpRequest(const HttpRequest& request) noexcept {
    const auto* route = Target(request);
    if (!route || request.body.size() > MaxBody || !request.maxResponse ||
        request.maxResponse > MaxResponseBody || request.headers.size() > 16 ||
        (route->publicImage && !request.headers.empty()) ||
        (request.method == "GET" && !request.body.empty())) return false;
    std::size_t total = 0;
    for (auto it = request.headers.begin(); it != request.headers.end(); ++it) {
        const auto& [key, value] = *it;
        if (key.size() > 64 || !AllowedHeader(*route, key, value)) return false;
        total += key.size() + value.size() + 4;
        if (total > MaxHeaders) return false;
        for (auto other = request.headers.begin(); other != it; ++other) {
            if (key.size() == other->first.size() &&
                std::equal(key.begin(), key.end(), other->first.begin(), [](char a, char b) {
                    return (a >= 'A' && a <= 'Z' ? a + 32 : a) == (b >= 'A' && b <= 'Z' ? b + 32 : b);
                })) return false;
        }
    }
    return true;
}
HttpResponse SendHttp(const HttpRequest& request, const Cancelled& cancelled) {
    // Folia bounds each sign-in request at 20s (LOGIN_REQUEST_LIMIT_MS).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    CheckTime(cancelled, deadline);
    if (!ValidHttpRequest(request)) Invalid();
    const auto& route = *Target(request);
    Internet session{WinHttpOpen(L"LumeMusicAuth/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};
    if (!session.value) Fail();
    // Login is a one-shot user action: give slow mobile/ISP paths a little more
    // room than the old 5s, and let WinHTTP reconnect once on a dropped socket.
    Check(WinHttpSetTimeouts(session.value, 8000, 8000, 8000, 8000));
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    // Windows 10 before 20H1 rejects the TLS 1.3 flag; fall back to TLS 1.2.
    if (!WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        Check(WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)));
    }
    DWORD retries = 2;
    Check(WinHttpSetOption(session.value, WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries)));
    // Networks whose IPv6 route is broken while IPv4 works (Folia's ipv6-failed
    // verdict) otherwise wait out the whole connect timeout on the AAAA address.
    // Happy-eyeballs fallback needs Windows 10 2004+; older systems ignore it.
#ifdef WINHTTP_OPTION_IPV6_FAST_FALLBACK
    BOOL fastFallback = TRUE;
    WinHttpSetOption(session.value, WINHTTP_OPTION_IPV6_FAST_FALLBACK, &fastFallback, sizeof(fastFallback));
#endif
    const auto host = Wide(route.host);
    Internet connection{WinHttpConnect(session.value, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) Fail();
    const auto path = Wide(std::string_view(request.url).substr(8 + route.host.size()));
    const auto method = Wide(request.method);
    Internet handle{WinHttpOpenRequest(connection.value, method.c_str(), path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_ESCAPE_DISABLE)};
    if (!handle.value) Fail();
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    Check(WinHttpSetOption(handle.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    Check(WinHttpSetOption(handle.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
    DWORD headerLimit = static_cast<DWORD>(MaxHeaders);
    Check(WinHttpSetOption(handle.value, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headerLimit, sizeof(headerLimit)));
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    Check(WinHttpSetOption(handle.value, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress)));
    DWORD autoLogon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    Check(WinHttpSetOption(handle.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &autoLogon, sizeof(autoLogon)));
    auto state = std::make_shared<Pending>();
    state->body = request.body;
    for (const auto& [key, value] : request.headers) state->headers += Wide(key) + L": " + Wide(value) + L"\r\n";
    if (WinHttpSetStatusCallback(handle.value, Status,
        WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) Fail();
    // WinHTTP owns this reference until HANDLE_CLOSING, including after cancellation.
    auto owner = std::make_unique<Context>(state);
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(owner.get());
    Check(WinHttpSetOption(handle.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)));
    owner.release();
    CheckTime(cancelled, deadline);
    Check(WinHttpSendRequest(handle.value, state->headers.c_str(), static_cast<DWORD>(state->headers.size()),
        state->body.empty() ? WINHTTP_NO_REQUEST_DATA : state->body.data(),
        static_cast<DWORD>(state->body.size()), static_cast<DWORD>(state->body.size()), context));
    Await(state, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, cancelled, deadline);
    CheckTime(cancelled, deadline);
    Check(WinHttpReceiveResponse(handle.value, nullptr));
    Await(state, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, cancelled, deadline);
    HttpResponse response;
    DWORD status = 0, bytes = sizeof(status);
    Check(WinHttpQueryHeaders(handle.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX));
    response.status = status;
    std::size_t budget = MaxHeaders;
    response.contentType = Header(handle.value, WINHTTP_QUERY_CONTENT_TYPE, nullptr, budget);
    response.location = Header(handle.value, WINHTTP_QUERY_LOCATION, nullptr, budget);
    DWORD index = 0;
    for (;;) {
        CheckTime(cancelled, deadline);
        auto cookie = Header(handle.value, WINHTTP_QUERY_SET_COOKIE, &index, budget);
        if (cookie.empty()) break;
        if (response.cookies.size() == 64) Invalid();
        response.cookies.push_back(std::move(cookie));
    }
    for (;;) {
        CheckTime(cancelled, deadline);
        Check(WinHttpReadData(handle.value, state->buffer.data(), static_cast<DWORD>(state->buffer.size()), nullptr));
        const auto count = Await(state, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, cancelled, deadline);
        if (!count) break;
        if (count > state->buffer.size() || count > request.maxResponse - response.body.size()) Invalid();
        response.body.append(state->buffer.data(), count);
    }
    CheckTime(cancelled, deadline);
    return response;
}
Reply ExecuteProvider(Provider provider, Operation operation, Json& secrets, const Cancelled& cancelled) {
    CheckCancelled(cancelled);
    switch (provider) {
    case Provider::Netease: return RunNetease(operation, secrets, cancelled, SendHttp);
    case Provider::QQ: return RunQQ(operation, secrets, cancelled, SendHttp);
    case Provider::Kugou: return RunKugou(operation, secrets, cancelled, SendHttp);
    }
    Invalid();
}
}  // namespace lume::music::auth
