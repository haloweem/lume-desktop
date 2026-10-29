#pragma once

#include <cstdint>
#include <string>

namespace lume::wallpaper {

enum class MpvCommand { Pause, Resume };
enum class WallpaperState { Stopped, Playing, Paused, Failed };

struct DisplayWallpaper {
    std::uint64_t displayId = 0;
    std::string mediaPath;
    WallpaperState state = WallpaperState::Stopped;
};

class IMpvController {
public:
    virtual ~IMpvController() = default;
    virtual bool Start(std::uint64_t displayId, const std::string& mediaPath) = 0;
    virtual bool Command(std::uint64_t displayId, MpvCommand command) = 0;
    virtual void Stop(std::uint64_t displayId) = 0;
    virtual bool Recover(std::uint64_t displayId) = 0;
};

}  // namespace lume::wallpaper
