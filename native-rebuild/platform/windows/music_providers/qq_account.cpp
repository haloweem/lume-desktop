#include "platform/windows/music_provider_http.h"
#include "platform/windows/music_providers/catalog_requests.h"

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace lume::music::auth {
namespace {

// Protocol adapted from Folia Major's electron/qqMusicAuth.cjs and
// electron/qqMusicDevice.cjs, copyright Folia Major contributors (chthollyphile).
// SPDX-License-Identifier: AGPL-3.0-only
constexpr char kQrShow[] = "https://ssl.ptlogin2.qq.com/ptqrshow";
constexpr char kQrPoll[] = "https://ssl.ptlogin2.qq.com/ptqrlogin";
constexpr char kCheckSig[] = "https://ssl.ptlogin2.graph.qq.com/check_sig";
constexpr char kAuthorize[] = "https://graph.qq.com/oauth2.0/authorize";
constexpr char kMusicApi[] = "https://u.y.qq.com/cgi-bin/musicu.fcg";
constexpr char kQimei[] = "https://api.tencentmusic.com/tme/trpc/proxy";
constexpr char kQimeiPublicKey[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDEIxgwoutfwoJxcGQeedgP7FG9qaIu"
    "S0qzfR8gWkrkTZKM2iWHn2ajQpBRZjMSoSf6+KJGvar2ORhBfpDXyVtZCKpqLQ+FLkpnc"
    "ClKVIrBwv6PHyUvuCb0rIarmgDnzkfQAqVufEtR64iazGDKatvJ9y6B9NMbHddGSAUmRT"
    "CrHQIDAQAB\n"
    "-----END PUBLIC KEY-----";
constexpr char kQimeiSecret[] = "ZdJqM15EeO2zWc08";
constexpr char kQimeiAppKey[] = "0AND0HD6FE4HY80F";
constexpr char kQimeiChannel[] = "10003505";
constexpr char kQimeiPackage[] = "com.tencent.qqmusic";

using Json = nlohmann::json;

std::int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Hash33(std::string_view value, std::uint32_t initial = 0) {
    std::uint32_t hash = initial;
    for (const unsigned char character : value) {
        hash += (hash << 5) + character;
    }
    return std::to_string(hash & 0x7fffffff);
}

std::string Query(std::initializer_list<std::pair<std::string_view, std::string>> values) {
    std::string result;
    bool first = true;
    for (const auto& [key, value] : values) {
        result += first ? '?' : '&';
        first = false;
        result += UrlEncode(key);
        result += '=';
        result += UrlEncode(value);
    }
    return result;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool IsAllowedUrl(std::string_view url) {
    for (const auto* endpoint : {kQrShow, kQrPoll, kCheckSig, kAuthorize, kMusicApi, kQimei}) {
        if (url == endpoint || StartsWith(url, std::string(endpoint) + "?")) return true;
    }
    return false;
}

struct Failure : std::runtime_error {
    explicit Failure(std::string category) : std::runtime_error(category) {}
};
struct InvalidCredential {};

HttpResponse Send(const Exchange& exchange, HttpRequest request, const Cancelled& cancelled) {
    CheckCancelled(cancelled);
    if (!exchange) throw std::runtime_error("QQ transport unavailable");
    if (request.provider != Provider::QQ || !IsAllowedUrl(request.url)) {
        throw std::runtime_error("QQ request target rejected");
    }
    HttpResponse response;
    try {
        response = exchange(request, cancelled);
    } catch (...) {
        CheckCancelled(cancelled);
        throw Failure("network");
    }
    CheckCancelled(cancelled);
    // Folia's retry-later case: the platform asks this client to slow down.
    if (response.status == 429) throw Failure("rate-limited");
    if (response.body.size() > request.maxResponse || response.location.size() > 8192 ||
        response.cookies.size() > 64) throw Failure("invalid-response");
    std::size_t size = response.location.size();
    for (const auto& cookie : response.cookies) size += cookie.size();
    if (size > 32768) throw Failure("invalid-response");
    return response;
}

void RequireOk(const HttpResponse& response, std::string_view operation) {
    if (response.status < 200 || response.status >= 300) {
        throw Failure("service");
    }
    (void)operation;
}

void RequireResponseWithRedirect(const HttpResponse& response, std::string_view operation) {
    if (response.status < 200 || response.status >= 400) {
        throw Failure("service");
    }
    (void)operation;
}

bool Token(std::string_view value) {
    return !value.empty() && value.size() <= 4096 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return c > 32 && c < 127 && c != ';' && c != ',' && c != '"' && c != '\\';
        });
}

std::string CookieValue(const std::vector<std::string>& cookies, std::string_view name) {
    const std::string prefix = std::string(name) + '=';
    std::string result;
    for (const auto& header : cookies) {
        const auto semicolon = header.find(';');
        const auto pair = header.substr(0, semicolon);
        if (pair.size() >= prefix.size()) {
            bool equal = true;
            for (std::size_t i = 0; i < prefix.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(pair[i])) !=
                    std::tolower(static_cast<unsigned char>(prefix[i]))) {
                    equal = false;
                    break;
                }
            }
            if (equal) {
                auto value = pair.substr(prefix.size());
                if (!value.empty() && !Token(value)) throw Failure("invalid-response");
                // QQ may return a valid session cookie followed by an expired
                // duplicate while clearing an older login state.
                if (!value.empty() || result.empty()) result = std::move(value);
            }
        }
    }
    return result;
}

struct Ptui {
    int code = -1;
    std::string callback;
};

Ptui ParsePtui(std::string_view body) {
    if (body.size() > 16384) throw Failure("invalid-response");
    const auto begin = body.find("ptuiCB(");
    if (begin == std::string_view::npos) throw std::runtime_error("invalid QQ QR status");
    std::vector<std::string> args;
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw Failure("invalid-response");
    };
    std::size_t i = begin + 7;
    while (args.size() < 3) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
        if (i == body.size() || body[i++] != '\'') throw Failure("invalid-response");
        std::string value;
        bool complete = false;
        while (i < body.size()) {
            char c = body[i++];
            if (c == '\'') { complete = true; break; }
            if (c == '\\') {
                if (i == body.size()) throw Failure("invalid-response");
                c = body[i++];
                if (c == 'x' || c == 'u') {
                    const auto digits = c == 'x' ? 2u : 4u;
                    if (body.size() - i < digits) throw Failure("invalid-response");
                    unsigned code = 0;
                    for (unsigned part = 0; part < digits; ++part) code = code * 16 + hex(body[i++]);
                    if (code < 32 || code >= 127) throw Failure("invalid-response");
                    c = static_cast<char>(code);
                } else if (c != '/' && c != '\\' && c != '\'' && c != '"') {
                    throw Failure("invalid-response");
                }
            }
            if (static_cast<unsigned char>(c) < 32) throw Failure("invalid-response");
            value.push_back(c);
        }
        if (!complete) throw Failure("invalid-response");
        args.push_back(std::move(value));
        if (args.size() < 3) {
            while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
            if (i == body.size() || body[i++] != ',') throw Failure("invalid-response");
        }
    }
    if (args.size() < 3 || args[0].empty() || args[0].size() > 3 ||
        args[0].find_first_not_of("0123456789") != std::string::npos) {
        throw Failure("invalid-response");
    }
    return {std::stoi(args[0]), args[2]};
}

bool IsDigits(std::string_view value) {
    return !value.empty() && value.size() <= 18 &&
        value.find_first_not_of('0') != std::string_view::npos &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c >= '0' && c <= '9';
    });
}

std::string Text(const Json& object, const char* key) {
    const auto field = object.find(key);
    return field != object.end() && field->is_string() ? field->get<std::string>() : std::string{};
}

std::string Scalar(const Json& object, const char* key) {
    const auto field = object.find(key);
    if (field == object.end()) return {};
    if (field->is_number_integer()) return field->dump();
    return Text(object, key);
}

std::int64_t Number(const Json& object, const char* key) {
    const auto value = Scalar(object, key);
    if (value.empty()) return 0;
    if (value.size() > 15 || value.find_first_not_of("0123456789") != std::string::npos)
        throw Failure("invalid-response");
    return std::stoll(value);
}

// QQ tier artwork lives at .../lv-icon/<ver>/<scale>/[d-](s)vip<N>.png, where
// the d- prefix is the dimmed (expired) variant.
std::string QqIconFile(const std::string& url) {
    const auto slash = url.find_last_of('/');
    auto file = slash == std::string::npos ? url : url.substr(slash + 1);
    file = file.substr(0, file.find_first_of("?#"));
    std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return file.rfind("d-", 0) == 0 ? file.substr(2) : file;
}
bool QqSuperIconUrl(const std::string& url) { return QqIconFile(url).rfind("svip", 0) == 0; }
// Standard tier artwork: .../lv-icon/v<N>/<scale>/[d-](s)vip<N>.png, not other
// products sharing the folder (e.g. /lv-icon/v10/audio/ for audiobooks).
bool QqTierIconUrl(const std::string& url) {
    const auto at = url.find("/lv-icon/v");
    if (at == std::string::npos) return false;
    const auto slashes = std::count(url.begin() + static_cast<std::ptrdiff_t>(at) + 9, url.end(), '/');
    return slashes == 2;
}
bool QqGreenIconUrl(const std::string& url) { return QqIconFile(url).rfind("vip", 0) == 0; }
std::string QqIconForState(const std::string& url, bool active) {
    if (url.find("/lv-icon/") == std::string::npos || (!QqSuperIconUrl(url) && !QqGreenIconUrl(url))) return url;
    const auto slash = url.find_last_of('/');
    if (slash == std::string::npos) return url;
    const bool dimmed = url.compare(slash + 1, 2, "d-") == 0;
    if (active && dimmed) return url.substr(0, slash + 1) + url.substr(slash + 3);
    if (!active && !dimmed) return url.substr(0, slash + 1) + "d-" + url.substr(slash + 1);
    return url;
}

// "YYYY-MM-DD HH:MM:SS" in China time to Unix milliseconds; 0 if absent.
std::int64_t QqTime(const std::string& text) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (text.size() > 32 || std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3 || y < 2000)
        return 0;
    const auto days = [](int year, int month, int day) {
        year -= month <= 2;
        const int era = year / 400, yoe = year - era * 400;
        const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        return static_cast<std::int64_t>(era) * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    };
    return ((days(y, mo, d) * 86400 + h * 3600 + mi * 60 + s) - 8 * 3600) * 1000;
}

std::string ProfileText(const Json& object, std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        auto value = Text(object, key);
        if (!value.empty()) return value;
    }
    return {};
}

std::int64_t ProfileNumber(const Json& object, std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        const auto field = object.find(key);
        if (field == object.end() || field->is_null()) continue;
        if (field->is_boolean()) return field->get<bool>() ? 1 : 0;
        if (!field->is_string() && !field->is_number_integer()) throw Failure("invalid-response");
        return Number(object, key);
    }
    return 0;
}

std::string ProfileImage(std::string url) {
    if (StartsWith(url, "http://")) url.replace(0, 7, "https://");
    return PublicImageUrl(url) ? url : std::string{};
}

void CheckCode(const Json& object, bool required = false) {
    if (!object.is_object()) throw Failure("invalid-response");
    if (!object.contains("code")) {
        if (required) throw Failure("invalid-response");
        return;
    }
    const auto code = Scalar(object, "code");
    if (code == "0") return;
    if (code == "1000" || code == "104009" || code == "104013" ||
        code == "104400" || code == "104401") throw InvalidCredential{};
    throw Failure(code.empty() ? "invalid-response" : "service");
}

Json ResultData(const Json& response, const char* key) {
    CheckCode(response);
    const auto request = response.at(key);
    CheckCode(request, true);
    const auto data = request.at("data");
    CheckCode(data);
    return data;
}

bool Fresh(const Json& object, const char* field) {
    const auto age = NowSeconds() * 1000 - Number(object, field);
    return age >= 0 && age < 24 * 60 * 60 * 1000;
}

bool Expired(const Json& credential) {
    const auto now = NowSeconds() * 1000;
    auto absolute = Number(credential, "expiresAt");
    if (absolute >= 1'000'000'000 && absolute < 1'000'000'000'000) absolute *= 1000;
    if (absolute >= 1'000'000'000'000 && absolute <= now) return true;
    const auto lifetime = Number(credential, "keyExpiresIn");
    const auto created = Number(credential, "musickeyCreateTime");
    const auto updated = Number(credential, "updatedAt");
    const auto start = created > 0 ? created * 1000 : updated;
    return lifetime > 0 && start > 0 && start + lifetime * 1000 <= now;
}

std::string Decode(std::string_view encoded) {
    std::string value;
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw Failure("invalid-response");
    };
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        char c = encoded[i];
        if (c == '%') {
            if (i + 2 >= encoded.size()) throw Failure("invalid-response");
            c = static_cast<char>(hex(encoded[i + 1]) * 16 + hex(encoded[i + 2]));
            i += 2;
        } else if (c == '+') c = ' ';
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) >= 127)
            throw Failure("invalid-response");
        value += c;
    }
    return value;
}

std::map<std::string, std::string> Parameters(std::string_view url, std::string_view endpoint) {
    if (url.size() > 8192 || !StartsWith(url, std::string(endpoint) + "?") ||
        url.find_first_of("#\\\r\n") != std::string_view::npos) throw Failure("invalid-response");
    url.remove_prefix(endpoint.size() + 1);
    std::map<std::string, std::string> params;
    while (!url.empty()) {
        const auto end = url.find('&');
        const auto pair = url.substr(0, end);
        const auto equal = pair.find('=');
        if (equal == std::string_view::npos ||
            !params.emplace(Decode(pair.substr(0, equal)), Decode(pair.substr(equal + 1))).second)
            throw Failure("invalid-response");
        if (end == std::string_view::npos) break;
        url.remove_prefix(end + 1);
    }
    return params;
}

std::string Uuid() {
    auto hex = RandomHex(16);
    hex[12] = '4';
    hex[16] = "89ab"[std::stoul(hex.substr(16, 1), nullptr, 16) % 4];
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) +
        "-" + hex.substr(16, 4) + "-" + hex.substr(20);
}

std::string RandomInt(std::int64_t min, std::int64_t max) {
    const auto value = std::stoull(RandomHex(4), nullptr, 16);
    return std::to_string(min + static_cast<std::int64_t>(value % static_cast<std::uint64_t>(max - min + 1)));
}

std::string CreateImei() {
    std::string digits;
    for (int i = 0; i < 14; ++i) digits += RandomInt(0, 9);
    int sum = 0;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        int value = digits[i] - '0';
        if (i % 2 == 1) {
            value *= 2;
            if (value > 9) value -= 9;
        }
        sum += value;
    }
    digits += static_cast<char>('0' + ((10 - sum % 10) % 10));
    return digits;
}

Json CreateDevice() {
    const auto build = RandomInt(1'000'000, 9'999'999);
    return {
        {"display", "QMAPI." + RandomInt(100'000, 999'999) + ".001"},
        {"product", "iarim"}, {"device", "sagit"}, {"board", "eomam"},
        {"model", "MI 6"},
        {"fingerprint", "xiaomi/iarim/sagit:10/eomam.200122.001/" + build + ":user/release-keys"},
        {"bootId", RandomHex(4) + "-" + RandomHex(2) + "-" + RandomHex(2) + "-" +
            RandomHex(2) + "-" + RandomHex(6)},
        {"procVersion", "Linux 5.4.0-54-generic-" + RandomHex(4) +
            " (android-build@google.com)"},
        {"imei", CreateImei()}, {"brand", "Xiaomi"},
        {"version", {{"release", "10"}, {"sdk", 29}}},
        {"androidId", RandomHex(8)}, {"openUdid", RandomHex(16)},
        {"qimei16", ""}, {"qimei36", ""}, {"qimeiUpdatedAt", 0},
        {"sessionUid", ""}, {"sessionSid", ""}, {"sessionVkey", ""},
        {"sessionUpdatedAt", 0},
    };
}

Json& Device(Json& secrets) {
    if (!secrets.contains("device") || !secrets["device"].is_object() ||
        !secrets["device"].value("openUdid", "").size()) {
        secrets["device"] = CreateDevice();
    }
    return secrets["device"];
}

std::string UtcDate(std::int64_t seconds, const char* format) {
    const auto time = static_cast<std::time_t>(seconds);
    std::tm utc{};
    if (gmtime_s(&utc, &time) != 0) throw Failure("invalid-response");
    std::ostringstream text;
    text << std::put_time(&utc, format);
    return text.str();
}

std::string BeaconId(std::int64_t now) {
    const auto date = UtcDate(now, "%Y-%m-01");
    const auto rand1 = RandomInt(100'000, 999'999);
    const auto rand2 = RandomInt(100'000'000, 999'999'999);
    const std::vector<int> dated{1, 2, 13, 14, 17, 18, 21, 22, 25, 26, 29, 30, 33, 34, 37, 38};
    std::string result;
    for (int index = 1; index <= 40; ++index) {
        if (std::find(dated.begin(), dated.end(), index) != dated.end()) {
            result += "k" + std::to_string(index) + ":" + date + rand1 + "." + rand2;
        } else if (index == 3) {
            result += "k3:0000000000000000";
        } else if (index == 4) {
            auto hex = RandomHex(8);
            std::replace(hex.begin(), hex.end(), '0', '1');
            result += "k4:" + hex;
        } else {
            result += "k" + std::to_string(index) + ":" + RandomInt(0, 9999);
        }
        result += ';';
    }
    return result;
}

Json QimeiPayload(const Json& device, std::int64_t now) {
    const auto uptime = UtcDate(now - std::stoll(RandomInt(0, 14'400)), "%Y-%m-%d %H:%M:%S");
    const auto reserved = Json{
        {"harmony", "0"}, {"clone", "0"}, {"containe", ""},
        {"oz", "UhYmelwouA+V2nPWbOvLTgN2/m8jwGB+yUB5v9tysQg="},
        {"oo", "Xecjt+9S1+f8Pz2VLSxgpw=="}, {"kelong", "0"},
        {"uptimes", uptime}, {"multiUser", "0"}, {"bod", device.value("brand", "")},
        {"dv", device.value("device", "")}, {"firstLevel", ""},
        {"manufact", device.value("brand", "")}, {"name", device.value("model", "")},
        {"host", "se.infra"}, {"kernel", device.value("procVersion", "")},
    };
    return {
        {"androidId", device.value("androidId", "")}, {"platformId", 1},
        {"appKey", kQimeiAppKey}, {"appVersion", "14.9.0.8"},
        {"beaconIdSrc", BeaconId(now)}, {"brand", device.value("brand", "")},
        {"channelId", kQimeiChannel}, {"cid", ""}, {"imei", device.value("imei", "")},
        {"imsi", ""}, {"mac", ""}, {"model", device.value("model", "")},
        {"networkType", "unknown"}, {"oaid", ""}, {"osVersion", "Android 10,level 29"},
        {"qimei", ""}, {"qimei36", ""}, {"sdkVersion", "1.2.13.6"},
        {"targetSdkVersion", "33"}, {"audit", ""}, {"userId", "{}"},
        {"packageId", kQimeiPackage}, {"deviceType", "Phone"}, {"sdkName", ""},
        {"reserved", reserved.dump()},
    };
}

HttpRequest MakeJsonRequest(std::string body, std::string_view cookie = {}) {
    HttpRequest request;
    request.provider = Provider::QQ;
    request.method = "POST";
    request.url = kMusicApi;
    request.headers = {
        {"content-type", "application/json"},
        {"referer", "https://y.qq.com/"},
        {"user-agent", "QQMusic 14090008(android 10)"},
    };
    if (!cookie.empty()) request.headers["cookie"] = std::string(cookie);
    request.body = std::move(body);
    request.maxResponse = 512 * 1024;
    return request;
}

std::string CredentialCookie(const Json& credential) {
    const auto uin = credential.value("uin", "");
    const auto key = credential.value("musicKey", "");
    return "uin=" + uin + "; qqmusic_uin=" + uin + "; qm_keyst=" + key + "; qqmusic_key=" + key;
}

Json BuildComm(const Json& device, const Json& qimei, int loginType = 2) {
    Json comm = {
        {"ct", 11}, {"cv", 14090008}, {"v", 14090008}, {"chid", "10003505"},
        {"tmeAppID", "qqmusic"}, {"tmeLoginType", loginType},
        {"QIMEI", qimei.value("q16", "")}, {"QIMEI36", qimei.value("q36", "")},
        {"OpenUDID", device.value("openUdid", "")}, {"udid", device.value("openUdid", "")},
        {"OpenUDID2", device.value("openUdid", "")},
        {"aid", device.value("androidId", "")}, {"os_ver", "10"},
        {"phonetype", device.value("model", "MI 6")}, {"devicelevel", "29"},
        {"newdevicelevel", "29"}, {"rom", device.value("fingerprint", "")},
    };
    if (!device.value("sessionUid", "").empty()) comm["uid"] = device.value("sessionUid", "");
    if (!device.value("sessionSid", "").empty()) comm["sid"] = device.value("sessionSid", "");
    return comm;
}

Json ParseJsonBody(const HttpResponse& response) {
    try {
        return Json::parse(response.body);
    } catch (...) {
        throw std::runtime_error("QQ returned invalid JSON");
    }
}

bool ValidCredential(const Json& credential) {
    return credential.is_object()
        && IsDigits(Text(credential, "uin"))
        && Token(Text(credential, "musicKey"));
}

Json NormalizeCredential(const Json& data) {
    auto uin = Scalar(data, "str_musicid");
    if (uin.empty()) uin = Scalar(data, "musicid");
    return {
        {"uin", uin}, {"musicKey", Text(data, "musickey")},
        {"loginType", Number(data, "loginType") ? Number(data, "loginType") : 2},
        {"accessToken", Text(data, "access_token")}, {"refreshToken", Text(data, "refresh_token")},
        {"refreshKey", Text(data, "refresh_key")}, {"openId", Text(data, "openid")},
        {"unionId", Text(data, "unionid")}, {"expiresAt", Number(data, "expired_at")},
        {"keyExpiresIn", Number(data, "keyExpiresIn")},
        {"musickeyCreateTime", Number(data, "musickeyCreateTime")},
        {"needRefreshKeyIn", Number(data, "needRefreshKeyIn")},
        {"updatedAt", NowSeconds() * 1000},
    };
}

Json EnsureQimei(Json& secrets, const Cancelled& cancelled, const Exchange& exchange) {
    auto& device = Device(secrets);
    const auto now = NowSeconds();
    if (!device.value("qimei16", "").empty() && !device.value("qimei36", "").empty() &&
        Fresh(device, "qimeiUpdatedAt")) {
        return {{"q16", device.value("qimei16", "")}, {"q36", device.value("qimei36", "")}};
    }
    const auto key = RandomHex(8);
    const auto nonce = RandomHex(8);
    const auto params = QimeiPayload(device, now).dump();
    const auto encryptedKey = Base64(RsaEncrypt(key, kQimeiPublicKey));
    const auto encryptedParams = Base64(Aes(params, key, key));
    const auto extra = std::string("{\"appKey\":\"") + kQimeiAppKey + "\"}";
    const auto timestamp = std::to_string(now);
    Json body = {
        {"app", 0}, {"os", 1},
        {"qimeiParams", {
            {"key", encryptedKey}, {"params", encryptedParams}, {"time", timestamp},
            {"nonce", nonce},
            {"sign", HashHex(encryptedKey + encryptedParams + std::to_string(now * 1000) +
                nonce + kQimeiSecret + extra)},
            {"extra", extra},
        }},
    };
    HttpRequest request;
    request.provider = Provider::QQ;
    request.method = "POST";
    request.url = kQimei;
    request.headers = {
        {"Host", "api.tencentmusic.com"}, {"method", "GetQimei"},
        {"service", "trpc.tme_datasvr.qimeiproxy.QimeiProxy"},
        {"appid", "qimei_qq_android"},
        {"sign", HashHex("qimei_qq_androidpzAuCmaFAaFaHrdakPjLIEqKrGnSOOvH" + timestamp)},
        {"user-agent", "QQMusic"}, {"timestamp", timestamp},
        {"content-type", "application/json"},
    };
    request.body = body.dump();
    request.maxResponse = 256 * 1024;
    const auto response = Send(exchange, std::move(request), cancelled);
    RequireOk(response, "QIMEI request");
    auto payload = ParseJsonBody(response);
    CheckCode(payload);
    auto outer = payload.value("data", Json{});
    if (outer.is_string()) outer = Json::parse(outer.get<std::string>());
    CheckCode(outer);
    const auto data = outer.value("data", Json{});
    if (!data.is_object() || !Token(Text(data, "q16")) || !Token(Text(data, "q36"))) {
        throw std::runtime_error("QQ returned invalid QIMEI");
    }
    device["qimei16"] = data.value("q16", "");
    device["qimei36"] = data.value("q36", "");
    device["qimeiUpdatedAt"] = now * 1000;
    return {{"q16", device["qimei16"]}, {"q36", device["qimei36"]}};
}

Json MusicRequest(Json payload, const Json& credential, const Cancelled& cancelled, const Exchange& exchange,
                  std::string_view operation) {
    const auto response = Send(exchange, MakeJsonRequest(payload.dump(), ValidCredential(credential)
        ? CredentialCookie(credential) : std::string{}), cancelled);
    RequireOk(response, operation);
    return ParseJsonBody(response);
}

void EnsureSession(Json& secrets, const Json& qimei, const Cancelled& cancelled, const Exchange& exchange) {
    auto& device = Device(secrets);
    if (!device.value("sessionUid", "").empty() && !device.value("sessionSid", "").empty() &&
        Fresh(device, "sessionUpdatedAt")) return;
    const auto response = MusicRequest({
        {"comm", BuildComm(device, qimei)},
        {"req_0", {
            {"module", "music.getSession.session"}, {"method", "GetSession"},
            {"param", {{"uid", ""}, {"vkey", 0}, {"caller", 0}}},
        }},
    }, {}, cancelled, exchange, "session request");
    const auto session = ResultData(response, "req_0").at("session");
    if (!Token(Scalar(session, "uid")) || !Token(Scalar(session, "sid"))) {
        throw std::runtime_error("QQ returned invalid device session");
    }
    device["sessionUid"] = Scalar(session, "uid");
    device["sessionSid"] = Scalar(session, "sid");
    device["sessionVkey"] = Scalar(session, "vkey");
    device["sessionUpdatedAt"] = NowSeconds() * 1000;
}

Json ExchangeCode(Json& secrets, std::string_view code, const Cancelled& cancelled, const Exchange& exchange) {
    const auto qimei = EnsureQimei(secrets, cancelled, exchange);
    EnsureSession(secrets, qimei, cancelled, exchange);
    try {
        const auto response = Send(exchange, MakeJsonRequest(Json{
            {"comm", BuildComm(Device(secrets), qimei)},
            {"req_0", {
                {"module", "QQConnectLogin.LoginServer"}, {"method", "QQLogin"},
                {"param", {{"code", std::string(code)}}},
            }},
        }.dump(), {}), cancelled);
        if (response.status < 200 || response.status >= 300) {
            if (response.status >= 400 && response.status <= 599)
                throw Failure("qq-credential-http-" + std::to_string(response.status));
            throw Failure("qq-credential-malformed");
        }
        const auto payload = ParseJsonBody(response);
        const auto checked = [](const Json& value, bool required) {
            if (!value.is_object()) throw Failure("qq-credential-malformed");
            if (!value.contains("code")) {
                if (required) throw Failure("qq-credential-malformed");
                return;
            }
            const auto valueCode = Scalar(value, "code");
            if (valueCode == "0") return;
            if (valueCode.empty() || valueCode.size() > 8 ||
                valueCode.find_first_not_of("0123456789") != std::string::npos)
                throw Failure("qq-credential-malformed");
            throw Failure("qq-credential-code-" + valueCode);
        };
        checked(payload, false);
        const auto& request = payload.at("req_0");
        checked(request, true);
        const auto& data = request.at("data");
        checked(data, false);
        const auto credential = NormalizeCredential(data);
        if (!IsDigits(Text(credential, "uin"))) throw Failure("qq-credential-missing-id");
        if (!Token(Text(credential, "musicKey"))) throw Failure("qq-credential-missing-key");
        if (Expired(credential)) throw Failure("qq-credential-expired");
        return credential;
    } catch (const Failure& failure) {
        CheckCancelled(cancelled);
        const std::string_view reason(failure.what());
        if (StartsWith(reason, "qq-credential-")) throw;
        throw Failure(reason == "network" ? "qq-credential-network" : "qq-credential-malformed");
    } catch (...) {
        CheckCancelled(cancelled);
        throw Failure("qq-credential-malformed");
    }
}

Reply Profile(Json& secrets, const Json& credential, const Cancelled& cancelled, const Exchange& exchange) {
    const auto qimei = EnsureQimei(secrets, cancelled, exchange);
    const auto& device = Device(secrets);
    const auto userId = credential.value("uin", "");
    auto comm = BuildComm(device, qimei, credential.value("loginType", 2));
    comm["qq"] = userId;
    comm["authst"] = credential.value("musicKey", "");
    const auto response = MusicRequest({
        {"comm", comm},
        {"req_1", {
            {"module", "userInfo.BaseUserInfoServer"}, {"method", "get_user_baseinfo_v2"},
            {"param", {{"vec_uin", Json::array({userId})}}},
        }},
    }, credential, cancelled, exchange, "profile request");
    const auto baseMap = ResultData(response, "req_1").at("map_userinfo");
    Json base;
    if (baseMap.is_object() && baseMap.contains(userId)) base = baseMap[userId];
    if (base.is_object() && base.contains("base")) base = base.at("base");
    if (base.contains("uin") && Scalar(base, "uin") != userId) throw Failure("invalid-response");
    auto displayName = Text(base, "nick");
    if (displayName.empty()) displayName = Text(base, "nickname");
    if (displayName.empty() || displayName.size() > 256 ||
        std::any_of(displayName.begin(), displayName.end(), [](unsigned char c) { return c < 32; }))
        throw Failure("invalid-response");
    Reply reply{"signed-in", {}, userId, displayName, {}, {}, {}};
    reply.avatarUrl = ProfileImage(ProfileText(base, {"headurl", "avatarUrl", "logo"}));
    if (reply.avatarUrl.empty()) reply.avatarUrl = ProfileImage(Text(credential, "avatarUrl"));
    if (reply.avatarUrl.empty()) reply.avatarUrl = "https://q1.qlogo.cn/g?b=qq&nk=" + UrlEncode(userId) + "&s=100";
    // VIP metadata decorates an already verified identity; it cannot reject it.
    try {
        const auto vipResponse = MusicRequest({
            {"comm", comm},
            {"req_0", {
                {"module", "userInfo.VipQueryServer"}, {"method", "SRFVipQuery_V2"},
                {"param", {{"uin_list", Json::array({userId})}}},
            }},
        }, credential, cancelled, exchange, "VIP request");
        const auto vipMap = ResultData(vipResponse, "req_0").value("infoMap", Json::object());
        Json vip = Json::object();
        if (!vipMap.is_object()) throw Failure("invalid-response");
        if (vipMap.contains(userId)) vip = vipMap.at(userId);
        if (vip.is_object() && vip.contains("vip")) vip = vip.at("vip");
        if (!vip.is_object()) throw Failure("invalid-response");
        if (!vip.empty()) {
            const auto level = ProfileNumber(vip, {"vipLevel", "vip_level", "iCurLevel", "level"});
            if (level > 1000) throw Failure("invalid-response");
            auto expiresAt = ProfileNumber(vip, {"vipEnd"});
            if (expiresAt > 0 && expiresAt < 10'000'000'000LL) expiresAt *= 1000;
            const auto genericName = ProfileText(vip, {"vipName", "vip_name"});
            if (genericName.size() > 128 ||
                std::any_of(genericName.begin(), genericName.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                throw Failure("invalid-response");
            const auto now = NowSeconds() * 1000;
            if (expiresAt == 0) expiresAt = QqTime(ProfileText(vip, {"sOverDateTime"}));
            // Live account (2026-10-11), green only: iVipFlag=1 and iSuperVip=1
            // with the icon help text 豪华绿钻 and an upsell "¥1升SVIP", while
            // HugeVip=0. So iSuperVip is the 豪华 (deluxe) green tier, and the
            // 超级会员 SVIP is HugeVip with HugeVipStart/HugeVipEnd.
            const bool luxury = ProfileNumber(vip, {"iSuperVip", "isSuperVip", "is_super_vip", "superVip", "super_vip"}) > 0;
            auto superExpiresAt = ProfileNumber(vip, {"svipEnd", "hugeVipEnd"});
            if (superExpiresAt > 0 && superExpiresAt < 10'000'000'000LL) superExpiresAt *= 1000;
            if (superExpiresAt == 0) superExpiresAt = QqTime(ProfileText(vip, {"HugeVipEnd"}));
            const bool superActive = ProfileNumber(vip, {"HugeVip", "hugeVip", "isSvip", "svip"}) > 0 &&
                (superExpiresAt == 0 || superExpiresAt > now);
            const bool greenActive = (ProfileNumber(vip, {"isVip", "is_vip", "iVipFlag"}) > 0 || luxury) &&
                (expiresAt == 0 || expiresAt > now);
            auto superLevel = ProfileNumber(vip, {"svipLevel", "superVipLevel", "super_vip_level"});
            if (superLevel <= 0 || superLevel > 1000) superLevel = superActive ? level : 0;
            // vipName/vipIcon describe the account's current top tier, so they
            // belong to SVIP when it is active, otherwise to green.
            const auto genericIcon = ProfileImage(ProfileText(vip, {"vipIcon", "vip_icon", "vipIconAlt", "iconUrl"}));
            const bool genericSuper = superActive || QqSuperIconUrl(genericIcon) ||
                genericName.find(u8"\u8d85\u7ea7") != std::string::npos;
            Membership green{"qq-green-vip", luxury ? u8"\u8c6a\u534e\u7eff\u94bb" : u8"\u7eff\u94bb", level, greenActive,
                genericSuper ? std::string{} : genericIcon, expiresAt};
            if (!genericSuper && !genericName.empty()) green.label = genericName;
            Membership super{"qq-super-vip", u8"\u8d85\u7ea7\u4f1a\u5458", superLevel, superActive,
                ProfileImage(ProfileText(vip, {"superVipIcon", "svipIcon"})), superExpiresAt};
            if (super.iconUrl.empty() && genericSuper) super.iconUrl = genericIcon;
            if (level > 0 && (green.iconUrl.empty() || super.iconUrl.empty())) {
                try {
                    const auto icons = MusicRequest({
                        {"comm", comm},
                        {"req_0", {{"module", "music.lvz.VipIconUiShowSvr"}, {"method", "GetVipIconUiV2"},
                            {"param", {{"PID", level}}}}},
                    }, credential, cancelled, exchange, "VIP icon request");
                    const auto list = ResultData(icons, "req_0").at("UserInfoUI").at("iconlist");
                    if (!list.is_array() || list.size() > 16) throw Failure("invalid-response");
                    for (const auto& icon : list) {
                        if (!icon.is_object()) throw Failure("invalid-response");
                        const auto url = ProfileImage(Text(icon, "srcUrl"));
                        // Classify by the tier artwork file only. A live SVIP account
                        // returned the green vip5.png with a desc mentioning "svip",
                        // plus an audiobook (/audio/) icon; neither desc nor other
                        // product icons identify the tier.
                        if (!QqTierIconUrl(url)) continue;
                        const bool superIcon = QqSuperIconUrl(url);
                        const bool greenIcon = !superIcon && QqGreenIconUrl(url);
                        if (!superIcon && !greenIcon) continue;
                        auto& membership = superIcon ? super : green;
                        if (membership.iconUrl.empty()) membership.iconUrl = url;
                    }
                } catch (...) { CheckCancelled(cancelled); }
            }
            if (green.iconUrl.empty()) green.iconUrl = std::string("https://y.qq.com/mediastyle/lv-icon/v14/2x/") +
                (green.active ? "vip5.png" : "d-vip5.png");
            if (super.iconUrl.empty()) super.iconUrl = std::string("https://y.qq.com/mediastyle/lv-icon/v14/2x/") +
                (super.active ? "svip5.png" : "d-svip5.png");
            // Lit or dimmed artwork follows the membership state, not the server.
            green.iconUrl = QqIconForState(green.iconUrl, green.active);
            super.iconUrl = QqIconForState(super.iconUrl, super.active);
            reply.memberships = {std::move(green), std::move(super)};
        }
    } catch (...) { CheckCancelled(cancelled); }
    secrets["profile"] = {{"userId", userId}, {"displayName", displayName},
        {"avatarUrl", reply.avatarUrl}, {"memberships", reply.memberships}};
    return reply;
}

Reply ProfileOrFallback(Json& secrets, const Json& credential, const Cancelled& cancelled,
                        const Exchange& exchange) {
    try {
        return Profile(secrets, credential, cancelled, exchange);
    } catch (const InvalidCredential&) {
        throw;
    } catch (const Failure& failure) {
        const auto reason = std::string_view(failure.what());
        if (reason != "network" && reason != "service") throw;
        CheckCancelled(cancelled);
        const auto userId = credential.value("uin", "");
        if (!IsDigits(userId)) throw;
        Reply reply{"signed-in", {}, userId, userId, {},
            "https://q1.qlogo.cn/g?b=qq&nk=" + UrlEncode(userId) + "&s=100", {}};
        secrets["profile"] = {{"userId", reply.userId}, {"displayName", reply.displayName},
            {"avatarUrl", reply.avatarUrl}, {"memberships", Json::array()}};
        return reply;
    }
}

Reply Error(std::string message) {
    return {"error", {}, {}, {}, {}, {}, std::move(message)};
}

std::string StageError(std::string_view stage) {
    if (stage == "status") return "qq-status-response";
    if (stage == "authorization") return "qq-authorization-response";
    if (stage == "device") return "qq-device-response";
    if (stage == "credential") return "qq-credential-response";
    if (stage == "profile") return "qq-profile-response";
    return "service";
}

Reply Begin(Json& secrets, const Cancelled& cancelled, const Exchange& exchange) {
    const auto query = Query({
        {"appid", "716027609"}, {"e", "2"}, {"l", "M"}, {"s", "3"},
        {"d", "72"}, {"v", "4"}, {"t", "0." + RandomInt(100'000'000, 999'999'999)},
        {"daid", "383"}, {"pt_3rd_aid", "100497308"},
    });
    HttpRequest request;
    request.provider = Provider::QQ;
    request.method = "GET";
    request.url = std::string(kQrShow) + query;
    request.headers = {{"referer", "https://xui.ptlogin2.qq.com/"}};
    request.maxResponse = 256 * 1024 - 32;
    const auto response = Send(exchange, std::move(request), cancelled);
    RequireOk(response, "QR request");
    const auto qrsig = CookieValue(response.cookies, "qrsig");
    if (qrsig.empty() || response.body.size() < 33 ||
        response.body.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) != 0 ||
        response.body.compare(12, 4, "IHDR") != 0) throw Failure("invalid-response");
    const auto attempt = RandomHex(16);
    secrets["attempt"] = {{"id", attempt}, {"qrsig", qrsig}};
    return {"authorizing", "waiting", {}, {}, "data:image/png;base64," + Base64(response.body), {}, {}};
}

Reply Poll(Json& secrets, const Cancelled& cancelled, const Exchange& exchange) {
    const auto attempt = secrets.value("attempt", Json{});
    if (!attempt.is_object() || attempt.value("id", "").empty() || attempt.value("qrsig", "").empty()) {
        return {"expired", {}, {}, {}, {}, {}, "expired"};
    }
    const auto timestamp = std::to_string(NowSeconds() * 1000);
    const auto query = Query({
        {"u1", "https://graph.qq.com/oauth2.0/login_jump"},
        {"ptqrtoken", Hash33(attempt.value("qrsig", ""))},
        {"ptredirect", "0"}, {"h", "1"}, {"t", "1"}, {"g", "1"},
        {"from_ui", "1"}, {"ptlang", "2052"}, {"action", "0-0-" + timestamp},
        {"js_ver", "20102616"}, {"js_type", "1"}, {"pt_uistyle", "40"},
        {"aid", "716027609"}, {"daid", "383"}, {"pt_3rd_aid", "100497308"},
        {"has_onekey", "1"},
    });
    HttpRequest request;
    request.provider = Provider::QQ;
    request.method = "GET";
    request.url = std::string(kQrPoll) + query;
    request.headers = {
        {"cookie", "qrsig=" + attempt.value("qrsig", "")},
        {"referer", "https://xui.ptlogin2.qq.com/"},
    };
    const auto response = Send(exchange, std::move(request), cancelled);
    RequireOk(response, "QR poll");
    const auto status = ParsePtui(response.body);
    if (status.code == 66) return {"authorizing", "waiting", {}, {}, {}, {}, {}};
    if (status.code == 67) return {"authorizing", "scanned", {}, {}, {}, {}, {}};
    if (status.code == 65) {
        secrets.erase("attempt");
        return {"expired", {}, {}, {}, {}, {}, "expired"};
    }
    if (status.code != 0) return Error("service");
    auto callback = Parameters(status.callback, kCheckSig);
    const auto uin = callback["uin"];
    const auto sigx = callback["ptsigx"];
    if (!IsDigits(uin) || !Token(sigx)) throw Failure("invalid-response");
    const auto check = Send(exchange, HttpRequest{
        Provider::QQ, "GET", std::string(kCheckSig) + Query({
            {"uin", uin}, {"pttype", "1"}, {"service", "ptqrlogin"}, {"nodirect", "0"},
            {"ptsigx", sigx}, {"s_url", "https://graph.qq.com/oauth2.0/login_jump"},
            {"ptlang", "2052"}, {"ptredirect", "100"}, {"aid", "716027609"},
            {"daid", "383"}, {"j_later", "0"}, {"low_login_hour", "0"},
            {"regmaster", "0"}, {"pt_login_type", "3"}, {"pt_aid", "0"},
            {"pt_aaid", "16"}, {"pt_light", "0"}, {"pt_3rd_aid", "100497308"},
        }), {{"referer", "https://xui.ptlogin2.qq.com/"}}, {}, 256 * 1024
    }, cancelled);
    // WinHTTP does not follow redirects here. QQ has returned several valid
    // 2xx/3xx variants over time, so only reject actual HTTP failures.
    RequireResponseWithRedirect(check, "QQ signature check");
    const auto pSkey = CookieValue(check.cookies, "p_skey");
    if (pSkey.empty()) throw Failure("invalid-response");
    const auto form = std::string("response_type=code&client_id=100497308&redirect_uri=") +
        UrlEncode("https://y.qq.com/portal/wx_redirect.html?login_type=1&surl=https://y.qq.com/") +
        "&scope=get_user_info%2Cget_app_friends&state=state&switch=&from_ptlogin=1&src=1" +
        "&update_auth=1&openapi=1010_1030&g_tk=" + Hash33(pSkey, 5381) +
        "&auth_time=" + std::to_string(NowSeconds() * 1000) + "&ui=" + Uuid();
    const auto authorize = Send(exchange, HttpRequest{
        Provider::QQ, "POST", kAuthorize,
        {{"cookie", [&] {
            std::string value;
            for (const auto& cookie : check.cookies) {
                const auto pair = cookie.substr(0, cookie.find(';'));
                const auto equal = pair.find('=');
                if (equal == std::string::npos || equal == 0 ||
                    pair.substr(0, equal).find_first_not_of(
                        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos ||
                    (!pair.substr(equal + 1).empty() && !Token(pair.substr(equal + 1))))
                    throw Failure("invalid-response");
                // QQ also sends expired auxiliary cookies during successful login.
                // They do not authorize this request and must not reject p_skey.
                if (pair.substr(equal + 1).empty()) continue;
                if (!value.empty()) value += "; ";
                value += pair;
            }
            return value;
        }()}, {"content-type", "application/x-www-form-urlencoded"}, {"referer", "https://graph.qq.com/"}},
        form, 256 * 1024
    }, cancelled);
    RequireResponseWithRedirect(authorize, "QQ authorization");
    auto location = Parameters(authorize.location, "https://y.qq.com/portal/wx_redirect.html");
    const auto code = location["code"];
    const auto state = location.find("state");
    if (!Token(code) || (state != location.end() && state->second != "state"))
        throw Failure("invalid-response");
    const auto credential = ExchangeCode(secrets, code, cancelled, exchange);
    // The OAuth code is consumed at this point. Keep the verified credential even
    // when optional profile enrichment is temporarily unavailable.
    secrets["credential"] = credential;
    const auto reply = ProfileOrFallback(secrets, credential, cancelled, exchange);
    secrets.erase("attempt");
    return reply;
}

Reply Refresh(Json& secrets, const Cancelled& cancelled, const Exchange& exchange) {
    if (!secrets.contains("credential")) return {};
    const auto old = secrets.value("credential", Json{});
    if (!ValidCredential(old)) throw InvalidCredential{};
    const bool expired = Expired(old);
    if (expired && (Text(old, "refreshToken").empty() || Text(old, "refreshKey").empty()))
        throw InvalidCredential{};
    if (!expired) return Profile(secrets, old, cancelled, exchange);
    const auto qimei = EnsureQimei(secrets, cancelled, exchange);
    const auto response = MusicRequest({
        {"comm", BuildComm(Device(secrets), qimei, old.value("loginType", 2))},
        {"req_0", {
            {"module", "music.login.LoginServer"}, {"method", "Login"},
            {"param", {
                {"openid", old.value("openId", "")},
                {"access_token", old.value("accessToken", "")},
                {"refresh_token", old.value("refreshToken", "")},
                {"expired_in", Number(old, "expiresAt")},
                {"musicid", std::stoll(old.value("uin", "0"))},
                {"str_musicid", old.value("uin", "")},
                {"musickey", old.value("musicKey", "")},
                {"refresh_key", old.value("refreshKey", "")},
                {"unionid", old.value("unionId", "")}, {"loginMode", 2},
            }},
        }},
    }, old, cancelled, exchange, "credential refresh");
    const auto data = ResultData(response, "req_0");
    auto refreshed = NormalizeCredential(data);
    if (!ValidCredential(refreshed) || Expired(refreshed) ||
        refreshed["uin"] != old["uin"]) throw InvalidCredential{};
    for (const char* field : {"loginType", "accessToken", "refreshToken", "refreshKey", "openId", "unionId"}) {
        if (refreshed[field].is_null() || refreshed[field] == "" || refreshed[field] == 0) {
            refreshed[field] = old.value(field, refreshed[field]);
        }
    }
    secrets["credential"] = refreshed;
    const auto reply = ProfileOrFallback(secrets, refreshed, cancelled, exchange);
    secrets.erase("attempt");
    return reply;
}

}  // namespace

Json QQCatalogComm(Json& secrets, bool playback, const Cancelled& cancelled, const Exchange& exchange) {
    try {
        const auto credential = secrets.value("credential", Json::object());
        if (!ValidCredential(credential) || Expired(credential)) throw std::runtime_error("expired");
        const auto qimei = EnsureQimei(secrets, cancelled, exchange);
        auto comm = BuildComm(Device(secrets), qimei, credential.value("loginType", 2));
        comm["qq"] = credential.at("uin");
        comm["authst"] = credential.at("musicKey");
        if (playback) {
            auto& device = Device(secrets);
            if (Text(device, "sessionUid").empty() || Text(device, "sessionSid").empty() ||
                !Fresh(device, "sessionUpdatedAt")) {
                const auto response = MusicRequest({
                    {"comm", comm},
                    {"req_0", {{"module", "music.getSession.session"}, {"method", "GetSession"},
                        {"param", {{"uid", Text(device, "sessionUid")}, {"vkey", 0}, {"caller", 0}}}}}
                }, credential, cancelled, exchange, "catalog device session");
                const auto session = ResultData(response, "req_0").at("session");
                if (!Token(Scalar(session, "uid")) || !Token(Scalar(session, "sid")))
                    throw std::runtime_error("invalid-response");
                device["sessionUid"] = Scalar(session, "uid");
                device["sessionSid"] = Scalar(session, "sid");
                device["sessionUpdatedAt"] = NowSeconds() * 1000;
                comm["uid"] = device["sessionUid"];
                comm["sid"] = device["sessionSid"];
            }
            comm["ct"] = 19;
            comm["tmeLoginType"] = Text(credential, "musicKey").find("W_X") == 0 ? 1 : 2;
        }
        return comm;
    } catch (const InvalidCredential&) {
        throw std::runtime_error("expired");
    } catch (const Failure& failure) {
        throw std::runtime_error(std::string_view(failure.what()) == "network" ? "service" : failure.what());
    }
}

Reply RunQQ(Operation operation, Json& secrets, const Cancelled& cancelled, const Exchange& exchange) {
    std::string stage = "qr";
    const Exchange observed = [&](const HttpRequest& request, const Cancelled& stop) {
        const auto path = request.url.substr(0, request.url.find('?'));
        if (path == kQrPoll) stage = "status";
        else if (path == kCheckSig || path == kAuthorize) stage = "authorization";
        else if (path == kQimei) stage = "device";
        else if (path == kMusicApi) {
            const auto body = Json::parse(request.body);
            const auto module = body.at(body.contains("req_0") ? "req_0" : "req_1").value("module", "");
            stage = module == "music.getSession.session" ? "device" :
                module == "userInfo.BaseUserInfoServer" || module == "userInfo.VipQueryServer" ||
                module == "music.lvz.VipIconUiShowSvr" ? "profile" : "credential";
        }
        return exchange(request, stop);
    };
    // The session owns durable storage; commit only a fully checked operation.
    Json next = secrets.is_null() ? Json::object() : secrets;
    // Except the device: it may already have met QQ (QIMEI, credential exchange)
    // when a later step fails, and a retry must present the same one.
    const auto keepDevice = [&] {
        if (secrets.is_object() && next.is_object() && next.contains("device") && next["device"].is_object())
            secrets["device"] = next["device"];
    };
    const auto invalid = [&] { keepDevice(); return Error(stage == "qr" ? "invalid-response" : "qq-" + stage + "-response"); };
    try {
        CheckCancelled(cancelled);
        if (!next.is_object()) throw Failure("invalid-response");
        Reply reply;
        switch (operation) {
        case Operation::Begin: reply = Begin(next, cancelled, observed); break;
        case Operation::Poll: reply = Poll(next, cancelled, observed); break;
        case Operation::Refresh: reply = Refresh(next, cancelled, observed); break;
        default: throw Failure("service");
        }
        CheckCancelled(cancelled);
        if (reply.status == "signed-in") next.erase("attempt");
        if (reply.status != "error") secrets = std::move(next);
        else keepDevice();
        return reply;
    } catch (const InvalidCredential&) {
        CheckCancelled(cancelled);
        if (operation != Operation::Refresh) { keepDevice(); return Error("service"); }
        secrets.erase("credential");
        secrets.erase("profile");
        if (secrets.contains("device") && secrets["device"].is_object()) {
            for (const auto* key : {"sessionUid", "sessionSid", "sessionVkey", "sessionUpdatedAt"})
                secrets["device"].erase(key);
        }
        return {"expired", {}, {}, {}, {}, {}, "expired"};
    } catch (const Failure& error) {
        CheckCancelled(cancelled);
        keepDevice();
        const auto reason = std::string_view(error.what());
        return reason == "invalid-response" || reason == "service" || reason == "network"
            ? Error(operation == Operation::Poll && stage != "qr"
                ? StageError(stage) : std::string(error.what()))
            : Error(error.what());
    } catch (...) {
        CheckCancelled(cancelled);
        return invalid();
    }
}

}  // namespace lume::music::auth
