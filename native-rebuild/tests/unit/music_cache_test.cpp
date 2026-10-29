#include "core/music/music_cache.h"

#include <iostream>
#include <string>
#include <vector>

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
    lume::music::MusicResourceCache cache(16, 256 * 1024, 2000);
    Check(!cache.PutArtwork("too-large", std::string(256 * 1024 + 1, 'x')),
        "artwork over the byte limit is rejected");
    for (int index = 0; index < 16; ++index)
        Check(cache.PutArtwork("cover-" + std::to_string(index), "cover"), "bounded artwork insert succeeds");
    Check(cache.Size() == 16, "cache keeps the configured item limit");
    Check(cache.Artwork("cover-0").has_value(), "oldest item is initially available");
    Check(cache.PutArtwork("cover-new", "cover"), "new item enters a full cache");
    Check(!cache.Artwork("cover-1").has_value(), "least recently used item is evicted");
    Check(cache.Artwork("cover-0").has_value(), "reading an item retains it");
    Check(cache.PutLyrics("song", std::vector<std::string>(2000, "line")), "bounded lyrics are accepted");
    Check(!cache.PutLyrics("too-many-lines", std::vector<std::string>(2001, "line")),
        "lyrics over the line limit are rejected");
    Check(cache.Lyrics("song")->size() == 2000, "lyrics preserve the bounded line count");
    if (!passed) return 1;
    std::cout << "music cache bounds passed\n";
    return 0;
}
