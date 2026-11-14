#include "apps/shell/island_media_adapter.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace lume;
void Check(bool value, const char* why) {
    if (!value) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
struct Fixture {
    unsigned artworkCalls = 0, lyricReads = 0, lyricConversions = 0;
    std::string resolvedId;
    std::vector<music::LyricLine> lines;
    std::vector<music::LyricLine> converted;
    shell::IslandMediaAdapter adapter{
        [this](const music::Track& track) { ++artworkCalls; resolvedId = track.id; return "resolved:" + track.artwork; },
        [this] { ++lyricReads; return lines; },
        [this](const std::vector<music::LyricLine>& value) -> dynamic_island::LyricCues {
            ++lyricConversions; converted = value;
            if (value.empty()) return nullptr;
            return std::make_shared<const std::vector<dynamic_island::LyricCue>>(
                std::vector<dynamic_island::LyricCue>{{value.front().startTime, value.front().endTime,
                    L"conversion-" + std::to_wstring(lyricConversions)}});
        }
    };
    music::MusicSnapshot snapshot;
    Fixture() {
        snapshot.session = 23; snapshot.status = music::PlaybackStatus::Playing;
        snapshot.playing = true; snapshot.position = 12.5;
        snapshot.current = music::Track{"track-a",u8"歌曲标题",u8"演唱者","album","local-source","cover-a",150.25};
        snapshot.analysis.energy = .25;
        for (std::size_t i = 0; i < music::kMusicAnalysisBins; ++i)
            snapshot.analysis.bins[i] = static_cast<double>(i) / 32;
        lines = {{1, 2, "first"}, {2, 4, "second"}};
    }
};
void EmptyAndProjection() {
    Fixture f;
    auto empty = f.snapshot; empty.current.reset();
    const auto none = f.adapter.Project(empty, true);
    Check(none.id.empty() && none.artwork.empty() && none.title.empty() && none.artist.empty() &&
        !none.available && !none.playing && none.session == 0 && !none.lyrics,
        "no current track yields the island's empty media without stale playback data");
    Check(f.artworkCalls == 0 && f.lyricReads == 0 && f.lyricConversions == 0,
        "an empty snapshot does not read artwork or lyrics");

    const auto media = f.adapter.Project(f.snapshot, false);
    Check(media.session == 23 && media.id == "track-a" && media.artwork == "resolved:cover-a" &&
        media.title == L"歌曲标题" && media.artist == L"演唱者" && media.available && media.playing &&
        media.position == 12.5 && media.duration == 150.25 && media.energy == .25f && media.app.empty(),
        "public track and playback fields preserve the old projection");
    Check(f.artworkCalls == 1 && f.resolvedId == "track-a" && f.lyricReads == 0 && f.lyricConversions == 0 && !media.lyrics,
        "disabled island lyrics do not read or convert provider data");
    for (std::size_t i = 0; i < media.spectrum.size(); ++i)
        Check(media.spectrum[i] == static_cast<float>(2 * i + 1) / 32,
            "32 analysis bins project to 16 pair maxima in the same order");

    f.snapshot.status = music::PlaybackStatus::Paused; f.snapshot.playing = false;
    const auto paused = f.adapter.Project(f.snapshot, false);
    Check(paused.available && !paused.playing && paused.position == 12.5 && paused.id == media.id,
        "paused playback remains available with its position and identity");
    f.snapshot.status = music::PlaybackStatus::Error;
    const auto failed = f.adapter.Project(f.snapshot, false);
    Check(!failed.available && failed.id == media.id && failed.title == media.title && failed.artwork == media.artwork,
        "playback error marks unavailable without dropping the current track projection");
    f.snapshot.analysis.bins[0] = -.5; f.snapshot.analysis.bins[1] = -1;
    f.snapshot.analysis.bins[30] = 2; f.snapshot.analysis.bins[31] = 1.5;
    const auto peaks = f.adapter.Project(f.snapshot, false);
    Check(peaks.spectrum.front() == 0 && peaks.spectrum.back() == 2,
        "existing zero-baseline maxima and unbounded positive values are preserved");
}
void LyricsAndCache() {
    Fixture f;
    const auto first = f.adapter.Project(f.snapshot, true);
    Check(first.lyrics && f.lyricReads == 1 && f.lyricConversions == 1 && f.converted.size() == 2 &&
        f.converted.front().startTime == 1 && f.converted.back().fullText == "second",
        "first enabled projection forwards the current lyric lines for conversion");
    const auto second = f.adapter.Project(f.snapshot, true);
    Check(second.lyrics == first.lyrics && f.lyricReads == 2 && f.lyricConversions == 1,
        "unchanged lyric key shares immutable cues without reconverting every snapshot");
    f.lines.back().fullText = "changed later line";
    const auto retained = f.adapter.Project(f.snapshot, true);
    Check(retained.lyrics == first.lyrics && f.lyricConversions == 1,
        "existing key semantics intentionally do not broaden to all lyric contents");
    f.lines.front().fullText = "new first line";
    const auto changed = f.adapter.Project(f.snapshot, true);
    Check(changed.lyrics != first.lyrics && f.lyricConversions == 2,
        "a changed first line rebuilds the projection cache");
    f.lines.push_back({4, 8, "third"});
    const auto more = f.adapter.Project(f.snapshot, true);
    Check(more.lyrics != changed.lyrics && f.lyricConversions == 3,
        "a changed line count rebuilds the cache");
    f.snapshot.current->id = "track-b";
    const auto next = f.adapter.Project(f.snapshot, true);
    Check(next.lyrics != more.lyrics && f.lyricConversions == 4,
        "a new track identity rebuilds cues even with matching lyric text");
    const auto reads = f.lyricReads;
    const auto disabled = f.adapter.Project(f.snapshot, false);
    Check(!disabled.lyrics && f.lyricReads == reads && f.lyricConversions == 4,
        "disabling lyrics returns no cues and skips loading without clearing the cache");
    auto empty = f.snapshot; empty.current.reset();
    Check(!f.adapter.Project(empty, true).lyrics && f.lyricReads == reads,
        "an intervening empty snapshot does not load or expose cached lyrics");
    Check(f.adapter.Project(f.snapshot, true).lyrics == next.lyrics && f.lyricConversions == 4,
        "reenabling or resuming preserves the existing matching cache lifetime");
    f.lines.clear();
    Check(!f.adapter.Project(f.snapshot, true).lyrics && f.lyricConversions == 5,
        "empty lyrics replace an earlier nonempty cache with the converter's null result");
    Check(!f.adapter.Project(f.snapshot, true).lyrics && f.lyricConversions == 5,
        "an empty lyric result is also cached by key");
}
void ProviderFailureIsNotHidden() {
    shell::IslandMediaAdapter adapter{
        [](const music::Track&) -> std::string { throw std::runtime_error("artwork"); },
        [] { return std::vector<music::LyricLine>{}; },
        [](const std::vector<music::LyricLine>&) { return dynamic_island::LyricCues{}; }
    };
    Fixture f; bool threw = false;
    try { adapter.Project(f.snapshot, false); } catch (const std::runtime_error&) { threw = true; }
    Check(threw, "projection keeps provider exception behavior instead of silently changing playback policy");
}
}
int main() {
    EmptyAndProjection(); LyricsAndCache(); ProviderFailureIsNotHidden();
    std::cout << "PASS island media projection, spectrum, provider boundaries and lyric cache\n";
}
