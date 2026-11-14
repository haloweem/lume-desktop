#include "apps/shell/island_media_adapter.h"
#include "platform/windows/text_util.h"
#include <algorithm>
#include <utility>

namespace lume::shell {
IslandMediaAdapter::IslandMediaAdapter(ResolveArtwork artwork, ReadLyrics lyrics, ConvertLyrics convert)
    : resolveArtwork_(std::move(artwork)), readLyrics_(std::move(lyrics)), convertLyrics_(std::move(convert)) {}

dynamic_island::Media IslandMediaAdapter::Project(const music::MusicSnapshot& snapshot, bool lyricsEnabled) {
    dynamic_island::Media media;
    if (!snapshot.current) return media;

    media.session = snapshot.session; media.id = snapshot.current->id;
    media.artwork = resolveArtwork_(*snapshot.current);
    media.title = text::Wide(snapshot.current->title); media.artist = text::Wide(snapshot.current->artist);
    media.available = snapshot.status != music::PlaybackStatus::Error;
    media.playing = snapshot.playing; media.position = snapshot.position;
    media.duration = snapshot.current->duration;
    media.energy = static_cast<float>(snapshot.analysis.energy);
    if (lyricsEnabled) {
        const auto lines = readLyrics_();
        // Preserve the existing track/count/first-text identity and cache
        // lifetime; projection does not introduce another lyric fetch policy.
        const auto key = snapshot.current->id + "#" + std::to_string(lines.size()) + "#" +
            (lines.empty() ? std::string{} : lines.front().fullText);
        if (key != lyricsKey_) { lyricsKey_ = key; lyrics_ = convertLyrics_(lines); }
        media.lyrics = lyrics_;
    }
    for (std::size_t i = 0; i < media.spectrum.size(); ++i) {
        const auto begin = i * music::kMusicAnalysisBins / media.spectrum.size();
        const auto end = std::max(begin + 1, (i + 1) * music::kMusicAnalysisBins / media.spectrum.size());
        float peak = 0;
        for (auto n = begin; n < end; ++n) peak = std::max(peak, static_cast<float>(snapshot.analysis.bins[n]));
        media.spectrum[i] = peak;
    }
    return media;
}
}
