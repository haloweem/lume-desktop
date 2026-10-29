#pragma once
#include "core/dynamic_island/island_model.h"
#include "core/music/music_lyrics.h"
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace lume::shell {

// The line whose time contains position; a gap shorter than a few seconds
// keeps the previous line instead of blinking empty.
std::wstring LyricLineAt(const std::vector<music::LyricLine>& lines, double position);
// The island's timed form of lyric lines (empty lines kept as gaps).
dynamic_island::LyricCues LyricCuesOf(const std::vector<music::LyricLine>& lines);

// Lyrics for music another program plays (QQ Music, NetEase Cloud Music, ...).
// Windows hands over only title, artist and, from some players, the position:
// the lyrics are looked up by title and artist in the online catalogues
// (NetEase, KuGou, QQ; no account needed) on a background thread and cached.
// QQ Music's position is read from its window (PlayerPosition). A player with
// no timeline at all is timed from the moment its track changed, paused while
// it pauses; a seek inside it is not visible, and a track already playing
// when Lume started shows no lyrics until the next one.
class ExternalLyrics final {
public:
    ExternalLyrics();
    ~ExternalLyrics();
    ExternalLyrics(const ExternalLyrics&) = delete;
    ExternalLyrics& operator=(const ExternalLyrics&) = delete;
    // Adds the lyrics to the snapshot (none while loading, without lyrics, or
    // when the position is unknown) and, for a player without a timeline,
    // the clock position. Call from one thread.
    void Apply(dynamic_island::Media& media);
    void Clear();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    std::thread worker_;
};

}  // namespace lume::shell
