#pragma once

#include "apps/shell/music_library_session.h"

#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lume::shell {

struct MusicLibraryWorkerOptions {
    std::filesystem::path root;
    // Notification only; post to the UI thread, never call Drain/Shutdown here.
    std::function<void()> wake;
};

enum class MusicLibraryPlaybackAction { None, Restore, Play, Enqueue, InsertNext };

struct MusicLibraryWorkerResult {
    // Only state/artworkId/artworkUrl/error may be sent to the page.
    std::wstring state;
    std::optional<MusicLibraryImportResult> imported;
    // Native-only: these tracks/snapshots retain authorized local sources.
    std::vector<music::Track> playTracks;
    std::string playId;
    MusicLibraryPlaybackAction action = MusicLibraryPlaybackAction::None;
    std::uint64_t generation = 0;
    // Completion marker survives supersession; restoredQueue does not.
    bool restored = false;
    std::optional<music::MusicSnapshot> restoredQueue;
    std::string artworkId;
    std::string artworkUrl;
    std::string error;
};

class MusicLibraryWorker final {
public:
    explicit MusicLibraryWorker(MusicLibraryWorkerOptions options);
    ~MusicLibraryWorker();
    MusicLibraryWorker(const MusicLibraryWorker&) = delete;
    MusicLibraryWorker& operator=(const MusicLibraryWorker&) = delete;

    // False means rejected (invalid input, full queue, corrupt restore, or shutdown).
    // The caller must publish its failure ACK when a request is rejected.
    bool Restore();
    bool Import(std::vector<std::wstring> paths);
    bool Remove(std::string id);
    bool Page(std::size_t offset = 0, std::size_t limit = 100);
    // Play returns a library window of at most kMaxMusicTracks containing id.
    // Enqueue/InsertNext return only the requested track.
    bool Play(std::string id,
        MusicLibraryPlaybackAction action = MusicLibraryPlaybackAction::Play);
    // Coalesces persistent fields; position-only writes at most once per 30s.
    // A failed persistent snapshot is suppressed until it changes or retry is true.
    // Corrupt restored queue files block default saves, including shutdown flush.
    // retry=true explicitly authorizes replacement/repair of that file.
    // Shutdown attempts to flush pending changes; Drain reports errors in state too.
    bool SaveQueue(music::MusicSnapshot snapshot, bool retry = false);
    bool Artwork(std::string id);
    // Initially hidden. Visible pages lazily share one server, <=100 leases and
    // <=8 MiB PNG bytes total (including the optional explicit Artwork request).
    // State projects only lease URLs or empty artwork; off-page leases are revoked.
    // Hiding invalidates readers immediately and asynchronously releases all leases.
    void SetVisible(bool visible);
    // UI thread only. No disk work; superseded Play/Restore actions are discarded.
    std::vector<MusicLibraryWorkerResult> Drain();
    // Native display lookup only: bounded memory, NOT playback authorization.
    std::optional<music::Track> Resolve(std::string_view id) const;
    bool HasWork() const noexcept;
    // Cancels queued operations, joins worker, flushes pending queue, revokes artwork.
    // Do not call from wake. Save errors remain available through Drain after shutdown.
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lume::shell
