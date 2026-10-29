#pragma once

#include "core/lifecycle/service.h"
#include "core/wallpaper/mpv_controller.h"

#include <map>
#include <memory>
#include <optional>
#include <set>

namespace lume::wallpaper {

class WallpaperService final : public IService {
public:
    explicit WallpaperService(std::unique_ptr<IMpvController> controller) : controller_(std::move(controller)) {}
    ~WallpaperService() override { Stop(); }
    bool Start() override { running_ = controller_ != nullptr; return running_; }
    void Stop() override;
    void Suspend() override;
    void Resume() override;
    void Dispose() override;
    bool Set(std::uint64_t displayId, const std::string& mediaPath);
    bool Pause(std::uint64_t displayId);
    bool Resume(std::uint64_t displayId);
    bool Recover(std::uint64_t displayId);
    bool StopDisplay(std::uint64_t displayId);
    std::optional<DisplayWallpaper> state(std::uint64_t displayId) const;

private:
    std::unique_ptr<IMpvController> controller_;
    std::map<std::uint64_t, DisplayWallpaper> displays_;
    std::set<std::uint64_t> automaticallyPaused_;
    bool running_ = false;
};

}  // namespace lume::wallpaper
