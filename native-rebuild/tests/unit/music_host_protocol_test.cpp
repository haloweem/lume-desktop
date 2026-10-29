#include "core/music/music_host_protocol.h"
#include "core/ui/music_page_protocol.h"
#include "vendor/nlohmann/json.hpp"

#include <cstdlib>
#include <iostream>

using lume::music::DecodeHostCommand;
using lume::music::EncodeHostEvent;

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    const auto valid = R"({"version":1,"session":"abc","sequence":1,"action":"seek","value":12.5})";
    const auto command = DecodeHostCommand(valid);
    Expect(command.has_value(), "valid seek command is accepted");
    Expect(command->session == "abc" && command->sequence == 1, "command identity is preserved");
    Expect(command->value == 12.5, "seek value is preserved");
    const auto prepare = DecodeHostCommand(R"({"version":1,"session":"abc","sequence":2,"action":"prepare","value":120,"follow":true})");
    Expect(prepare && prepare->follow && prepare->value == 120 &&
        DecodeHostCommand(lume::music::EncodeHostCommand(*prepare))->follow, "buffering target and follow clock roundtrip");

    for (const auto* invalid : {
        R"({"version":1,"session":"abc","sequence":1,"action":"unknown"})",
        R"({"version":2,"session":"abc","sequence":1,"action":"play"})",
        R"({"version":1,"session":"abc","sequence":0,"action":"play"})",
        R"({"version":1,"session":"abc","sequence":1,"action":"seek","value":-1})",
        R"({"version":1,"session":"abc","sequence":1,"action":"play","value":1})",
        R"({"version":1,"session":"abc","sequence":1,"action":"prepare"})",
        R"({"version":1,"session":"abc","sequence":1,"action":"prepare","value":1,"follow":1})",
        R"({"version":1,"session":"abc","sequence":1,"action":"play","follow":true})",
    }) {
        Expect(!DecodeHostCommand(invalid).has_value(), "invalid command is rejected");
    }

    std::string oversized(65537, 'x');
    Expect(!DecodeHostCommand(oversized).has_value(), "oversized frame is rejected");

    lume::music::HostEvent event;
    event.session = "abc";
    event.sequence = 2;
    event.status = "ready";
    event.trackId = "one";
    event.position = 4.0;
    event.duration = 30.0;
    const auto encoded = EncodeHostEvent(event);
    Expect(encoded.find("\"status\":\"ready\"") != std::string::npos, "event contains status");
    Expect(encoded.find("\"trackId\":\"one\"") != std::string::npos, "event contains track identity");

    const auto volume = DecodeHostCommand(R"({"version":1,"session":"s","sequence":1,"action":"setVolume","value":0.25})");
    const auto muted = DecodeHostCommand(R"({"version":1,"session":"s","sequence":2,"action":"setMuted","value":true})");
    Expect(volume && volume->value == 0.25 && muted && muted->flag, "native control commands decode");
    Expect(DecodeHostCommand(lume::music::EncodeHostCommand(*muted))->flag, "mute uses a boolean wire value");
    for (const auto* invalid : {
        R"({"version":1,"session":"s","sequence":1,"action":"setVolume","value":true})",
        R"({"version":1,"session":"s","sequence":1,"action":"setVolume","value":1.1})",
        R"({"version":1,"session":"s","sequence":1,"action":"setVolume"})",
        R"({"version":1,"session":"s","sequence":1,"action":"setMuted","value":1})",
        R"({"version":1,"session":"s","sequence":1,"action":"play","analysis":true})",
    }) Expect(!DecodeHostCommand(invalid), "invalid controls and analysis requests are rejected");
    event.volume = 0.25;
    event.ready = false;
    event.muted = true;
    event.analysis.energy = 0.5;
    event.analysis.bins.fill(0.125);
    const auto roundtrip = lume::music::DecodeHostEvent(EncodeHostEvent(event));
    Expect(roundtrip && !roundtrip->ready && roundtrip->volume == 0.25 && roundtrip->muted &&
        roundtrip->analysis.energy == 0.5 && roundtrip->analysis.bins[31] == 0.125, "bounded analysis roundtrips");
    auto json = nlohmann::json::parse(EncodeHostEvent(event));
    json["ready"] = "true";
    Expect(!lume::music::DecodeHostEvent(json.dump()), "buffering readiness must be a boolean");
    json = nlohmann::json::parse(EncodeHostEvent(event));
    json["analysis"]["bins"].push_back(0);
    Expect(!lume::music::DecodeHostEvent(json.dump()), "oversized analysis bins fail closed");
    json = nlohmann::json::parse(EncodeHostEvent(event));
    json["analysis"]["energy"] = -1;
    Expect(!lume::music::DecodeHostEvent(json.dump()), "negative analysis fails closed");
    using lume::ui::ParseMusicPageJson;
    const auto pageVolume = ParseMusicPageJson(LR"({"version":1,"scope":"music","action":"setVolume","value":0.3})");
    const auto pageMute = ParseMusicPageJson(LR"({"version":1,"scope":"music","action":"setMuted","value":true})");
    const auto next = ParseMusicPageJson(LR"({"version":1,"scope":"music","action":"insertNext","session":1,"id":"opaque"})");
    Expect(pageVolume && pageVolume->volume == 0.3 && pageMute && pageMute->flag && next &&
        next->action == lume::ui::MusicAction::InsertNext, "page controls use native typed actions");
    for (const auto* invalid : {
        LR"({"version":1,"scope":"music","action":"setVolume","value":2})",
        LR"({"version":1,"scope":"music","action":"setVolume","value":"0.2"})",
        LR"({"version":1,"scope":"music","action":"setMuted","value":1})",
        LR"({"version":1,"scope":"music","action":"insertNext","session":1,"id":""})",
        LR"({"version":1,"scope":"music","action":"insertNext","session":1,"id":"x","source":"forged.wav"})",
    }) Expect(!ParseMusicPageJson(invalid), "page controls cannot inject sources or invalid values");
    lume::ui::MusicPageState page;
    lume::music::Track track;
    track.id = "opaque";
    track.source = "https://example.invalid/audio?secret=signed";
    page.snapshot.current = track;
    page.snapshot.queue = {track};
    const auto projection = lume::ui::EncodeMusicPageState(page);
    Expect(projection.find(L"secret") == std::wstring::npos && projection.find(L"source") == std::wstring::npos,
        "page projection never contains playback sources");
    const auto lyrics = lume::ui::EncodeMusicLyrics({1, "netease:123",
        {{1, 3, "ab", {{1, 1.5, "a"}, {1.5, 2.5, "b"}}, "translation"}}});
    const auto lyricsJson = nlohmann::json::parse(lyrics);
    Expect(lyricsJson["lines"][0]["words"][1]["endTime"] == 2.5 &&
        lyricsJson["lines"][0]["translation"] == "translation", "provider timing and translation are preserved");
    const auto badLyrics = lume::ui::EncodeMusicLyrics({1, "netease:123",
        {{1, 3, "ab", {{1, 5, "ab"}}, "translation"}}});
    Expect(nlohmann::json::parse(badLyrics)["lines"].empty(), "shared lyric validation rejects out-of-line words");

    std::cout << "music host protocol behavior passed\n";
    return 0;
}
