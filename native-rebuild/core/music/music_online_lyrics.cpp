#include "core/music/music_online_lyrics.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <stdexcept>

namespace lume::music {
namespace {

using Json = auth::Json;
constexpr std::size_t kResponseLimit = 4 * kLyricsMaxFileBytes;

void Require(bool valid, const char* error = "invalid") {
    if (!valid) throw std::runtime_error(error);
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
        text.front() == '\r' || text.front() == '\n')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
        text.back() == '\r' || text.back() == '\n')) text.remove_suffix(1);
    return text;
}

int Integer(std::string_view text) {
    Require(!text.empty() && text.find_first_not_of("0123456789") == text.npos);
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
        value >= 0 && value <= 86400000);
    return value;
}

// Bound the already-parsed JSON without first allocating a serialized copy.
void BoundJson(const Json& value, std::size_t& bytes, std::size_t& nodes, unsigned depth = 0) {
    Require(depth <= 16 && ++nodes <= 20000, "response-limit");
    Require(!value.is_binary() && !value.is_discarded(), "invalid-response");
    if (value.is_string()) bytes += value.get_ref<const std::string&>().size();
    else if (value.is_structured()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (value.is_object()) bytes += it.key().size();
            Require(bytes <= kResponseLimit, "response-limit");
            BoundJson(it.value(), bytes, nodes, depth + 1);
        }
    } else bytes += 8;
    Require(bytes <= kResponseLimit, "response-limit");
}

Json ReadJson(std::string_view text) {
    auto result = Json::parse(text.begin(), text.end(), [](int depth, Json::parse_event_t, Json&) {
        Require(depth <= 16, "response-limit");
        return true;
    });
    std::size_t bytes = 0, nodes = 0;
    BoundJson(result, bytes, nodes);
    return result;
}

std::string Base64(std::string_view text) {
    Require(text.size() <= kResponseLimit, "file-limit");
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto pad = text.find('=');
    const auto count = pad == text.npos ? text.size() : pad;
    Require(count % 4 != 1, "encoding");
    if (pad != text.npos) {
        Require(text.size() % 4 == 0 && text.size() - count <= 2 &&
            text.substr(count).find_first_not_of('=') == text.npos, "encoding");
    }
    Require(count / 4 * 3 + (count % 4 * 6) / 8 <= kLyricsMaxFileBytes, "file-limit");
    std::string result;
    std::uint32_t bits = 0;
    unsigned available = 0;
    for (char c : text.substr(0, count)) {
        const auto digit = alphabet.find(c);
        Require(digit != alphabet.npos, "encoding");
        bits = (bits << 6) | static_cast<unsigned>(digit);
        available += 6;
        if (available >= 8) {
            available -= 8;
            result += static_cast<char>((bits >> available) & 255);
        }
    }
    Require(!available || !(bits & ((1u << available) - 1)), "encoding");
    return result;
}

std::string Field(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || object[key].is_null()) return {};
    Require(object[key].is_string(), "invalid-response");
    const auto& text = object[key].get_ref<const std::string&>();
    Require(text.size() <= kResponseLimit, "file-limit");
    return text;
}

std::string FirstField(const Json& object, std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        auto text = Field(object, key);
        if (!text.empty()) return text;
    }
    return {};
}

bool Flag(const Json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && (*it == true || *it == 1 || *it == "1");
}

bool HasCodeError(const Json& object, int success) {
    if (!object.contains("code")) return false;
    const auto& code = object["code"];
    return code != success && code != std::to_string(success);
}

// QRC XML is a restricted provider envelope, not general XML: no declarations
// with entities, external resources, or recursive entity expansion are allowed.
std::string QrcText(std::string text) {
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    const auto trimmed = Trim(text);
    if (trimmed.empty() || trimmed.front() != '<') return text;
    Require(text.find("<!") == text.npos, "unsupported-qrc-xml");
    const auto element = text.find("<Lyric_1");
    Require(element != text.npos, "unsupported-qrc-xml");
    const auto name = text.find("LyricContent", element);
    Require(name != text.npos && text.find("LyricContent", name + 12) == text.npos,
        "invalid-qrc-xml");
    auto tail = Trim(std::string_view(text).substr(name + 12));
    Require(!tail.empty() && tail.front() == '=', "invalid-qrc-xml");
    tail = Trim(tail.substr(1));
    Require(!tail.empty() && (tail.front() == '"' || tail.front() == '\''), "invalid-qrc-xml");
    const auto end = tail.find(tail.front(), 1);
    Require(end != tail.npos, "invalid-qrc-xml");
    tail = tail.substr(1, end - 1);
    std::string result;
    while (!tail.empty()) {
        if (tail.front() != '&') {
            result += tail.front();
            tail.remove_prefix(1);
            continue;
        }
        const auto semicolon = tail.find(';');
        Require(semicolon != tail.npos && semicolon <= 12, "invalid-qrc-xml");
        const auto entity = tail.substr(1, semicolon - 1);
        if (entity == "amp") result += '&';
        else if (entity == "lt") result += '<';
        else if (entity == "gt") result += '>';
        else if (entity == "quot") result += '"';
        else if (entity == "apos") result += '\'';
        else if (!entity.empty() && entity.front() == '#') {
            auto number = entity.substr(1);
            int base = 10;
            if (!number.empty() && number.front() == 'x') { base = 16; number.remove_prefix(1); }
            std::uint32_t code = 0;
            Require(!number.empty(), "invalid-qrc-xml");
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), code, base);
            Require(parsed.ec == std::errc{} && parsed.ptr == number.data() + number.size() &&
                code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF), "invalid-qrc-xml");
            if (code < 0x80) result += static_cast<char>(code);
            else if (code < 0x800) {
                result += static_cast<char>(0xC0 | (code >> 6));
                result += static_cast<char>(0x80 | (code & 63));
            } else if (code < 0x10000) {
                result += static_cast<char>(0xE0 | (code >> 12));
                result += static_cast<char>(0x80 | ((code >> 6) & 63));
                result += static_cast<char>(0x80 | (code & 63));
            } else {
                result += static_cast<char>(0xF0 | (code >> 18));
                result += static_cast<char>(0x80 | ((code >> 12) & 63));
                result += static_cast<char>(0x80 | ((code >> 6) & 63));
                result += static_cast<char>(0x80 | (code & 63));
            }
        } else Require(false, "invalid-qrc-xml");
        tail.remove_prefix(semicolon + 1);
    }
    return result;
}

struct Marker {
    std::size_t begin = 0, end = 0;
    int start = 0, duration = 0;
};

bool ReadMarker(std::string_view text, std::size_t at, char close, bool thirdRequired, Marker& marker) {
    if (at + 1 >= text.size()) return false;
    const char first = text[at + 1];
    if ((first < '0' || first > '9') && first != '-' && first != '+') return false;
    const auto end = text.find(close, at + 1);
    Require(end != text.npos);
    auto body = text.substr(at + 1, end - at - 1);
    const auto comma = body.find(',');
    if (comma == body.npos) return false;
    const int start = Integer(body.substr(0, comma));
    body.remove_prefix(comma + 1);
    const auto third = body.find(',');
    Require(!thirdRequired || third != body.npos);
    const int duration = Integer(body.substr(0, third));
    if (third != body.npos) Integer(body.substr(third + 1));
    marker = {at, end + 1, start, duration};
    return true;
}

std::vector<std::string> KrcTranslations(std::string_view value) {
    const auto language = ReadJson(Base64(value));
    std::vector<std::string> result;
    Require(language.is_object() && language.contains("content") && language["content"].is_array());
    for (const auto& track : language["content"]) {
        if (!track.is_object() || !track.contains("type") || track["type"] != 1) continue;
        Require(track.contains("lyricContent") && track["lyricContent"].is_array());
        Require(track["lyricContent"].size() <= kLyricsMaxLines, "line-limit");
        for (const auto& row : track["lyricContent"]) {
            Require(row.is_array());
            std::string text;
            for (const auto& part : row) {
                Require(part.is_string());
                text += part.get_ref<const std::string&>();
                Require(text.size() <= kLyricsMaxLineBytes, "line-limit");
            }
            Require(ValidLyricText(text), "encoding");
            result.push_back(std::move(text));
        }
        break;
    }
    return result;
}

LocalLyricsResult WordLines(LyricFormat format, std::string_view input) {
    Require(input.size() <= kLyricsMaxFileBytes, "file-limit");
    Require(input.substr(0, 4) != "krc1", "unsupported-krc-compression");
    Require(ValidLyricText(input), "encoding");
    if (input.substr(0, 3) == "\xEF\xBB\xBF") input.remove_prefix(3);
    LocalLyricsResult result;
    std::vector<std::string> translations;
    std::size_t physicalLines = 0, totalWords = 0;
    while (!input.empty()) {
        const auto newline = input.find('\n');
        auto text = input.substr(0, newline);
        input.remove_prefix(newline == input.npos ? input.size() : newline + 1);
        if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
        Require(++physicalLines <= kLyricsMaxLines, "line-limit");
        if (format == LyricFormat::Krc && text.substr(0, 10) == "[language:") {
            text = Trim(text);
            Require(text.back() == ']');
            translations = KrcTranslations(text.substr(10, text.size() - 11));
            continue;
        }
        Require(text.size() <= kLyricsMaxLineBytes, "line-limit");
        if (text.empty() || text.front() != '[') continue;
        Marker lineMarker;
        if (!ReadMarker(text, 0, ']', false, lineMarker)) continue;
        Require(std::count(text.begin(), text.begin() + lineMarker.end, ',') == 1);
        Require(lineMarker.duration > 0 && lineMarker.start + lineMarker.duration <= 86400000);
        LyricLine line{lineMarker.start / 1000.0,
            (lineMarker.start + lineMarker.duration) / 1000.0, {}, {}, {}};
        text.remove_prefix(lineMarker.end);
        const char open = format == LyricFormat::Krc ? '<' : '(';
        const char close = format == LyricFormat::Krc ? '>' : ')';
        std::vector<Marker> markers;
        for (std::size_t pos = 0; (pos = text.find(open, pos)) != text.npos; ++pos) {
            Marker marker;
            if (ReadMarker(text, pos, close, format == LyricFormat::Yrc, marker)) {
                Require(markers.size() < kLyricsMaxWordsPerLine && ++totalWords <= kLyricsMaxWords, "word-limit");
                markers.push_back(marker);
                pos = marker.end - 1;
            }
        }
        if (markers.empty()) {
            // Provider line-only data stays line-only; never interpolate word times.
            line.fullText = std::string(text);
        } else {
            const auto prefix = text.substr(0, markers.front().begin);
            const bool suffix = format == LyricFormat::Qrc && !Trim(prefix).empty();
            Require(suffix ? Trim(text.substr(markers.back().end)).empty() : Trim(prefix).empty());
            for (std::size_t i = 0; i < markers.size(); ++i) {
                const auto& marker = markers[i];
                const auto begin = suffix ? (i ? markers[i - 1].end : 0) : marker.end;
                const auto end = suffix ? marker.begin :
                    (i + 1 < markers.size() ? markers[i + 1].begin : text.size());
                const int start = marker.start + (format == LyricFormat::Krc ? lineMarker.start : 0);
                Require(start <= 86400000 && start + marker.duration <= 86400000);
                std::string word(text.substr(begin, end - begin));
                line.fullText += word;
                line.words.push_back({start / 1000.0, (start + marker.duration) / 1000.0, std::move(word)});
            }
        }
        result.lines.push_back(std::move(line));
    }
    // KRC language rows refer to source-line order, not chronological sort order.
    Require(translations.empty() || translations.size() == result.lines.size(), "translation-count");
    for (std::size_t i = 0; i < translations.size(); ++i)
        result.lines[i].translation = std::move(translations[i]);
    std::stable_sort(result.lines.begin(), result.lines.end(),
        [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
    Require(ValidateLyrics(result.lines), "invalid-timeline");
    return result;
}

LyricFormat Detect(std::string_view text, LyricFormat timed) {
    for (auto at = text.find('['); at != text.npos; at = text.find('[', at + 1)) {
        const auto end = text.find(']', at);
        if (end == text.npos) break;
        const auto body = text.substr(at + 1, end - at - 1);
        if (!body.empty() && body.front() >= '0' && body.front() <= '9')
            return body.find(',') != body.npos ? timed : LyricFormat::Lrc;
    }
    return timed;
}

std::string NeteaseField(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || object[key].is_null()) return {};
    Require(object[key].is_object(), "invalid-response");
    return Field(object[key], "lyric");
}

std::string DecodeQQ(std::string text) {
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    if (Trim(text).empty()) return {};
    if (text.find('[') == text.npos && Trim(text).front() != '<') text = Base64(text);
    return QrcText(std::move(text));
}

}  // namespace

LocalLyricsResult ParseTimedLyrics(LyricFormat format, std::string_view text,
    std::string_view translation, double duration) noexcept {
    try {
        Require(text.size() <= kLyricsMaxFileBytes && translation.size() <= kLyricsMaxFileBytes, "file-limit");
        const std::string qrc = format == LyricFormat::Qrc ? QrcText(std::string(text)) : std::string{};
        if (format == LyricFormat::Qrc) text = qrc;
        auto result = format == LyricFormat::Lrc ? ParseLrc(text, duration) : WordLines(format, text);
        if (!result.error.empty()) return result;
        if (!translation.empty()) {
            const std::string decoded = format == LyricFormat::Qrc ? QrcText(std::string(translation)) :
                std::string(translation);
            const auto transFormat = Detect(decoded, format == LyricFormat::Lrc ? LyricFormat::Qrc : format);
            auto trans = transFormat == LyricFormat::Lrc ? ParseLrc(decoded) : WordLines(transFormat, decoded);
            if (!trans.error.empty()) return trans;
            for (auto& line : result.lines) {
                if (!line.translation.empty()) continue;
                auto next = std::lower_bound(trans.lines.begin(), trans.lines.end(), line.startTime,
                    [](const auto& item, double time) { return item.startTime < time; });
                const LyricLine* best = nullptr;
                double distance = 1.0;
                if (next != trans.lines.begin()) {
                    const auto& previous = *std::prev(next);
                    if (line.startTime - previous.startTime < distance) {
                        distance = line.startTime - previous.startTime;
                        best = &previous;
                    }
                }
                if (next != trans.lines.end() && next->startTime - line.startTime < distance) best = &*next;
                if (best) line.translation = best->fullText;
            }
        }
        Require(ValidateLyrics(result.lines), "invalid-timeline");
        return result;
    } catch (const std::runtime_error& error) {
        return {{}, error.what()};
    } catch (...) {
        return {{}, "invalid"};
    }
}

LocalLyricsResult ParseOnlineLyrics(auth::Provider provider, const auth::Json& response,
    double duration) noexcept {
    try {
        Require(response.is_object(), "invalid-response");
        std::size_t bytes = 0, nodes = 0;
        BoundJson(response, bytes, nodes);
        if (provider == auth::Provider::Netease) {
            Require(!HasCodeError(response, 200), "provider-error");
            if (Flag(response, "pureMusic") || Flag(response, "nolyric")) return {};
            for (const char* field : {"lrc", "yrc", "ytlrc", "tlyric"})
                if (response.contains(field) && response[field].is_object() && Flag(response[field], "pureMusic"))
                    return {};
            auto yrc = NeteaseField(response, "yrc");
            auto translatedYrc = NeteaseField(response, "ytlrc");
            if (response.contains("lrc") && response["lrc"].is_object()) {
                if (yrc.empty()) yrc = NeteaseField(response["lrc"], "yrc");
                if (translatedYrc.empty()) translatedYrc = NeteaseField(response["lrc"], "ytlrc");
            }
            const auto lrc = NeteaseField(response, "lrc");
            if (lrc.find(u8"\u7EAF\u97F3\u4E50\uFF0C\u8BF7\u6B23\u8D4F") != std::string::npos) return {};
            return ParseTimedLyrics(yrc.empty() ? LyricFormat::Lrc : LyricFormat::Yrc,
                yrc.empty() ? lrc : yrc,
                !yrc.empty() && !translatedYrc.empty() ? translatedYrc : NeteaseField(response, "tlyric"),
                duration);
        }
        if (provider == auth::Provider::QQ) {
            Require(!HasCodeError(response, 0), "provider-error");
            const Json* item = &response;
            for (const char* wrapper : {"request", "req_0"}) {
                if (response.contains(wrapper)) {
                    item = &response[wrapper];
                    break;
                }
            }
            Require(item->is_object() && !HasCodeError(*item, 0), "provider-error");
            if (item->contains("data")) item = &(*item)["data"];
            else if (item->contains("lyric") && (*item)["lyric"].is_object() && (*item)["lyric"].contains("data"))
                item = &(*item)["lyric"]["data"];
            Require(item->is_object(), "invalid-response");
            Require(!HasCodeError(*item, 0), "provider-error");
            Require(!Flag(*item, "crypt"), "unsupported-qrc-encryption");
            const auto main = DecodeQQ(FirstField(*item, {"lyric", "lrc", "lyricText"}));
            const auto trans = DecodeQQ(FirstField(*item, {"trans", "translation", "transLyric"}));
            return ParseTimedLyrics(Detect(main, LyricFormat::Qrc), main, trans, duration);
        }
        if (provider == auth::Provider::Kugou) {
            Require(!HasCodeError(response, 0), "provider-error");
            Require(!response.contains("error_code") || response["error_code"] == 0 ||
                response["error_code"] == "0", "provider-error");
            auto text = Field(response, "decodeContent");
            if (text.empty()) {
                text = Field(response, "content");
                if (!text.empty() && text.front() != '[' && text.substr(0, 4) != "krc1") text = Base64(text);
            }
            Require(text.substr(0, 4) != "krc1", "unsupported-krc-compression");
            if (text.find(u8"\u7EAF\u97F3\u4E50\uFF0C\u8BF7\u6B23\u8D4F") != std::string::npos) return {};
            return ParseTimedLyrics(Detect(text, LyricFormat::Krc), text, {}, duration);
        }
        return {{}, "unsupported-provider"};
    } catch (const std::runtime_error& error) {
        return {{}, error.what()};
    } catch (...) {
        return {{}, "invalid-response"};
    }
}

LocalLyricsResult ParseOnlineLyrics(std::string_view provider, const auth::Json& response,
    double duration) noexcept {
    if (provider == "netease") return ParseOnlineLyrics(auth::Provider::Netease, response, duration);
    if (provider == "qq") return ParseOnlineLyrics(auth::Provider::QQ, response, duration);
    if (provider == "kugou") return ParseOnlineLyrics(auth::Provider::Kugou, response, duration);
    return {{}, "unsupported-provider"};
}

}  // namespace lume::music
