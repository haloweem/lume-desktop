// Exercise the real matcher/cache with a deterministic provider, never a network.
#include "apps/shell/external_lyrics.cpp"
#include "platform/windows/dynamic_island/media_timeline.h"
#include <iostream>
#include <limits>

namespace {
std::atomic<unsigned> lookups{0};
unsigned failures = 0;
void Check(bool value, const char* reason) {
    if (!value) { ++failures; std::cerr << "FAIL: " << reason << '\n'; }
}
}

namespace lume::music::online {
Json Execute(auth::Provider, std::string_view action, const Json&, const Json&,
    const auth::Cancelled& cancelled, const auth::Exchange&) {
    if (cancelled && cancelled()) throw std::runtime_error("cancelled");
    ++lookups;
    if (action == "search") return Json{{"tracks", Json::array({{
        {"id", "fixture"}, {"title", "Song"}, {"artist", "Singer"}, {"duration", 180}
    }})}};
    return Json{{"lrc", {{"lyric", "[00:01]one\n[00:10]two\n[00:30]three"}}}};
}
}

int main() {
    using namespace lume;
    using platform::windows::ReadMediaTimeline;
    const auto absent = ReadMediaTimeline(0, 0, 13438700000.0, false, true);
    Check(!absent.known && absent.position == 0, "NetEase default SMTC timestamp is unknown, never thirteen billion seconds");
    const auto incomplete = ReadMediaTimeline(0, 35, .25, true, true);
    Check(incomplete.known && incomplete.position == 35.25 && incomplete.duration == 0,
        "a real position can advance without a reported total length");
    Check(ReadMediaTimeline(180, 35, 100, true, false).position == 35, "pause does not advance an old timestamp");
    Check(ReadMediaTimeline(180, 179.5, 1, true, true).position == 180, "known total bounds playing position");
    Check(!ReadMediaTimeline(180, 0, 0, false, true).known, "length without an update timestamp is not a measured position");
    Check(!ReadMediaTimeline(0, std::numeric_limits<double>::infinity(), 0, true, true).known,
        "nonfinite position is rejected");
    Check(!ReadMediaTimeline(0, 35, -10, true, true).known, "future timestamp is rejected");
    Check(ReadMediaTimeline(0, 35, -.1, true, true).position == 35, "small timestamp skew does not move backward");

    dynamic_island::Model model;
    dynamic_island::Media media;
    media.available = media.playing = true;
    media.session = 1; media.id = "track"; media.app = "cloudmusic.exe";
    media.title = L"Song"; media.artist = L"Singer";
    media.position = 35.25; media.positionKnown = true;
    const auto cues = std::make_shared<const std::vector<dynamic_island::LyricCue>>(
        std::vector<dynamic_island::LyricCue>{{1,10,L"one"},{10,30,L"two"},{30,180,L"three"}});
    media.lyrics = cues;
    for (int i = 0; i < 5; ++i) {
        media.position = 35.25 + i;
        model.Update(media);
        Check(model.Playback().position == media.position && model.Playback().lyrics == cues,
            "successive snapshots with unknown total never reset the lyric position to zero");
    }
    media.positionKnown = false;
    model.Update(media);
    Check(model.Playback().position == 0 && !model.Playback().lyrics, "unknown position cannot present timed lyrics");
    media.app.clear(); media.duration = 180; media.position = 35;
    model.Update(media);
    Check(model.Playback().position == 35 && model.Playback().lyrics == cues, "internal player retains its existing timeline contract");

    const auto page = [](const char* title, const char* artist, double duration = 180) {
        return music::auth::Json{{"tracks", music::auth::Json::array({{
            {"id", "one"}, {"title", title}, {"artist", artist}, {"duration", duration}
        }})}};
    };
    const shell::Request request{"key", L"Song", L"Singer", 180, 1};
    Check(shell::Match(page("Song", "Singer"), request).has_value(), "same recording is accepted");
    Check(!shell::Match(page("Song", "Cover Singer"), request), "another singer is not a lyric fallback");
    Check(!shell::Match(page("Song (Live)", "Singer"), request), "live version cannot match the studio title");
    Check(!shell::Match(page("Song", "Singer", 195), request), "different recording length is rejected");
    Check(shell::Match(page("Song", "Jay"), {"key", L"Song", L"周杰伦", 180, 1}).has_value(),
        "known catalogue artist alias remains accepted");
    Check(shell::Normal(L"Song（Live）") == shell::Normal(L"Song (Live)"), "bracket typography preserves version text");

    shell::ExternalLyrics lyrics;
    media.app = "cloudmusic.exe"; media.duration = 0; media.position = 45; media.positionKnown = false;
    lyrics.Apply(media);
    Check(!media.lyrics && media.position == 45 && lookups == 0, "unknown position neither starts a guessed clock nor a lyric search");
    media.positionKnown = true;
    const auto until = GetTickCount64() + 3000;
    do { lyrics.Apply(media); if (media.lyrics) break; Sleep(5); } while (GetTickCount64() < until);
    Check(media.lyrics && media.position == 45 && lookups == 2, "real unknown-length timeline receives actual parsed lyrics");
    const auto cached = media.lyrics;
    media.position = 12; lyrics.Apply(media);
    Check(media.lyrics == cached && media.position == 12 && lookups == 2, "backward seek keeps the same cue set and player position");
    media.playing = false; media.position = 30; lyrics.Apply(media);
    Check(media.lyrics == cached && media.position == 30, "pause preserves the exact player reading");
    media.positionKnown = false; lyrics.Apply(media);
    Check(!media.lyrics && media.position == 30, "loss of timeline clears stale synchronized lyrics without inventing progress");
    media.title = L"Next song"; lyrics.Apply(media);
    Check(!media.lyrics && lookups == 2, "track changes with no timeline do not start an approximate lyric clock");
    std::cout << (failures ? "FAIL" : "PASS") << " external lyric recording, measured timeline and seek/pause regression\n";
    return failures ? 1 : 0;
}
