#include "core/music/music_provider.h"

#include <iostream>

namespace {
bool passed = true;

void Check(bool value, const char* message) {
    if (!value) {
        passed = false;
        std::cerr << "FAIL: " << message << '\n';
    }
}
}

int main() {
    lume::music::Track track;
    track.id = "cached-1";
    track.title = "Midnight City";
    track.artist = "M83";
    track.album = "Hurry Up";
    lume::music::CachedMusicProvider provider({track});

    Check(provider.LoadHome(false).error == lume::music::ProviderError::SignedOut,
        "home requires a signed-in account");
    const auto home = provider.LoadHome(true);
    Check(home.error == lume::music::ProviderError::None && home.cached && home.tracks.size() == 1,
        "cached home returns bounded tracks");
    const auto search = provider.Search("m83", true);
    Check(search.error == lume::music::ProviderError::None && search.tracks.size() == 1,
        "search matches cached artist metadata");
    Check(provider.Search("unknown", true).error == lume::music::ProviderError::NotFound,
        "empty search result is a stable not-found state");
    Check(provider.LoadLyrics("cached-1", true).error == lume::music::ProviderError::Empty,
        "lyrics without a cache stay in a stable empty state");

    if (!passed) return 1;
    std::cout << "music provider boundary passed\n";
    return 0;
}
