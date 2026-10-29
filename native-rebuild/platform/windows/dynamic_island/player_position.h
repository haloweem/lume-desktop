#pragma once
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace lume::platform::windows {

// Playback position of players that do not report one to Windows: QQ Music
// publishes title and artist only, but its window shows "mm:ss" elapsed and
// total time, readable through UI Automation (also while minimized or hidden
// in the tray). A background thread reads the cached text element every
// 250 ms; the moment the seconds digit changes fixes the fraction, so the
// result is accurate to the sampling interval. Nothing is clicked or changed.
class PlayerPosition final {
public:
    struct Reading { double position = 0, duration = 0; };
    PlayerPosition();
    ~PlayerPosition();
    PlayerPosition(const PlayerPosition&) = delete;
    PlayerPosition& operator=(const PlayerPosition&) = delete;
    // The position for the program (AppUserModelID) playing title, if it is
    // one this reads and its window shows the times; starts reading on demand
    // and stops when not asked for a few seconds.
    std::optional<Reading> Read(const std::string& app, const std::wstring& title, bool playing);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    std::thread worker_;
};

}  // namespace lume::platform::windows
