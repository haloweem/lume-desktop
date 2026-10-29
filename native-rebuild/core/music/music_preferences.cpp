#include "core/music/music_preferences.h"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lume::music {
namespace {
using Json = nlohmann::json;

bool FontFamily(const std::string& value) {
    if (value.size() > kMusicPreferencesMaxFontBytes) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto byte = static_cast<unsigned char>(value[i]);
        // C0, DEL and Unicode C1 controls cannot be font family names.
        if (byte < 0x20 || byte == 0x7f) return false;
        if (byte == 0xc2 && i + 1 < value.size()) {
            const auto next = static_cast<unsigned char>(value[i + 1]);
            if (next >= 0x80 && next <= 0x9f) return false;
        }
    }
    try {
        // nlohmann's strict serializer validates UTF-8, including overlong/surrogate sequences.
        static_cast<void>(Json(value).dump(-1, ' ', true));
        return true;
    } catch (...) { return false; }
}

bool Color(std::string& value) {
    if (value.size() != 7 || value.front() != '#') return false;
    for (std::size_t i = 1; i < value.size(); ++i) {
        auto& ch = value[i];
        if (ch >= 'A' && ch <= 'F') ch = static_cast<char>(ch - 'A' + 'a');
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    return true;
}

template<class Char>
Json Parse(std::basic_string_view<Char> input) {
    if (input.empty() || input.size() > kMusicPreferencesMaxBytes) throw std::invalid_argument("size");
    std::vector<std::set<std::string>> keys;
    auto json = Json::parse(input.begin(), input.end(), [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 4) throw std::invalid_argument("depth");
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

bool operator==(const MusicPreferences& left, const MusicPreferences& right) {
    return left.version == right.version && left.mode == right.mode && left.effectMode == right.effectMode &&
        left.fontFamily == right.fontFamily &&
        left.animationIntensity == right.animationIntensity && left.lyricsFontColor == right.lyricsFontColor &&
        left.lyricsKaraokeFillColor == right.lyricsKaraokeFillColor &&
        left.sonnetLyricsFontColor == right.sonnetLyricsFontColor &&
        left.sonnetKaraokeFillColor == right.sonnetKaraokeFillColor &&
        left.sonnetGlowColor == right.sonnetGlowColor;
}

bool NormalizeMusicPreferences(const MusicPreferences& input, MusicPreferences& output) {
    constexpr std::array<std::string_view, 14> modes{
        "partita", "fume", "sonnet", "gallery", "3d", "lumiere", "motif", "cadenza", "tilt", "cappella", "diorama", "pendolo",
        "classic", "tempera"};
    constexpr std::array<std::string_view, 4> effects{"none", "rain-window", "snow", "star-river"};
    if (input.version != 3 || std::find(modes.begin(), modes.end(), input.mode) == modes.end() ||
        std::find(effects.begin(), effects.end(), input.effectMode) == effects.end() ||
        (input.animationIntensity != "calm" && input.animationIntensity != "normal" && input.animationIntensity != "chaotic") ||
        !FontFamily(input.fontFamily)) return false;
    auto next = input;
    if (!Color(next.lyricsFontColor) || !Color(next.lyricsKaraokeFillColor) ||
        !Color(next.sonnetLyricsFontColor) || !Color(next.sonnetKaraokeFillColor) ||
        !Color(next.sonnetGlowColor)) return false;
    output = std::move(next);
    return true;
}

bool DecodeMusicPreferences(const Json& json, MusicPreferences& output, std::string* error) {
    if (error) *error = "invalid";
    try {
        MusicPreferences next;
        if (!json.is_object() || !json.at("version").is_number_integer()) return false;
        if (json.at("version") == 1) {
            if (!detail::MusicPreferencesKeys(json, {"version", "mode", "fontFamily", "animationIntensity",
                    "lyricsFontColor", "lyricsKaraokeFillColor"})) return false;
        } else if (json.at("version") == 2 || json.at("version") == 3) {
            if (json.at("version") == 2) {
                if (!detail::MusicPreferencesKeys(json, {"version", "mode", "fontFamily", "animationIntensity",
                        "lyricsFontColor", "lyricsKaraokeFillColor", "sonnetColors"})) return false;
            } else {
                if (!detail::MusicPreferencesKeys(json, {"version", "mode", "fontFamily", "animationIntensity",
                        "lyricsFontColor", "lyricsKaraokeFillColor", "sonnetColors", "effectMode"})) return false;
                next.effectMode = json.at("effectMode").get<std::string>();
            }
            if (!detail::MusicPreferencesKeys(json.at("sonnetColors"),
                    {"lyricsFontColor", "karaokeFillColor", "glowColor"})) return false;
            const auto& colors = json.at("sonnetColors");
            next.sonnetLyricsFontColor = colors.at("lyricsFontColor").get<std::string>();
            next.sonnetKaraokeFillColor = colors.at("karaokeFillColor").get<std::string>();
            next.sonnetGlowColor = colors.at("glowColor").get<std::string>();
        } else {
            return false;
        }
        next.mode = json.at("mode").get<std::string>();
        next.fontFamily = json.at("fontFamily").get<std::string>();
        next.animationIntensity = json.at("animationIntensity").get<std::string>();
        next.lyricsFontColor = json.at("lyricsFontColor").get<std::string>();
        next.lyricsKaraokeFillColor = json.at("lyricsKaraokeFillColor").get<std::string>();
        // Existing installations may have one of the removed lyric styles saved.
        // Keep their other preferences while selecting the first available style.
        if (next.mode == "monet") next.mode = "partita";
        if (!NormalizeMusicPreferences(next, output)) return false;
        if (error) error->clear();
        return true;
    } catch (...) { return false; }
}

Json EncodeMusicPreferences(const MusicPreferences& preferences) {
    MusicPreferences p;
    if (!NormalizeMusicPreferences(preferences, p)) throw std::invalid_argument("music preferences");
    return Json{{"version", 3}, {"mode", p.mode}, {"effectMode", p.effectMode}, {"fontFamily", p.fontFamily},
        {"animationIntensity", p.animationIntensity}, {"lyricsFontColor", p.lyricsFontColor},
        {"lyricsKaraokeFillColor", p.lyricsKaraokeFillColor},
        {"sonnetColors", Json{{"lyricsFontColor", p.sonnetLyricsFontColor},
            {"karaokeFillColor", p.sonnetKaraokeFillColor}, {"glowColor", p.sonnetGlowColor}}}};
}

namespace detail {
bool MusicPreferencesKeys(const Json& json, std::initializer_list<std::string_view> keys) {
    if (!json.is_object() || json.size() != keys.size()) return false;
    for (const auto key : keys) if (!json.contains(std::string(key))) return false;
    return true;
}

bool MusicPreferencesInteger(const Json& value, std::uint64_t& output, bool positive) {
    if (!value.is_number_integer()) return false;
    if (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) return false;
    const auto next = value.get<std::uint64_t>();
    if (next > kMusicPreferencesMaxSafeInteger || (positive && next == 0)) return false;
    output = next;
    return true;
}

Json ParseMusicPreferencesJson(std::string_view value) { return Parse(value); }
Json ParseMusicPreferencesJson(std::wstring_view value) {
    if (value.empty() || value.size() > kMusicPreferencesMaxBytes) throw std::invalid_argument("size");
    // Validate code units before nlohmann's wide adapter combines surrogate pairs.
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto code = static_cast<std::uint32_t>(value[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (code >= 0xd800 && code <= 0xdbff) {
                if (++i >= value.size()) throw std::invalid_argument("surrogate");
                const auto low = static_cast<std::uint32_t>(value[i]);
                if (low < 0xdc00 || low > 0xdfff) throw std::invalid_argument("surrogate");
            } else if (code >= 0xdc00 && code <= 0xdfff) throw std::invalid_argument("surrogate");
        } else if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
            throw std::invalid_argument("code point");
        }
    }
    return Parse(value);
}
}
}  // namespace lume::music
