#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lume::music {

enum class PlaybackStatus { Empty, Ready, Playing, Paused, Error };
enum class RepeatMode { None, One, All };

inline constexpr std::size_t kMusicAnalysisBins = 32;

struct AudioAnalysis {
    // Normalized RMS and linear-frequency magnitudes from decoded output PCM.
    double energy = 0.0;
    std::array<double, kMusicAnalysisBins> bins{};
};

struct Track {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string source;
    std::string artwork;
    double duration = 0.0;
    bool vipRequired = false;
    std::string membershipBadge;
};

inline bool ValidAudioQuality(std::string_view value, std::string_view provider = {}) {
    if (value == "standard" || value == "high" || value == "lossless" || value == "hires") return true;
    return (provider.empty() || provider == "netease") &&
        (value == "jyeffect" || value == "sky" || value == "jymaster");
}

inline bool IsKugouHttpAudioUrl(std::string_view url) {
    // Historical native playback keeps HTTP only for fs.*.kugou.com media.
    if (url.size() > 8192 || url.substr(0, 7) != "http://" ||
        url.find_first_of("\\@# \r\n\t") != url.npos ||
        std::any_of(url.begin(), url.end(), [](unsigned char c) { return c < 33 || c >= 127; })) return false;
    const auto host = url.substr(7, url.find_first_of("/?", 7) - 7);
    constexpr std::string_view domain = ".kugou.com";
    return host.substr(0, 3) == "fs." && host.size() > domain.size() &&
        host.substr(host.size() - domain.size()) == domain &&
        host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") == host.npos;
}

struct ResolvedAudioSource {
    std::string url;
    std::string quality;
    std::uint32_t bitrate = 0;
    // Native-only lease guard; local and legacy sources need no account lease.
    std::function<bool()> current = {};
};

struct AudioQualityState {
    std::string requestedQuality = "standard";
    std::string quality;
    std::uint32_t bitrate = 0;
    bool switching = false;
};

struct MusicSnapshot {
    PlaybackStatus status = PlaybackStatus::Empty;
    std::vector<Track> queue;
    std::optional<Track> current;
    std::size_t currentIndex = 0;
    double position = 0.0;
    bool playing = false;
    RepeatMode repeat = RepeatMode::None;
    bool shuffle = false;
    std::uint64_t session = 0;
    std::uint64_t revision = 0;
    double volume = 1.0;
    bool muted = false;
    AudioAnalysis analysis;
};

}  // namespace lume::music
