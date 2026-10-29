#pragma once

#include "core/music/music_types.h"

#include <optional>
#include <string_view>

namespace lume::music {

inline constexpr std::size_t kMaxLibraryTracks = 4096;
inline constexpr std::size_t kMaxLibraryIndexBytes = 32 * 1024 * 1024;
inline constexpr std::size_t kMaxLibraryQueueBytes = 128 * 1024;

struct LibraryEntry {
    Track track;
    std::string identity;
    std::uint64_t bytes = 0;
    std::uint64_t modified = 0;
    // Runtime only. Persisted paths are revalidated by the native session.
    std::string unavailable;
};

enum class LibraryChange { Added, Updated, Unchanged, Full, Invalid };

class MusicLibrary final {
public:
    explicit MusicLibrary(std::size_t capacity = kMaxLibraryTracks);
    LibraryChange Upsert(LibraryEntry entry);
    bool Remove(std::string_view id);
    bool SetAvailability(std::string_view id, std::string error);
    const LibraryEntry* Find(std::string_view id) const noexcept;
    const LibraryEntry* FindIdentity(std::string_view identity) const noexcept;
    const std::vector<LibraryEntry>& Entries() const noexcept { return entries_; }
    std::vector<Track> Tracks() const;
    std::size_t Capacity() const noexcept { return capacity_; }
    std::string Encode() const;
    bool Decode(std::string_view bytes) noexcept;
    std::optional<std::string> EncodeQueue(const MusicSnapshot& snapshot) const;
    std::optional<MusicSnapshot> DecodeQueue(std::string_view bytes) const noexcept;

private:
    std::size_t capacity_;
    std::vector<LibraryEntry> entries_;
};

}  // namespace lume::music
