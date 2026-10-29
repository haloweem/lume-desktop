// Protocol port of KuGouMusicApi 1.5.1 (Lines), module/login_qr_*, register_dev,
// user_detail, util/{crypto,helper,request}, and Folia electron/kugouApiBridge.cjs.
// Folia bridge reference is AGPL-3.0; retain its license in the distribution.
// MIT License
// Copyright (c) 2023 MakcRe
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
#include <cstdio>
#include <chrono>

namespace lume::music::auth {
namespace {
using Ordered = nlohmann::ordered_json;
using Params = std::map<std::string, std::string>;
constexpr char PublicKey[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDECi0Np2UR87scwrvTr72L6oO01rBb"
    "bBPriSDFPxr3Z5syug0O24QyQO8bg27+0+4kBzTBTBOZ/WWU0WryL1JSXRTXLgFVxtz"
    "IY41Pe7lPOgsfTCn5kZcvKhYKJesKnnJDNr5/abvTGf+rHG3YRwsCHcQ08/q6ifSioBs"
    "zvb3QiwIDAQAB\n-----END PUBLIC KEY-----";
struct Failure { const char* error; };
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
        i->is_number_integer() ? std::to_string(i->get<std::int64_t>()) : Text(j, key, 16);
    if (s.empty() || s.size() > 16 || s.front() == '0' ||
        !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
    // Historical user_detail converts userid to a JavaScript Number.
    if (std::stoull(s) > 9007199254740991ULL) return {};
    return s;
}
int Number(const Json& j, const char* key, int fallback = -1) {
    const auto i = j.find(key);
    if (i == j.end()) return fallback;
    if (!i->is_number_integer()) throw Failure{"invalid-response"};
    return i->get<int>();
}
std::string Upper(std::string s) {
    for (auto& c : s) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
    return s;
}
std::string Hex(std::string_view bytes, bool upper = false) {
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string result;
    for (unsigned char c : bytes) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
std::string Decimal(std::string_view hex) {
    std::string result = "0";
    for (char c : hex) {
        unsigned carry = c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0';
        for (auto i = result.rbegin(); i != result.rend(); ++i) {
            carry += static_cast<unsigned>(*i - '0') * 16;
            *i = static_cast<char>('0' + carry % 10); carry /= 10;
        }
        while (carry) { result.insert(result.begin(), static_cast<char>('0' + carry % 10)); carry /= 10; }
    }
    return result;
}
std::string Now() {
    return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
void Device(Json& secrets) {
    if (Text(secrets, "KUGOU_API_GUID", 32).size() != 32 ||
        Text(secrets, "KUGOU_API_MID", 39).empty()) {
        auto guid = Upper(RandomHex(16));
        guid[12] = '4';
        const auto nibble = static_cast<unsigned>(std::stoul(guid.substr(16, 1), nullptr, 16));
        guid[16] = "89AB"[nibble & 3];
        secrets["KUGOU_API_PLATFORM"] = "lite";
        secrets["KUGOU_API_GUID"] = guid;
        secrets["KUGOU_API_MID"] = Decimal(HashHex(guid));
        secrets["KUGOU_API_DEV"] = Upper(RandomHex(5));
        const auto bytes = Upper(RandomHex(6));
        std::string mac;
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            if (i) mac += ':';
            mac += bytes.substr(i, 2);
        }
        secrets["KUGOU_API_MAC"] = mac;
        secrets["KUGOU_API_WEBGL"] = Decimal(RandomHex(8));
        secrets.erase("dfid");
        secrets.erase("dfidTriedAt");
    }
}
void ClearAuth(Json& secrets) {
    secrets.erase("token"); secrets.erase("userid"); secrets.erase("qrKey");
}
Reply State(const char* status, const char* error = "", const char* phase = "") {
    Reply reply;
    reply.status = status; reply.error = error; reply.phase = phase;
    return reply;
}
HttpResponse Request(const char* url, Params params, std::string body, bool web, const Json& secrets,
                     const Cancelled& cancelled, const Exchange& exchange, std::size_t maxResponse = 256 * 1024) {
    CheckCancelled(cancelled);
    Params defaults = {{"dfid", Text(secrets, "dfid", 256).empty() ? "-" : Text(secrets, "dfid", 256)},
        {"mid", Text(secrets, "KUGOU_API_MID", 39)}, {"uuid", "-"},
        {"appid", "3116"}, {"clientver", "11440"}, {"clienttime", Now()}};
    if (!Text(secrets, "token").empty()) defaults["token"] = Text(secrets, "token");
    if (!Id(secrets, "userid").empty()) defaults["userid"] = Id(secrets, "userid");
    for (const auto& p : params) defaults[p.first] = p.second;
    const std::string salt = web ? "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt" : "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA";
    std::string signedText = salt;
    for (const auto& p : defaults) signedText += p.first + "=" + p.second;
    defaults["signature"] = HashHex(signedText + (web ? "" : body) + salt);
    HttpRequest request;
    request.maxResponse = maxResponse;
    request.provider = Provider::Kugou;
    request.method = body.empty() ? "GET" : "POST";
    request.url = url;
    char separator = '?';
    for (const auto& p : defaults) {
        request.url += separator + p.first + "=" + UrlEncode(p.second); separator = '&';
    }
    request.headers = {{"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
        {"dfid", defaults["dfid"]}, {"mid", defaults["mid"]}, {"clienttime", defaults["clienttime"]},
        {"kg-rc", "1"}, {"kg-thash", "5d816a0"}, {"kg-rec", "1"}, {"kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"}};
    if (!body.empty()) request.headers["Content-Type"] =
        body.front() == '{' ? "application/json" : "application/x-www-form-urlencoded";
    if (std::string_view(url) == "https://gateway.kugou.com/v3/get_my_info")
        request.headers["x-router"] = "usercenter.kugou.com";
    request.body = std::move(body);
    HttpResponse response;
    try { response = exchange(request, cancelled); }
    catch (...) { CheckCancelled(cancelled); throw Failure{"network"}; }
    CheckCancelled(cancelled);
    if (response.status == 401 || response.status == 403) throw Failure{"expired"};
    if (response.status == 429) throw Failure{"rate-limited"};
    if (response.status < 200 || response.status >= 300) throw Failure{"service"};
    if (response.body.empty() || response.body.size() > request.maxResponse) throw Failure{"invalid-response"};
    return response;
}
Json Parse(const std::string& text) {
    auto j = Json::parse(text, nullptr, false);
    if (!j.is_object()) throw Failure{"invalid-response"};
    return j;
}
bool Verification(const Json& j) {
    return Number(j, "errcode") == 20028 || Number(j, "error_code") == 20028 ||
        Text(j, "error").find(u8"\u672c\u6b21\u8bf7\u6c42\u9700\u8981\u9a8c\u8bc1") != std::string::npos ||
        Text(j, "error_msg").find(u8"\u672c\u6b21\u8bf7\u6c42\u9700\u8981\u9a8c\u8bc1") != std::string::npos ||
        Text(j, "msg").find(u8"\u672c\u6b21\u8bf7\u6c42\u9700\u8981\u9a8c\u8bc1") != std::string::npos;
}
void Success(const Json& j) {
    if (Number(j, "status") != 1 || Number(j, "error_code", 0) != 0 ||
        Number(j, "errcode", 0) != 0) throw Failure{"service"};
}
void Register(Json& secrets, const Cancelled& cancelled, const Exchange& exchange) {
    Ordered data = {
        {"availableRamSize", 4983533568LL}, {"availableRomSize", 48114719},
        {"availableSDSize", 48114717}, {"basebandVer", ""}, {"batteryLevel", 100},
        {"batteryStatus", 3}, {"brand", "Redmi"}, {"buildSerial", "unknown"},
        {"device", "marble"}, {"imei", Text(secrets, "KUGOU_API_GUID")},
        {"imsi", ""}, {"manufacturer", "Xiaomi"}, {"uuid", Text(secrets, "KUGOU_API_GUID")}
    };
    for (const auto* sensor : {"accelerometer", "gravity", "gyroscope", "light", "magnetic",
                              "orientation", "pressure", "step_counter", "temperature"}) {
        data[sensor] = false; data[std::string(sensor) + "Value"] = "";
    }
    const auto key = RandomHex(3);
    const auto digest = HashHex(key);
    const auto body = Base64(Aes(data.dump(), digest.substr(0, 16), digest.substr(16, 16)));
    Ordered credentials = {{"aes", key}, {"uid", 0}, {"token", Text(secrets, "token")}};
    if (!Id(secrets, "userid").empty()) credentials["uid"] = Id(secrets, "userid");
    const auto p = Hex(RsaEncrypt(credentials.dump(), PublicKey));
    const auto response = Request("https://userservice.kugou.com/risk/v2/r_register_dev",
        {{"part", "1"}, {"platid", "1"}, {"p", p}}, body, false, secrets, cancelled, exchange);
    // register_dev requests arraybuffer: the wire response is raw AES-CBC ciphertext.
    const auto result = Parse(Aes(response.body, digest.substr(0, 16), digest.substr(16, 16), false));
    Success(result);
    const auto dataIt = result.find("data");
    if (dataIt == result.end() || !dataIt->is_object()) throw Failure{"invalid-response"};
    const auto dfid = Text(*dataIt, "dfid", 256);
    if (dfid.empty() || dfid == "-") throw Failure{"invalid-response"};
    secrets["dfid"] = dfid;
}
// The risk service intermittently answers with an undecryptable body (live
// 2026-10-15: about one registration in three) or with status 1 and an empty
// data object (live 2026-10-22: about one in two); a fresh key fixes the former.
// When every try comes back without a dfid the call goes on with "-", as
// KuGouMusicApi's defaults do: the QR endpoints answer without one, and a later
// "needs verification" reply still forces a registration.
constexpr int RegisterAttempts = 2;
constexpr std::int64_t RegisterPauseSeconds = 30;
void RegisterWithRetry(Json& secrets, const Cancelled& cancelled, const Exchange& exchange, bool force) {
    // The service rate-limits new devices (live 2026-10-22: back-to-back tries all
    // came back empty, the same device got its dfid about 20s later), so a failed
    // round is not repeated on every QR poll but after a short pause.
    if (!force && Id(secrets, "dfidTriedAt").size() &&
        std::stoll(Now()) - std::stoll(Id(secrets, "dfidTriedAt")) < RegisterPauseSeconds) return;
    for (int attempt = 0; attempt < RegisterAttempts; ++attempt) {
        CheckCancelled(cancelled);
        try { Register(secrets, cancelled, exchange); secrets.erase("dfidTriedAt"); return; }
        catch (const Failure& failure) { if (std::string_view(failure.error) != "invalid-response") throw; }
        catch (const std::runtime_error&) {}  // undecryptable reply (music crypto error)
    }
    secrets["dfidTriedAt"] = Now();
}
Json Call(const char* url, const Params& params, const std::string& body, bool web, Json& secrets,
          const Cancelled& cancelled, const Exchange& exchange, std::size_t maxResponse = 256 * 1024) {
    if (Text(secrets, "dfid", 256).empty() || Text(secrets, "dfid", 256) == "-")
        RegisterWithRetry(secrets, cancelled, exchange, false);
    auto result = Parse(Request(url, params, body, web, secrets, cancelled, exchange, maxResponse).body);
    if (Verification(result)) {
        secrets.erase("dfid");
        RegisterWithRetry(secrets, cancelled, exchange, true);
        result = Parse(Request(url, params, body, web, secrets, cancelled, exchange, maxResponse).body);
    }
    return result;
}
std::string KugouImage(std::string url) {
    if (url.rfind("http://", 0) == 0) url = "https://" + url.substr(7);
    return PublicImageUrl(url) ? url : std::string{};
}
std::int64_t Integer(const Json& j, const char* key) {
    const auto i = j.find(key);
    if (i == j.end() || i->is_null()) return 0;
    if (i->is_number_integer()) return i->get<std::int64_t>();
    if (i->is_string()) {
        const auto& text = i->get_ref<const std::string&>();
        if (!text.empty() && text.size() < 19 && std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
            return std::stoll(text);
    }
    return 0;
}
// "YYYY-MM-DD HH:MM:SS" (China time) to Unix milliseconds; 0 if absent.
std::int64_t KugouTime(const Json& j, const char* key) {
    const auto text = Text(j, key, 32);
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3 || y < 2000) return 0;
    const auto days = [](int year, int month, int day) {
        year -= month <= 2;
        const int era = year / 400, yoe = year - era * 400;
        const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        return static_cast<std::int64_t>(era) * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    };
    return ((days(y, mo, d) * 86400 + h * 3600 + mi * 60 + s) - 8 * 3600) * 1000;
}

Reply Profile(Json& secrets, const Cancelled& cancelled, const Exchange& exchange) {
    const auto token = Text(secrets, "token");
    const auto userid = Id(secrets, "userid");
    if (token.empty() || userid.empty()) throw Failure{"expired"};
    const auto time = std::stoll(Now());
    std::string plain = Ordered{{"token", token}, {"clienttime", time}}.dump();
    if (plain.size() > 128) throw Failure{"invalid-response"};
    // Unlike NetEase, Kugou's RSA_NO_PADDING input is right-padded with zeroes.
    plain.resize(128, '\0');
    const auto pk = Hex(RsaEncrypt(plain, PublicKey, true), true);
    const auto body = Ordered{{"visit_time", time}, {"usertype", 1}, {"p", pk},
        {"userid", std::stoull(userid)}}.dump();
    const auto result = Call("https://gateway.kugou.com/v3/get_my_info", {{"plat", "1"}},
                              body, false, secrets, cancelled, exchange);
    Success(result);
    const auto data = result.find("data");
    if (data == result.end() || !data->is_object()) throw Failure{"invalid-response"};
    const auto returnedId = data->contains("userid") ? Id(*data, "userid") :
        data->contains("user_id") ? Id(*data, "user_id") : userid;
    Reply reply = State("signed-in");
    reply.userId = userid;
    reply.displayName = Text(*data, "nickname", 256);
    if (reply.displayName.empty() || returnedId != userid) throw Failure{"invalid-response"};
    reply.avatarUrl = KugouImage(Text(*data, "pic", 2048));
    // vip_type/m_type: 豪华VIP and 音乐包; su_vip_*: 超级VIP. A live account
    // without membership returned zeros and empty times (2026-10-11).
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto superEnd = KugouTime(*data, "su_vip_end_time");
    const bool superActive = superEnd > now;
    const bool vipActive = superActive || Integer(*data, "vip_type") > 0 || Integer(*data, "m_type") > 0;
    std::string label = superActive ? u8"\u8d85\u7ea7VIP" : Integer(*data, "vip_type") > 0 ? u8"\u8c6a\u534eVIP" :
        Integer(*data, "m_type") > 0 ? u8"\u97f3\u4e50\u5305" : u8"\u9177\u72d7VIP";
    reply.memberships = {Membership{"kugou-vip", label, Integer(*data, "svip_level"), vipActive, {}, superActive ? superEnd : 0}};
    secrets.erase("qrKey");
    return reply;
}
}  // namespace

Json KugouCatalogRequest(const char* path, Params params, std::string body, const char* router,
                        Json& secrets, const Cancelled& cancelled, const Exchange& exchange) {
    try {
        Device(secrets);
        if (std::string_view(path) == "/v5/url") {
            // KuGouMusicApi util/helper.signKey, lite client; signature is still required.
            params["key"] = HashHex(params.at("hash") + "185672dd44712f60bb1736df5a377e82" +
                "3116" + Text(secrets, "KUGOU_API_MID", 39) +
                (Id(secrets, "userid").empty() ? "0" : Id(secrets, "userid")));
        }
        const std::string url = std::string("https://gateway.kugou.com") + path;
        return Call(url.c_str(), params, body, false, secrets, cancelled,
            [&](const HttpRequest& original, const Cancelled& stop) {
                auto request = original;
                if (router && request.url.compare(0, url.size(), url) == 0)
                    request.headers["x-router"] = router;
                return exchange(request, stop);
            }, kCatalogMaxResponse);
    } catch (const Failure& failure) {
        throw std::runtime_error(std::string_view(failure.error) == "network" ? "service" : failure.error);
    }
}

namespace {
// A failed attempt commits nothing but the device, which may already be
// registered; a retry must present the same one instead of minting another.
void KeepDevice(Json& secrets, const Json& next) {
    if (!secrets.is_object() || !next.is_object()) return;
    for (const auto* key : {"KUGOU_API_PLATFORM", "KUGOU_API_GUID", "KUGOU_API_MID", "KUGOU_API_DEV",
                            "KUGOU_API_MAC", "KUGOU_API_WEBGL", "dfid", "dfidTriedAt"})
        if (next.contains(key)) secrets[key] = next[key];
}
}  // namespace

Reply RunKugou(Operation operation, Json& secrets, const Cancelled& cancelled, const Exchange& exchange) {
    Json next = secrets.is_object() ? secrets : Json::object();
    try {
        CheckCancelled(cancelled);
        if (operation == Operation::Refresh && (Text(next, "token").empty() || Id(next, "userid").empty()))
            return State("signed-out");
        Device(next);
        Reply reply;
        if (operation == Operation::Begin) {
            ClearAuth(next);
            const auto result = Call("https://login-user.kugou.com/v2/qrcode",
                {{"appid", "1001"}, {"type", "1"}, {"plat", "4"}, {"srcappid", "2919"},
                 {"qrcode_txt", "https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=3116&"}},
                 "", true, next, cancelled, exchange);
            Success(result);
            const auto data = result.find("data");
            if (data == result.end() || !data->is_object()) throw Failure{"invalid-response"};
            const auto key = Text(*data, "qrcode", 512);
            if (key.empty()) throw Failure{"invalid-response"};
            next["qrKey"] = key;
            reply = State("authorizing", "", "waiting");
            reply.qrText = "https://h5.kugou.com/apps/loginQRCode/html/index.html?qrcode=" + UrlEncode(key);
        } else if (operation == Operation::Poll) {
            const auto key = Text(next, "qrKey", 512);
            if (key.empty()) throw Failure{"expired"};
            const auto result = Call("https://login-user.kugou.com/v2/get_userinfo_qrcode",
                {{"appid", "3116"}, {"plat", "4"}, {"srcappid", "2919"}, {"qrcode", key}},
                "", true, next, cancelled, exchange);
            Success(result);
            const auto data = result.find("data");
            if (data == result.end() || !data->is_object()) throw Failure{"invalid-response"};
            switch (Number(*data, "status")) {
            case 0: throw Failure{"expired"};
            case 1: reply = State("authorizing", "", "waiting"); break;
            case 2:
            case 3: reply = State("authorizing", "", "scanned"); break;
            case 4: {
                const auto token = Text(*data, "token");
                const auto userid = Id(*data, "userid");
                if (token.empty() || userid.empty()) throw Failure{"invalid-response"};
                next["token"] = token; next["userid"] = userid;
                reply = Profile(next, cancelled, exchange);
                break;
            }
            default: throw Failure{"invalid-response"};
            }
        } else reply = Profile(next, cancelled, exchange);
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
