#pragma once

#include "vendor/nlohmann/json.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace lume::music {

inline constexpr std::size_t kMusicBackgroundMaxRecordBytes = 16384;
inline constexpr std::size_t kMusicBackgroundMaxNameBytes = 512;
inline constexpr std::uint64_t kMusicBackgroundMaxSafeInteger = 9007199254740991ULL;

enum class MusicBackgroundKind { Image, Video };
enum class MusicBackgroundStorage { Plain, Protected };

struct MusicBackgroundMedia {
    std::string id;
    std::string name;
    std::string extension;
    MusicBackgroundKind kind = MusicBackgroundKind::Image;
    std::string mimeType;
};

bool operator==(const MusicBackgroundMedia&, const MusicBackgroundMedia&);
bool operator!=(const MusicBackgroundMedia&, const MusicBackgroundMedia&);
bool NormalizeMusicBackgroundMedia(const MusicBackgroundMedia&, MusicBackgroundMedia&);
bool DecodeMusicBackgroundMedia(const nlohmann::json&, MusicBackgroundMedia&, std::string* error = nullptr);
nlohmann::json EncodeMusicBackgroundMedia(const MusicBackgroundMedia&);
std::string MusicBackgroundUrl(const MusicBackgroundMedia&);

enum class MusicBackgroundResult { Ready, Busy, Invalid, Load, Save, Conflict, Unsupported, TooLarge };

bool ValidMusicBackgroundSourceId(std::string_view) noexcept;

namespace background_detail {
bool Keys(const nlohmann::json&, std::initializer_list<std::string_view>);
bool Integer(const nlohmann::json&, std::uint64_t&, bool positive = false);
nlohmann::json Parse(std::string_view);
nlohmann::json Parse(std::wstring_view);
}

}  // namespace lume::music
