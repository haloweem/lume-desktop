// SPDX-License-Identifier: AGPL-3.0-only
// Historical Folia provider protocol port. See music_providers/ONLINE_PROVENANCE.md.
#include "platform/windows/music_online_provider.h"
#include "platform/windows/music_providers/catalog_requests.h"
#include "core/music/music_online_lyrics.h"
#include "core/music/music_types.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <deque>
#include <limits>
#include <mutex>
#include <set>

namespace lume::music::online {
namespace {
using auth::Provider;
using Params = std::map<std::string, std::string>;
constexpr std::size_t MaxResponse = auth::kCatalogMaxResponse;
// Account playlists are delivered whole with the first home page.
constexpr std::size_t kMaxLibraryCollections = 1000;
struct KugouCard { const char* id; const char* title; };
constexpr KugouCard KugouCards[] = {
    {"3006", u8"VIP \u4e13\u5c5e\u63a8\u8350"},
    {"3001", u8"\u79c1\u4eba\u4e13\u5c5e\u597d\u6b4c"},
    {"3004", u8"\u5c0f\u4f17\u5b9d\u85cf\u4f73\u4f5c"},
    {"3014", u8"\u559c\u6b22\u8fd9\u9996\u6b4c\u7684 TA \u4e5f\u559c\u6b22"},
    {"3101", u8"\u6982\u5ff5\u65b0\u63a8"},
    {"3005", u8"\u6f6e\u6d41\u5c1d\u9c9c"},
};
[[noreturn]] void Fail(const char* error = "invalid-response") { throw std::runtime_error(error); }
const Json& At(const Json& j, const char* path) {
    static const Json missing;
    const Json::json_pointer pointer(path);
    return j.contains(pointer) ? j.at(pointer) : missing;
}
std::string Text(const Json& j, std::initializer_list<const char*> keys, std::size_t max = 4096) {
    for (const auto* key : keys) {
        const auto& value = At(j, key);
        if (value.is_null()) continue;
        std::string text;
        if (value.is_string()) text = value.get<std::string>();
        else if (value.is_number_integer()) text = value.dump();
        else continue;
        if (text.size() > max || std::any_of(text.begin(), text.end(),
            [](unsigned char c) { return c < 32 && c != '\n' && c != '\t'; })) Fail();
        if (!text.empty()) return text;
    }
    return {};
}
// Page text must be one control-free line within the session limits. Platform
// descriptions routinely contain line breaks; one must not reject a whole page.
std::string Plain(std::string text, std::size_t max) {
    for (auto& c : text) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    if (text.size() > max) {
        while (max && (static_cast<unsigned char>(text[max]) & 0xC0) == 0x80) --max;
        text.resize(max);
    }
    return text;
}
std::int64_t Number(const Json& j, std::initializer_list<const char*> keys, std::int64_t fallback = 0) {
    const auto value = Text(j, keys, 20);
    if (value.empty()) return fallback;
    if (value.find_first_not_of("0123456789") != value.npos || value.size() > 16) Fail();
    const auto n = std::stoll(value);
    if (n > 9007199254740991LL) Fail();
    return n;
}
bool Flag(const Json& j, const char* path) {
    const auto& v = At(j, path);
    return v == true || v == 1 || v == "1";
}
bool Paid(const Json& value) {
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<std::int64_t>() > 0;
    if (value.is_number_unsigned()) return value.get<std::uint64_t>() > 0;
    if (!value.is_string()) return false;
    const auto& text = value.get_ref<const std::string&>();
    if (text.empty() || text.size() > 16 ||
        text.find_first_not_of("0123456789") != text.npos) return false;
    return text.find_first_not_of('0') != text.npos;
}
bool NeteaseFee(const Json& track, const Json& wrapped, std::int64_t fee) {
    for (const auto* value : {&track, &wrapped})
        for (const auto* path : {"/fee", "/privilege/fee"}) {
            const auto& v = At(*value, path);
            if (v.is_number_integer() && v.get<std::int64_t>() == fee) return true;
        }
    return false;
}
bool VipRequired(const Json& wrapped, const Json& track, Provider provider) {
    const auto paid = [&](const Json& value, std::initializer_list<const char*> paths) {
        return std::any_of(paths.begin(), paths.end(), [&](const char* path) {
            return Paid(At(value, path));
        });
    };
    // NetEase fee: 1 = VIP song, 4 = bought album, 8 = free at standard
    // quality (VIP only for higher qualities), 0 = free.
    if (provider == Provider::Netease)
        return NeteaseFee(track, wrapped, 1) || NeteaseFee(track, wrapped, 4) ||
            paid(track, {"/privilege/payPlay"}) || paid(wrapped, {"/privilege/payPlay"});
    if (provider == Provider::QQ)
        return paid(track, {"/pay/pay_play", "/pay/payplay", "/pay_play"}) ||
            paid(wrapped, {"/pay/pay_play", "/pay/payplay", "/pay_play"});
    // KuGou privilege of the standard file decides: 10 = 音乐包/VIP only
    // (live 2026-10-11: v5/url answers fail_process pkg/buy for a free
    // account), 8 = free. Search spells the fields PayType/Privilege.
    for (const auto* value : {&track, &wrapped})
        for (const auto* path : {"/Privilege", "/privilege", "/privilege_128", "/audio_info/privilege",
                                 "/audio_info/privilege_128"}) {
            const auto& v = At(*value, path);
            std::int64_t privilege = -1;
            if (v.is_number_integer()) privilege = v.get<std::int64_t>();
            else if (v.is_string() && !v.get_ref<const std::string&>().empty() && v.get_ref<const std::string&>().size() < 4 &&
                     v.get_ref<const std::string&>().find_first_not_of("0123456789") == std::string::npos)
                privilege = std::stoll(v.get<std::string>());
            if (privilege > 0) return privilege == 10;
        }
    return paid(track, {"/pay_type", "/PayType", "/audio_info/pay_type", "/is_vip", "/audio_info/is_vip"}) ||
        paid(wrapped, {"/pay_type", "/PayType", "/audio_info/pay_type", "/is_vip", "/audio_info/is_vip"});
}
std::string MembershipBadge(const Json& wrapped, const Json& track, Provider provider) {
    const auto paid = [&](const Json& value, std::initializer_list<const char*> paths) {
        return std::any_of(paths.begin(), paths.end(), [&](const char* path) {
            return Paid(At(value, path));
        });
    };
    if (provider == Provider::Netease) {
        if (paid(track, {"/redplus", "/redPlus", "/svipLevel", "/privilege/svipLevel"}) ||
            paid(wrapped, {"/redplus", "/redPlus", "/svipLevel", "/privilege/svipLevel"}))
            return "netease-svip";
        if (NeteaseFee(track, wrapped, 1)) return "netease-vip";
        return {};
    }
    if (provider == Provider::QQ) {
        if (paid(track, {"/isSuperVip", "/is_super_vip", "/superVip", "/super_vip", "/isSvip", "/svip"}) ||
            paid(wrapped, {"/isSuperVip", "/is_super_vip", "/superVip", "/super_vip", "/isSvip", "/svip"}))
            return "qq-super-vip";
        if (VipRequired(wrapped, track, provider)) return "qq-green-vip";
        return {};
    }
    if (VipRequired(wrapped, track, provider)) return "kugou-vip";
    return {};
}
const Json& List(const Json& j, std::initializer_list<const char*> paths) {
    for (const auto* path : paths) {
        const auto& v = At(j, path);
        if (v.is_array()) return v;
    }
    Fail();
}
bool Token(std::string_view s, bool digits = false) {
    return !s.empty() && s.size() <= 128 &&
        s.find_first_not_of(digits ? "0123456789" :
            "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-") == s.npos;
}
std::string Id(const Json& args, const char* key, Provider provider, bool track = true) {
    auto id = Text(args, {key}, 160);
    const auto prefix = std::string(auth::Name(provider)) + ":";
    if (track && id.compare(0, prefix.size(), prefix) == 0) id.erase(0, prefix.size());
    if (!Token(id, provider == Provider::Netease)) Fail();
    if (provider == Provider::Netease &&
        (id.size() > 16 || id.front() == '0' || Number(Json{{"id", id}}, {"/id"}) == 0)) Fail();
    if (provider == Provider::Kugou && track) {
        if (id.size() != 32 || id.find_first_not_of("0123456789abcdefABCDEF") != id.npos) Fail();
        std::transform(id.begin(), id.end(), id.begin(), [](char c) {
            return c >= 'a' && c <= 'f' ? static_cast<char>(c - 32) : c;
        });
    }
    return id;
}
std::string InputText(const Json& args, const char* field, std::size_t max) {
    const auto& value = At(args, field);
    if (!value.is_string()) Fail();
    auto text = Text(args, {field}, max);
    if (text.empty() || text.find_first_not_of(" \r\n\t") == text.npos ||
        text.find_first_of("\r\n\t") != text.npos) Fail();
    return text;
}
int PageNumber(const Json& args, const char* path, int fallback, int max, bool cap) {
    const auto& value = At(args, path);
    if (value.is_null()) return fallback;
    if (!value.is_number_integer() || value < (cap ? 1 : 0)) Fail();
    if (value > max) { if (cap) return max; Fail(); }
    return value.get<int>();
}
std::string Https(std::string url, Provider provider, bool media = false) {
    if (url.compare(0, 2, "//") == 0) url.insert(0, "https:");
    if (url.compare(0, 7, "http://") == 0) url.replace(0, 7, "https://");
    if (url.empty()) return {};
    if (url.size() > 8192 || url.compare(0, 8, "https://") != 0 ||
        url.find_first_of("\\@# \r\n\t") != url.npos ||
        std::any_of(url.begin(), url.end(), [](unsigned char c) { return c < 33 || c >= 127; })) return {};
    const auto end = url.find_first_of("/?", 8);
    const auto host = url.substr(8, end == url.npos ? url.size() - 8 : end - 8);
    if (host.empty() || host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != host.npos)
        return {};
    const auto suffix = [&](std::string_view domain) {
        return host == domain || (host.size() > domain.size() &&
            host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
            host[host.size() - domain.size() - 1] == '.');
    };
    const bool allowed = provider == Provider::Netease ? suffix("music.126.net") :
        provider == Provider::QQ ? suffix("qq.com") || suffix("qqmusic.com") : suffix("kugou.com");
    if (media && !allowed) return {};
    // Artwork is provider-controlled, but private/literal hosts and credentials are never accepted.
    if (!media && !allowed && !(provider == Provider::QQ && suffix("gtimg.cn")) &&
        !(provider == Provider::Kugou && suffix("kgimg.com"))) return {};
    return url;
}
Json Page() {
    return {{"tracks", Json::array()}, {"playlists", Json::array()},
            {"recommendations", Json::array()}, {"more", false}};
}
Json AudioSource(const std::string& url, const std::string& quality = {}, std::int64_t bitrate = 0) {
    if (bitrate < 0 || bitrate > 10000000) Fail();
    return {{"url", url}, {"quality", quality}, {"bitrate", bitrate}};
}
std::string ReportedQuality(const Json& song, std::string_view provider = {}) {
    const auto level = Text(song, {"/level", "/quality"});
    if (music::ValidAudioQuality(level, provider)) return level;
    if (level == "exhigh" || level == "higher" || level == "320") return "high";
    if (level == "128") return "standard";
    const auto type = Text(song, {"/type", "/format", "/extname"});
    if (type == "flac" || type == "ape" || type == "wav")
        return Number(song, {"/sr", "/sample_rate"}) >= 96000 ? "hires" : "lossless";
    const auto bitrate = Number(song, {"/br", "/bitrate"});
    if (bitrate > 0 && bitrate <= 320000)
        return bitrate > 128000 ? "high" : "standard";
    return {};
}
std::string QQQuality(const std::string& path) {
    auto filename = path.substr(0, path.find('?'));
    const auto slash = filename.find_last_of('/');
    if (slash != filename.npos) filename.erase(0, slash + 1);
    if (filename.rfind("RS01", 0) == 0) return "hires";
    if (filename.rfind("F000", 0) == 0 || filename.rfind("A000", 0) == 0) return "lossless";
    if (filename.rfind("M800", 0) == 0) return "high";
    if (filename.rfind("M500", 0) == 0 || filename.rfind("C400", 0) == 0) return "standard";
    return {};
}

bool LegacyChineseCharset(std::string_view contentType) {
    std::string lower;
    lower.reserve(contentType.size());
    for (const auto character : contentType)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    const auto key = lower.find("charset");
    if (key == lower.npos) return false;
    auto cursor = key + 7;
    while (cursor < lower.size() && std::isspace(static_cast<unsigned char>(lower[cursor]))) ++cursor;
    if (cursor == lower.size() || lower[cursor++] != '=') return false;
    while (cursor < lower.size() && std::isspace(static_cast<unsigned char>(lower[cursor]))) ++cursor;
    const auto end = lower.find_first_of("; \t\r\n", cursor);
    const auto value = lower.substr(cursor, end == lower.npos ? lower.size() - cursor : end - cursor);
    return value == "gb2312" || value == "gbk" || value == "gb18030";
}

std::string DecodeLegacyChinese(std::string_view bytes) {
    constexpr UINT Gb18030CodePage = 54936;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) Fail();
    const auto length = static_cast<int>(bytes.size());
    const auto wideLength = MultiByteToWideChar(Gb18030CodePage, MB_ERR_INVALID_CHARS,
        bytes.data(), length, nullptr, 0);
    if (!wideLength) Fail();
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(Gb18030CodePage, MB_ERR_INVALID_CHARS, bytes.data(), length,
        wide.data(), wideLength) != wideLength) Fail();
    const auto utf8Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
    if (!utf8Length) Fail();
    std::string utf8(static_cast<std::size_t>(utf8Length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wideLength,
        utf8.data(), utf8Length, nullptr, nullptr) != utf8Length) Fail();
    return utf8;
}

// ponytail: one bounded process cache; only metadata, no credentials or resolved URLs.
// Credential fingerprints separate account-local playlist/file IDs and disappear by FIFO eviction.
std::mutex cacheMutex;
std::map<std::string, Json> metadata;
std::deque<std::string> insertionOrder;
void RememberLocked(const std::string& key, Json value) {
    if (!metadata.count(key)) {
        if (metadata.size() >= 2048) {
            metadata.erase(insertionOrder.front());
            insertionOrder.pop_front();
        }
        insertionOrder.push_back(key);
    }
    metadata[key] = std::move(value);
}
Json RecallMetadata(const std::string& key) {
    std::lock_guard lock(cacheMutex);
    const auto it = metadata.find(key);
    return it == metadata.end() ? Json::object() : it->second;
}
std::string Artists(const Json& j, std::initializer_list<const char*> paths) {
    for (const auto* path : paths) {
        const auto& values = At(j, path);
        if (!values.is_array()) continue;
        if (values.size() > 100) Fail();
        std::string result;
        for (const auto& artist : values) {
            const auto name = Text(artist, {"/name", "/Name", "/author_name", "/base/author_name"}, 512);
            if (!name.empty()) { if (!result.empty()) result += ", "; result += name; }
        }
        if (result.size() > 4096) Fail();
        if (!result.empty()) return result;
    }
    return Text(j, {"/SingerName", "/singername", "/singer_name", "/author_name"}, 4096);
}
struct Context {
    Provider provider;
    Json secrets;
    const auth::Cancelled& cancelled;
    const auth::Exchange& transport;
    std::string scope;
    std::string leasedUserId;
    int offset, limit;
    std::map<std::string, Json> pending;
    Json likedState;
    // Set by QLibrary when an account source failed; lets the home page tell an
    // empty library apart from a broken one instead of returning a silent empty list.
    std::string libraryFailure;

    Json Recall(const std::string& key) const {
        const auto found = pending.find(key);
        return found == pending.end() ? RecallMetadata(key) : found->second;
    }
    void Remember(const std::string& key, Json value) {
        pending[key] = std::move(value);
    }
    void Commit() {
        std::lock_guard lock(cacheMutex);
        auth::CheckCancelled(cancelled);
        for (auto& [key, value] : pending) RememberLocked(key, std::move(value));
    }

    auth::HttpResponse Send(const auth::HttpRequest& request, const auth::Cancelled&) const {
        auth::CheckCancelled(cancelled);
        if (!auth::ValidHttpRequest(request)) Fail();
        auth::HttpResponse response;
        try { response = transport(request, cancelled); }
        catch (...) { auth::CheckCancelled(cancelled); Fail("service"); }
        auth::CheckCancelled(cancelled);
        return response;
    }
    auth::Exchange Exchange() {
        return [this](const auth::HttpRequest& r, const auth::Cancelled& c) { return Send(r, c); };
    }
    Json Http(auth::HttpRequest request) {
        request.maxResponse = MaxResponse;
        const auto response = Send(request, cancelled);
        if (response.status == 401 || response.status == 403) Fail("expired");
        if (response.status < 200 || response.status >= 300) Fail("service");
        if (response.body.empty() || response.body.size() > request.maxResponse) Fail();
        const auto body = LegacyChineseCharset(response.contentType) ?
            DecodeLegacyChinese(response.body) : response.body;
        bool tooDeep = false;
        auto value = Json::parse(body, [&](int depth, Json::parse_event_t, Json&) {
            if (depth > 32) { tooDeep = true; return false; }
            return true;
        }, false);
        if (tooDeep) Fail();
        if (!value.is_object()) Fail();
        return value;
    }
    Json N(const char* path, Json data, bool weapi = true) {
        const auto value = auth::NeteaseCatalogRequest(path, data, weapi, secrets, cancelled, Exchange());
        const auto code = Number(value, {"/code"}, -1);
        if (code == 301 || code == 302 || code == 401 || code == 403) Fail("expired");
        if (code != 200) Fail(code == -1 ? "invalid-response" : "service");
        return value;
    }
    void Auth() const {
        if (provider == Provider::Netease && Text(secrets, {"/MUSIC_U"}).empty()) Fail("expired");
        if (provider == Provider::QQ && (Text(secrets, {"/credential/musicKey"}).empty() ||
            !Token(Text(secrets, {"/credential/uin"}), true))) Fail("expired");
        if (provider == Provider::Kugou && (Text(secrets, {"/token"}).empty() ||
            !Token(Text(secrets, {"/userid"}), true))) Fail("expired");
    }
    std::string User() {
        Auth();
        if (provider == Provider::QQ) return Text(secrets, {"/credential/uin"});
        if (provider == Provider::Kugou) return Text(secrets, {"/userid"});
        if (!leasedUserId.empty()) return leasedUserId;
        const auto profile = N("/api/nuser/account/get", Json::object());
        const auto id = Text(profile, {"/profile/userId", "/account/id"});
        if (!Token(id, true)) Fail();
        return id;
    }
    std::string QQCookie() const {
        const auto uin = Text(secrets, {"/credential/uin"});
        const auto key = Text(secrets, {"/credential/musicKey"});
        if (uin.empty() && key.empty()) return {};
        Auth();
        if (key.find_first_of(";,\r\n\t\"\\") != key.npos) Fail();
        return "uin=" + uin + "; qqmusic_uin=" + uin + "; qm_keyst=" + key + "; qqmusic_key=" + key;
    }
    Json Q(const char* module, const char* method, Json params, Json comm = nullptr,
           const char* requestKey = "req_0", bool raw = false, bool anonymous = false) {
        if (comm.is_null()) comm = {{"ct", 24}, {"cv", 0},
            {"uin", Text(secrets, {"/credential/uin"}).empty() ? "0" : Text(secrets, {"/credential/uin"})},
            {"authst", Text(secrets, {"/credential/musicKey"})}};
        auth::HttpRequest request{Provider::QQ, "POST", "https://u.y.qq.com/cgi-bin/musicu.fcg",
            {{"content-type", "application/json"}, {"referer", "https://y.qq.com/"},
             {"cookie", anonymous ? "" : QQCookie()}},
            Json{{"comm", comm}, {requestKey, {{"module", module}, {"method", method},
                {"param", params}}}}.dump(), MaxResponse};
        auto response = Http(std::move(request));
        for (const auto* object : {&response, &response[requestKey]}) {
            const auto code = Number(*object, {"/code"}, -1);
            if (code == 1000 || code == 104009 || code == 104013 ||
                code == 104400 || code == 104401) Fail("expired");
            if (code != 0) Fail(code == -1 ? "invalid-response" : "service");
        }
        if (!response[requestKey].contains("data") || !response[requestKey]["data"].is_object()) Fail();
        return raw ? response : response[requestKey]["data"];
    }
    Json QGet(const char* path, Params params) {
        std::string url = std::string("https://c.y.qq.com") + path;
        for (const auto& [key, value] : params)
            url += (url.find('?') == url.npos ? "?" : "&") + key + "=" + auth::UrlEncode(value);
        const auto response = Http({Provider::QQ, "GET", url,
            {{"referer", "https://y.qq.com/"}, {"cookie", QQCookie()}}, {}, MaxResponse});
        const auto code = Number(response, {"/code"}, -1);
        if (code != 0) Fail(code == 1000 ? "expired" : code == -1 ? "invalid-response" : "service");
        if (!At(response, "/data").is_object()) Fail();
        return response["data"];
    }
    Json K(const char* path, Params params = {}, Json body = nullptr, const char* router = nullptr) {
        auto response = auth::KugouCatalogRequest(path, std::move(params),
            body.is_null() ? "" : body.dump(), router, secrets, cancelled,
            [&](const auth::HttpRequest& original, const auth::Cancelled& stop) {
                auto request = original;
                if (request.url.find("https://gateway.kugou.com/v1/album_audio/lite?") == 0)
                    request.headers["kg-tid"] = "255";
                return Send(request, stop);
            });
        if (Number(response, {"/status"}, -1) != 1 ||
            Number(response, {"/error_code", "/errcode"}) != 0) Fail("service");
        return response;
    }
    Json LyricsPayload(Json body, double duration) {
        // Keep the native payload contract, but never deliver undecodable bytes as successful lyrics.
        const auto parsed = ParseOnlineLyrics(provider, body, duration);
        if (parsed.error.compare(0, 12, "unsupported-") == 0) Fail("unsupported");
        if (!parsed.error.empty()) Fail();
        if (parsed.lines.empty()) Fail("unavailable");
        return body;
    }
    Json KAnonymousSearch(const std::string& query) {
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto mid = auth::HashHex(std::to_string(millis));
        Params params = {{"sorttype", "0"}, {"keyword", query}, {"pagesize", std::to_string(limit)},
            {"page", std::to_string(offset / limit + 1)}, {"userid", "0"}, {"appid", "3116"}, {"token", ""},
            {"clienttime", std::to_string(millis / 1000)}, {"iscorrection", "1"}, {"uuid", "-"},
            {"mid", mid}, {"dfid", "-"}, {"clientver", "11070"}, {"platform", "AndroidFilter"}};
        const std::string salt = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA";
        std::string input = salt;
        for (const auto& [key, value] : params) input += key + "=" + value;
        params["signature"] = auth::HashHex(input + salt);
        std::string url = "https://complexsearch.kugou.com/v2/search/song";
        for (const auto& [key, value] : params)
            url += (url.find('?') == url.npos ? "?" : "&") + key + "=" + auth::UrlEncode(value);
        const auto result = Http({Provider::Kugou, "GET", url,
            {{"User-Agent", "Android14-1070-11070-201-0-SearchSong-wifi"},
             {"KG-Rec", "1"}, {"KG-RC", "1"}, {"KG-CLIENTTIMEMS", std::to_string(millis)},
             {"mid", mid}, {"x-router", "complexsearch.kugou.com"}}, {}, MaxResponse});
        const auto code = Number(result, {"/error_code"});
        if (code != 0 && code != 200) Fail("service");
        if (Number(result, {"/status"}, 1) == 0) Fail("service");
        return result;
    }
    Json Track(const Json& wrapped) {
        const Json* raw = &wrapped;
        for (const auto* path : {"/track_info", "/songInfo", "/songinfo", "/song"})
            if (At(wrapped, path).is_object()) { raw = &At(wrapped, path); break; }
        const auto& j = *raw;
        std::string id, title, artist, album, art;
        double duration = 0;
        Json extra = Json::object();
        if (provider == Provider::Netease) {
            id = Text(j, {"/id"});
            title = Text(j, {"/name"}, 1024);
            artist = Artists(j, {"/ar", "/artists"});
            album = Text(j, {"/al/name", "/album/name"}, 1024);
            art = Text(j, {"/al/picUrl", "/album/picUrl"}, 8192);
            duration = Number(j, {"/dt", "/duration"}) / 1000.0;
        } else if (provider == Provider::QQ) {
            id = Text(j, {"/mid", "/songmid", "/songMid"});
            title = Text(j, {"/name", "/songname", "/title"}, 1024);
            artist = Artists(j, {"/singer", "/singers"});
            album = Text(j, {"/album/name", "/album/title", "/albumname"}, 1024);
            const auto albumMid = Text(j, {"/album/mid", "/albummid"});
            art = Text(j, {"/coverUrl", "/cover_url", "/diss_cover", "/imgurl", "/picurl",
                "/picUrl", "/pic", "/avatar", "/album_pic"}, 8192);
            if (art.empty() && Token(albumMid))
                art = "https://y.gtimg.cn/music/photo_new/T002R300x300M000" + albumMid + ".jpg";
            const auto rawDuration = Number(j, {"/interval", "/duration"});
            duration = rawDuration > 10000 ? rawDuration / 1000.0 : rawDuration;
            auto media = Text(j, {"/file/media_mid", "/file/mediaMid"});
            if (media.empty()) media = id;
            if (!Token(media)) Fail();
            extra = {{"songId", Number(j, {"/songid", "/songId", "/id"})},
                {"songType", Number(j, {"/type", "/songtype"})}, {"mediaMid", media}};
            // Per-format file sizes say which encodings exist. QQ still signs
            // links for missing ones (live: size_hires 0 -> RS01 URL -> 404).
            const auto file = j.find("file");
            if (file != j.end() && file->is_object() && file->contains("size_flac"))
                extra["formats"] = {{"hires", Number(j, {"/file/size_hires"}) > 0},
                    {"lossless", Number(j, {"/file/size_flac"}) > 0},
                    {"high", Number(j, {"/file/size_320mp3"}) > 0}};
        } else {
            id = Text(j, {"/FileHash", "/fileHash", "/hash", "/Hash", "/audio_hash",
                "/audio_info/hash", "/audio_info/hash_128"});
            if (id.size() != 32) id = Text(j, {"/audio_info/hash", "/audio_info/hash_128"});
            title = Text(j, {"/SongName", "/songname", "/songName", "/ori_audio_name", "/OriSongName",
                "/FileName", "/name", "/song_name", "/filename",
                "/audio_name", "/audio_info/ori_audio_name", "/audio_info/audio_name",
                "/base/SongName", "/base/songname", "/base/songName", "/base/audio_name", "/base/audioName"}, 1024);
            artist = Artists(j, {"/Singers", "/singers", "/singerinfo", "/authors"});
            const auto split = title.find(" - ");
            if (split != title.npos) {
                if (artist.empty()) artist = title.substr(0, split);
                if (artist == title.substr(0, split)) title.erase(0, split + 3);
            }
            album = Text(j, {"/AlbumName", "/album_name", "/albumname", "/album_info/album_name",
                "/album_info/name", "/albuminfo/name", "/base/album_name", "/base/albumName"}, 1024);
            art = Text(j, {"/album_info/sizable_cover", "/trans_param/union_cover",
                "/audio_info/trans_param/union_cover", "/Image",
                "/image", "/img", "/sizable_cover", "/cover"}, 8192);
            const auto pos = art.find("{size}");
            if (pos != art.npos) art.replace(pos, 6, "400");
            const auto rawDuration = Number(j, {"/Duration", "/duration", "/timelen", "/timelength",
                "/timeLength", "/time_length", "/audio_info/duration", "/audio_info/timelength",
                "/audio_info/timelen", "/audio_info/duration_128"});
            duration = rawDuration >= 10000 ? rawDuration / 1000.0 : rawDuration;
            extra = {{"albumId", Number(j, {"/AlbumID", "/album_id", "/album_info/album_id", "/base/album_id"})},
                {"albumAudioId", Number(j, {"/AlbumAudioID", "/album_audio_id", "/album_audio/audio_id",
                    "/base/album_audio_id", "/base/albumAudioId"})},
                {"mixSongId", Number(j, {"/MixSongID", "/mixsongid", "/mix_song_id",
                    "/base/MixSongID", "/base/mixsongid", "/base/mixSongId"})},
                {"fileId", Number(j, {"/fileid", "/file_id"})}};
        }
        id = Id(Json{{"id", id}}, "/id", provider);
        title = Plain(title, 1024); artist = Plain(artist, 1024); album = Plain(album, 1024);
        if (title.find_first_not_of(' ') == title.npos || duration < 0 || duration > 7 * 24 * 3600) Fail();
        const auto stable = std::string(auth::Name(provider)) + ":" + id;
        const auto membershipBadge = MembershipBadge(wrapped, j, provider);
        auto artwork = Https(art, provider);
        if (artwork.size() > 2048) artwork.clear();
        Json track = {{"id", stable}, {"title", title}, {"artist", artist}, {"album", album},
            {"artwork", artwork}, {"duration", duration}, {"source", "online:" + stable},
            {"vipRequired", VipRequired(wrapped, j, provider)}};
        if (!membershipBadge.empty()) track["membershipBadge"] = membershipBadge;
        if (provider == Provider::QQ || provider == Provider::Kugou) {
            extra["title"] = title; extra["artist"] = artist; extra["album"] = album;
            extra["duration"] = duration;
            Remember(scope + "track:" + id, std::move(extra));
        }
        return track;
    }
    void Tracks(Json& page, const Json& list, std::size_t begin = 0) {
        if (!list.is_array()) Fail();
        std::set<std::string> seen;
        for (std::size_t i = begin; i < list.size() && i - begin < static_cast<std::size_t>(limit); ++i) {
            auth::CheckCancelled(cancelled);
            auto track = SafeTrack(list[i]);
            if (!track.is_null() && seen.insert(track["id"].get<std::string>()).second)
                page["tracks"].push_back(std::move(track));
        }
    }
    // One removed, user-uploaded or malformed row must not hide the rest of a
    // page; a cancelled request still aborts. Returns null for a skipped row.
    Json SafeTrack(const Json& row) {
        try { return Track(row); }
        catch (const std::runtime_error& error) {
            if (std::string_view(error.what()) == "cancelled") throw;
            auth::CheckCancelled(cancelled);
            return nullptr;
        }
    }
    Json SafeCollection(const Json& row, bool owned = false) {
        try { return Collection(row, owned); }
        catch (const std::runtime_error& error) {
            if (std::string_view(error.what()) == "cancelled") throw;
            auth::CheckCancelled(cancelled);
            return nullptr;
        }
    }
    Json Collection(const Json& j, bool owned = false, std::string type = "playlist") {
        std::string id, title, art, description;
        Json extra = Json::object();
        if (provider == Provider::Netease) {
            id = Text(j, {"/id"});
            title = Text(j, {"/name"}, 1024);
            art = Text(j, {"/coverImgUrl", "/picUrl"}, 8192);
        } else if (provider == Provider::QQ) {
            id = Text(j, {"/disstid", "/dissid", "/tid"});
            if (id.empty() || id == "0") id = Text(j, {"/dirid"});
            title = Text(j, {"/dissname", "/diss_name", "/name", "/title"}, 1024);
            art = Text(j, {"/coverUrl", "/cover_url", "/diss_cover", "/imgurl", "/picurl",
                "/picUrl", "/pic", "/avatar", "/album_pic", "/logo", "/cover"}, 8192);
            const auto albumMid = Text(j, {"/diss_cover_mid", "/cover_mid", "/albummid", "/albumMid"});
            if (art.empty() && Token(albumMid))
                art = "https://y.gtimg.cn/music/photo_new/T002R300x300M000" + albumMid + ".jpg";
            extra = {{"dissId", Number(j, {"/disstid", "/dissid", "/tid"})},
                {"dirId", Number(j, {"/dirid"})}, {"owned", owned}};
        } else {
            if (Number(j, {"/source"}) == 2 || j.contains("musiclib_id")) type = "album";
            id = type == "album" ? Text(j, {"/musiclib_id", "/album_id", "/list_create_listid"}) :
                Text(j, {"/global_collection_id", "/globalCollectionId", "/specialid", "/id"});
            title = Text(j, {"/name", "/listname", "/specialname", "/title"}, 1024);
            art = Text(j, {"/pic", "/img", "/image", "/sizable_cover", "/cover"}, 8192);
            const auto pos = art.find("{size}");
            if (pos != art.npos) art.replace(pos, 6, "400");
            extra = {{"listId", Number(j, {"/listid", "/list_id"})}, {"owned", owned},
                {"type", type}};
        }
        id = Id(Json{{"id", id}}, "/id", provider, false);
        description = Plain(Text(j, {"/description", "/desc", "/intro"}, 65536), 2000);
        title = Plain(title, 1024);
        if (title.find_first_not_of(' ') == title.npos) Fail();
        if (provider != Provider::Netease) {
            // A public recommendation for the same ID must not erase account-owned routing metadata.
            const auto existing = Recall(scope + "collection:" + id);
            if (owned || !Flag(existing, "/owned")) Remember(scope + "collection:" + id, extra);
            if (owned && ((provider == Provider::QQ && Number(extra, {"/dirId"}) == 201) ||
                (provider == Provider::Kugou && type == "playlist" && !j.contains("musicLibId") &&
                    Number(extra, {"/listId"}) == 2))) {
                auto liked = extra;
                liked["id"] = id;
                if (provider == Provider::Kugou) liked["global_collection_id"] = id;
                Remember(scope + "likedCollection", std::move(liked));
            }
        }
        auto artwork = Https(art, provider);
        if (artwork.size() > 2048) artwork.clear();
        Json collection = {{"id", id}, {"providerId", auth::Name(provider)}, {"type", type},
            {"title", title}, {"artwork", artwork}, {"owned", owned},
            {"description", description}};
        if (provider == Provider::QQ && owned && Number(extra, {"/dirId"}) == 201)
            collection["isLiked"] = true;
        return collection;
    }
    Json NDetails(const Json& ids) {
        Json c = Json::array();
        for (const auto& id : ids) {
            auto text = id.is_object() ? Text(id, {"/id"}) :
                Text(Json{{"id", id}}, {"/id"});
            if (!Token(text, true)) Fail();
            c.push_back({{"id", std::stoll(text)}});
        }
        if (c.empty()) return Json::array();
        return List(N("/api/v3/song/detail", {{"c", c.dump()}}, false), {"/songs"});
    }
    Json NPlaylist(const std::string& id) {
        // Historical trackIds + song/detail pagination, without requesting all full song objects.
        auto body = N("/api/v6/playlist/detail", {{"id", id}, {"n", 0}, {"s", 0}}, false);
        const auto& ids = List(body, {"/playlist/trackIds"});
        Json selected = Json::array();
        for (std::size_t i = static_cast<std::size_t>(offset); i < ids.size() &&
            selected.size() < static_cast<std::size_t>(limit); ++i) selected.push_back(ids[i]);
        auto page = Page();
        Tracks(page, NDetails(selected));
        page["more"] = static_cast<std::size_t>(offset + limit) < ids.size();
        return page;
    }
    Json Netease(std::string_view action, const Json& args) {
        auto page = Page();
        if (action == "home" || action == "explore") {
            if (action == "home") {
                // Recommendations are optional: their failure must not hide the account's own playlists.
                try {
                    const auto body = N("/api/personalized/playlist", {{"limit", limit}, {"total", true}, {"n", 1000}});
                    for (const auto& item : List(body, {"/result"})) {
                        if (page["recommendations"].size() == static_cast<std::size_t>(limit)) break;
                        auto collection = SafeCollection(item);
                        if (!collection.is_null()) page["recommendations"].push_back(std::move(collection));
                    }
                } catch (...) {
                    auth::CheckCancelled(cancelled);
                    if (Text(secrets, {"/MUSIC_U"}).empty()) throw;
                }
                if (!Text(secrets, {"/MUSIC_U"}).empty()) {
                    // The whole account library arrives with the first home page (created
                    // lists first, then subscribed); the page has no separate paging for it.
                    const auto uid = User();
                    std::set<std::string> seen;
                    for (int start = 0; offset == 0 && start < static_cast<int>(kMaxLibraryCollections); start += 50) {
                        const auto library = N("/api/user/playlist", {{"uid", uid}, {"offset", start},
                            {"limit", 50}, {"includeVideo", true}});
                        const auto& list = List(library, {"/playlist"});
                        for (const auto& item : list) {
                            if (page["playlists"].size() == kMaxLibraryCollections) break;
                            auto collection = SafeCollection(item, Text(item, {"/creator/userId"}) == uid);
                            if (!collection.is_null() && seen.insert(collection["id"].get<std::string>()).second)
                                page["playlists"].push_back(std::move(collection));
                        }
                        if (!Flag(library, "/more") || list.empty()) break;
                    }
                    // Home lists the account's daily recommended songs, like
                    // the QQ home lists its first recommendation's tracks.
                    try {
                        const auto daily = N("/api/v3/discovery/recommend/songs", Json::object());
                        Tracks(page, List(daily, {"/data/dailySongs", "/recommend"}));
                    } catch (...) { auth::CheckCancelled(cancelled); }
                }
                if (page["tracks"].empty() && !page["recommendations"].empty()) {
                    // Signed out, or no daily list: show the first recommended playlist.
                    try {
                        const auto first = Text(page["recommendations"][0], {"/id"});
                        page["tracks"] = NPlaylist(first)["tracks"];
                    } catch (...) { auth::CheckCancelled(cancelled); }
                }
            } else {
                const auto body = N("/api/playlist/list", {{"cat", u8"\u5168\u90e8"},
                    {"order", "hot"}, {"limit", limit}, {"offset", offset}, {"total", true}});
                for (const auto& item : List(body, {"/playlists"})) {
                    if (page["playlists"].size() == static_cast<std::size_t>(limit)) break;
                    auto collection = SafeCollection(item);
                    if (!collection.is_null()) page["playlists"].push_back(std::move(collection));
                }
                page["more"] = Flag(body, "/more");
            }
        } else if (action == "playlist") return NPlaylist(Id(args, "/id", provider, false));
        else if (action == "search") {
            const auto body = N("/api/cloudsearch/pc", {{"s", InputText(args, "/query", 512)},
                {"type", 1}, {"limit", limit}, {"offset", offset}, {"total", true}}, false);
            const auto total = Number(body, {"/result/songCount"}, -1);
            if (total < 0) Fail();
            if (total) Tracks(page, List(body, {"/result/songs"}));
            page["more"] = offset + limit < total;
        } else if (action == "favorites") {
            Auth();
            const auto ids = LikedSnapshot(offset == 0);
            if (!ids.is_array()) Fail("unavailable");
            Json selected = Json::array();
            for (std::size_t i = static_cast<std::size_t>(offset); i < ids.size() &&
                selected.size() < static_cast<std::size_t>(limit); ++i)
                selected.push_back(Id(Json{{"id", ids[i]}}, "/id", provider));
            Tracks(page, NDetails(selected));
            page["more"] = static_cast<std::size_t>(offset + limit) < ids.size();
        } else if (action == "recent") {
            Auth();
            if (offset >= 100) Fail("unsupported");
            const auto body = N("/api/play-record/song/list", {{"limit", std::min(100, offset + limit)}});
            Json songs = Json::array();
            for (const auto& item : List(body, {"/data/list"})) songs.push_back(At(item, "/data"));
            Tracks(page, songs, static_cast<std::size_t>(offset));
            const auto total = Number(body, {"/data/total"}, static_cast<std::int64_t>(songs.size()));
            page["more"] = offset + limit < std::min<std::int64_t>(100, total);
        } else if (action == "like") {
            Auth();
            N("/api/radio/like", {{"alg", "itembased"}, {"trackId", Id(args, "/id", provider)},
                {"like", args.at("value")}, {"time", "3"}});
            return {{"liked", args.at("value")}};
        } else if (action == "createPlaylist") {
            Auth();
            auto body = N("/api/playlist/create", {{"name", InputText(args, "/title", 256)},
                {"privacy", "0"}, {"type", "NORMAL"}});
            page["playlists"].push_back(Collection(body.at("playlist"), true));
        } else if (action == "addToPlaylist") {
            Auth();
            N("/api/playlist/manipulate/tracks", {{"op", "add"},
                {"pid", Id(args, "/id", provider, false)}, {"imme", "true"},
                {"trackIds", Json::array({Id(args, "/trackId", provider)}).dump()}}, false);
            return {{"ok", true}};
        } else if (action == "resolve") {
            const auto id = Id(args, "/id", provider);
            const auto quality = args.value("quality", std::string("standard"));
            const auto body = N("/api/song/enhance/player/url/v1",
                {{"ids", Json::array({id}).dump()}, {"level", quality == "high" ? "exhigh" : quality},
                 {"encodeType", "flac"}}, false);
            const auto& values = List(body, {"/data"});
            if (values.empty()) Fail("unavailable");
            const auto& song = values[0];
            if (Text(song, {"/id"}) != id) Fail();
            if (Number(song, {"/code"}, 200) != 200 ||
                (!At(song, "/freeTrialInfo").is_null() && At(song, "/freeTrialInfo") != "null"))
                Fail("unavailable");
            auto url = Https(Text(song, {"/url"}, 8192), provider, true);
            if (url.empty()) Fail("unavailable");
            return AudioSource(url, ReportedQuality(song), Number(song, {"/br"}));
        } else if (action == "lyrics") {
            return N("/api/song/lyric/v1", {{"id", Id(args, "/id", provider)}, {"cp", false},
                {"tv", 0}, {"lv", 0}, {"rv", 0}, {"kv", 0}, {"yv", 0}, {"ytv", 0}, {"yrv", 0}}, false);
        } else Fail("unsupported");
        return page;
    }

    Json QQMetadata(const std::string& id) {
        auto song = Recall(scope + "track:" + id);
        if (song.empty() || !Number(song, {"/songId"})) {
            const auto body = Q("music.pf_song_detail_svr", "get_song_detail_yqq", {{"song_mid", id}});
            const auto track = Track(body.at("track_info"));
            if (track["id"] != "qq:" + id) Fail();
            song = Recall(scope + "track:" + id);
        }
        return song;
    }
    // whole: return every merged collection (home page) instead of the offset/limit slice.
    Json QLibrary(bool loadLikedCover = false, bool whole = false) {
        libraryFailure.clear();
        const auto uid = User();
        Json merged = Json::array();
        std::set<std::string> seen;
        // ponytail: scan at most 1000 rows per source; merge before slicing, never drop a stream's tail.
        for (const bool owned : {true, false}) {
            try {
                int start = 0;
                for (int batch = 0; ; ++batch) {
                    if (batch == 10) break;
                    const auto body = owned ? QGet("/rsc/fcgi-bin/fcg_user_created_diss",
                        {{"hostUin", "0"}, {"hostuin", uid}, {"sin", std::to_string(start)},
                         {"size", "100"}, {"g_tk", "5381"}, {"loginUin", uid},
                         {"format", "json"}, {"platform", "yqq.json"}, {"needNewCode", "0"}}) :
                        QGet("/fav/fcgi-bin/fcg_get_profile_order_asset.fcg",
                        {{"ct", "20"}, {"cid", "205360956"}, {"userid", uid}, {"reqtype", "3"},
                         {"sin", std::to_string(start)}, {"ein", std::to_string(start + 99)}, {"format", "json"}});
                    const auto& list = owned ? List(body, {"/disslist"}) : List(body, {"/cdlist", "/disslist"});
                    if (list.size() > 100) Fail();
                    for (const auto& item : list) {
                        const auto dir = Number(item, {"/dirid"});
                        if (owned && Number(item, {"/tid"}) == 0 && (dir == 205 || dir == 206)) continue;
                        auto collection = SafeCollection(item, owned);
                        if (!collection.is_null() && seen.insert(collection["id"].get<std::string>()).second)
                            merged.push_back(std::move(collection));
                    }
                    start += static_cast<int>(list.size());
                    const auto total = Number(body, {"/total", "/totalnum"}, start + (list.size() == 100 ? 1 : 0));
                    if (start >= total) break;
                    if (list.empty()) Fail();
                }
            } catch (const std::runtime_error& error) {
                // QQ exposes the two private-library sources independently. One expired or
                // malformed source must not hide the other source or public recommendations.
                const std::string_view code(error.what());
                if (code == "unsupported" || code == "cancelled") throw;
                auth::CheckCancelled(cancelled);
                if (libraryFailure.empty()) libraryFailure = std::string(code);
            }
        }
        auto page = Page();
        const std::size_t first = whole ? 0 : static_cast<std::size_t>(offset);
        const std::size_t end = whole ? kMaxLibraryCollections : static_cast<std::size_t>(offset + limit);
        for (std::size_t i = first; i < merged.size() && i < end; ++i) {
            if (loadLikedCover && Flag(merged[i], "/isLiked")) {
                // The built-in list's cover is its newest song, not QQ's library placeholder.
                merged[i]["artwork"] = "";
                try {
                    auto cover = *this;
                    cover.offset = 0; cover.limit = 1;
                    const auto tracks = cover.QPlaylist(merged[i]["id"].get<std::string>());
                    if (!tracks["tracks"].empty()) merged[i]["artwork"] = tracks["tracks"][0]["artwork"];
                    pending = std::move(cover.pending);
                } catch (...) { auth::CheckCancelled(cancelled); }
            }
            page["playlists"].push_back(merged[i]);
        }
        page["more"] = end < merged.size();
        return page;
    }
    std::string QLiked() {
        Auth();
        auto liked = Recall(scope + "likedCollection");
        if (liked.empty()) {
            auto library = *this;
            library.offset = 0; library.limit = 100;
            library.QLibrary();
            liked = library.Recall(scope + "likedCollection");
            pending = std::move(library.pending);
        }
        return Id(liked, "/id", provider, false);
    }
    Json QPlaylistBody(const std::string& id) {
        const auto meta = Recall(scope + "collection:" + id);
        auto diss = Number(meta, {"/dissId"}, -1);
        if (diss < 0) {
            if (!Token(id, true)) Fail();
            diss = id == "201" ? 0 : Number(Json{{"id", id}}, {"/id"});
        }
        // Historical bridge uses dirid only for a built-in folder with no diss ID.
        const auto dir = diss == 0 ? Number(meta, {"/dirId"}, id == "201" ? 201 : 0) : 0;
        return Q("music.srfDissInfo.DissInfo", "CgiGetDiss",
            {{"disstid", diss}, {"dirid", dir}, {"tag", 1}, {"song_begin", offset},
             {"song_num", limit}, {"userinfo", 1}, {"orderlist", 1}});
    }
    Json QPlaylist(const std::string& id) {
        const auto body = QPlaylistBody(id);
        auto page = Page();
        const auto& list = List(body, {"/songlist"});
        Tracks(page, list);
        // song_begin is positional and counts rows QQ withholds (removed songs), so
        // a short page is not the end; the page advances by the requested size.
        page["more"] = offset + limit <
            Number(body, {"/total_song_num"}, offset + static_cast<int>(list.size()));
        return page;
    }
    void QWriteResult(const Json& data) {
        const auto code = Number(data, {"/retCode", "/retcode"}, -1);
        if (code == -1 && !At(data, "/result").is_object()) Fail();
        if (code > 0) Fail("service");
    }
    Json QQ(std::string_view action, const Json& args) {
        auto page = Page();
        if (action == "home" || action == "explore") {
            const bool signedIn = !Text(secrets, {"/credential/musicKey"}).empty();
            const auto* target = action == "home" ? "recommendations" : "playlists";
            try {
                const auto body = QGet("/splcloud/fcgi-bin/fcg_get_diss_by_tag.fcg",
                    {{"format", "json"}, {"categoryId", "10000000"}, {"sortId", "5"},
                     {"sin", std::to_string(offset)}, {"ein", std::to_string(offset + limit - 1)}});
                for (const auto& item : List(body, {"/list"})) {
                    if (page[target].size() == static_cast<std::size_t>(limit)) break;
                    auto collection = SafeCollection(item);
                    if (!collection.is_null()) page[target].push_back(std::move(collection));
                }
                page["more"] = offset + limit < Number(body, {"/sum"}, offset + static_cast<int>(page[target].size()));
            } catch (...) {
                // Recommendations must not hide a signed-in account's own playlists.
                auth::CheckCancelled(cancelled);
                if (action != "home" || !signedIn) throw;
            }
            if (action == "home" && signedIn) {
                // The whole account library arrives with the first home page.
                if (offset == 0) {
                    auto library = QLibrary(true, true);
                    page["playlists"] = std::move(library["playlists"]);
                    // A failed private source with an empty result is not "no
                    // playlists"; report it so the page can offer a retry
                    // instead of rendering a blank library.
                    if (page["playlists"].empty() && !libraryFailure.empty())
                        Fail(libraryFailure.c_str());
                }
                if (offset == 0) {
                    Remember(scope + "homeCollection", page["recommendations"].empty() ? Json::object() :
                        Json{{"id", page["recommendations"][0]["id"]}});
                }
                // MusicHomeNextLayout focuses the first recommendation and reads that collection's tracks.
                const auto homeId = Text(Recall(scope + "homeCollection"), {"/id"});
                if (!homeId.empty()) {
                    const auto songs = QPlaylist(homeId);
                    page["tracks"] = songs["tracks"];
                    page["more"] = page["more"].get<bool>() || songs["more"].get<bool>();
                }
            }
        } else if (action == "search") {
            const auto body = Q("music.search.SearchCgiService", "DoSearchForQQMusicDesktop",
                {{"search_type", 0}, {"query", InputText(args, "/query", 512)},
                 {"page_num", offset / limit + 1}, {"num_per_page", limit}, {"sin", offset}, {"grp", 1}});
            const auto& list = List(body, {"/body/item_song", "/body/song/list"});
            Tracks(page, list);
            page["more"] = offset + limit < Number(body, {"/meta/sum", "/total", "/totalNum"},
                offset + static_cast<int>(list.size()) + (list.size() >= static_cast<std::size_t>(limit) ? 1 : 0));
        } else if (action == "playlist") return QPlaylist(Id(args, "/id", provider, false));
        else if (action == "favorites") return QPlaylist(QLiked());
        else if (action == "recent") Fail("unsupported");
        else if (action == "createPlaylist") {
            Auth();
            const auto result = Q("music.musicasset.PlaylistDetailWrite", "CreateFolder",
                {{"dirid", 0}, {"title", InputText(args, "/title", 256)}});
            if (Number(result, {"/retCode", "/retcode"}) != 0) Fail("service");
            return {{"ok", true}};
        } else if (action == "like" || action == "addToPlaylist") {
            Auth();
            const auto id = Id(args, action == "like" ? "/id" : "/trackId", provider);
            const auto song = QQMetadata(id);
            const auto songId = Number(song, {"/songId"});
            if (!songId) Fail("unavailable");
            Json comm = nullptr;
            std::int64_t dir = 201, tid = 0;
            const bool liked = action == "like" ? args.at("value").get<bool>() : true;
            if (action == "like") comm = auth::QQCatalogComm(secrets, false, cancelled, Exchange());
            else {
                const auto meta = Recall(scope + "collection:" + Id(args, "/id", provider, false));
                tid = Number(meta, {"/dissId"}); dir = Number(meta, {"/dirId"});
                if (!Flag(meta, "/owned") || !tid || dir == 201) Fail("unavailable");
            }
            const auto result = Q("music.musicasset.PlaylistDetailWrite", liked ? "AddSonglist" : "DelSonglist",
                {{"dirId", dir}, {"tid", tid}, {"bFmtUtf8", true},
                 {"v_songInfo", Json::array({{{"songId", songId},
                    {"songType", liked ? Number(song, {"/songType"}) : 0}}})}}, comm);
            QWriteResult(result);
            return action == "like" ? Json{{"liked", liked}} : Json{{"ok", true}};
        } else if (action == "resolve") {
            Auth();
            const auto id = Id(args, "/id", provider);
            const auto song = QQMetadata(id);
            auto comm = auth::QQCatalogComm(secrets, true, cancelled, Exchange());
            const auto mid = Text(song, {"/mediaMid"});
            if (!Token(mid)) Fail();
            const auto quality = args.value("quality", std::string("standard"));
            // Unknown formats (listings without file sizes) are still tried.
            const auto has = [&](const char* format) {
                const auto formats = song.find("formats");
                return formats == song.end() || !formats->is_object() || formats->value(format, true);
            };
            Json filenames = Json::array();
            if (quality == "hires" && has("hires")) filenames.push_back("RS01" + mid + ".flac");
            if ((quality == "hires" || quality == "lossless") && has("lossless")) filenames.push_back("F000" + mid + ".flac");
            if (quality != "standard" && has("high")) filenames.push_back("M800" + mid + ".mp3");
            filenames.push_back("C400" + mid + ".m4a");
            filenames.push_back("M500" + mid + ".mp3");
            const auto body = Q("vkey.GetVkeyServer", "CgiGetVkey",
                {{"uin", User()}, {"filename", filenames},
                 {"guid", std::to_string(10000000 + std::stoul(auth::RandomHex(4), nullptr, 16) % 90000000)},
                 {"songmid", Json::array_t(filenames.size(), Json(id))},
                 {"songtype", Json::array_t(filenames.size(), Json(0))},
                 {"loginflag", 1}, {"platform", "20"}}, comm);
            const auto ret = Number(body, {"/retcode"});
            if (ret == 104009 || ret == 104013) Fail("expired");
            if (ret) Fail("unavailable");
            auto results = List(body, {"/midurlinfo"});
            if (results.size() > 16) Fail();
            const auto rank = [&](const Json& item) {
                const auto actual = QQQuality(Text(item, {"/purl"}, 8192));
                for (std::size_t i = 0; i < filenames.size(); ++i)
                    if (!actual.empty() && actual == QQQuality(filenames[i].get<std::string>())) return i;
                return filenames.size();
            };
            std::stable_sort(results.begin(), results.end(), [&](const auto& a, const auto& b) {
                return rank(a) < rank(b);
            });
            for (const auto& item : results) {
                const auto result = Number(item, {"/result"});
                if (result == 104009 || result == 104013) Fail("expired");
                if (result) continue;
                const auto path = Text(item, {"/purl"}, 8192);
                if (path.empty()) continue;
                const auto actual = QQQuality(path);
                const auto bitrate = actual == "high" ? 320000 : 0;
                if (!Text(item, {"/songmid"}).empty() && Text(item, {"/songmid"}) != id) Fail();
                if (path.find("://") != path.npos) {
                    const auto url = Https(path, provider, true);
                    if (!url.empty()) return AudioSource(url, actual, bitrate);
                } else {
                    if (path.find_first_of("\\@#\r\n") != path.npos || path.find("..") != path.npos ||
                        path.compare(0, 2, "//") == 0) continue;
                    for (const auto& sip : List(body, {"/sip"})) {
                        if (!sip.is_string()) continue;
                        auto base = Https(sip.get<std::string>(), provider, true);
                        if (base.empty()) continue;
                        if (base.back() != '/') base += '/';
                        const auto url = Https(base + (path.front() == '/' ? path.substr(1) : path), provider, true);
                        if (!url.empty()) return AudioSource(url, actual, bitrate);
                    }
                }
            }
            Fail("unavailable");
        } else if (action == "lyrics") {
            const auto id = Id(args, "/id", provider);
            const auto song = Recall(scope + "track:" + id);
            // yt-dlp 2025.09.26 QQMusicIE: songMID alone selects public Base64 LRC.
            // Do not guess crypt/qrc flags or require private songId/device metadata.
            return LyricsPayload(Q("music.musichallSong.PlayLyricInfo", "GetPlayLyricInfo",
                {{"songMID", id}}, {{"ct", 24}, {"cv", 0}, {"format", "json"}, {"uin", 0}},
                "request", true, true),
                song.value("duration", 0.0));
        } else Fail("unsupported");
        return page;
    }

    const Json& KList(const Json& j) {
        std::deque<const Json*> queue{&j};
        // Bounded breadth-first traversal of the historical envelope keys, never arbitrary recursion.
        for (int visited = 0; !queue.empty() && visited < 64; ++visited) {
            const auto* current = queue.front(); queue.pop_front();
            if (current->is_array()) return *current;
            if (!current->is_object()) continue;
            for (const auto* key : {"info", "lists", "list", "songs", "song_list", "songlist", "songList",
                "audios", "items", "records", "song_info", "data", "result", "response", "body"})
                if (current->contains(key)) queue.push_back(&current->at(key));
        }
        Fail();
    }
    Json KLibraryPage(int page, int pageSize) {
        if (page < 1 || pageSize < 1) Fail("unsupported");
        const auto uid = User(), token = Text(secrets, {"/token"});
        return K("/v7/get_all_list", {{"plat", "1"}, {"userid", uid}, {"token", token}},
            {{"userid", uid}, {"token", token}, {"total_ver", 979}, {"type", 2},
             {"page", page}, {"pagesize", pageSize}}, "cloudlist.service.kugou.com");
    }
    Json KLibrary() {
        if (offset % limit) Fail("unsupported");
        return KLibraryPage(offset / limit + 1, limit);
    }
    Json KPlaylistBody(const std::string& id) {
        return K("/pubsongs/v2/get_other_list_file_nofilt",
            {{"area_code", "1"}, {"begin_idx", std::to_string(offset)}, {"plat", "1"}, {"type", "1"},
             {"mode", "1"}, {"personal_switch", "1"}, {"extend_fields", "abtags,hot_cmt,popularization"},
             {"pagesize", std::to_string(limit)}, {"global_collection_id", id}});
    }
    Json KPlaylist(const std::string& id) {
        const auto body = KPlaylistBody(id);
        auto page = Page();
        const auto& list = KList(body);
        Tracks(page, list);
        page["more"] = offset + limit < Number(body, {"/data/count", "/data/total", "/data/total_count"},
            offset + static_cast<int>(list.size()) + (list.size() >= static_cast<std::size_t>(limit) ? 1 : 0));
        return page;
    }
    Json KAlbum(const std::string& id) {
        if (!Token(id, true)) Fail();
        auto page = Page();
        const int end = offset + limit;
        // The historical album endpoint caps pages at 30; cover arbitrary UI slices with at most five calls.
        for (int start = offset / 30 * 30; start < end; start += 30) {
            const auto body = K("/v1/album_audio/lite", {},
                {{"album_id", id}, {"is_buy", ""}, {"page", start / 30 + 1}, {"pagesize", 30}},
                "openapi.kugou.com");
            const auto& rows = KList(body);
            if (rows.size() > 30) Fail();
            const auto total = Number(body, {"/data/total", "/total", "/data/total_count", "/total_count", "/data/count"},
                start + static_cast<int>(rows.size()) + (rows.size() == 30 ? 1 : 0));
            for (int i = std::max(0, offset - start); i < static_cast<int>(rows.size()) && start + i < end; ++i) {
                auto track = SafeTrack(rows[i]);
                if (!track.is_null()) page["tracks"].push_back(std::move(track));
            }
            page["more"] = end < total;
            if (start + static_cast<int>(rows.size()) >= total) break;
            if (rows.size() < 30) Fail();
        }
        return page;
    }
    Json KCard(const KugouCard& card, bool refresh = false) {
        const auto key = scope + "card:" + card.id;
        auto cached = Recall(key);
        if (!refresh && cached.contains("tracks")) return cached;
        const auto body = K("/youth/v1/song/single_card_recommend",
            {{"card_id", card.id}, {"area_code", "1"}, {"platform", "ops"},
             {"module_id", "1"}, {"ver", "v2"}, {"pagesize", "30"}},
            {{"tagid", ""}, {"u_info", ""}, {"source_mixsong", ""}});
        const auto& rows = KList(body);
        if (rows.size() > 30) Fail();
        cached = {{"tracks", Json::array()}};
        std::set<std::string> seen;
        for (const auto& row : rows) {
            auto track = SafeTrack(row);
            if (!track.is_null() && seen.insert(track["id"].get<std::string>()).second)
                cached["tracks"].push_back(std::move(track));
        }
        auto title = Text(body, {"/data/card_name", "/data/cardName", "/data/module_name",
            "/data/moduleName", "/data/title", "/data/name"}, 1024);
        cached["collection"] = {{"id", std::string("kugou-card-") + card.id}, {"providerId", "kugou"},
            {"type", "playlist"}, {"title", title.empty() ? card.title : title},
            {"artwork", cached["tracks"].empty() ? Json("") : cached["tracks"][0]["artwork"]},
            {"owned", false}, {"description", Text(body, {"/data/description", "/data/desc",
                "/data/subtitle", "/data/sub_title"})}};
        Remember(key, cached);
        return cached;
    }
    Json KRecent() {
        const auto uid = User();
        if (offset + limit > 1000) Fail("unsupported");
        Json songs = Json::array(), cursor;
        std::set<std::string> seen, cursors;
        // ponytail: replay the server's opaque bp cursor, at most ten batches / 1000 rows per action.
        int scanned = 0;
        for (int batch = 0; batch < 10; ++batch) {
            Json data = {{"token", Text(secrets, {"/token"})}, {"userid", uid},
                {"source_classify", "app"}, {"to_subdivide_sr", 1}};
            if (!cursor.is_null()) data["bp"] = cursor;
            const auto body = K("/playhistory/v1/get_songs", {}, data);
            const auto& rows = KList(body);
            scanned += static_cast<int>(rows.size());
            if (scanned > 1000) Fail("unsupported");
            for (const auto& row : rows) {
                auto track = SafeTrack(row);
                if (!track.is_null() && seen.insert(track["id"].get<std::string>()).second) songs.push_back(std::move(track));
            }
            cursor = At(body, "/data/bp");
            if (cursor.is_null()) cursor = At(body, "/bp");
            const bool more = !cursor.is_null() && cursor != "" && cursor != 0 &&
                cursor != false && cursor != Json::object() && cursor != Json::array();
            if (more && (cursor.dump().size() > 4096 || !cursors.insert(cursor.dump()).second)) Fail();
            if (!more || songs.size() >= static_cast<std::size_t>(offset + limit)) {
                auto page = Page();
                for (std::size_t i = offset; i < songs.size() && i < static_cast<std::size_t>(offset + limit); ++i)
                    page["tracks"].push_back(songs[i]);
                page["more"] = more || songs.size() > static_cast<std::size_t>(offset + limit);
                return page;
            }
            if (rows.empty()) Fail();
        }
        Fail("unsupported");
    }
    Json KLiked() {
        Auth();
        const auto cached = Recall(scope + "likedCollection");
        if (!cached.empty()) return cached;
        auto library = *this;
        library.limit = 100;
        // Same bounded library scan as QQ; titles never identify the built-in list.
        for (int batch = 0; batch < 10; ++batch) {
            library.offset = batch * 100;
            const auto body = library.KLibrary();
            const auto& rows = KList(body);
            if (rows.size() > 100) Fail();
            for (const auto& item : rows) {
                if (Number(item, {"/listid", "/list_id"}) != 2 ||
                    Number(item, {"/type"}) != 0 || Number(item, {"/source"}) == 2 ||
                    item.contains("musiclib_id") || item.contains("musicLibId")) continue;
                Collection(item, true);
                return Recall(scope + "likedCollection");
            }
            const auto end = library.offset + static_cast<int>(rows.size());
            const auto total = Number(body, {"/data/total", "/data/count"}, end + (rows.size() == 100 ? 1 : 0));
            if (end >= total) break;
            if (rows.empty()) Fail();
        }
        Fail("unavailable");
    }
    Params KWriteParams() {
        return {{"last_time", std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())},
            {"last_area", "gztx"}, {"userid", User()}, {"token", Text(secrets, {"/token"})}};
    }
    Json KLyric(const char* path, Params params, bool defaults) {
        if (defaults) {
            params["dfid"] = Text(secrets, {"/dfid"}).empty() ? "-" : Text(secrets, {"/dfid"});
            params["mid"] = Text(secrets, {"/KUGOU_API_MID"}).empty() ? "-" : Text(secrets, {"/KUGOU_API_MID"});
            params["uuid"] = "-";
            params["appid"] = "3116";
            params["clientver"] = "11440";
            params["clienttime"] = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            if (!Text(secrets, {"/token"}).empty()) params["token"] = Text(secrets, {"/token"});
            if (!Text(secrets, {"/userid"}).empty()) params["userid"] = Text(secrets, {"/userid"});
        }
        // Historical notSign is not notSignature: the package signs both lyric requests.
        const std::string salt = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA";
        auto input = salt;
        for (const auto& [key, value] : params) input += key + "=" + value;
        params["signature"] = auth::HashHex(input + salt);
        std::string url = std::string("https://lyrics.kugou.com") + path;
        for (const auto& [key, value] : params)
            url += (url.find('?') == url.npos ? "?" : "&") + key + "=" + auth::UrlEncode(value);
        Params headers = {{"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
            {"dfid", Text(secrets, {"/dfid"}).empty() ? "-" : Text(secrets, {"/dfid"})},
            {"mid", Text(secrets, {"/KUGOU_API_MID"}).empty() ? "undefined" : Text(secrets, {"/KUGOU_API_MID"})},
            {"kg-rc", "1"}, {"kg-rec", "1"}, {"kg-thash", "5d816a0"},
            {"kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"}};
        if (defaults) headers["clienttime"] = params.at("clienttime");
        const auto result = Http({Provider::Kugou, "GET", url,
            std::move(headers), {}, MaxResponse});
        if (Number(result, {"/status"}, 1) == 0 ||
            Number(result, {"/error_code", "/errcode"}) != 0) Fail("service");
        return result;
    }
    std::int64_t KLikedFileId(std::int64_t listId, const std::string& id) {
        // ponytail: at most 1000 liked rows; report unavailable beyond this bounded scan.
        for (int page = 1; page <= 10; ++page) {
            const auto body = K("/v4/get_list_all_file", {},
                {{"listid", std::to_string(listId)}, {"userid", User()}, {"area_code", 1},
                 {"show_relate_goods", 0}, {"pagesize", 100}, {"allplatform", 1},
                 {"show_cover", 1}, {"type", 0}, {"token", Text(secrets, {"/token"})}, {"page", page}},
                "cloudlist.service.kugou.com");
            const auto& list = KList(body);
            for (const auto& row : list) {
                const auto hash = Text(row, {"/hash", "/FileHash", "/audio_info/hash"});
                if (hash.empty()) continue;
                if (Id(Json{{"id", hash}}, "/id", provider) == id)
                    return Number(row, {"/fileid", "/file_id"});
            }
            if (list.size() < 100) break;
        }
        Fail("unavailable");
    }
    Json Kugou(std::string_view action, const Json& args) {
        auto page = Page();
        if (action == "recent") return KRecent();
        if (action == "home" || action == "explore") {
            std::set<std::string> seen;
            const int first = action == "home" ? 0 : offset;
            std::string cardError;
            for (int i = first; i < 6 && i < first + limit; ++i) {
                // One unavailable recommendation card must not hide the others or the library.
                Json card;
                try { card = KCard(KugouCards[i], offset == 0); }
                catch (const std::runtime_error& error) {
                    if (std::string_view(error.what()) == "cancelled") throw;
                    auth::CheckCancelled(cancelled);
                    cardError = error.what();
                    continue;
                }
                page["recommendations"].push_back(card["collection"]);
                for (const auto& track : card["tracks"]) {
                    if (page["tracks"].size() == static_cast<std::size_t>(limit)) break;
                    if (seen.insert(track["id"].get<std::string>()).second) page["tracks"].push_back(track);
                }
            }
            page["more"] = action == "explore" && offset + limit < 6;
            // Every card failed: report it rather than an empty, error-free page.
            if (page["recommendations"].empty() && !cardError.empty() &&
                (action == "explore" || Text(secrets, {"/token"}).empty())) Fail(cardError.c_str());
            if (action == "home" && !Text(secrets, {"/token"}).empty() && offset == 0) {
                // The whole account library arrives with the first home page.
                // Gateways may cap pagesize below the requested 100; page by the
                // size the first page actually returned so no rows are skipped.
                auto library = *this;
                std::set<std::string> collections;
                int pageSize = 100;
                for (int pageNumber = 1; pageNumber <= 64; ++pageNumber) {
                    const auto body = library.KLibraryPage(pageNumber, pageSize);
                    const auto& list = KList(body);
                    for (const auto& item : list) {
                        if (page["playlists"].size() == kMaxLibraryCollections) break;
                        auto collection = library.SafeCollection(item, Number(item, {"/type"}) == 0);
                        if (!collection.is_null() && collections.insert(collection["id"].get<std::string>()).second)
                            page["playlists"].push_back(std::move(collection));
                    }
                    if (page["playlists"].size() == kMaxLibraryCollections) break;
                    if (pageNumber == 1 && list.size() < static_cast<std::size_t>(pageSize))
                        pageSize = std::max<int>(1, static_cast<int>(list.size()));
                    const auto end = (pageNumber - 1) * pageSize + static_cast<int>(list.size());
                    const auto total = Number(body, {"/data/total", "/data/count"}, -1);
                    if (list.empty() || (total > 0 && end >= total)) break;
                    if (total <= 0 && list.size() < static_cast<std::size_t>(pageSize)) break;
                }
                pending = std::move(library.pending);
            }
        } else if (action == "search") {
            if (offset % limit) Fail("unsupported");
            const auto query = InputText(args, "/query", 512);
            const bool authenticated = !Text(secrets, {"/token"}).empty();
            if (authenticated) Auth();
            auto body = authenticated ? K("/v3/search/song",
                {{"albumhide", "0"}, {"iscorrection", "1"}, {"keyword", query},
                 {"nocollect", "0"}, {"page", std::to_string(offset / limit + 1)},
                 {"pagesize", std::to_string(limit)}, {"platform", "AndroidFilter"}},
                nullptr, "complexsearch.kugou.com") : KAnonymousSearch(query);
            const auto& list = KList(body);
            Tracks(page, list);
            page["more"] = offset + limit < Number(body, {"/data/total"}, offset + static_cast<int>(list.size()));
        } else if (action == "playlist") {
            const auto id = Id(args, "/id", provider, false);
            if (id.compare(0, 11, "kugou-card-") == 0) {
                for (const auto& card : KugouCards) {
                    if (id != std::string("kugou-card-") + card.id) continue;
                    const auto songs = KCard(card)["tracks"];
                    for (std::size_t i = offset; i < songs.size() && i < static_cast<std::size_t>(offset + limit); ++i)
                        page["tracks"].push_back(songs[i]);
                    page["more"] = static_cast<std::size_t>(offset + limit) < songs.size();
                    return page;
                }
                Fail("unsupported");
            }
            const auto meta = Recall(scope + "collection:" + id);
            if (Text(meta, {"/type"}) == "album") return KAlbum(id);
            return KPlaylist(id);
        } else if (action == "favorites") {
            const auto liked = KLiked();
            return KPlaylist(Id(Json{{"id", Text(liked, {"/global_collection_id", "/globalCollectionId"})}},
                "/id", provider, false));
        } else if (action == "resolve") {
            const auto id = Id(args, "/id", provider);
            const auto song = Recall(scope + "track:" + id);
            auto hash = id;
            std::transform(hash.begin(), hash.end(), hash.begin(), [](char c) {
                return c >= 'A' && c <= 'F' ? static_cast<char>(c + 32) : c;
            });
            const auto requested = args.value("quality", std::string("standard"));
            const std::vector<std::string> qualities = requested == "hires" ?
                std::vector<std::string>{"high", "flac", "320", "128"} :
                requested == "lossless" ? std::vector<std::string>{"flac", "320", "128"} :
                requested == "high" ? std::vector<std::string>{"320", "128"} : std::vector<std::string>{"128"};
            const bool hasMetadata = Number(song, {"/albumId"}) || Number(song, {"/albumAudioId"});
            for (const auto& quality : qualities) for (int variant = 0; variant < (hasMetadata ? 2 : 1); ++variant) {
              try {
                auto body = K("/v5/url", {{"album_id", variant ? "0" : std::to_string(Number(song, {"/albumId"}))},
                {"area_code", "1"}, {"hash", hash}, {"ssa_flag", "is_fromtrack"}, {"version", "11436"},
                {"page_id", "967177915"}, {"quality", quality},
                {"album_audio_id", variant ? "0" : std::to_string(Number(song, {"/albumAudioId"}))},
                {"behavior", "play"}, {"pid", "411"}, {"cmd", "26"}, {"pidversion", "3001"},
                {"IsFreePart", "0"}, {"ppage_id", "356753938,823673182,967485191"}, {"cdnBackup", "1"},
                {"kcard", "0"}, {"module", ""}}, nullptr, "trackercdn.kugou.com");
            const Json* data = &body;
            if (At(body, "/data").is_object()) data = &body["data"];
            else if (At(body, "/data").is_array() && !body["data"].empty()) data = &body["data"][0];
            if (Flag(*data, "/is_free_part") || Flag(*data, "/isFreePart")) Fail("unavailable");
            const Json* urls = &At(*data, "/play_url");
            if (urls->is_null()) urls = &At(*data, "/playUrl");
            if (urls->is_null()) urls = &At(*data, "/url");
            const auto candidates = urls->is_array() ? *urls : Json::array({*urls});
            for (const auto& value : candidates) {
                if (!value.is_string()) continue;
                auto text = value.get<std::string>();
                std::size_t begin = 0;
                while (begin < text.size()) {
                    auto end = text.find(',', begin);
                    if (end != text.npos) {
                        auto next = end + 1;
                        while (next < text.size() &&
                            std::isspace(static_cast<unsigned char>(text[next]))) ++next;
                        if (text.compare(next, 7, "http://") != 0 &&
                            text.compare(next, 8, "https://") != 0) end = text.npos;
                    }
                    auto candidate = text.substr(begin, end == text.npos ? text.size() - begin : end - begin);
                    while (!candidate.empty() && std::isspace(static_cast<unsigned char>(candidate.front())))
                        candidate.erase(candidate.begin());
                    while (!candidate.empty() && std::isspace(static_cast<unsigned char>(candidate.back())))
                        candidate.pop_back();
                    const auto url = music::IsKugouHttpAudioUrl(candidate) ? candidate : Https(candidate, provider, true);
                    if (!url.empty()) return AudioSource(url, ReportedQuality(*data, "kugou"),
                        Number(*data, {"/bitrate", "/br"}));
                    if (end == text.npos) break;
                    begin = end + 1;
                }
            }
              } catch (const std::runtime_error& error) {
                auth::CheckCancelled(cancelled);
                const std::string_view code(error.what());
                if (code != "unavailable" && code != "service") throw;
              }
            }
            Fail("unavailable");
        } else if (action == "createPlaylist") {
            const auto params = KWriteParams();
            K("/cloudlist.service/v5/add_list", params,
                {{"userid", User()}, {"token", Text(secrets, {"/token"})}, {"total_ver", 0},
                 {"name", InputText(args, "/title", 256)}, {"type", 0}, {"source", 1},
                 {"is_pri", 0}, {"list_create_gid", ""}, {"from_shupinmv", 0}});
            return {{"ok", true}};
        } else if (action == "like" || action == "addToPlaylist") {
            Auth();
            const auto id = Id(args, action == "like" ? "/id" : "/trackId", provider);
            const auto song = Recall(scope + "track:" + id);
            const bool liked = action == "like" ? args.at("value").get<bool>() : true;
            std::int64_t listId = 0;
            if (action == "like") listId = Number(KLiked(), {"/listId"});
            else {
                const auto collection = Recall(scope + "collection:" + Id(args, "/id", provider, false));
                listId = Number(collection, {"/listId"});
                if (!Flag(collection, "/owned") || Text(collection, {"/type"}) != "playlist" || listId == 2)
                    Fail("unavailable");
            }
            if (!listId) Fail("unavailable");
            if (!liked) {
                const auto fileId = KLikedFileId(listId, id);
                if (!fileId) Fail("unavailable");
                K("/v4/delete_songs", {},
                    {{"listid", std::to_string(listId)}, {"userid", User()},
                     {"data", Json::array({{{"fileid", fileId}}})},
                     {"type", 0}, {"token", Text(secrets, {"/token"})}, {"list_ver", 0}},
                    "cloudlist.service.kugou.com");
                return {{"liked", false}};
            }
            K("/cloudlist.service/v6/add_song", KWriteParams(),
                {{"userid", User()}, {"token", Text(secrets, {"/token"})}, {"listid", std::to_string(listId)},
                 {"list_ver", 0}, {"type", 0}, {"slow_upload", 1}, {"scene", "false;null"},
                 {"data", Json::array({{{"number", 1}, {"name", Text(song, {"/title"})}, {"hash", id},
                    {"size", 0}, {"sort", 0}, {"timelen", 0}, {"bitrate", 0},
                    {"album_id", Number(song, {"/albumId"})}, {"mixsongid", Number(song, {"/mixSongId"})}}})}});
            return action == "like" ? Json{{"liked", true}} : Json{{"ok", true}};
        } else if (action == "lyrics") {
            const auto id = Id(args, "/id", provider);
            const auto song = Recall(scope + "track:" + id);
            // Search results carry the album-audio id as MixSongID only.
            const auto albumAudioId = Number(song, {"/albumAudioId"}) ? Number(song, {"/albumAudioId"}) : Number(song, {"/mixSongId"});
            Json candidates = Json::array();
            try {
                candidates = List(KLyric("/v1/search", {{"album_audio_id", std::to_string(albumAudioId)},
                    {"appid", "1005"}, {"clientver", "20489"}, {"duration", "0"}, {"hash", id},
                    {"keyword", ""}, {"lrctxt", "1"}, {"man", "no"}}, false), {"/candidates"});
            } catch (const std::runtime_error&) { auth::CheckCancelled(cancelled); }
            if (candidates.empty()) {
                // The signed search answers some tracks with an empty body; the
                // public search by file hash still lists their lyrics.
                std::string url = "https://krcs.kugou.com/search?ver=1&man=yes&client=mobi&keyword=&duration=&hash=" +
                    auth::UrlEncode(id) + "&album_audio_id=" + std::to_string(albumAudioId);
                candidates = List(Http({Provider::Kugou, "GET", url, {}, {}, MaxResponse}), {"/candidates"});
            }
            if (candidates.empty()) Fail("unavailable");
            const auto lyricId = Text(candidates[0], {"/id"}, 128);
            const auto accessKey = Text(candidates[0], {"/accesskey"}, 256);
            if (lyricId.empty() || accessKey.empty()) Fail("unavailable");
            return LyricsPayload(KLyric("/download", {{"ver", "1"}, {"client", "android"}, {"id", lyricId},
                {"accesskey", accessKey}, {"fmt", "lrc"}, {"charset", "utf8"}}, true),
                song.value("duration", 0.0));
        } else Fail("unsupported");
        return page;
    }
    Json LikedIds() {
        Json ids = Json::array();
        std::set<std::string> seen;
        if (provider == Provider::Netease) {
            const auto body = N("/api/song/like/get", {{"uid", User()}}, false);
            const auto& list = List(body, {"/ids"});
            if (list.size() > 10000) Fail("unsupported");
            for (const auto& value : list) {
                auth::CheckCancelled(cancelled);
                std::string id;
                try { id = std::string(auth::Name(provider)) + ":" + Id(Json{{"id", value}}, "/id", provider); }
                catch (const std::runtime_error&) { continue; }
                if (seen.insert(id).second) ids.push_back(id);
            }
            return ids;
        }
        Auth();
        const auto collection = provider == Provider::Kugou ? Id(Json{{"id",
            Text(KLiked(), {"/global_collection_id", "/globalCollectionId"})}}, "/id", provider, false) : QLiked();
        offset = 0; limit = 100;
        // ponytail: at most 100 batches / 10000 liked rows; omit the optional snapshot if incomplete.
        for (int batch = 0; batch < 100; ++batch) {
            const auto body = provider == Provider::QQ ? QPlaylistBody(collection) : KPlaylistBody(collection);
            const auto& rows = provider == Provider::QQ ? List(body, {"/songlist"}) : KList(body);
            if (rows.size() > 100 || offset + rows.size() > 10000) Fail("unsupported");
            const auto total = provider == Provider::QQ ? Number(body, {"/total_song_num"}, -1) :
                Number(body, {"/data/count", "/data/total", "/data/total_count"}, -1);
            if (total > 10000) Fail("unsupported");
            const auto end = offset + static_cast<int>(rows.size());
            for (const auto& row : rows) {
                auth::CheckCancelled(cancelled);
                const Json* track = &row;
                for (const auto* path : {"/track_info", "/songInfo", "/songinfo", "/song"})
                    if (At(row, path).is_object()) { track = &At(row, path); break; }
                auto rawId = provider == Provider::QQ ? Text(*track, {"/mid", "/songmid", "/songMid"}) :
                    Text(*track, {"/FileHash", "/fileHash", "/hash", "/Hash", "/audio_hash",
                        "/audio_info/hash", "/audio_info/hash_128"});
                if (provider == Provider::Kugou && rawId.size() != 32)
                    rawId = Text(*track, {"/audio_info/hash", "/audio_info/hash_128"});
                std::string id;
                try { id = std::string(auth::Name(provider)) + ":" + Id(Json{{"id", rawId}}, "/id", provider); }
                catch (const std::runtime_error&) { continue; }
                if (seen.insert(id).second) ids.push_back(id);
            }
            // Platforms withhold removed songs but still count them in the total,
            // so the reported total may never be reached: an empty page also ends.
            if (rows.empty() || (total >= 0 ? end >= total : rows.size() < 100)) return ids;
            // Advance by wire rows, not by deduplicated IDs.
            offset = end;
        }
        Fail("unsupported");
    }
    std::string LikedKey() const { return scope + "likedIds:" + leasedUserId; }
    Json LikedSnapshot(bool refresh) {
        Auth();
        if (!refresh && !likedState.is_null()) return At(likedState, "/ids");
        const auto previous = likedState.is_null() ? Recall(LikedKey()) : likedState;
        if (!refresh && At(previous, "/ids").is_array()) {
            likedState = previous;
            return At(likedState, "/ids");
        }
        try {
            auto likes = *this;
            auto ids = likes.LikedIds();
            auth::CheckCancelled(cancelled);
            pending = std::move(likes.pending);
            likedState = {{"ids", std::move(ids)}};
            Remember(LikedKey(), likedState);
        } catch (...) {
            auth::CheckCancelled(cancelled);
            // An empty object records a failed attempt only for this request, never an empty snapshot.
            likedState = previous;
        }
        return At(likedState, "/ids");
    }
    void UpdateLiked(const std::string& id, bool value) {
        likedState = Recall(LikedKey());
        if (!At(likedState, "/ids").is_array()) {
            likedState = nullptr;
            LikedSnapshot(true);
        }
        if (!At(likedState, "/ids").is_array()) return;
        auto& ids = likedState["ids"];
        const auto found = std::find(ids.begin(), ids.end(), id);
        if (value && found == ids.end()) ids.push_back(id);
        if (!value && found != ids.end()) ids.erase(found);
        if (ids.size() > 10000) {
            likedState = Json::object();
        }
        Remember(LikedKey(), likedState);
    }
};
}  // namespace

Json Execute(Provider provider, std::string_view action, const Json& arguments,
             const Json& credentials, const auth::Cancelled& cancelled, const auth::Exchange& exchange) {
    try {
        auth::CheckCancelled(cancelled);
        if (!*auth::Name(provider) || !arguments.is_object() ||
            (!credentials.is_object() && !credentials.is_null()) || !exchange) Fail();
        const std::set<std::string_view> actions = {"home", "explore", "playlist", "search", "favorites",
            "recent", "likes", "like", "createPlaylist", "addToPlaylist", "resolve", "lyrics"};
        if (!actions.count(action)) Fail("unsupported");
        // No UI route, credentials, providerData or transport overrides are accepted.
        for (auto it = arguments.begin(); it != arguments.end(); ++it) {
            const auto& key = it.key();
            if (key == "_userId" || (action != "likes" && (key == "offset" || key == "limit"))) continue;
            if (key == "id" && (action == "playlist" || action == "like" || action == "resolve" ||
                action == "lyrics" || action == "addToPlaylist")) continue;
            if (key == "trackId" && action == "addToPlaylist") continue;
            if (key == "query" && action == "search") continue;
            if (key == "title" && action == "createPlaylist") continue;
            if (key == "value" && action == "like") continue;
            if (key == "quality" && action == "resolve") continue;
            Fail();
        }
        if (action == "like" && !At(arguments, "/value").is_boolean()) Fail();
        if (arguments.contains("quality") && (!arguments["quality"].is_string() ||
            !music::ValidAudioQuality(arguments["quality"].get<std::string>(), auth::Name(provider)))) Fail();
        if (credentials.dump().size() > 64 * 1024) Fail();
        if (arguments.contains("_userId") && !arguments["_userId"].is_string()) Fail();
        const auto userId = Text(arguments, {"/_userId"}, 32);
        if (!userId.empty() && !Token(userId, true)) Fail();
        const auto credentialUserId = provider == Provider::QQ ? Text(credentials, {"/credential/uin"}) :
            provider == Provider::Kugou ? Text(credentials, {"/userid"}) : "";
        if (!userId.empty() && !credentialUserId.empty() && userId != credentialUserId) Fail("expired");
        Context context{provider, credentials.is_null() ? Json::object() : credentials, cancelled, exchange,
            std::string(auth::Name(provider)) + ":" + auth::HashHex(credentials.dump(), "SHA256") + ":",
            userId,
            PageNumber(arguments, "/offset", 0, 100000, false),
            PageNumber(arguments, "/limit", 50, 100, true), {}, nullptr};
        Json result;
        if (action == "likes") {
            const auto ids = context.LikedSnapshot(true);
            if (!ids.is_array()) Fail("unavailable");
            result = Page();
            result["likedIds"] = ids;
        } else {
            switch (provider) {
            case Provider::Netease: result = context.Netease(action, arguments); break;
            case Provider::QQ: result = context.QQ(action, arguments); break;
            case Provider::Kugou: result = context.Kugou(action, arguments); break;
            }
        }
        if (action == "home" || action == "explore" || action == "playlist" || action == "search" ||
            action == "favorites" || action == "recent" || action == "like") {
            try {
                if (action == "like")
                    context.UpdateLiked(std::string(auth::Name(provider)) + ":" +
                        Id(arguments, "/id", provider), result.at("liked").get<bool>());
                const auto ids = context.LikedSnapshot(context.offset == 0 &&
                    (action == "home" || (action == "favorites" && provider != Provider::Netease)));
                if (ids.is_array()) result["likedIds"] = ids;
            } catch (...) { auth::CheckCancelled(cancelled); }
        }
        context.Commit();
        return result;
    } catch (const std::runtime_error& error) {
        auth::CheckCancelled(cancelled);
        const std::string_view code(error.what());
        if (code == "service" || code == "expired" || code == "unsupported" || code == "unavailable" ||
            code == "invalid-response" || code == "cancelled") throw;
        Fail();
    } catch (...) {
        auth::CheckCancelled(cancelled);
        Fail();
    }
}
}  // namespace lume::music::online
