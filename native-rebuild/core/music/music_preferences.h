#pragma once

#include "vendor/nlohmann/json.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace lume::music {

inline constexpr std::size_t kMusicPreferencesMaxBytes = 4096;
inline constexpr std::size_t kMusicPreferencesMaxFontBytes = 128;
inline constexpr std::uint64_t kMusicPreferencesMaxSafeInteger = 9007199254740991ULL;

struct MusicPreferences {
    int version = 3;
    std::string mode = "partita";
    std::string effectMode = "none";
    std::string fontFamily;
    std::string animationIntensity = "normal";
    std::string lyricsFontColor = "#ffffff";
    std::string lyricsKaraokeFillColor = "#ffffff";
    std::string sonnetLyricsFontColor = "#ffffff";
    std::string sonnetKaraokeFillColor = "#38bdf8";
    std::string sonnetGlowColor = "#60a5fa";
};

bool operator==(const MusicPreferences&, const MusicPreferences&);
bool NormalizeMusicPreferences(const MusicPreferences&, MusicPreferences&);
// Accepts exact v1/v2/v3 DTOs; v1/v2 migrate with no effect.
// v1 uses original Sonnet defaults; v2 preserves its Sonnet colors.
bool DecodeMusicPreferences(const nlohmann::json&, MusicPreferences&, std::string* error = nullptr);
// Throws std::invalid_argument for an invalid typed v3 preference; emits canonical v3.
nlohmann::json EncodeMusicPreferences(const MusicPreferences&);

enum class MusicPreferencesResult { Ready, Invalid, Save, Conflict, Load };

namespace detail {
bool MusicPreferencesKeys(const nlohmann::json&, std::initializer_list<std::string_view>);
bool MusicPreferencesInteger(const nlohmann::json&, std::uint64_t&, bool positive = false);
// Bounded structured parsing with duplicate-key/depth rejection. Throws on malformed input.
nlohmann::json ParseMusicPreferencesJson(std::string_view);
nlohmann::json ParseMusicPreferencesJson(std::wstring_view);
}

}  // namespace lume::music
