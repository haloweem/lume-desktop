#pragma once
#include "apps/shell/music_library_worker.h"
#include "core/ui/music_library_protocol.h"
#include "vendor/nlohmann/json.hpp"
#include <functional>
#include <string>
#include <vector>

namespace lume::shell {
// UI-thread policy between the library worker, playback and its page. The
// worker keeps file authorization and generations; this coordinator owns the
// restoration gate and the monotonic, sanitized page projection.
class MusicLibraryCoordinator final {
public:
    struct LibraryPort {
        std::function<void(bool)> setVisible;
        std::function<bool(std::size_t, std::size_t)> page;
        std::function<bool(std::string)> remove;
        std::function<bool(std::string, MusicLibraryPlaybackAction)> play;
        std::function<bool(const music::MusicSnapshot&)> saveQueue;
    };
    struct PlaybackPort {
        std::function<std::uint64_t()> sessionId;
        std::function<void(const music::MusicSnapshot&)> restoreQueue;
        std::function<void(std::vector<music::Track>, std::string)> playTracks;
        std::function<void(std::vector<music::Track>, bool)> enqueueTracks;
    };
    using Post = std::function<void(const std::wstring&)>;

    // Construction stores ports only: no playback, publication or I/O.
    MusicLibraryCoordinator(bool available, LibraryPort, PlaybackPort, Post);
    void Command(const ui::MusicLibraryCommand&);
    // Rejected external import requests use the same page ACK policy.
    void Failure(const std::string&);
    void Process(std::vector<MusicLibraryWorkerResult>);
    // Call after desktop/island consumers, and before worker shutdown, as before.
    void SaveQueue(const music::MusicSnapshot&);
    bool Restored() const noexcept { return restored_; }
    const nlohmann::json& Snapshot() const noexcept { return state_; }
private:
    void Publish(const std::wstring&);
    bool available_;
    LibraryPort library_;
    PlaybackPort playback_;
    Post post_;
    std::uint64_t revision_ = 0;
    bool restored_ = false;
    nlohmann::json state_ = {{"version",1},{"type","music-library-state"},{"loaded",false},
        {"error",""},{"total",0},{"offset",0},{"limit",100},{"tracks",nlohmann::json::array()}};
};
}
