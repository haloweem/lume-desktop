#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lume::music {

inline constexpr std::size_t kLyricsMaxFileBytes = 256 * 1024;
inline constexpr std::size_t kLyricsMaxLines = 2000;
inline constexpr std::size_t kLyricsMaxLineBytes = 4096;
inline constexpr std::size_t kLyricsMaxWordsPerLine = 1000;
inline constexpr std::size_t kLyricsMaxWords = 16000;
inline constexpr double kLyricsMaxSeconds = 86400.0;

struct LyricWord {
    double startTime = 0;
    double endTime = 0;
    std::string text;
};

struct LyricLine {
    double startTime = 0;
    double endTime = 0;
    std::string fullText;
    // Seconds on the same clock as the line; empty when no word timing was supplied.
    std::vector<LyricWord> words;
    std::string translation;
};

struct LocalLyricsResult {
    std::vector<LyricLine> lines;
    std::string error;
};

LocalLyricsResult ParseLrc(std::string_view input, double duration = 0) noexcept;
bool ValidLyricText(std::string_view text) noexcept;
// Sorted starts, bounded UTF-8/text/word counts, and words contained in their line.
// Overlapping lines/words and zero-duration words are valid provider data.
bool ValidateLyrics(const std::vector<LyricLine>& lines) noexcept;

}  // namespace lume::music
