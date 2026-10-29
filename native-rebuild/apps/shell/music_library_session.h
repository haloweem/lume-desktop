#pragma once

#include "core/music/music_library.h"

#include <filesystem>
#include <functional>
#include <memory>

namespace lume::shell {

struct MusicLibrarySessionOptions {
    // Required absolute, local root. No user-config discovery, no account coupling.
    std::filesystem::path root;
    std::size_t maxTracks = music::kMaxLibraryTracks;
    // Optional cooperative cancellation, checked between files. Must not reenter.
    std::function<bool()> cancelled = {};
};

struct MusicLibraryImportFailure {
    std::size_t input = 0;
    std::string error;
};

struct MusicLibraryImportResult {
    std::size_t added = 0;
    std::size_t updated = 0;
    std::size_t unchanged = 0;
    std::vector<MusicLibraryImportFailure> failures;
    std::vector<MusicLibraryImportFailure> warnings;
    // Nonempty means no index changes were committed.
    std::string error;
};

class MusicLibrarySession final {
public:
    explicit MusicLibrarySession(MusicLibrarySessionOptions options);
    ~MusicLibrarySession();
    MusicLibrarySession(const MusicLibrarySession&) = delete;
    MusicLibrarySession& operator=(const MusicLibrarySession&) = delete;

    // Synchronous, single-owner API: parent schedules disk/metadata work off its UI thread.
    // Construction and State are I/O-free. Other calls lazily restore the index.
    bool Restore();
    // Up to 256 selected paths; explicit directories scan up to 1024 audio files,
    // 16384 entries and 16 levels. No background discovery or directory watching.
    MusicLibraryImportResult Import(const std::vector<std::wstring>& paths);
    // Native only. Rechecks file identity/access; never expose returned sources to the bridge.
    std::vector<music::Track> Tracks();
    std::optional<music::Track> Resolve(std::string_view id);
    bool Remove(std::string_view id);
    // Local-only queue, <= kMaxMusicTracks; unknown/provider IDs fail without replacing disk state.
    bool SaveQueue(const music::MusicSnapshot& snapshot);
    std::optional<music::MusicSnapshot> RestoredQueue();
    std::wstring State(std::size_t offset = 0, std::size_t limit = 100) const;
    // Bounded PNG bytes for a native resource handler; never a file path or data URI.
    std::optional<std::string> Artwork(std::string_view id);
    const std::string& LastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lume::shell
