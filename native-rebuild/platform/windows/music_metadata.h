#pragma once

#include <cstddef>
#include <string>

namespace lume::music {

inline constexpr std::size_t kMaxMusicArtworkBytes = 256 * 1024;
inline constexpr unsigned kMusicArtworkEdge = 256;

struct MusicMetadata {
    std::string title;
    std::string artist;
    std::string album;
    double duration = 0;
    std::string artworkPng;
    // Missing tags/cover are normal; unavailable OS handlers or invalid images are warnings.
    std::string warning;
};

// Uses the read-only Windows property handler and WIC; never downloads a cover.
// OS handlers are synchronous: parent runs this off the UI thread. Cancellation
// is between files, not a hard deadline for an individual installed handler.
MusicMetadata ReadMusicMetadata(const std::wstring& path) noexcept;

}  // namespace lume::music
