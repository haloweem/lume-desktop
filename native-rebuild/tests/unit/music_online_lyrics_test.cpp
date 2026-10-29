#include "core/music/music_online_lyrics.h"

#include <iostream>
#include <limits>

namespace {
using namespace lume::music;
using Json = auth::Json;
bool passed = true;

void Check(bool value, const char* message) {
    if (!value) {
        passed = false;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::string B64(std::string_view text) {
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    unsigned bits = 0, count = 0;
    for (unsigned char byte : text) {
        bits = (bits << 8) | byte;
        count += 8;
        while (count >= 6) {
            count -= 6;
            result += alphabet[(bits >> count) & 63];
        }
    }
    if (count) result += alphabet[(bits << (6 - count)) & 63];
    while (result.size() % 4) result += '=';
    return result;
}

void TestNetease() {
    const Json raw{
        {"code", 200},
        {"yrc", {{"lyric", "{\"t\":0,\"c\":[{\"tx\":\"credit\"}]}\n"
            "[4000,1000](4200,500,0)later\n"
            "[1000,2000](1100,700,0)Hello (2000,600,0)world"}}},
        {"lrc", {{"lyric", "[00:01.00]line fallback"}}},
        {"ytlrc", {{"lyric", "[00:01.00]translated\n[00:04.20]later translation"}}},
        {"tlyric", {{"lyric", "[00:01.00]wrong older translation"}}}
    };
    const auto parsed = ParseOnlineLyrics("netease", raw, 30);
    Check(parsed.error.empty() && parsed.lines.size() == 2, "NetEase YRC is selected and sorted");
    if (parsed.lines.size() != 2) return;
    const auto& first = parsed.lines.front();
    Check(first.startTime == 1 && first.endTime == 3 && first.words.size() == 2,
        "YRC retains the supplied line interval");
    if (first.words.size() == 2)
        Check(first.words[0].startTime == 1.1 && first.words[0].endTime == 1.8 &&
            first.words[1].startTime == 2 && first.words[1].endTime == 2.6,
            "YRC words have absolute, unmodified millisecond timing");
    Check(first.fullText == "Hello world" && first.translation == "translated",
        "YRC concatenates real words and prefers aligned ytlrc");
    Check(parsed.lines[1].translation == "later translation", "translation follows nearest timestamp under one second");

    const auto nested = ParseOnlineLyrics("netease", Json{{"lrc", {
        {"lyric", "[00:01]fallback"}, {"yrc", {{"lyric", "[1000,500](1000,500,0)actual"}}},
        {"ytlrc", {{"lyric", "[00:01]nested translation"}}}}}});
    Check(nested.lines.size() == 1 && nested.lines[0].fullText == "actual" &&
        nested.lines[0].translation == "nested translation", "historical nested NetEase fields are supported");
    const auto lrc = ParseOnlineLyrics("netease", Json{
        {"lrc", {{"lyric", "[00:01]line one\n[00:02]line two"}}},
        {"tlyric", {{"lyric", "[00:01]translated line"}}}}, 10);
    Check(lrc.lines.size() == 2 && lrc.lines[0].words.empty() &&
        lrc.lines[0].translation == "translated line" && lrc.lines[1].translation.empty() &&
        lrc.lines[1].endTime == 10, "LRC remains line-only with bounded last interval and translation");
    Check(ParseOnlineLyrics("netease", Json{{"code", 500}}).error == "provider-error", "NetEase service errors are not empty success");
    Check(ParseOnlineLyrics("netease", Json{{"pureMusic", true}}).lines.empty(), "instrumental flag has no fabricated lines");
    Check(ParseOnlineLyrics("netease", Json{{"lrc", nullptr}, {"tlyric", nullptr}}).error.empty(),
        "missing optional NetEase tracks may be null");
    Check(ParseOnlineLyrics("netease", Json{{"lrc", {{"lyric",
        u8"[00:00]\u7EAF\u97F3\u4E50\uFF0C\u8BF7\u6B23\u8D4F"}}}}).lines.empty(),
        "historical instrumental notice is not displayed as a lyric");
}

void TestQQ() {
    const std::string content = "[1000,2000]Hello (1100,700)world(2000,600)";
    const Json raw{{"req_0", {{"code", 0}, {"data", {
        {"crypt", 0}, {"qrc", 1}, {"lyric", B64(content)},
        {"trans", B64("[1000,2000]translated(1000,2000)")}}}}}};
    const auto parsed = ParseOnlineLyrics("qq", raw, 30);
    Check(parsed.error.empty() && parsed.lines.size() == 1, "QQ req_0 data/base64 is decoded");
    if (parsed.lines.size() == 1) {
        const auto& line = parsed.lines[0];
        Check(line.words.size() == 2 && line.fullText == "Hello world" && line.translation == "translated",
            "QQ suffix markers and QRC translation are normalized");
        if (line.words.size() == 2)
            Check(line.words[0].startTime == 1.1 && line.words[1].endTime == 2.6,
                "QRC timing is absolute, not relative to the line");
    }
    const auto prefix = ParseTimedLyrics(LyricFormat::Qrc, "[1000,1000](1000,500)A(1500,500)B");
    Check(prefix.lines.size() == 1 && prefix.lines[0].fullText == "AB", "historical leading QRC markers are supported");
    const auto spaced = ParseTimedLyrics(LyricFormat::Qrc, "[1000,1000]  (1000,1000)A");
    Check(spaced.lines.size() == 1 && spaced.lines[0].fullText == "A", "leading formatting space does not switch QRC marker orientation");
    const auto suffixSpace = ParseTimedLyrics(LyricFormat::Qrc, "[1000,1000]A(1000,1000) \r\n");
    Check(suffixSpace.lines.size() == 1 && suffixSpace.lines[0].fullText == "A", "suffix QRC tolerates trailing formatting whitespace");
    const auto xml = ParseOnlineLyrics("qq", Json{{"lyric",
        "<QrcInfos><LyricInfo><Lyric_1 LyricType=\"1\" LyricContent=\""
        "[1000,1000]A&amp;B(1000,1000)&#10;[2000,1000]&#x4F60;(2000,1000)\"/>"
        "</LyricInfo></QrcInfos>"}});
    Check(xml.error.empty() && xml.lines.size() == 2 && xml.lines[0].fullText == "A&B" &&
        xml.lines[1].fullText == "\xE4\xBD\xA0", "restricted QRC XML handles named and numeric entities");
    const auto xmlBom = ParseOnlineLyrics("qq", Json{{"lyric",
        "\xEF\xBB\xBF<Lyric_1 LyricContent='[1000,1000]x(1000,1000)'/>"}});
    Check(xmlBom.lines.size() == 1 && xmlBom.lines[0].fullText == "x", "BOM is accepted before QRC XML");
    const auto plain = ParseOnlineLyrics("qq", Json{{"request", {{"data", {
        {"lyric", B64("[00:01]ordinary")}, {"trans", "[00:01]translation"}}}}}}, 8);
    Check(plain.lines.size() == 1 && plain.lines[0].words.empty() &&
        plain.lines[0].translation == "translation", "QQ supports plain LRC without synthesizing words");
    Check(ParseOnlineLyrics("qq", Json{{"data", {{"crypt", 1}, {"lyric", "0123456789abcdef"}}}}).error ==
        "unsupported-qrc-encryption", "encrypted QRC is explicitly unsupported without an inflate decoder");
    Check(!ParseOnlineLyrics("qq", Json{{"lyric", "Ww==="}}).error.empty(), "malformed base64 is rejected");
    Check(!ParseOnlineLyrics("qq", Json{{"lyric", "Ww=A"}}).error.empty(), "interior padding is rejected");
    Check(!ParseOnlineLyrics("qq", Json{{"lyric", "Wx=="}}).error.empty(), "noncanonical base64 pad bits are rejected");
    Check(ParseOnlineLyrics("qq", Json{{"lyric", " \t"}}).error.empty(), "blank QQ payload does not read an empty string view");
    const auto encodedUtf8 = ParseOnlineLyrics("qq", Json{{"lyric", B64("[00:01]\xE4\xBD\xA0")}});
    Check(encodedUtf8.lines.size() == 1 && encodedUtf8.lines[0].fullText == "\xE4\xBD\xA0",
        "base64 UTF-8 is decoded without changing the text");
    Check(!ParseTimedLyrics(LyricFormat::Qrc,
        "<!DOCTYPE x [<!ENTITY x SYSTEM 'file:///private'>]><Lyric_1 LyricContent='&x;'/>").error.empty(),
        "QRC XML never resolves external or recursive entities");
}

void TestKugou() {
    const std::string language = B64(R"({"content":[{"type":0,"lyricContent":[["ignored"],["ignored"]]},{"type":1,"lyricContent":[["second"," translation"],["first translation"]]}]})");
    const std::string krc = "[language:" + language + "]\n"
        "[5000,1000]<100,400,0>second\n"
        "[1000,2000]<100,700,0>Hello <1000,600,0>world";
    const auto parsed = ParseOnlineLyrics("kugou", Json{{"decodeContent", krc}, {"content", "unused encrypted data"}});
    Check(parsed.error.empty() && parsed.lines.size() == 2, "KuGou prefers historical decodeContent");
    if (parsed.lines.size() == 2) {
        Check(parsed.lines[0].translation == "first translation" && parsed.lines[1].translation == "second translation",
            "embedded translations follow their source lines when sorting");
        Check(parsed.lines[0].words.size() == 2 && parsed.lines[0].words[0].startTime == 1.1 &&
            parsed.lines[0].words[1].startTime == 2.0, "KRC offsets are relative to line start");
    }
    const auto lineOnly = ParseTimedLyrics(LyricFormat::Krc, "[1000,1000]line-only");
    Check(lineOnly.lines.size() == 1 && lineOnly.lines[0].words.empty(), "KRC text without word tags stays line-only");
    Check(ParseOnlineLyrics("kugou", Json{{"content", B64("krc1compressed")}}).error ==
        "unsupported-krc-compression", "binary compressed KRC is honestly reported");
    const auto decoded = ParseOnlineLyrics("kugou", Json{{"content", B64("[1000,500]<0,500,0>x")}});
    Check(decoded.lines.size() == 1 && decoded.lines[0].fullText == "x", "base64 plaintext KRC works");
    Check(!ParseTimedLyrics(LyricFormat::Krc, "[language:!!!]\n[1000,1000]<0,1000,0>x").error.empty(),
        "bad embedded language encoding is rejected");
    Check(!ParseTimedLyrics(LyricFormat::Krc, "[language:" + language + "]\n[1000,500]<0,500,0>x").error.empty(),
        "translation row count mismatch cannot silently attach the wrong text");
    Check(ParseOnlineLyrics("kugou", Json{{"error_code", 1}}).error == "provider-error",
        "KuGou error_code is not silently treated as missing lyrics");
}

void TestValidationAndBounds() {
    const std::vector<LyricLine> valid{
        {1, 3, "AB", {{1, 2, "A"}, {1.5, 1.5, "B"}}, "translation"},
        {2, 4, "C", {{2.5, 4, "C"}}, ""}
    };
    Check(ValidateLyrics(valid), "overlapping lines/words and zero-duration words preserve provider data");
    auto invalid = valid;
    invalid[0].words[0].startTime = -1;
    Check(!ValidateLyrics(invalid), "word before its line is rejected");
    invalid = valid;
    invalid[0].words[0].endTime = 3.1;
    Check(!ValidateLyrics(invalid), "word beyond its line is rejected");
    invalid = valid;
    invalid[0].words[1].startTime = 0.5;
    Check(!ValidateLyrics(invalid), "decreasing word starts are rejected");
    invalid = valid;
    invalid[0].words[0].endTime = std::numeric_limits<double>::infinity();
    Check(!ValidateLyrics(invalid), "infinite word time is rejected");
    invalid = valid;
    invalid[0].startTime = std::numeric_limits<double>::quiet_NaN();
    Check(!ValidateLyrics(invalid), "NaN line time is rejected");
    invalid = valid;
    invalid[0].fullText = "not the words";
    Check(!ValidateLyrics(invalid), "word concatenation must match full text");
    invalid = valid;
    invalid[0].translation = "\xFF";
    Check(!ValidateLyrics(invalid), "invalid UTF-8 translation is rejected");
    invalid = valid;
    invalid[0].words[0].text = std::string("A\0", 2);
    Check(!ValidateLyrics(invalid), "embedded NUL is rejected");
    invalid = valid;
    invalid[0].words.assign(kLyricsMaxWordsPerLine + 1, {1, 1, ""});
    Check(!ValidateLyrics(invalid), "per-line word count is bounded");
    invalid.assign(kLyricsMaxLines + 1, {0, 1, "", {}, ""});
    Check(!ValidateLyrics(invalid), "line count is bounded independently");
    invalid.assign(17, {0, 1, "", std::vector<LyricWord>(1000, {0, 0, ""}), ""});
    Check(!ValidateLyrics(invalid), "total word count is bounded");
    invalid.assign(100, {0, 1, std::string(4096, 'x'), {}, ""});
    Check(!ValidateLyrics(invalid), "aggregate text bytes are bounded");

    for (const auto format : {LyricFormat::Yrc, LyricFormat::Qrc, LyricFormat::Krc}) {
        Check(!ParseTimedLyrics(format, std::string(kLyricsMaxFileBytes + 1, 'x')).error.empty(), "input bytes are bounded");
        Check(!ParseTimedLyrics(format, "[1000,2000]x\xFF").error.empty(), "invalid UTF-8 is rejected before output");
        Check(!ParseTimedLyrics(format, "[999999999999999999,20]x").error.empty(), "timestamp overflow is rejected");
        Check(!ParseTimedLyrics(format, "[86399999,2]x").error.empty(), "one-day ceiling is enforced");
    }
    Check(!ParseTimedLyrics(LyricFormat::Yrc, "[1000,1000](500,300,0)x").error.empty(),
        "YRC word outside line is rejected, not shifted");
    Check(!ParseTimedLyrics(LyricFormat::Krc, "[1000,1000]<900,200,0>x").error.empty(),
        "KRC word outside line is rejected, not clamped");
    Check(!ParseTimedLyrics(LyricFormat::Yrc, "[1000,1000](1000,1000)x").error.empty(),
        "malformed YRC cannot masquerade as timed words");
    Check(!ParseTimedLyrics(LyricFormat::Yrc, "[1000,1000](-1000,1000,0)x").error.empty(),
        "negative word markers cannot silently become ordinary text");
    Check(!ParseTimedLyrics(LyricFormat::Yrc, "[-1,1000](0,999,0)x").error.empty(),
        "negative line markers fail closed");
    Check(!ParseTimedLyrics(LyricFormat::Yrc, "[1000,1000,0](1000,1000,0)x").error.empty(),
        "malformed three-field line markers fail closed");
    const auto spaces = ParseTimedLyrics(LyricFormat::Yrc, "\xEF\xBB\xBF[1000,1000](1000,1000,0)x \r\n");
    Check(spaces.lines.size() == 1 && spaces.lines[0].fullText == "x " &&
        spaces.lines[0].words[0].text == "x ", "word whitespace survives BOM and CRLF normalization");
    const auto unicode = ParseTimedLyrics(LyricFormat::Yrc, "[1000,1000](1000,1000,0)\xF0\x9F\x8E\xB5");
    Check(unicode.lines.size() == 1 && ValidateLyrics(unicode.lines), "UTF-8 non-BMP text survives");
    const auto overlapping = ParseTimedLyrics(LyricFormat::Yrc,
        "[1000,2000](1000,2000,0)a\n[2000,2000](2000,2000,0)b", {}, 2.5);
    Check(overlapping.lines.size() == 2 && overlapping.lines[1].endTime == 4,
        "supplied timing is not truncated by approximate track duration");
    Check(ParseOnlineLyrics("unknown", Json::object()).error == "unsupported-provider", "unknown provider is explicit");
    Check(!ParseOnlineLyrics("qq", Json::array()).error.empty(), "wrong JSON root type is rejected");
    Check(!ParseOnlineLyrics("netease", Json{{"lrc", {{"lyric", 42}}}}).error.empty(), "wrong text field type is rejected");
    Json deep = Json::object();
    for (int i = 0; i < 20; ++i) deep = Json{{"nested", deep}};
    Check(ParseOnlineLyrics("qq", deep).error == "response-limit", "preparsed JSON nesting is bounded");
    Check(ParseOnlineLyrics("qq", Json{{"extra", std::string(4 * kLyricsMaxFileBytes + 1, 'x')}}).error ==
        "response-limit", "aggregate response bytes are bounded before extraction");
    Check(!ParseOnlineLyrics("qq", Json{{"extra", Json::binary({1, 2, 3})}}).error.empty(),
        "non-JSON binary values cannot bypass response accounting");
    std::string manyLines;
    for (std::size_t i = 0; i <= kLyricsMaxLines; ++i) manyLines += "[1000,1000](1000,1000,0)x\n";
    Check(!ParseTimedLyrics(LyricFormat::Yrc, manyLines).error.empty(), "physical provider line count is bounded");
    const auto translationEdge = ParseTimedLyrics(LyricFormat::Yrc,
        "[2000,500](2000,500,0)x", "[00:01]too far\n[00:03]also too far");
    Check(translationEdge.lines.size() == 1 && translationEdge.lines[0].translation.empty(),
        "exactly one second translation mismatch is not attached");
}

}  // namespace

int main() {
    TestNetease();
    TestQQ();
    TestKugou();
    TestValidationAndBounds();
    if (!passed) return 1;
    std::cout << "music online lyrics parser behavior passed\n";
    return 0;
}
