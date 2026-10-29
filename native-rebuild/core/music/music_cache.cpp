#include "core/music/music_cache.h"

#include <algorithm>
#include <utility>

namespace lume::music {

MusicResourceCache::MusicResourceCache(std::size_t maxEntries,
                                       std::size_t maxArtworkBytes,
                                       std::size_t maxLyricsLines)
    : maxEntries_(std::max<std::size_t>(1, maxEntries)),
      maxArtworkBytes_(maxArtworkBytes),
      maxLyricsLines_(maxLyricsLines) {}

std::list<MusicResourceCache::Entry>::iterator MusicResourceCache::Find(const std::string& key) {
    return std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.key == key;
    });
}

void MusicResourceCache::Touch(std::list<Entry>::iterator entry) {
    if (entry != entries_.begin()) entries_.splice(entries_.begin(), entries_, entry);
}

void MusicResourceCache::Trim() {
    while (entries_.size() > maxEntries_) entries_.pop_back();
}

bool MusicResourceCache::PutArtwork(std::string key, std::string value) {
    if (key.empty() || value.size() > maxArtworkBytes_) return false;
    auto entry = Find(key);
    if (entry != entries_.end()) entries_.erase(entry);
    entries_.push_front({std::move(key), std::move(value), {}, false});
    Trim();
    return true;
}

std::optional<std::string> MusicResourceCache::Artwork(const std::string& key) {
    auto entry = Find(key);
    if (entry == entries_.end() || entry->isLyrics) return std::nullopt;
    Touch(entry);
    return entries_.front().artwork;
}

bool MusicResourceCache::PutLyrics(std::string key, std::vector<std::string> lines) {
    if (key.empty() || lines.size() > maxLyricsLines_) return false;
    auto entry = Find(key);
    if (entry != entries_.end()) entries_.erase(entry);
    entries_.push_front({std::move(key), {}, std::move(lines), true});
    Trim();
    return true;
}

std::optional<std::vector<std::string>> MusicResourceCache::Lyrics(const std::string& key) {
    auto entry = Find(key);
    if (entry == entries_.end() || !entry->isLyrics) return std::nullopt;
    Touch(entry);
    return entries_.front().lyrics;
}

}  // namespace lume::music
