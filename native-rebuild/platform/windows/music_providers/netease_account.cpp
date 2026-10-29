// Protocol port of @neteasecloudmusicapienhanced/api 4.40.1:
// module/login_qr_{key,check,create}, login_status, user_account and util/{crypto,request}.
// The MIT License (MIT)
// Copyright (c) 2013-2022 Binaryify
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "platform/windows/music_provider_http.h"
#include "platform/windows/music_providers/catalog_requests.h"

#include <algorithm>
#include <chrono>

namespace lume::music::auth {
namespace {
using Ordered = nlohmann::ordered_json;
constexpr char PublicKey[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDgtQn2JZ34ZC28NWYpAUd98iZ37BUr"
    "X/aKzmFbt7clFSs6sXqHauqKWqdtLkF2KexO40H1YTX8z2lSgBBOAxLsvaklV8k4cBF"
    "K9snQXE9/DDaFt6Rr7iVZMldczhC0JNgTz+SHXT6CBHuX3e9SdB1Ua44oncaTWz7OBGL"
    "bCiK45wIDAQAB\n-----END PUBLIC KEY-----";

struct Failure { std::string error; };
std::string Text(const Json& j, const char* key, std::size_t max = 4096) {
    const auto i = j.find(key);
    if (i == j.end() || !i->is_string()) return {};
    const auto s = i->get<std::string>();
    if (s.size() > max || std::any_of(s.begin(), s.end(),
        [](unsigned char c) { return c < 32 || c == 127; })) return {};
    return s;
}
std::string Id(const Json& j, const char* key) {
    const auto i = j.find(key);
    if (i == j.end()) return {};
    const auto s = i->is_number_unsigned() ? std::to_string(i->get<std::uint64_t>()) :
        i->is_number_integer() ? std::to_string(i->get<std::int64_t>()) : Text(j, key, 128);
    if (s.empty() || s.size() > 128 || s.front() == '0' ||
        !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
    return s;
}
std::string Hex(std::string_view bytes, bool upper = false) {
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
std::string Cookie(const Json& secrets, const char* name) {
    const auto value = Text(secrets, name);
    return value.find(';') == std::string::npos ? value : std::string{};
}
void MergeCookies(Json& secrets, const HttpResponse& response, bool authenticated) {
    if (response.cookies.size() > 64) throw Failure{"invalid-response"};
    for (const auto& entry : response.cookies) {
        if (entry.size() > 8192 || entry.find_first_of("\r\n") != std::string::npos)
            throw Failure{"invalid-response"};
        const auto end = entry.find(';');
        const auto pair = entry.substr(0, end);
        const auto split = pair.find('=');
        if (split == std::string::npos) continue;
        const auto name = pair.substr(0, split);
        if (name != "__csrf" && name != "MUSIC_A" && !(authenticated && name == "MUSIC_U")) continue;
        const auto value = pair.substr(split + 1);
        Json check = {{name, value}};
        if (value.empty()) secrets.erase(name);
        else if (Cookie(check, name.c_str()).empty()) throw Failure{"invalid-response"};
        else secrets[name] = value;
    }
}
void ClearAuth(Json& secrets) {
    secrets.erase("MUSIC_U");
    secrets.erase("__csrf");
    secrets.erase("qrKey");
}
Reply State(const char* status, std::string error = "", const char* phase = "") {
    Reply reply;
    reply.status = status; reply.error = error; reply.phase = phase;
    return reply;
}

struct Result { Json body; HttpResponse response; };
Result Request(const char* path, Ordered data, bool weapi, Json& secrets,
               const Cancelled& cancelled, const Exchange& exchange, std::size_t maxResponse = 256 * 1024) {
    CheckCancelled(cancelled);
    if (Text(secrets, "deviceId", 128).empty()) secrets["deviceId"] = RandomHex(16);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    HttpRequest request;
    request.maxResponse = maxResponse;
    request.provider = Provider::Netease;
    request.method = "POST";
    request.headers["Content-Type"] = "application/x-www-form-urlencoded";
    data["e_r"] = false;
    if (weapi) {
        request.url = std::string("https://music.163.com/weapi/") + (path + 5);
        request.headers["Referer"] = "https://music.163.com";
        request.headers["User-Agent"] = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 Edg/124.0.0.0";
        request.headers["Cookie"] = "os=pc; appver=3.1.17.204416; channel=netease; __remember_me=true";
        for (const auto* name : {"MUSIC_U", "__csrf", "MUSIC_A"})
            if (!Cookie(secrets, name).empty()) request.headers["Cookie"] +=
                std::string("; ") + name + "=" + Cookie(secrets, name);
        data["csrf_token"] = Cookie(secrets, "__csrf");
        auto key = RandomHex(8);
        const auto inner = Base64(Aes(data.dump(), "0CoJUm6Qyw8W8jud", "0102030405060708"));
        const auto params = Base64(Aes(inner, key, "0102030405060708"));
        std::reverse(key.begin(), key.end());
        // forge's NONE mode interprets the reversed key as a big-endian integer.
        key.insert(0, 128 - key.size(), '\0');
        request.body = "params=" + UrlEncode(params) + "&encSecKey=" +
            Hex(RsaEncrypt(key, PublicKey, true));
    } else {
        Ordered header = {
            {"osver", "Microsoft-Windows-10-Professional-build-19045-64bit"},
            {"deviceId", Text(secrets, "deviceId", 128)}, {"os", "pc"},
            {"appver", "3.1.17.204416"}, {"versioncode", "140"}, {"mobilename", ""},
            {"buildver", std::to_string(millis / 1000)}, {"resolution", "1920x1080"},
            {"__csrf", Cookie(secrets, "__csrf")}, {"channel", "netease"},
            {"requestId", std::to_string(millis) + "_0" +
                std::to_string(std::stoul(RandomHex(2), nullptr, 16) % 900 + 100)}
        };
        for (const auto* name : {"MUSIC_U", "MUSIC_A"})
            if (!Cookie(secrets, name).empty()) header[name] = Cookie(secrets, name);
        std::string cookie;
        for (auto it = header.begin(); it != header.end(); ++it) {
            if (!cookie.empty()) cookie += "; ";
            cookie += UrlEncode(it.key()) + "=" + UrlEncode(it.value().get<std::string>());
        }
        request.headers["Cookie"] = cookie;
        request.headers["User-Agent"] = "NeteaseMusic 9.0.90/5038 (iPhone; iOS 16.2; zh_CN)";
        request.url = std::string("https://interfacepc.music.163.com/eapi/") + (path + 5);
        data["header"] = header;
        const auto text = data.dump();
        const auto digest = HashHex(std::string("nobody") + path + "use" + text + "md5forencrypt");
        const auto envelope = std::string(path) + "-36cd479b6b5-" + text + "-36cd479b6b5-" + digest;
        request.body = "params=" + Hex(Aes(envelope, "e82ckenh8dichen8", ""), true);
    }
    HttpResponse response;
    try { response = exchange(request, cancelled); }
    catch (...) { CheckCancelled(cancelled); throw Failure{"network"}; }
    CheckCancelled(cancelled);
    if (response.status == 401 || response.status == 403) throw Failure{"expired"};
    if (response.status == 429) throw Failure{"rate-limited"};
    if (response.status < 200 || response.status >= 300) throw Failure{"service"};
    if (response.body.empty() || response.body.size() > request.maxResponse) throw Failure{"invalid-response"};
    auto body = Json::parse(response.body, nullptr, false);
    if (!body.is_object()) throw Failure{"invalid-response"};
    return {std::move(body), std::move(response)};
}
int Code(const Json& j) {
    const auto i = j.find("code");
    if (i == j.end() || !i->is_number_integer()) throw Failure{"invalid-response"};
    return i->get<int>();
}
// A QR step NetEase declined with its own code (8821 is its risk check). Like
// Folia's createLoginQrCheck, the page gets the real code instead of a
// generic "QR failed".
std::string Refused(int code) {
    return code > 0 && code < 100000000 ? "netease-code-" + std::to_string(code) : std::string("service");
}
// https form of a NetEase CDN image, or empty when it is not one.
std::string NeteaseImage(std::string url) {
    if (url.rfind("http://", 0) == 0) url = "https://" + url.substr(7);
    return PublicImageUrl(url) ? url : std::string{};
}
std::int64_t Integer(const Json& j, const char* key) {
    const auto i = j.find(key);
    if (i == j.end() || i->is_null()) return 0;
    if (i->is_number_integer()) return i->get<std::int64_t>();
    if (i->is_boolean()) return i->get<bool>() ? 1 : 0;
    return 0;
}
// 黑胶VIP / 黑胶SVIP from the membership service; vipType on the account
// (11 = 黑胶VIP, live 2026-10-11) is the fallback when that call fails.
void Memberships(Reply& reply, const Json& account, Json& secrets, const Cancelled& cancelled,
                 const Exchange& exchange) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    Membership vip{"netease-vip", u8"\u9ed1\u80f6VIP", 0, false, {}, 0};
    Membership svip{"netease-svip", u8"\u9ed1\u80f6SVIP", 0, false, {}, 0};
    bool known = false;
    try {
        const auto result = Request("/api/music-vip-membership/client/vip/info",
            Ordered{{"userId", reply.userId}}, true, secrets, cancelled, exchange);
        const auto data = result.body.find("data");
        if (Code(result.body) == 200 && data != result.body.end() && data->is_object()) {
            known = true;
            const auto level = Integer(*data, "redVipLevel");
            const auto tier = [&](const char* key, Membership& target) {
                const auto entry = data->find(key);
                if (entry == data->end() || !entry->is_object()) return;
                const auto expires = Integer(*entry, "expireTime");
                target.expiresAt = expires > 0 ? expires : 0;
                target.active = Integer(*entry, "vipCode") > 0 && expires > now;
                target.level = target.active ? level : 0;
            };
            tier("associator", vip);
            tier("redplus", svip);
            // SVIP includes every VIP benefit.
            if (svip.active && !vip.active) { vip.active = true; vip.level = level; }
        }
    } catch (...) { CheckCancelled(cancelled); }
    if (!known && account.is_object()) {
        const auto type = Integer(account, "vipType");
        vip.active = type == 10 || type == 11 || type >= 100;
    }
    reply.memberships = {std::move(vip), std::move(svip)};
}

Reply Profile(Json& secrets, bool refresh, const Cancelled& cancelled, const Exchange& exchange) {
    if (Cookie(secrets, "MUSIC_U").empty()) throw Failure{"expired"};
    const auto result = Request(refresh ? "/api/w/nuser/account/get" : "/api/nuser/account/get",
                                Ordered::object(), true, secrets, cancelled, exchange);
    const auto code = Code(result.body);
    if (code == 301 || code == 302 || code == 401 || code == 403) throw Failure{"expired"};
    if (code != 200) throw Failure{"service"};
    const auto p = result.body.find("profile");
    if (p == result.body.end()) throw Failure{"invalid-response"};
    if (p->is_null()) throw Failure{"expired"};
    if (!p->is_object()) throw Failure{"invalid-response"};
    Reply reply = State("signed-in");
    reply.userId = Id(*p, "userId");
    reply.displayName = Text(*p, "nickname", 256);
    if (reply.userId.empty() || reply.displayName.empty()) throw Failure{"invalid-response"};
    const auto a = result.body.find("account");
    if (a != result.body.end() && (!a->is_object() || Id(*a, "id") != reply.userId))
        throw Failure{"invalid-response"};
    reply.avatarUrl = NeteaseImage(Text(*p, "avatarUrl", 2048));
    Memberships(reply, a != result.body.end() ? *a : Json{}, secrets, cancelled, exchange);
    MergeCookies(secrets, result.response, true);
    if (Cookie(secrets, "MUSIC_U").empty()) throw Failure{"expired"};
    secrets.erase("qrKey");
    secrets.erase("MUSIC_A");
    return reply;
}
}  // namespace

Json NeteaseCatalogRequest(const char* path, const Json& data, bool weapi, Json& secrets,
                          const Cancelled& cancelled, const Exchange& exchange) {
    try {
        return Request(path, Ordered(data), weapi, secrets, cancelled, exchange, kCatalogMaxResponse).body;
    } catch (const Failure& failure) {
        throw std::runtime_error(failure.error == "network" ? std::string("service") : failure.error);
    }
}

namespace {
// A failed attempt commits nothing but the deviceId, so a retry presents the
// same device instead of minting another.
void KeepDevice(Json& secrets, const Json& next) {
    if (secrets.is_object() && next.is_object() && next.contains("deviceId")) secrets["deviceId"] = next["deviceId"];
}
}  // namespace

Reply RunNetease(Operation operation, Json& secrets, const Cancelled& cancelled, const Exchange& exchange) {
    Json next = secrets.is_object() ? secrets : Json::object();
    try {
        CheckCancelled(cancelled);
        Reply reply;
        if (operation == Operation::Begin) {
            ClearAuth(next);
            const auto result = Request("/api/login/qrcode/unikey", Ordered{{"type", 3}},
                                        false, next, cancelled, exchange);
            if (const auto code = Code(result.body); code != 200) throw Failure{Refused(code)};
            const auto key = Text(result.body, "unikey", 512);
            if (key.empty()) throw Failure{"invalid-response"};
            MergeCookies(next, result.response, false);
            next["qrKey"] = key;
            reply = State("authorizing", "", "waiting");
            reply.qrText = "https://music.163.com/login?codekey=" + UrlEncode(key);
        } else if (operation == Operation::Poll) {
            const auto key = Text(next, "qrKey", 512);
            if (key.empty()) throw Failure{"expired"};
            const auto result = Request("/api/login/qrcode/client/login",
                Ordered{{"key", key}, {"type", 3}}, false, next, cancelled, exchange);
            switch (const auto code = Code(result.body)) {
            case 800: throw Failure{"expired"};
            case 801: reply = State("authorizing", "", "waiting"); break;
            case 802: reply = State("authorizing", "", "scanned"); break;
            case 803:
                // Only the confirmed QR response may supply a new authentication cookie.
                next.erase("MUSIC_U");
                MergeCookies(next, result.response, true);
                if (Cookie(next, "MUSIC_U").empty()) throw Failure{"invalid-response"};
                reply = Profile(next, false, cancelled, exchange);
                break;
            default: throw Failure{Refused(code)};
            }
        } else {
            if (Cookie(next, "MUSIC_U").empty()) return State("signed-out");
            reply = Profile(next, true, cancelled, exchange);
        }
        CheckCancelled(cancelled);
        secrets = std::move(next);
        return reply;
    } catch (const Failure& failure) {
        if (cancelled && cancelled()) {
            if (secrets.is_object()) secrets.erase("qrKey");
            return State("signed-out", "cancelled");
        }
        if (std::string_view(failure.error) == "expired") {
            ClearAuth(next); secrets = std::move(next);
            return State("expired", "expired");
        }
        KeepDevice(secrets, next);
        return State("error", failure.error);
    } catch (...) {
        if (cancelled && cancelled()) {
            if (secrets.is_object()) secrets.erase("qrKey");
            return State("signed-out", "cancelled");
        }
        KeepDevice(secrets, next);
        return State("error", "invalid-response");
    }
}
}  // namespace lume::music::auth
