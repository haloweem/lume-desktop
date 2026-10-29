#pragma once
#include "core/dynamic_island/island_model.h"
#include "platform/windows/dynamic_island/player_position.h"
#include <optional>
#include <memory>
#include <thread>

namespace lume::platform::windows {
class SystemMediaSession final {
public:
    SystemMediaSession();
    ~SystemMediaSession();
    SystemMediaSession(const SystemMediaSession&) = delete;
    SystemMediaSession& operator=(const SystemMediaSession&) = delete;
    std::optional<dynamic_island::Media> Snapshot();
    void Control(dynamic_island::Action);
    // Play or pause explicitly: a toggle read from a status the player has
    // not updated yet would undo a second quick click.
    void SetPlaying(bool playing);
private:
    struct Impl;
    // Shared with the worker so a WinRT call that never returns can be left
    // behind at shutdown instead of blocking the UI thread.
    std::shared_ptr<Impl> impl_;
    std::thread worker_;
    PlayerPosition position_;  // for players that report no timeline
};
}
