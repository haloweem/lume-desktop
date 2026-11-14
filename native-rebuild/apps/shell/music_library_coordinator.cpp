#include "apps/shell/music_library_coordinator.h"
#include <utility>

namespace lume::shell {
MusicLibraryCoordinator::MusicLibraryCoordinator(bool available, LibraryPort library, PlaybackPort playback, Post post)
    : available_(available), library_(std::move(library)), playback_(std::move(playback)), post_(std::move(post)) {}

void MusicLibraryCoordinator::Command(const ui::MusicLibraryCommand& command) {
    if (command.action == "visibility") { library_.setVisible(command.visible); return; }
    if (!available_) { Failure("unavailable"); return; }
    bool accepted = false;
    if (command.action == "snapshot") accepted = library_.page(0, 100);
    else if (command.action == "page") accepted = library_.page(command.offset, command.limit);
    else if (command.action == "remove") accepted = library_.remove(command.id);
    else accepted = library_.play(command.id, command.action == "play" ? MusicLibraryPlaybackAction::Play :
        command.action == "enqueue" ? MusicLibraryPlaybackAction::Enqueue : MusicLibraryPlaybackAction::InsertNext);
    if (!accepted) Failure("busy");
}

void MusicLibraryCoordinator::Failure(const std::string& error) {
    auto json = state_;
    json["error"] = error;
    const auto bytes = json.dump(-1, ' ', true);
    Publish(std::wstring(bytes.begin(), bytes.end()));
}

void MusicLibraryCoordinator::Publish(const std::wstring& state) {
    auto json = nlohmann::json::parse(state);
    json.erase("scope");
    json["type"] = "music-library-state";
    json["revision"] = ++revision_;
    // Preserve the worker's explicit local media leases. Placeholder IDs,
    // provider URLs and filesystem artwork do not cross the page boundary.
    for (auto& track : json["tracks"]) {
        const auto image = track.value("artwork", std::string{});
        if (image.rfind("http://127.0.0.1:", 0) != 0) track["artwork"] = "";
    }
    state_ = json;
    const auto bytes = json.dump(-1, ' ', true);
    post_(std::wstring(bytes.begin(), bytes.end()));
}

void MusicLibraryCoordinator::Process(std::vector<MusicLibraryWorkerResult> results) {
    for (auto& result : results) {
        if (!result.state.empty()) Publish(result.state);
        else if (!result.error.empty()) Failure(result.error);
        if (result.restored && !restored_) {
            restored_ = result.error.empty();
            if (result.restoredQueue && playback_.sessionId() == 0)
                playback_.restoreQueue(*result.restoredQueue);
        }
        if (!result.error.empty()) continue;
        if (result.action == MusicLibraryPlaybackAction::Play)
            playback_.playTracks(std::move(result.playTracks), std::move(result.playId));
        else if (result.action == MusicLibraryPlaybackAction::Enqueue || result.action == MusicLibraryPlaybackAction::InsertNext)
            playback_.enqueueTracks(std::move(result.playTracks), result.action == MusicLibraryPlaybackAction::InsertNext);
    }
}

void MusicLibraryCoordinator::SaveQueue(const music::MusicSnapshot& snapshot) {
    if (restored_ && available_) library_.saveQueue(snapshot);
}
}
