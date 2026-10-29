#include "core/music/music_background.h"
#include "core/wallpaper/wallpaper_id_policy.h"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <vector>

namespace lume::music {
bool ValidMusicBackgroundSourceId(std::string_view value) noexcept {
    return wallpaper::IsSafeWallpaperId(value);
}
namespace {
using Json = nlohmann::json;

bool Utf8Text(const std::string& value, std::size_t limit) {
    if (value.empty() || value.size() > limit) return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto code = static_cast<unsigned char>(value[index]);
        if (code < 0x20 || code == 0x7f) return false;
        if (code == 0xc2 && index + 1 < value.size()) {
            const auto next = static_cast<unsigned char>(value[index + 1]);
            if (next >= 0x80 && next <= 0x9f) return false;
        }
    }
    try {
        static_cast<void>(Json(value).dump(-1, ' ', true));
        return true;
    } catch (...) { return false; }
}

bool Extension(const std::string& value) {
    static constexpr std::array<std::string_view, 7> values{
        "jpg", "jpeg", "png", "webp", "gif", "mp4", "webm"};
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool Id(const std::string& value) {
    if (value.empty() || value.size() > 80) return false;
    for (const auto byte : value) {
        if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-')) return false;
    }
    return true;
}

bool Mapping(const MusicBackgroundMedia& value) {
    const bool image = value.extension == "jpg" || value.extension == "jpeg" || value.extension == "png" ||
        value.extension == "webp" || value.extension == "gif";
    const bool video = value.extension == "mp4" || value.extension == "webm";
    if (image && (value.kind != MusicBackgroundKind::Image || value.mimeType.rfind("image/", 0) != 0)) return false;
    if (video && (value.kind != MusicBackgroundKind::Video || value.mimeType.rfind("video/", 0) != 0)) return false;
    if (!image && !video) return false;
    if (value.extension == "jpg" || value.extension == "jpeg") return value.mimeType == "image/jpeg";
    if (value.extension == "png") return value.mimeType == "image/png";
    if (value.extension == "webp") return value.mimeType == "image/webp";
    if (value.extension == "gif") return value.mimeType == "image/gif";
    if (value.extension == "mp4") return value.mimeType == "video/mp4";
    return value.mimeType == "video/webm";
}

template<class Char>
Json ParseImpl(std::basic_string_view<Char> input) {
    if (input.empty() || input.size() > kMusicBackgroundMaxRecordBytes) throw std::invalid_argument("size");
    std::vector<std::set<std::string>> keys;
    auto json = Json::parse(input.begin(), input.end(), [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 5) throw std::invalid_argument("depth");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key &&
            (keys.empty() || !keys.back().insert(value.get<std::string>()).second))
            throw std::invalid_argument("duplicate key");
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
    if (!json.is_object()) throw std::invalid_argument("object");
    return json;
}
}

bool operator==(const MusicBackgroundMedia& left, const MusicBackgroundMedia& right) {
    return left.id == right.id && left.name == right.name && left.extension == right.extension &&
        left.kind == right.kind && left.mimeType == right.mimeType;
}

bool operator!=(const MusicBackgroundMedia& left, const MusicBackgroundMedia& right) { return !(left == right); }

bool NormalizeMusicBackgroundMedia(const MusicBackgroundMedia& input, MusicBackgroundMedia& output) {
    if (!Id(input.id) || !Utf8Text(input.name, kMusicBackgroundMaxNameBytes) ||
        !Extension(input.extension) || !Mapping(input)) return false;
    output = input;
    return true;
}

bool DecodeMusicBackgroundMedia(const Json& json, MusicBackgroundMedia& output, std::string* error) {
    if (error) *error = "invalid";
    try {
        if (!background_detail::Keys(json, {"id", "name", "extension", "kind", "mimeType"}) ||
            !json.at("id").is_string() || !json.at("name").is_string() || !json.at("extension").is_string() ||
            !json.at("kind").is_string() || !json.at("mimeType").is_string()) return false;
        MusicBackgroundMedia next;
        next.id = json.at("id").get<std::string>();
        next.name = json.at("name").get<std::string>();
        next.extension = json.at("extension").get<std::string>();
        next.mimeType = json.at("mimeType").get<std::string>();
        const auto kind = json.at("kind").get<std::string>();
        if (kind == "image") next.kind = MusicBackgroundKind::Image;
        else if (kind == "video") next.kind = MusicBackgroundKind::Video;
        else return false;
        if (!NormalizeMusicBackgroundMedia(next, output)) return false;
        if (error) error->clear();
        return true;
    } catch (...) { return false; }
}

Json EncodeMusicBackgroundMedia(const MusicBackgroundMedia& media) {
    MusicBackgroundMedia normalized;
    if (!NormalizeMusicBackgroundMedia(media, normalized)) throw std::invalid_argument("music background media");
    return Json{{"id", normalized.id}, {"name", normalized.name}, {"extension", normalized.extension},
        {"kind", normalized.kind == MusicBackgroundKind::Image ? "image" : "video"},
        {"mimeType", normalized.mimeType}};
}

std::string MusicBackgroundUrl(const MusicBackgroundMedia& media) {
    MusicBackgroundMedia normalized;
    if (!NormalizeMusicBackgroundMedia(media, normalized)) throw std::invalid_argument("music background media");
    return "https://lume-music.local/" + normalized.id + "." + normalized.extension;
}

namespace background_detail {
bool Keys(const Json& json, std::initializer_list<std::string_view> keys) {
    if (!json.is_object() || json.size() != keys.size()) return false;
    for (const auto key : keys) if (!json.contains(std::string(key))) return false;
    return true;
}

bool Integer(const Json& value, std::uint64_t& output, bool positive) {
    if (!value.is_number_integer()) return false;
    if (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) return false;
    const auto next = value.get<std::uint64_t>();
    if (next > kMusicBackgroundMaxSafeInteger || (positive && next == 0)) return false;
    output = next;
    return true;
}

Json Parse(std::string_view value) { return ParseImpl(value); }

Json Parse(std::wstring_view value) {
    if (value.empty() || value.size() > kMusicBackgroundMaxRecordBytes) throw std::invalid_argument("size");
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto code = static_cast<std::uint32_t>(value[index]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (code >= 0xd800 && code <= 0xdbff) {
                if (++index >= value.size()) throw std::invalid_argument("surrogate");
                const auto low = static_cast<std::uint32_t>(value[index]);
                if (low < 0xdc00 || low > 0xdfff) throw std::invalid_argument("surrogate");
            } else if (code >= 0xdc00 && code <= 0xdfff) throw std::invalid_argument("surrogate");
        } else if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
            throw std::invalid_argument("code point");
        }
    }
    return ParseImpl(value);
}
}
}  // namespace lume::music
