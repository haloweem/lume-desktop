#pragma once

#include <cstddef>
#include <list>
#include <optional>
#include <string>
#include <vector>

namespace lume::music {

class MusicResourceCache final {
public:
    explicit MusicResourceCache(std::size_t maxEntries = 16,
                                std::size_t maxArtworkBytes = 256 * 1024,
                                std::size_t maxLyricsLines = 2000);

    bool PutArtwork(std::string key, std::string value);
    std::optional<std::string> Artwork(const std::string& key);
    bool PutLyrics(std::string key, std::vector<std::string> lines);
    std::optional<std::vector<std::string>> Lyrics(const std::string& key);
    std::size_t Size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        std::string key;
        std::string artwork;
        std::vector<std::string> lyrics;
        bool isLyrics = false;
    };

    std::list<Entry>::iterator Find(const std::string& key);
    void Touch(std::list<Entry>::iterator entry);
    void Trim();

    std::size_t maxEntries_;
    std::size_t maxArtworkBytes_;
    std::size_t maxLyricsLines_;
    std::list<Entry> entries_;
};

}  // namespace lume::music
