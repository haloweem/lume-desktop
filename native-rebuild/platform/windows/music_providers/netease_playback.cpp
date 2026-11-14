// MIT protocol port of @neteasecloudmusicapienhanced/api 4.41.1, as used by
// Folia 77d52a3168ab2608174d2e5ad6d51b7bc8befd11. See ONLINE_PROVENANCE.md.
#include "platform/windows/music_providers/netease_playback.h"
#include <algorithm>
#include <chrono>
#include <mutex>

#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_MALLOC
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "vendor/miniz/miniz_tinfl.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace lume::music::auth {
namespace {
constexpr std::size_t Limit = 256 * 1024;
constexpr char SignKey[] = "mUHCwVNWJbunMqAHf5MImuirT6plvs6VSFW62MGHstFQxhBGdEoIhLItH3djc4+FB/OKty3+lL2rGeoFBpVe5g==";
constexpr char Agent[] = "NeteaseMusic/9.5.61.260802021928(9005061);Dalvik/2.1.0 (Linux; U; Android 12; HBN-AL00 Build/cd737a2.0)";
void Require(bool value) { if (!value) throw std::runtime_error("invalid-response"); }
std::string HexBytes(std::string_view hex) {
    Require(hex.size() % 2 == 0);
    std::string value;
    for (std::size_t i = 0; i < hex.size(); i += 2)
        value += static_cast<char>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16));
    return value;
}
const std::string& StaticKey() {
    static const auto key = HexBytes("ab1d5a430f6bb04a3f01e81ddd72bd916d5ce591248ac128714806d7f8fb1b84");
    return key;
}
std::string Scalar(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer() || value.is_boolean()) return value.dump();
    throw std::runtime_error("invalid-response");
}
std::string Form(const Json& value) {
    std::string body;
    for (const auto& item : value.items()) {
        if (!body.empty()) body += '&';
        body += UrlEncode(item.key()) + '=' + UrlEncode(Scalar(item.value()));
    }
    return body;
}
std::string Cookie(const Json& credentials, const char* name) {
    const auto value = credentials.value(name, std::string{});
    Require(value.size() <= 4096 && value.find_first_of(";\r\n") == value.npos &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 33 && c < 127; }));
    return value;
}
Json Parse(std::string_view text) {
    Require(!text.empty() && text.size() <= Limit);
    bool deep = false;
    auto value = Json::parse(text.begin(), text.end(), [&](int depth, Json::parse_event_t, Json&) {
        if (depth > 16) deep = true;
        return depth <= 16;
    }, false);
    Require(!deep && value.is_object());
    return value;
}
HttpResponse Send(const HttpRequest& request, const Cancelled& stop, const Exchange& exchange) {
    CheckCancelled(stop);
    HttpResponse response;
    try { response = exchange(request, stop); }
    catch (...) { CheckCancelled(stop); throw std::runtime_error("network"); }
    CheckCancelled(stop);
    if (response.status == 401 || response.status == 403) throw std::runtime_error("expired");
    if (response.status == 429) throw std::runtime_error("rate-limited");
    if (response.status >= 500) throw std::runtime_error("network");
    if (response.status < 200 || response.status >= 300) throw std::runtime_error("service");
    Require(!response.body.empty() && response.body.size() <= request.maxResponse);
    return response;
}
Json PublicKey(const std::string& device, const Cancelled& stop, const Exchange& exchange) {
    struct Cached { Json value; std::chrono::steady_clock::time_point at; };
    static std::timed_mutex mutex;
    static std::map<std::string, Cached> cache;
    std::unique_lock lock(mutex, std::defer_lock);
    while (!lock.try_lock_for(std::chrono::milliseconds(20))) CheckCancelled(stop);
    CheckCancelled(stop);
    const auto now = std::chrono::steady_clock::now();
    if (const auto it = cache.find(device); it != cache.end() && now - it->second.at < std::chrono::hours(24))
        return it->second.value;
    const auto timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    auto nonce = HexBytes(RandomHex(16));
    for (auto& c : nonce) c = static_cast<char>('0' + static_cast<unsigned char>(c) % 10);
    HttpRequest request{Provider::Netease, "POST", "https://interface.music.163.com/api/bsr/sk/get"};
    request.headers = {{"User-Agent", Agent}, {"Content-Type", "application/x-www-form-urlencoded"},
        {"Cookie", "deviceId=" + UrlEncode(device)}};
    request.body = Form({{"appVersion", "9.5.61"}, {"currentKeyVersion", ""}, {"deviceId", device},
        {"nonce", nonce}, {"os", "android"}, {"requestType", "active"},
        {"signature", Base64(HmacSha256(SignKey, timestamp + nonce))},
        {"t1", ""}, {"t2", ""}, {"timestamp", timestamp}, {"uid", ""}});
    const auto result = Parse(Send(request, stop, exchange).body);
    Require(result.value("code", 0) == 200 && result.contains("data") && result["data"].is_object());
    const auto& data = result["data"];
    Require(data.at("signature") == Base64(HmacSha256(SignKey, Scalar(data.at("timestamp")) + nonce)));
    auto key = Parse(Aes(Unbase64(data.at("encryptedData").get<std::string>()), StaticKey(), "", false));
    Require(key.contains("version") && Scalar(key.at("version")).size() <= 128 &&
        key.contains("sk") && key["sk"].is_string() && !key["sk"].empty() && key["sk"].get_ref<const std::string&>().size() <= 4096 &&
        Unbase64(key.at("publicKey").get<std::string>()).size() == 32);
    if (cache.size() >= 16 && !cache.count(device)) cache.erase(cache.begin());
    cache[device] = {key, now};
    return key;
}
std::uint32_t Crc(std::string_view bytes) {
    std::uint32_t crc = 0xffffffff;
    for (unsigned char c : bytes) {
        crc ^= c;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return ~crc;
}
std::uint32_t Little(std::string_view bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
    return value;
}
std::string Gunzip(std::string_view bytes) {
    Require(bytes.size() >= 18 && static_cast<unsigned char>(bytes[2]) == 8);
    const auto flags = static_cast<unsigned char>(bytes[3]);
    Require(!(flags & 0xe0));
    std::size_t offset = 10, end = bytes.size() - 8;
    if (flags & 4) {
        Require(offset + 2 <= end);
        const auto count = static_cast<unsigned char>(bytes[offset]) | (static_cast<unsigned char>(bytes[offset + 1]) << 8);
        offset += 2 + count;
        Require(offset <= end);
    }
    for (const int flag : {8, 16}) if (flags & flag) {
        const auto zero = bytes.find('\0', offset);
        Require(zero != bytes.npos && zero < end); offset = zero + 1;
    }
    if (flags & 2) {
        Require(offset + 2 <= end);
        const auto crc = static_cast<unsigned char>(bytes[offset]) | (static_cast<unsigned char>(bytes[offset + 1]) << 8);
        Require((Crc(bytes.substr(0, offset)) & 0xffff) == static_cast<unsigned>(crc)); offset += 2;
    }
    const auto length = Little(bytes, end + 4);
    Require(offset < end && length > 0 && length <= Limit);
    std::string output(length, '\0');
    tinfl_decompressor decoder{}; tinfl_init(&decoder);
    auto inputSize = end - offset, outputSize = output.size();
    const auto status = tinfl_decompress(&decoder, reinterpret_cast<const mz_uint8*>(bytes.data() + offset),
        &inputSize, reinterpret_cast<mz_uint8*>(output.data()), reinterpret_cast<mz_uint8*>(output.data()),
        &outputSize, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    Require(status == TINFL_STATUS_DONE && inputSize == end - offset && outputSize == length && Crc(output) == Little(bytes, end));
    return output;
}
}
Json NeteaseXeapiEnvelope(const Json& data, const Json& publicKey) {
    const auto dynamic = HexBytes(RandomHex(16));
    auto encrypted = Aes(Json{{"content", Form(data)}, {"queryString", "e_r=true"}}.dump(), StaticKey(), "");
    const auto random = HexBytes(RandomHex(16));
    for (std::size_t i = 0; i < encrypted.size(); ++i) encrypted[i] ^= random[i & 15];
    const auto rotation = (static_cast<unsigned char>(random[0]) & 15) % encrypted.size();
    std::rotate(encrypted.begin(), encrypted.begin() + rotation, encrypted.end());
    auto c = Base64(Aes(random + encrypted, dynamic, ""));
    std::replace(c.begin(), c.end(), '+', '-'); std::replace(c.begin(), c.end(), '/', '_');
    while (!c.empty() && c.back() == '=') c.pop_back();
    const auto [ephemeral, secret] = X25519(Unbase64(publicKey.at("publicKey").get<std::string>()));
    const auto key = HmacSha256(HmacSha256(std::string(32, '\0'), secret), ephemeral + '\1').substr(0, 16);
    const auto iv = HexBytes(RandomHex(12));
    const auto s = ephemeral + iv + AesGcm(Base64(dynamic) + "|android|" + publicKey.at("sk").get<std::string>(), key, iv);
    return {{"C", c}, {"S", Base64(s)}, {"R", Base64(Aes(Scalar(publicKey.at("version")) + '|', StaticKey(), ""))}};
}
Json DecodeNeteaseXeapiResponse(std::string_view body) {
    Require(!body.empty() && body.size() <= Limit);
    // Refusals may be plaintext even though successful responses are encrypted.
    if (body.front() == '{') {
        try { return Parse(body); } catch (const std::runtime_error&) {}
    }
    const auto plain = Aes(body, "e82ckenh8dichen8", "", false);
    return Parse(plain.size() >= 2 && plain[0] == '\x1f' && plain[1] == '\x8b' ? Gunzip(plain) : plain);
}
Json NeteasePlaybackRequest(const Json& data, const Json& credentials, const Cancelled& stop, const Exchange& exchange) {
    auto device = Cookie(credentials, "deviceId");
    if (device.empty()) device = HashHex("LumeMusicDevice:" + credentials.dump(), "SHA256").substr(0, 32);
    const auto key = PublicKey(device, stop, exchange);
    const auto build = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    HttpRequest request{Provider::Netease, "POST", "https://interface3.music.163.com/xeapi/song/enhance/player/url/v1"};
    request.headers = {{"User-Agent", Agent}, {"Content-Type", "application/x-www-form-urlencoded;charset=utf-8"},
        {"X-Client-Enc-State", "ENCRYPTED"}, {"x-aeapi", "true"}, {"x-deviceid", device},
        {"x-os", "android"}, {"x-osver", "16"}, {"x-appver", "9.1.65"}, {"x-sdeviceid", device}, {"x-buildver", build}};
    auto cookie = "os=android; osver=16; appver=9.1.65; buildver=" + build + "; deviceId=" + UrlEncode(device) +
        "; sDeviceId=" + UrlEncode(device);
    for (const auto* field : {"MUSIC_U", "MUSIC_A", "__csrf"}) {
        const auto value = Cookie(credentials, field);
        if (!value.empty()) cookie += std::string("; ") + field + '=' + UrlEncode(value);
        if (std::string_view(field) == "MUSIC_U" && !value.empty()) request.headers["x-music-u"] = value;
    }
    request.headers["Cookie"] = std::move(cookie);
    request.body = Form(NeteaseXeapiEnvelope(data, key));
    return DecodeNeteaseXeapiResponse(Send(request, stop, exchange).body);
}
}
