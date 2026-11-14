#pragma once
#include "core/dynamic_island/island_model.h"
#include "core/music/music_lyrics.h"
#include "core/music/music_types.h"
#include <functional>
#include <string>
#include <vector>

namespace lume::shell {
// UI-thread projection between the player's public snapshot and the island's
// public media model. Providers retain ownership of playback, artwork I/O and
// lyric loading; this adapter owns only the projected lyric cache.
class IslandMediaAdapter final {
public:
    using ResolveArtwork = std::function<std::string(const music::Track&)>;
    using ReadLyrics = std::function<std::vector<music::LyricLine>()>;
    using ConvertLyrics = std::function<dynamic_island::LyricCues(const std::vector<music::LyricLine>&)>;

    IslandMediaAdapter(ResolveArtwork, ReadLyrics, ConvertLyrics);
    dynamic_island::Media Project(const music::MusicSnapshot&, bool lyricsEnabled);
private:
    ResolveArtwork resolveArtwork_;
    ReadLyrics readLyrics_;
    ConvertLyrics convertLyrics_;
    std::string lyricsKey_;
    dynamic_island::LyricCues lyrics_;
};
}
