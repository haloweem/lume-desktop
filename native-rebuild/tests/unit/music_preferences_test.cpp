#include "apps/shell/music_preferences_session.h"
#include "platform/windows/local_settings_file_store.h"

#include <windows.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace {
using Json = nlohmann::json;
using namespace lume::music;
using namespace lume::ui;
using lume::shell::MusicPreferencesSession;

void Check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

class Files final : public lume::settings::IFileStore {
public:
    std::map<std::string, std::string> data;
    std::string fail;
    unsigned writes = 0;
    mutable unsigned boundedReads = 0;
    bool Read(const std::string&, std::string&) const override {
        throw std::runtime_error("preference reads must be bounded");
    }
    bool ReadBounded(const std::string& path, std::size_t limit, std::string& out) const override {
        ++boundedReads;
        Check(limit == kMusicPreferencesMaxBytes, "consistent read limit");
        const auto it = data.find(path);
        if (it == data.end() || fail == "read:" + path || it->second.size() > limit) return false;
        out = it->second;
        return true;
    }
    bool Exists(const std::string& path) const override { return data.count(path) != 0; }
    bool Write(const std::string& path, const std::string& text) override {
        ++writes;
        if (fail == "write:" + path) return false;
        data[path] = text;
        return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        if (fail == "rename:" + from || !data.count(from)) return false;
        data[to] = data.at(from);
        data.erase(from);
        return true;
    }
    bool Remove(const std::string& path) override { data.erase(path); return true; }
};

Json LegacyPreferences() {
    return Json{{"version", 1}, {"mode", "partita"}, {"fontFamily", ""},
        {"animationIntensity", "normal"}, {"lyricsFontColor", "#ffffff"},
        {"lyricsKaraokeFillColor", "#ffffff"}};
}
Json V2Preferences() {
    auto value = LegacyPreferences();
    value["version"] = 2;
    value["sonnetColors"] = Json{{"lyricsFontColor", "#ffffff"}, {"karaokeFillColor", "#38bdf8"},
        {"glowColor", "#60a5fa"}};
    return value;
}
Json Preferences() {
    auto value = V2Preferences();
    value["version"] = 3;
    value["effectMode"] = "none";
    return value;
}
Json Apply(std::uint64_t request = 1, std::uint64_t revision = 0) {
    return Json{{"version", 1}, {"scope", "music-preferences"}, {"action", "apply"},
        {"request", request}, {"revision", revision}, {"preferences", Preferences()}};
}
Json Reset(std::uint64_t request, std::uint64_t revision) {
    return Json{{"version", 1}, {"scope", "music-preferences"}, {"action", "reset"},
        {"request", request}, {"revision", revision}};
}
Json Snapshot() { return Json{{"version", 1}, {"scope", "music-preferences"}, {"action", "snapshot"}}; }
Json Reply(MusicPreferencesSession& session, const Json& value) {
    const auto command = DecodeMusicPreferencesCommand(value);
    Check(command.has_value(), "fixture command decodes");
    return Json::parse(session.HandlePage(*command));
}
std::string Disk(const Json& preferences, std::uint64_t revision) {
    return Json{{"version", 1}, {"revision", revision}, {"preferences", preferences}}.dump();
}

void Codec() {
    Check(MusicPreferences{}.version == 3, "native preferences default to version 3");
    Check(EncodeMusicPreferences({}) == Preferences(), "eight exact default fields with no effect and original Sonnet colors");
    MusicPreferences output;
    output.mode = "partita";
    const auto reject = [&](const Json& value) {
        const auto before = output;
        std::string error;
        Check(!DecodeMusicPreferences(value, output, &error) && output == before && error == "invalid",
              "invalid DTO is transactional");
    };
    for (const auto* field : {"version", "mode", "fontFamily", "animationIntensity",
                             "lyricsFontColor", "lyricsKaraokeFillColor", "sonnetColors", "effectMode"}) {
        auto value = Preferences(); value.erase(field); reject(value);
        value = Preferences(); value[field] = nullptr; reject(value);
    }
    auto value = Preferences(); value["tuning"] = Json::object(); reject(value);
    for (const auto& version : {Json(0), Json(4), Json(3.0), Json(true), Json("3")}) {
        value = Preferences(); value["version"] = version; reject(value);
    }
    value = Preferences(); value["mode"] = "cadenze"; reject(value);
    value["mode"] = "wallpaper"; reject(value);
    value = Preferences(); value["animationIntensity"] = "fast"; reject(value);
    for (const auto* color : {"#fff", "ffffff", "#ggffff", "#12345678", " #123456"}) {
        value = Preferences(); value["lyricsFontColor"] = color; reject(value);
        value = Preferences(); value["lyricsKaraokeFillColor"] = color; reject(value);
    }
    for (const auto& font : {std::string(129, 'a'), std::string("bad\0font", 8), std::string("bad\nfont"),
                            std::string("bad\x7f"), std::string("bad\xc2\x85"), std::string("\xc0\xaf"),
                            std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80")}) {
        value = Preferences(); value["fontFamily"] = font; reject(value);
    }
    for (const auto* mode : {"partita", "fume", "sonnet", "gallery", "3d", "lumiere", "motif", "cadenza", "tilt", "cappella", "diorama", "pendolo",
                             "classic", "tempera"}) {
        value = Preferences(); value["mode"] = mode;
        Check(DecodeMusicPreferences(value, output) && output.mode == mode, "available modes accepted");
    }
    for (const auto* mode : {"monet"}) {
        value = Preferences(); value["mode"] = mode;
        value["fontFamily"] = "Arial"; value["lyricsFontColor"] = "#123456";
        Check(DecodeMusicPreferences(value, output) && output.mode == "partita" &&
              output.fontFamily == "Arial" && output.lyricsFontColor == "#123456",
              "removed mode migrates without losing other preferences");
        auto typed = output; typed.mode = mode;
        MusicPreferences unchanged = output;
        Check(!NormalizeMusicPreferences(typed, unchanged) && unchanged == output,
              "removed mode cannot be saved as a typed preference");
    }
    for (const auto* intensity : {"calm", "normal", "chaotic"}) {
        value = Preferences(); value["animationIntensity"] = intensity;
        Check(DecodeMusicPreferences(value, output), "all three intensities accepted");
    }
    value = Preferences(); value["fontFamily"] = std::string(128, 'a');
    value["lyricsFontColor"] = "#AB12EF"; value["lyricsKaraokeFillColor"] = "#FEDCBA";
    Check(DecodeMusicPreferences(value, output) && output.lyricsFontColor == "#ab12ef" &&
          output.lyricsKaraokeFillColor == "#fedcba", "128-byte font and normalized colors");
    value["fontFamily"] = u8"\u5fae\u8f6f\u96c5\u9ed1";
    Check(DecodeMusicPreferences(value, output) && EncodeMusicPreferences(output)["fontFamily"] == value["fontFamily"],
          "local Unicode family roundtrip");
    value["fontFamily"] = std::string(125, 'a') + u8"\u5b57";
    Check(DecodeMusicPreferences(value, output), "128 UTF-8 bytes accepted across multibyte boundary");
    value["fontFamily"] = std::string(126, 'a') + u8"\u5b57";
    Check(!DecodeMusicPreferences(value, output), "129 UTF-8 bytes rejected independently of character count");
}

void RemovedStyleMigration() {
    {
        Files files;
        MusicPreferencesSession session("p", files);
        auto apply = Apply(); apply["preferences"]["mode"] = "lumiere";
        Check(Reply(session, apply)["status"] == "ready", "Lumiere selection saves");
        MusicPreferencesSession reopened("p", files);
        Check(Json::parse(reopened.State())["preferences"]["mode"] == "lumiere",
              "Lumiere selection survives reopen");
    }
    for (const auto* mode : {"monet"}) {
        Files files;
        auto old = Preferences(); old["mode"] = mode;
        old["fontFamily"] = "Arial"; old["lyricsFontColor"] = "#123456";
        const auto bytes = Disk(old, 7);
        files.data["p"] = bytes;
        MusicPreferencesSession session("p", files);
        const auto loaded = Json::parse(session.State());
        Check(loaded["status"] == "ready" && loaded["revision"] == 7 &&
              loaded["preferences"]["mode"] == "partita" &&
              loaded["preferences"]["fontFamily"] == "Arial" &&
              loaded["preferences"]["lyricsFontColor"] == "#123456" &&
              files.data.at("p") == bytes && files.writes == 0,
              "removed style loads read-only with remaining preferences intact");
        auto changed = loaded["preferences"]; changed["mode"] = "sonnet";
        auto apply = Apply(1, 7); apply["preferences"] = changed;
        Check(Reply(session, apply)["revision"] == 8 &&
              Json::parse(files.data.at("p"))["preferences"] == changed &&
              files.data.at("p.bak") == bytes,
              "next save persists a supported style and backs up the original preferences");
    }
}

void SonnetCodecAndEquality() {
    auto value = Preferences();
    value["lyricsFontColor"] = "#123456";
    value["lyricsKaraokeFillColor"] = "#654321";
    value["sonnetColors"] = Json{{"lyricsFontColor", "#ABCDEF"}, {"karaokeFillColor", "#AA12EF"},
        {"glowColor", "#FE34DC"}};
    MusicPreferences output;
    std::string error = "before";
    Check(DecodeMusicPreferences(value, output, &error) && error.empty(), "v3 Sonnet colors decode");
    auto expected = value;
    expected["sonnetColors"] = Json{{"lyricsFontColor", "#abcdef"}, {"karaokeFillColor", "#aa12ef"},
        {"glowColor", "#fe34dc"}};
    Check(EncodeMusicPreferences(output) == expected, "Sonnet colors normalize independently of global colors");
    const auto original = output;
    MusicPreferences normalized;
    Check(NormalizeMusicPreferences(output, normalized) && normalized == original, "v3 typed normalization");
    const auto reject = [&](const Json& invalid) {
        Check(!DecodeMusicPreferences(invalid, output) && output == original, "invalid Sonnet object is transactional");
    };
    for (const auto& invalid : {Json(), Json(true), Json(1), Json("#abcdef"), Json::array(), Json::object()}) {
        auto next = expected; next["sonnetColors"] = invalid; reject(next);
    }
    for (const auto* field : {"lyricsFontColor", "karaokeFillColor", "glowColor"}) {
        auto next = expected; next["sonnetColors"].erase(field); reject(next);
        for (const auto& invalid : {Json(), Json(true), Json(123456), Json::array(), Json::object(),
                Json("#fff"), Json("ffffff"), Json("#ggffff"), Json("#12345678"), Json(" #123456"),
                Json("#123456 "), Json(std::string("#12\0abc", 7))}) {
            next = expected; next["sonnetColors"][field] = invalid; reject(next);
        }
        next = expected; next["sonnetColors"][field] = "#102030";
        MusicPreferences changed;
        Check(DecodeMusicPreferences(next, changed) && !(changed == original) && !(original == changed),
              "each Sonnet color participates in preference equality");
        Check(EncodeMusicPreferences(changed) == next, "changing one Sonnet color preserves every other field");
    }
    auto next = expected; next["sonnetColors"]["extra"] = "#ffffff"; reject(next);
    next = expected; next["sonnetColors"].erase("karaokeFillColor");
    next["sonnetColors"]["lyricsKaraokeFillColor"] = "#ffffff"; reject(next);
    MusicPreferences roundtrip;
    Check(DecodeMusicPreferences(EncodeMusicPreferences(original), roundtrip) && roundtrip == original,
          "v3 typed roundtrip equality");

    for (const auto member : {&MusicPreferences::sonnetLyricsFontColor, &MusicPreferences::sonnetKaraokeFillColor,
                             &MusicPreferences::sonnetGlowColor}) {
        auto typed = original; typed.*member = "#ABCDEF";
        normalized = original;
        Check(NormalizeMusicPreferences(typed, normalized) && normalized.*member == "#abcdef" &&
              typed.*member == "#ABCDEF", "typed Sonnet normalization does not mutate its input");
        Check(EncodeMusicPreferences(typed) == EncodeMusicPreferences(normalized), "typed encoder normalizes Sonnet colors");
        typed.*member = "invalid";
        normalized = original;
        Check(!NormalizeMusicPreferences(typed, normalized) && normalized == original,
              "typed invalid Sonnet color leaves output unchanged");
        bool threw = false;
        try { static_cast<void>(EncodeMusicPreferences(typed)); }
        catch (const std::invalid_argument&) { threw = true; }
        Check(threw, "typed encoder rejects malformed Sonnet color");
    }
}

void EffectCodecAndEquality() {
    MusicPreferences original;
    const auto initial = Preferences();
    Check(DecodeMusicPreferences(initial, original), "v3 DTO decodes before effect-only changes");
    Check(original.effectMode == "none" && MusicPreferences{}.effectMode == "none",
          "typed and decoded default effects are none");
    for (const auto* mode : {"partita", "fume", "sonnet", "gallery", "3d", "lumiere"}) {
        for (const auto* effect : {"none", "rain-window", "snow", "star-river"}) {
            auto value = initial; value["mode"] = mode; value["effectMode"] = effect;
            MusicPreferences output, normalized;
            Check(DecodeMusicPreferences(value, output) && output.effectMode == effect && EncodeMusicPreferences(output) == value &&
                  NormalizeMusicPreferences(output, normalized) && normalized == output,
                  "all effects are independent of every visualizer mode and roundtrip through typed normalization");
        }
    }
    auto value = initial; value["effectMode"] = "snow";
    MusicPreferences changed;
    Check(DecodeMusicPreferences(value, changed) && !(changed == original) && !(original == changed),
          "effect-only change participates in symmetric typed equality");
    for (const auto& invalid : {Json(), Json(true), Json(0), Json::array(), Json::object(),
            Json(""), Json("rain"), Json("classic"), Json("Snow"), Json(" snow"), Json("snow "),
            Json("rain_window"), Json(std::string("snow\0", 5))}) {
        auto bad = value; bad["effectMode"] = invalid;
        auto output = changed;
        Check(!DecodeMusicPreferences(bad, output) && output == changed,
              "invalid effect value or type leaves output unchanged");
        if (invalid.is_string()) {
            auto typed = changed; typed.effectMode = invalid.get<std::string>();
            Check(!NormalizeMusicPreferences(typed, output) && output == changed,
                  "typed invalid effect is rejected transactionally");
            bool threw = false;
            try { static_cast<void>(EncodeMusicPreferences(typed)); }
            catch (const std::invalid_argument&) { threw = true; }
            Check(threw, "typed encoder rejects an invalid effect instead of defaulting it");
        }
    }
    for (const auto version : {0, 1, 2, 4}) {
        auto typed = changed; typed.version = version;
        auto output = original;
        Check(!NormalizeMusicPreferences(typed, output) && output == original,
              "typed normalization only accepts v3 and does not migrate old typed values");
        bool threw = false;
        try { static_cast<void>(EncodeMusicPreferences(typed)); }
        catch (const std::invalid_argument&) { threw = true; }
        Check(threw, "typed encoder rejects old or unsupported typed versions");
    }
}

void LegacyMigration() {
    auto legacy = LegacyPreferences();
    legacy["mode"] = "sonnet"; legacy["fontFamily"] = "Arial"; legacy["animationIntensity"] = "calm";
    legacy["lyricsFontColor"] = "#AB12EF"; legacy["lyricsKaraokeFillColor"] = "#FEDCBA";
    auto expected = Preferences();
    expected["mode"] = "sonnet"; expected["fontFamily"] = "Arial"; expected["animationIntensity"] = "calm";
    expected["lyricsFontColor"] = "#ab12ef"; expected["lyricsKaraokeFillColor"] = "#fedcba";
    MusicPreferences migrated;
    Check(DecodeMusicPreferences(legacy, migrated) && migrated.version == 3 &&
          EncodeMusicPreferences(migrated) == expected, "v1 migration preserves globals and uses original Sonnet defaults");
    for (const auto* field : {"version", "mode", "fontFamily", "animationIntensity",
                             "lyricsFontColor", "lyricsKaraokeFillColor"}) {
        auto invalid = legacy; invalid.erase(field);
        Check(!DecodeMusicPreferences(invalid, migrated), "v1 migration requires all six fields");
        invalid = legacy; invalid[field] = nullptr;
        Check(!DecodeMusicPreferences(invalid, migrated), "v1 migration rejects malformed fields");
    }
    auto invalid = legacy; invalid["sonnetColors"] = expected["sonnetColors"];
    Check(!DecodeMusicPreferences(invalid, migrated), "v1 cannot smuggle Sonnet colors");
    invalid = legacy; invalid["extra"] = true;
    Check(!DecodeMusicPreferences(invalid, migrated), "v1 migration rejects unknown keys");
    invalid = legacy; invalid["effectMode"] = "none";
    Check(!DecodeMusicPreferences(invalid, migrated), "v1 cannot smuggle even the default effect");
    invalid = legacy; invalid["version"] = 2;
    Check(!DecodeMusicPreferences(invalid, migrated), "v2 cannot omit the Sonnet object");

    Files files; files.data["p"] = Disk(legacy, 8);
    const auto retained = files.data;
    MusicPreferencesSession session("p", files);
    auto state = Json::parse(session.State());
    Check(state["version"] == 1 && state["revision"] == 8 && state["preferences"] == expected &&
          files.data == retained && files.writes == 0, "v1 load migrates in memory without rewriting disk");
    auto apply = Apply(1, 8); apply["preferences"] = legacy;
    state = Reply(session, apply);
    const auto disk = Json::parse(files.data.at("p"));
    Check(state["revision"] == 9 && state["preferences"] == expected && disk["version"] == 1 &&
          disk["preferences"] == expected && files.data.at("p.bak") == retained.at("p"),
          "legacy command saves canonical v3 under unchanged v1 store and state envelopes");
    Files backup; backup.data["p"] = "broken"; backup.data["p.bak"] = Disk(legacy, 8);
    MusicPreferencesSession recovered("p", backup);
    Check(Json::parse(recovered.State())["preferences"] == expected && backup.writes == 0,
          "legacy backup migrates without implicit save");
}

void V2Migration() {
    auto legacy = V2Preferences();
    legacy["mode"] = "sonnet"; legacy["fontFamily"] = "Arial"; legacy["animationIntensity"] = "chaotic";
    legacy["lyricsFontColor"] = "#123ABC"; legacy["lyricsKaraokeFillColor"] = "#456DEF";
    legacy["sonnetColors"] = Json{{"lyricsFontColor", "#ABC123"}, {"karaokeFillColor", "#DEF456"},
        {"glowColor", "#789ABC"}};
    auto expected = legacy;
    expected["version"] = 3; expected["effectMode"] = "none";
    expected["lyricsFontColor"] = "#123abc"; expected["lyricsKaraokeFillColor"] = "#456def";
    expected["sonnetColors"] = Json{{"lyricsFontColor", "#abc123"}, {"karaokeFillColor", "#def456"},
        {"glowColor", "#789abc"}};
    MusicPreferences migrated;
    Check(DecodeMusicPreferences(legacy, migrated) && migrated.version == 3 &&
          EncodeMusicPreferences(migrated) == expected, "v2 migrates to no effect while preserving all colors and globals");
    const auto reject = [&](const Json& invalid) {
        const auto before = migrated;
        Check(!DecodeMusicPreferences(invalid, migrated) && migrated == before,
              "v2 migration strictly rejects malformed DTOs without changing output");
    };
    for (const auto* field : {"version", "mode", "fontFamily", "animationIntensity",
                             "lyricsFontColor", "lyricsKaraokeFillColor", "sonnetColors"}) {
        auto invalid = legacy; invalid.erase(field); reject(invalid);
        invalid = legacy; invalid[field] = nullptr; reject(invalid);
    }
    for (const auto* effect : {"none", "rain-window", "snow", "star-river"}) {
        auto invalid = legacy; invalid["effectMode"] = effect; reject(invalid);
        invalid = LegacyPreferences(); invalid["effectMode"] = effect; reject(invalid);
    }
    for (const auto* field : {"lyricsFontColor", "karaokeFillColor", "glowColor"}) {
        auto invalid = legacy; invalid["sonnetColors"].erase(field); reject(invalid);
        invalid = legacy; invalid["sonnetColors"][field] = "#fff"; reject(invalid);
    }
    auto invalid = legacy; invalid["sonnetColors"]["effectMode"] = "snow"; reject(invalid);
    invalid = legacy; invalid["extra"] = true; reject(invalid);
    invalid = legacy; invalid["version"] = 3; reject(invalid);

    Files files; files.data["p"] = Disk(legacy, 12);
    const auto retained = files.data;
    MusicPreferencesSession session("p", files);
    const auto loaded = Json::parse(session.State());
    Check(loaded["preferences"] == expected && loaded["revision"] == 12 &&
          files.data == retained && files.writes == 0, "v2 store load migrates without rewriting disk");
    auto apply = Apply(1, 12); apply["preferences"] = legacy;
    const auto saved = Reply(session, apply);
    Check(saved["version"] == 1 && saved["revision"] == 13 && saved["preferences"] == expected &&
          Json::parse(files.data.at("p")) == Json({{"version", 1}, {"revision", 13}, {"preferences", expected}}) &&
          files.data.at("p.bak") == retained.at("p"), "v2 command persists canonical v3 with exact v2 backup");
    Files backup; backup.data["p"] = "broken"; backup.data["p.bak"] = retained.at("p");
    MusicPreferencesSession recovered("p", backup);
    Check(Json::parse(recovered.State())["preferences"] == expected && backup.writes == 0,
          "v2 backup migrates all colors with no implicit save");
}

void Protocol() {
    Check(DecodeMusicPreferencesCommand(Snapshot()).has_value(), "snapshot command");
    Check(DecodeMusicPreferencesCommand(Reset(1, 0)).has_value(), "reset command");
    Check(DecodeMusicPreferencesCommand(Apply(kMusicPreferencesMaxSafeInteger, kMusicPreferencesMaxSafeInteger)).has_value(),
          "maximum safe request and revision");
    const auto reject = [](const Json& value) { Check(!DecodeMusicPreferencesCommand(value), "reject invalid command"); };
    for (const auto* key : {"version", "scope", "action", "request", "revision", "preferences"}) {
        auto value = Apply(); value.erase(key); reject(value);
    }
    auto value = Apply(); value["extra"] = 1; reject(value);
    value = Apply(); value["scope"] = "music"; reject(value);
    value = Apply(); value["version"] = 2; reject(value);
    value = Apply(); value["action"] = "play"; reject(value);
    value = Apply(); value["preferences"]["fontScale"] = 1; reject(value);
    value = Apply(); value["preferences"]["sonnetColors"]["unknown"] = "#ffffff"; reject(value);
    value = Apply(); value["preferences"]["sonnetColors"]["glowColor"] = "#xyzxyz"; reject(value);
    value = Apply(); value["preferences"]["effectMode"] = "rain"; reject(value);
    value = Apply(); value["preferences"].erase("effectMode"); reject(value);
    value = Apply(); value["preferences"]["version"] = 4; reject(value);
    for (const auto& legacy : {LegacyPreferences(), V2Preferences()}) {
        value = Apply(); value["preferences"] = legacy;
        const auto migrated = DecodeMusicPreferencesCommand(value);
        Check(migrated && EncodeMusicPreferences(*migrated->preferences)["effectMode"] == "none",
              "wire v1 envelope migrates exact v1/v2 DTOs to no effect");
        value["preferences"]["effectMode"] = "snow"; reject(value);
    }
    value = Snapshot(); value["request"] = 1; reject(value);
    value = Reset(1, 0); value["preferences"] = Preferences(); reject(value);
    for (const auto& number : {Json(-1), Json(1.5), Json(1.0), Json(true), Json("1"),
                              Json(kMusicPreferencesMaxSafeInteger + 1), Json(std::numeric_limits<std::uint64_t>::max())}) {
        value = Apply(); value["request"] = number; reject(value);
        value = Apply(); value["revision"] = number; reject(value);
    }
    value = Apply(); value["request"] = 0; reject(value);
    Check(!ParseMusicPreferencesPageJson(L"{\"version\":1,\"version\":1,\"scope\":\"music-preferences\",\"action\":\"snapshot\"}"),
          "duplicate envelope keys rejected");
    Check(!ParseMusicPreferencesPageJson(L"[]") && !ParseMusicPreferencesPageJson(L"{} trailing"), "malformed wire rejected");
    Check(!ParseMusicPreferencesPageJson(std::wstring(kMusicPreferencesMaxBytes + 1, L' ')), "bounded page message");
    value = Apply(); value["preferences"]["fontFamily"] = u8"\u5fae\u8f6f\u96c5\u9ed1";
    const auto escaped = value.dump(-1, ' ', true);
    const auto parsed = ParseMusicPreferencesPageJson(std::wstring(escaped.begin(), escaped.end()));
    Check(parsed && parsed->preferences->fontFamily == value["preferences"]["fontFamily"], "escaped Unicode page command");
    auto duplicate = escaped;
    const auto index = duplicate.find("\"fontFamily\":");
    duplicate.insert(index, "\"fontFamily\":\"other\",");
    Check(!ParseMusicPreferencesPageJson(std::wstring(duplicate.begin(), duplicate.end())), "duplicate nested fields rejected");
    duplicate = escaped;
    duplicate.insert(duplicate.find("\"glowColor\":"), "\"glowColor\":\"#123456\",");
    Check(!ParseMusicPreferencesPageJson(std::wstring(duplicate.begin(), duplicate.end())),
          "duplicate Sonnet keys rejected on the wire");
    duplicate = escaped;
    duplicate.insert(duplicate.find("\"effectMode\":"), "\"effectMode\":\"snow\",");
    Check(!ParseMusicPreferencesPageJson(std::wstring(duplicate.begin(), duplicate.end())),
          "duplicate effect keys rejected on the wire");
    auto bounded = escaped;
    bounded.append(kMusicPreferencesMaxBytes - bounded.size(), ' ');
    Check(ParseMusicPreferencesPageJson(std::wstring(bounded.begin(), bounded.end())).has_value(),
          "v3 command accepted at existing byte bound");
    bounded.push_back(' ');
    Check(!ParseMusicPreferencesPageJson(std::wstring(bounded.begin(), bounded.end())), "v3 command above bound rejected");
    bool depthRejected = false;
    try {
        detail::ParseMusicPreferencesJson(std::string_view("{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":0}}}}}}"));
    } catch (const std::exception&) { depthRejected = true; }
    Check(depthRejected, "existing parser depth limit retained");
    auto rawWide = std::wstring(escaped.begin(), escaped.end());
    const auto start = rawWide.find(L"\\u5fae");
    Check(start != std::wstring::npos, "find escaped Unicode fixture");
    rawWide.replace(start, 6, L"\u5fae");
    Check(ParseMusicPreferencesPageJson(rawWide).has_value(), "raw Unicode page input");
    rawWide.replace(start, 1, std::wstring(1, static_cast<wchar_t>(0xd800)) + L"A");
    Check(!ParseMusicPreferencesPageJson(rawWide), "unpaired UTF-16 code unit rejected before conversion");
    MusicPreferencesMessage message;
    message.preferences.fontFamily = u8"\u5fae\u8f6f\u96c5\u9ed1";
    const auto messageJson = Json::parse(EncodeMusicPreferencesPageState(message));
    Check(messageJson["version"] == 1 && messageJson["preferences"]["version"] == 3 &&
          messageJson["preferences"]["effectMode"] == "none" &&
          messageJson["preferences"]["sonnetColors"] == Preferences()["sonnetColors"] &&
          messageJson["preferences"]["fontFamily"] == message.preferences.fontFamily,
          "wide v1 state envelope carries canonical v3 preferences and Unicode");
}

void SessionAndPersistence() {
    Files files;
    MusicPreferencesSession session("p", files);
    auto state = Json::parse(session.State());
    Check(state == Json({{"version", 1}, {"type", "music-preferences-state"}, {"revision", 0}, {"request", 0},
          {"status", "ready"}, {"error", ""}, {"preferences", Preferences()}}), "fresh exact state envelope");
    Check(files.writes == 0, "fresh state does not create files");
    auto apply = Apply(17); apply["preferences"]["mode"] = "3d";
    apply["preferences"]["effectMode"] = "rain-window";
    apply["preferences"]["lyricsFontColor"] = "#AABBCC";
    apply["preferences"]["sonnetColors"]["lyricsFontColor"] = "#FE1234";
    apply["preferences"]["sonnetColors"]["karaokeFillColor"] = "#56ABCD";
    apply["preferences"]["sonnetColors"]["glowColor"] = "#7890EF";
    state = Reply(session, apply);
    Check(state["revision"] == 1 && state["request"] == 17 && state["status"] == "ready" &&
          state["preferences"]["lyricsFontColor"] == "#aabbcc" &&
          state["preferences"]["effectMode"] == "rain-window" &&
          state["preferences"]["sonnetColors"] == Json({{"lyricsFontColor", "#fe1234"},
              {"karaokeFillColor", "#56abcd"}, {"glowColor", "#7890ef"}}),
          "commit normalizes global and Sonnet colors and increments exactly once");
    const auto bytes = files.data.at("p");
    MusicPreferencesSession reopened("p", files);
    Check(Json::parse(reopened.State())["revision"] == 1 &&
          Json::parse(reopened.State())["preferences"] == state["preferences"], "reopened revision and all preferences");
    const auto writes = files.writes;
    state = Reply(session, Apply(18, 0));
    Check(state["error"] == "conflict" && state["revision"] == 1 && state["request"] == 18 &&
          state["preferences"]["mode"] == "3d" && state["preferences"]["effectMode"] == "rain-window" &&
          files.writes == writes, "stale revision cannot save or replace committed effect");
    state = Reply(session, Reset(19, 1));
    Check(state["preferences"] == Preferences() && state["revision"] == 2 && files.data.at("p.bak") == bytes,
          "reset commits defaults and backs up old revision");
    Check(Json::parse(session.State())["request"] == 0, "State never repeats mutation request");
    state = Reply(reopened, Apply(20, 1));
    Check(state["error"] == "conflict" && state["revision"] == 1, "second owner detects disk revision changes");
    MusicPreferencesCommand forged;
    forged.action = MusicPreferencesAction::Apply; forged.request = 0; forged.preferences = MusicPreferences{};
    Check(Json::parse(session.HandlePage(forged))["error"] == "invalid", "typed commands also validate request");
    forged.request = 21; forged.revision = 2; forged.preferences->fontFamily = std::string("bad\0font", 8);
    Check(Json::parse(session.HandlePage(forged))["error"] == "invalid", "typed command cannot bypass DTO validation");
    const auto retained = files.data;
    const auto committedWrites = files.writes;
    for (const auto member : {&MusicPreferences::sonnetLyricsFontColor, &MusicPreferences::sonnetKaraokeFillColor,
                             &MusicPreferences::sonnetGlowColor}) {
        forged.preferences = MusicPreferences{};
        (*forged.preferences).*member = "#fff";
        const auto rejected = Json::parse(session.HandlePage(forged));
        Check(rejected["error"] == "invalid" && rejected["revision"] == 2 &&
              rejected["preferences"] == Preferences() && files.data == retained && files.writes == committedWrites,
              "typed session rejects each invalid Sonnet color without changing committed state or disk");
    }
    for (const auto* effect : {"", "rain", "Snow", "snow "}) {
        forged.preferences = MusicPreferences{};
        forged.preferences->effectMode = effect;
        const auto rejected = Json::parse(session.HandlePage(forged));
        Check(rejected["error"] == "invalid" && rejected["revision"] == 2 &&
              rejected["preferences"] == Preferences() && files.data == retained && files.writes == committedWrites,
              "typed session rejects invalid effects without changing committed state or disk");
    }
}

void SonnetOnlyChanges() {
    Files files;
    MusicPreferencesSession session("p", files);
    auto expected = Preferences();
    expected["effectMode"] = "star-river";
    expected["lyricsFontColor"] = "#112233"; expected["lyricsKaraokeFillColor"] = "#445566";
    auto apply = Apply(); apply["preferences"] = expected;
    Check(Reply(session, apply)["revision"] == 1, "set independent global colors");
    std::uint64_t revision = 1;
    for (const auto* field : {"lyricsFontColor", "karaokeFillColor", "glowColor"}) {
        expected["sonnetColors"][field] = "#aabbcc";
        apply = Apply(revision + 1, revision); apply["preferences"] = expected;
        const auto state = Reply(session, apply);
        ++revision;
        Check(state["revision"] == revision && state["preferences"] == expected,
              "Sonnet-only changes increment revision and preserve effect, global and sibling colors");
        MusicPreferencesSession reopened("p", files);
        Check(Json::parse(reopened.State())["preferences"] == expected, "Sonnet-only changes persist");

        Files concurrent; concurrent.data["p"] = Disk(expected, revision);
        MusicPreferencesSession stale("p", concurrent);
        auto external = expected; external["sonnetColors"][field] = "#123456";
        concurrent.data["p"] = Disk(external, revision);
        const auto conflict = Reply(stale, Reset(20, revision));
        Check(conflict["error"] == "conflict" && conflict["revision"] == revision &&
              conflict["preferences"] == expected && concurrent.writes == 0,
              "same-revision external changes to each Sonnet color conflict without writes");
    }
    Check(Reply(session, Reset(30, revision))["preferences"] == Preferences(),
          "whole reset restores original blue Sonnet defaults and white global defaults");
}

void EffectOnlyChanges() {
    Files files;
    MusicPreferencesSession session("p", files);
    auto expected = Preferences();
    expected["mode"] = "sonnet"; expected["animationIntensity"] = "chaotic";
    expected["lyricsFontColor"] = "#123456"; expected["lyricsKaraokeFillColor"] = "#654321";
    expected["sonnetColors"] = Json{{"lyricsFontColor", "#102030"}, {"karaokeFillColor", "#405060"},
        {"glowColor", "#708090"}};
    auto apply = Apply(); apply["preferences"] = expected;
    Check(Reply(session, apply)["revision"] == 1, "seed independent colors and renderer before effects");
    std::uint64_t revision = 1;
    for (const auto* effect : {"rain-window", "snow", "star-river", "none"}) {
        const auto before = files.data.at("p");
        expected["effectMode"] = effect;
        apply = Apply(revision + 1, revision); apply["preferences"] = expected;
        const auto state = Reply(session, apply);
        ++revision;
        Check(state["revision"] == revision && state["preferences"] == expected &&
              files.data.at("p.bak") == before,
              "effect-only change commits once, preserves every other field and backs up exact prior state");
        MusicPreferencesSession reopened("p", files);
        Check(Json::parse(reopened.State())["preferences"] == expected &&
              Json::parse(files.data.at("p"))["version"] == 1, "effect-only changes persist under v1 store envelope");
        Files concurrent; concurrent.data["p"] = Disk(expected, revision);
        MusicPreferencesSession stale("p", concurrent);
        auto external = expected; external["effectMode"] = expected["effectMode"] == "none" ? "snow" : "none";
        concurrent.data["p"] = Disk(external, revision);
        const auto retained = concurrent.data;
        const auto conflict = Reply(stale, Reset(20, revision));
        Check(conflict["error"] == "conflict" && conflict["revision"] == revision &&
              conflict["preferences"] == expected && concurrent.writes == 0 && concurrent.data == retained,
              "same-revision external effect-only change is detected by typed equality");
    }
    expected["effectMode"] = "snow";
    apply = Apply(30, revision); apply["preferences"] = expected;
    const auto changed = Reply(session, apply);
    ++revision;
    Check(changed["revision"] == revision, "set nondefault effect before reset");
    Check(Reply(session, Reset(31, revision))["preferences"] == Preferences(),
          "whole reset restores no effect with independent Sonnet and global defaults");
}

void SaveFailures() {
    auto committed = Preferences();
    committed["effectMode"] = "star-river";
    committed["sonnetColors"] = Json{{"lyricsFontColor", "#123456"}, {"karaokeFillColor", "#abcdef"},
        {"glowColor", "#fedcba"}};
    const auto old = Disk(committed, 4);
    for (const auto* failure : {"write:p.tmp", "write:p.bak.tmp", "rename:p.bak.tmp", "rename:p.tmp", "read:p"}) {
        Files files; files.data["p"] = old;
        MusicPreferencesSession session("p", files);
        files.fail = failure;
        auto apply = Apply(5, 4); apply["preferences"]["mode"] = "gallery";
        apply["preferences"]["effectMode"] = "rain-window";
        apply["preferences"]["sonnetColors"]["glowColor"] = "#112233";
        const auto state = Reply(session, apply);
        Check(state["status"] == "error" && state["error"] == "save" && state["revision"] == 4 &&
              state["preferences"] == committed && files.data.at("p") == old,
              "save failure retains committed effect, all colors and main");
        Check(!files.data.count("p.tmp") && !files.data.count("p.bak.tmp"), "save failure cleans its temporary files");
        files.fail.clear();
        const auto retried = Reply(session, apply);
        Check(retried["revision"] == 5 && retried["preferences"] == apply["preferences"] &&
              Json::parse(files.data.at("p"))["preferences"] == apply["preferences"],
              "save retry uses unchanged revision and persists the new effect");
    }
    Files files; files.data["p"] = Disk(Preferences(), kMusicPreferencesMaxSafeInteger);
    MusicPreferencesSession full("p", files);
    const auto state = Reply(full, Reset(1, kMusicPreferencesMaxSafeInteger));
    Check(state["error"] == "invalid" && files.writes == 0, "revision overflow rejected without writes");
    Files resetFiles; resetFiles.data["p"] = old;
    MusicPreferencesSession resetSession("p", resetFiles);
    resetFiles.fail = "rename:p.tmp";
    Check(Reply(resetSession, Reset(8, 4))["error"] == "save" && resetFiles.data.at("p") == old &&
          Json::parse(resetSession.State())["revision"] == 4 &&
          Json::parse(resetSession.State())["preferences"] == committed,
          "reset failure retains revision, effect and all colors");
}

void RecoveryAndVersions() {
    auto other = Preferences(); other["mode"] = "sonnet";
    other["effectMode"] = "snow";
    const auto backup = Disk(other, 7);
    Files files; files.data["p"] = "broken"; files.data["p.bak"] = backup;
    MusicPreferencesSession recovered("p", files);
    Check(Json::parse(recovered.State())["preferences"] == other && Json::parse(recovered.State())["revision"] == 7 &&
          files.writes == 0, "bad main recovers backup without implicit save");
    Check(Reply(recovered, Reset(1, 7))["revision"] == 8 && files.data.at("p.bak") == backup,
          "recovered save preserves good backup");
    files.data.erase("p");
    MusicPreferencesSession missing("p", files);
    Check(Json::parse(missing.State())["revision"] == 7, "missing main recovers backup");
    for (const auto& version : {Json(0), Json(2), Json("future")}) {
        Files future; auto doc = Json::parse(backup); doc["version"] = version;
        future.data["p"] = doc.dump(); future.data["p.bak"] = backup;
        const auto retained = future.data;
        MusicPreferencesSession blocked("p", future);
        Check(Json::parse(blocked.State())["error"] == "load", "unknown/old schema is not treated as corrupt");
        Check(Reply(blocked, Reset(1, 0))["error"] == "load" && future.data == retained && future.writes == 0,
              "reset cannot overwrite unsupported schema");
    }
    Files appeared;
    MusicPreferencesSession fresh("p", appeared);
    auto future = Json::parse(backup); future["preferences"]["version"] = 4;
    appeared.data["p"] = future.dump();
    Check(Reply(fresh, Apply())["error"] == "load" && appeared.writes == 0, "late unsupported nested version protected");
    Files deepFuture;
    auto deep = Json::object();
    for (unsigned i = 0; i < 10; ++i) deep = Json{{"nested", deep}};
    auto futureDocument = Json::parse(backup); futureDocument["version"] = 2; futureDocument["extension"] = deep;
    deepFuture.data["p"] = futureDocument.dump(); deepFuture.data["p.bak"] = backup;
    MusicPreferencesSession deepBlocked("p", deepFuture);
    Check(Json::parse(deepBlocked.State())["error"] == "load" && Reply(deepBlocked, Reset(1, 0))["error"] == "load" &&
          deepFuture.writes == 0, "future schema is protected even beyond current allowed depth");
    Files futureBackup; futureBackup.data["p"] = Disk(Preferences(), 0); futureBackup.data["p.bak"] = future.dump();
    MusicPreferencesSession guarded("p", futureBackup);
    Check(Reply(guarded, Apply())["error"] == "load" && futureBackup.writes == 0, "unknown backup cannot be overwritten");
    for (const auto& bad : {std::string("broken"), std::string(kMusicPreferencesMaxBytes + 1, 'x'),
                            std::string("{\"version\":1,\"revision\":0,\"preferences\":{}}")}) {
        Files damaged; damaged.data["p"] = bad;
        MusicPreferencesSession session("p", damaged);
        Check(Json::parse(session.State())["error"] == "load" && Reply(session, Apply())["error"] == "load" &&
              damaged.writes == 0 && damaged.data.at("p") == bad, "unrecoverable data is preserved");
    }
    Files unknownKey;
    auto extra = Json::parse(backup); extra["extra"] = true; unknownKey.data["p"] = extra.dump();
    MusicPreferencesSession invalidDocument("p", unknownKey);
    Check(Json::parse(invalidDocument.State())["error"] == "load", "disk envelope unknown keys rejected");
    Files duplicate;
    duplicate.data["p"] = "{\"version\":1,\"version\":1," + backup.substr(1);
    MusicPreferencesSession invalidDuplicate("p", duplicate);
    Check(Json::parse(invalidDuplicate.State())["error"] == "load", "disk duplicate keys rejected");
    Files duplicateSonnet;
    auto duplicateColors = backup;
    duplicateColors.insert(duplicateColors.find("\"glowColor\":"), "\"glowColor\":\"#ffffff\",");
    duplicateSonnet.data["p"] = duplicateColors;
    MusicPreferencesSession invalidColors("p", duplicateSonnet);
    Check(Json::parse(invalidColors.State())["error"] == "load" && duplicateSonnet.writes == 0,
          "disk duplicate Sonnet keys rejected without writes");
    Files duplicateEffect;
    auto duplicateEffects = backup;
    duplicateEffects.insert(duplicateEffects.find("\"effectMode\":"), "\"effectMode\":\"none\",");
    duplicateEffect.data["p"] = duplicateEffects;
    MusicPreferencesSession invalidEffect("p", duplicateEffect);
    Check(Json::parse(invalidEffect.State())["error"] == "load" && duplicateEffect.writes == 0,
          "disk duplicate effect keys rejected without writes");
    Files malformedSonnet;
    auto badColors = other; badColors["sonnetColors"].erase("glowColor");
    malformedSonnet.data["p"] = Disk(badColors, 8); malformedSonnet.data["p.bak"] = backup;
    MusicPreferencesSession recoveredColors("p", malformedSonnet);
    Check(Json::parse(recoveredColors.State())["preferences"] == other &&
          Json::parse(recoveredColors.State())["revision"] == 7 && malformedSonnet.writes == 0,
          "malformed supported v3 recovers a valid backup without writes");
    for (const auto& malformed : {Json(), Json("rain"), Json(1)}) {
        auto badEffect = other; badEffect["effectMode"] = malformed;
        Files damaged; damaged.data["p"] = Disk(badEffect, 8); damaged.data["p.bak"] = backup;
        const auto retained = damaged.data;
        MusicPreferencesSession recoveredEffect("p", damaged);
        Check(Json::parse(recoveredEffect.State())["preferences"] == other &&
              Json::parse(recoveredEffect.State())["revision"] == 7 && damaged.data == retained && damaged.writes == 0,
              "malformed v3 effect recovers the exact backup effect without writes");
    }
    Files bounded;
    bounded.data["p"] = backup + std::string(kMusicPreferencesMaxBytes - backup.size(), ' ');
    MusicPreferencesSession atLimit("p", bounded);
    Check(Json::parse(atLimit.State())["preferences"] == other && bounded.writes == 0,
          "v3 disk accepted at exact 4096-byte bound");
    bounded.data["p"].push_back(' ');
    MusicPreferencesSession aboveLimit("p", bounded);
    Check(Json::parse(aboveLimit.State())["error"] == "load" && bounded.writes == 0,
          "v3 disk above 4096 bytes is preserved and rejected");
    Files missingMain; missingMain.data["p.bak"] = "bad";
    MusicPreferencesSession badBackup("p", missingMain);
    Check(Json::parse(badBackup.State())["error"] == "load", "invalid backup alone is not a fresh install");
    Files empty;
    MusicPreferencesSession emptyPath(std::filesystem::path{}, empty);
    Check(Json::parse(emptyPath.State())["error"] == "load" && Reply(emptyPath, Apply())["error"] == "load" &&
          empty.writes == 0, "empty injected path never resolves a real default location");
}

void LocalFiles() {
    const auto leaf = "lume-music-preferences-test-" + std::to_string(GetCurrentProcessId()) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / leaf;
    Check(std::filesystem::create_directory(root), "create isolated test root");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{root};
    const auto path = root / L"\u504f\u597d.json";
    lume::platform::windows::LocalSettingsFileStore files;
    auto legacy = LegacyPreferences();
    legacy["mode"] = "sonnet"; legacy["fontFamily"] = u8"\u5fae\u8f6f\u96c5\u9ed1";
    legacy["lyricsFontColor"] = "#123ABC"; legacy["lyricsKaraokeFillColor"] = "#456DEF";
    const auto legacyBytes = Disk(legacy, 7);
    Check(files.Write(path.u8string(), legacyBytes), "seed isolated real v1 preferences");
    MusicPreferencesSession session(path, files);
    auto expected = Preferences();
    expected["mode"] = "sonnet"; expected["fontFamily"] = legacy["fontFamily"];
    expected["lyricsFontColor"] = "#123abc"; expected["lyricsKaraokeFillColor"] = "#456def";
    std::string bytes;
    Check(Json::parse(session.State())["preferences"] == expected && Json::parse(session.State())["revision"] == 7 &&
          files.ReadBounded(path.u8string(), kMusicPreferencesMaxBytes, bytes) && bytes == legacyBytes &&
          !files.Exists(path.u8string() + ".bak"), "real v1 load migrates without rewriting or backup creation");
    expected["sonnetColors"] = Json{{"lyricsFontColor", "#112233"}, {"karaokeFillColor", "#445566"},
        {"glowColor", "#778899"}};
    expected["effectMode"] = "star-river";
    auto value = Apply(1, 7); value["preferences"] = expected;
    Check(Reply(session, value)["revision"] == 8, "real Unicode-path v3 save after migration");
    Check(files.ReadBounded(path.u8string(), kMusicPreferencesMaxBytes, bytes) &&
          Json::parse(bytes) == Json({{"version", 1}, {"revision", 8}, {"preferences", expected}}),
          "real persisted v1 envelope contains exact canonical v3 preferences");
    Check(files.ReadBounded(path.u8string() + ".bak", kMusicPreferencesMaxBytes, bytes) && bytes == legacyBytes,
          "real migration save retains exact v1 backup");
    MusicPreferencesSession reopened(path, files);
    Check(Json::parse(reopened.State())["preferences"] == expected,
          "real adapter reopens Unicode, effect and independent Sonnet colors");
    const auto reset = Reply(reopened, Reset(2, 8));
    Check(reset["revision"] == 9 && reset["preferences"] == Preferences(), "real whole reset restores no effect and Sonnet defaults");
    Check(files.Write(path.u8string(), "damaged"), "corrupt isolated main");
    MusicPreferencesSession recovered(path, files);
    Check(Json::parse(recovered.State())["revision"] == 8 &&
          Json::parse(recovered.State())["preferences"] == expected, "real v3 backup recovery preserves effect and Sonnet colors");
    Check(Reply(recovered, Reset(3, 8))["revision"] == 9, "real recovery save");
    const auto temp = std::filesystem::u8path(path.u8string() + ".tmp");
    Check(std::filesystem::create_directory(temp), "obstruct isolated temporary path");
    Check(Reply(recovered, Reset(4, 9))["error"] == "save", "real write failure reported");
    Check(Json::parse(recovered.State())["revision"] == 9, "real write failure retains revision");
}
}

int main() {
    const struct { const char* name; void (*run)(); } cases[] = {
        {"Codec", Codec}, {"RemovedStyleMigration", RemovedStyleMigration},
        {"SonnetCodecAndEquality", SonnetCodecAndEquality}, {"EffectCodecAndEquality", EffectCodecAndEquality},
        {"LegacyMigration", LegacyMigration}, {"V2Migration", V2Migration},
        {"Protocol", Protocol}, {"SessionAndPersistence", SessionAndPersistence}, {"SonnetOnlyChanges", SonnetOnlyChanges},
        {"EffectOnlyChanges", EffectOnlyChanges},
        {"SaveFailures", SaveFailures}, {"RecoveryAndVersions", RecoveryAndVersions}, {"LocalFiles", LocalFiles}};
    bool failed = false;
    for (const auto& test : cases) {
        try {
            test.run();
            std::cout << "PASS: " << test.name << '\n';
        } catch (const std::exception& error) {
            failed = true;
            std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
        }
    }
    return failed ? 1 : 0;
}
