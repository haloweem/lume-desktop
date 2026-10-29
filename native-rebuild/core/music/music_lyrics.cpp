#include "core/music/music_lyrics.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>

namespace lume::music {

bool ValidLyricText(std::string_view text) noexcept {
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index++]);
        if (first < 0x80) {
            if (first < 0x20 && first != '\t' && first != '\r' && first != '\n') return false;
            continue;
        }
        unsigned count = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { count = 1; code = first & 0x1F; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { count = 2; code = first & 0x0F; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { count = 3; code = first & 0x07; minimum = 0x10000; }
        else return false;
        if (index + count > text.size()) return false;
        while (count--) {
            const auto next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xC0) != 0x80) return false;
            code = (code << 6) | (next & 0x3F);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
    }
    return true;
}

bool ValidateLyrics(const std::vector<LyricLine>& lines) noexcept {
    if (lines.size() > kLyricsMaxLines) return false;
    std::size_t bytes = 0, words = 0;
    double previousStart = 0;
    const auto textValid = [&](std::string_view text) {
        if (text.size() > kLyricsMaxLineBytes || !ValidLyricText(text)) return false;
        bytes += text.size();
        return bytes <= kLyricsMaxFileBytes;
    };
    for (const auto& line : lines) {
        if (!std::isfinite(line.startTime) || !std::isfinite(line.endTime) ||
            line.startTime < previousStart || line.endTime <= line.startTime ||
            line.endTime > kLyricsMaxSeconds || !textValid(line.fullText) ||
            !textValid(line.translation) || line.words.size() > kLyricsMaxWordsPerLine)
            return false;
        words += line.words.size();
        if (words > kLyricsMaxWords) return false;
        double previousWordStart = line.startTime;
        std::size_t cursor = 0;
        for (const auto& word : line.words) {
            if (!std::isfinite(word.startTime) || !std::isfinite(word.endTime) ||
                word.startTime < previousWordStart || word.endTime < word.startTime ||
                word.endTime > line.endTime || !textValid(word.text) ||
                std::string_view(line.fullText).substr(cursor, word.text.size()) != word.text)
                return false;
            cursor += word.text.size();
            previousWordStart = word.startTime;
        }
        if (!line.words.empty() && cursor != line.fullText.size()) return false;
        previousStart = line.startTime;
    }
    return true;
}

namespace {

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

std::optional<int> Integer(std::string_view text) {
    if (text.empty()) return {};
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return {};
    return value;
}

std::optional<int> TimestampMillis(std::string_view text) {
    const auto colon = text.find(':');
    if (colon == std::string_view::npos || colon < 1 || colon > 4) return {};
    for (const auto value : text) if ((value < '0' || value > '9') && value != ':' && value != '.') return {};
    const auto minutes = Integer(text.substr(0, colon));
    const auto rest = text.substr(colon + 1);
    // [mm:ss.xx] is standard; some editors write the fraction as [mm:ss:xx].
    if (rest.size() < 2 || (rest.size() > 2 && ((rest[2] != '.' && rest[2] != ':') ||
        rest.size() < 4 || rest.size() > 6))) return {};
    const auto seconds = Integer(rest.substr(0, 2));
    if (!minutes || !seconds || *seconds >= 60) return {};
    int millis = (*minutes * 60 + *seconds) * 1000;
    if (rest.size() > 2) {
        const auto fraction = Integer(rest.substr(3));
        if (!fraction) return {};
        millis += *fraction * (rest.size() == 4 ? 100 : rest.size() == 5 ? 10 : 1);
    }
    if (millis >= 86400000) return {};
    return millis;
}

}  // namespace

LocalLyricsResult ParseLrc(std::string_view input, double duration) noexcept {
    try {
        if (input.size() > kLyricsMaxFileBytes) return {{}, "file-limit"};
        if (!ValidLyricText(input)) return {{}, "encoding"};
        if (input.substr(0, 3) == "\xEF\xBB\xBF") input.remove_prefix(3);
        LocalLyricsResult result;
        std::size_t physicalLines = 0, textBytes = 0;
        int offset = 0;
        while (!input.empty()) {
            const auto newline = input.find('\n');
            auto line = input.substr(0, newline);
            input.remove_prefix(newline == std::string_view::npos ? input.size() : newline + 1);
            if (++physicalLines > kLyricsMaxLines || line.size() > kLyricsMaxLineBytes) return {{}, "line-limit"};
            line = Trim(line);
            if (line.substr(0, 8) == "[offset:" && line.back() == ']') {
                auto number = line.substr(8, line.size() - 9);
                if (!number.empty() && number.front() == '+') number.remove_prefix(1);
                const auto parsed = Integer(number);
                if (parsed && *parsed >= -86400000 && *parsed <= 86400000) offset = *parsed;
                continue;
            }
            std::vector<int> timestamps;
            while (!line.empty() && line.front() == '[') {
                const auto close = line.find(']');
                if (close == std::string_view::npos) break;
                const auto time = TimestampMillis(line.substr(1, close - 1));
                if (!time) break;
                timestamps.push_back(*time);
                line.remove_prefix(close + 1);
            }
            if (timestamps.empty()) continue;
            line = Trim(line);
            if (result.lines.size() + timestamps.size() > kLyricsMaxLines ||
                textBytes + line.size() * timestamps.size() > kLyricsMaxFileBytes) return {{}, "line-limit"};
            for (const auto millis : timestamps) {
                result.lines.push_back({static_cast<double>(millis), 0, std::string(line), {}, {}});
                textBytes += line.size();
            }
        }
        const double end = std::isfinite(duration) && duration > 0 ? std::min(duration, kLyricsMaxSeconds) : kLyricsMaxSeconds;
        for (auto& line : result.lines) line.startTime = std::max(0.0, line.startTime + offset) / 1000.0;
        result.lines.erase(std::remove_if(result.lines.begin(), result.lines.end(),
            [end](const auto& line) { return line.startTime >= end; }), result.lines.end());
        std::stable_sort(result.lines.begin(), result.lines.end(),
            [](const auto& left, const auto& right) { return left.startTime < right.startTime; });
        // Plain LRC has one active line; repeated timestamps keep the first line.
        result.lines.erase(std::unique(result.lines.begin(), result.lines.end(),
            [](const auto& left, const auto& right) { return left.startTime == right.startTime; }), result.lines.end());
        for (std::size_t index = 0; index < result.lines.size(); ++index) {
            auto& line = result.lines[index];
            line.endTime = index + 1 < result.lines.size() ? result.lines[index + 1].startTime :
                (std::isfinite(duration) && duration > 0 ? end : std::min(end, line.startTime + 5.0));
        }
        return result;
    } catch (...) {
        return {{}, "invalid"};
    }
}

}  // namespace lume::music
