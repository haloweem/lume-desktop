#pragma once

#include "core/music/music_types.h"

#include <string>
#include <memory>
#include <string_view>
#include <vector>

namespace lume::music {

struct LocalMusicImportResult {
    std::vector<Track> tracks;
    std::string error;
};

LocalMusicImportResult ImportLocalMusicFiles(const std::vector<std::wstring>& paths) noexcept;

struct LocalMusicFileInfo {
    Track track;
    std::string identity;
    std::uint64_t bytes = 0;
    std::uint64_t modified = 0;
    std::string error;
    // Pins the verified directories and read handle until metadata extraction finishes.
    std::shared_ptr<void> readLease;
};

bool IsLocalMusicPath(std::wstring_view path) noexcept;
LocalMusicFileInfo InspectLocalMusicFile(const std::wstring& path) noexcept;

}  // namespace lume::music
