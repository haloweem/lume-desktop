#include "core/music/music_provider.h"

#include <algorithm>
#include <cctype>

namespace lume::music {
namespace {

std::string Fold(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

bool Matches(const Track& track, const std::string& query) {
    return Fold(track.title).find(query) != std::string::npos ||
        Fold(track.artist).find(query) != std::string::npos ||
        Fold(track.album).find(query) != std::string::npos;
}

}  // namespace

CachedMusicProvider::CachedMusicProvider(std::vector<Track> home)
    : home_(std::move(home)) {}

void CachedMusicProvider::ReplaceHome(std::vector<Track> home) {
    home_ = std::move(home);
    cancelled_ = false;
}

ProviderResult CachedMusicProvider::LoadHome(bool signedIn) {
    if (!signedIn) return {{}, ProviderError::SignedOut, false};
    if (home_.empty()) return {{}, ProviderError::Empty, false};
    return {home_, ProviderError::None, true};
}

ProviderResult CachedMusicProvider::Search(std::string_view query, bool signedIn) {
    if (!signedIn) return {{}, ProviderError::SignedOut, false};
    if (home_.empty()) return {{}, ProviderError::Empty, false};
    const auto folded = Fold(query);
    if (folded.empty()) return {home_, ProviderError::None, true};
    std::vector<Track> matches;
    for (const auto& track : home_)
        if (Matches(track, folded)) matches.push_back(track);
    if (matches.empty()) return {{}, ProviderError::NotFound, true};
    return {std::move(matches), ProviderError::None, true};
}

LyricsResult CachedMusicProvider::LoadLyrics(std::string_view trackId, bool signedIn) {
    if (!signedIn) return {{}, ProviderError::SignedOut, false};
    if (trackId.empty()) return {{}, ProviderError::NotFound, false};
    return {{}, ProviderError::Empty, false};
}

}  // namespace lume::music
