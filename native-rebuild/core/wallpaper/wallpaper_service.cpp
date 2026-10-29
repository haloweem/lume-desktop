#include "core/wallpaper/wallpaper_service.h"

namespace lume::wallpaper {

void WallpaperService::Stop() {
    if (!running_) return;
    for (const auto& item : displays_) controller_->Stop(item.first);
    for (auto& item : displays_) item.second.state = WallpaperState::Stopped;
    automaticallyPaused_.clear();
    running_ = false;
}

void WallpaperService::Suspend() {
    if (!running_) return;
    for (auto& item : displays_) {
        if (item.second.state == WallpaperState::Playing && controller_->Command(item.first, MpvCommand::Pause)) {
            item.second.state = WallpaperState::Paused;
            automaticallyPaused_.insert(item.first);
        }
    }
}

void WallpaperService::Resume() {
    if (!running_) return;
    for (auto& item : displays_) {
        if (item.second.state == WallpaperState::Paused && automaticallyPaused_.count(item.first) &&
            controller_->Command(item.first, MpvCommand::Resume)) {
            item.second.state = WallpaperState::Playing;
            automaticallyPaused_.erase(item.first);
        }
    }
}

void WallpaperService::Dispose() { Stop(); displays_.clear(); controller_.reset(); }

bool WallpaperService::Set(std::uint64_t displayId, const std::string& mediaPath) {
    if (!running_ || displayId == 0 || mediaPath.empty()) return false;
    const auto existing = displays_.find(displayId);
    if (existing != displays_.end()) controller_->Stop(displayId);
    if (!controller_->Start(displayId, mediaPath)) {
        displays_[displayId] = {displayId, mediaPath, WallpaperState::Failed};
        return false;
    }
    displays_[displayId] = {displayId, mediaPath, WallpaperState::Playing};
    automaticallyPaused_.erase(displayId);
    return true;
}

bool WallpaperService::Pause(std::uint64_t displayId) {
    const auto it = displays_.find(displayId);
    if (!running_ || it == displays_.end()) return false;
    if (it->second.state == WallpaperState::Paused) {
        automaticallyPaused_.erase(displayId);
        return true;
    }
    if (it->second.state != WallpaperState::Playing) return false;
    if (!controller_->Command(displayId, MpvCommand::Pause)) return false;
    it->second.state = WallpaperState::Paused; return true;
}

bool WallpaperService::Resume(std::uint64_t displayId) {
    const auto it = displays_.find(displayId);
    if (!running_ || it == displays_.end() || it->second.state != WallpaperState::Paused) return false;
    if (!controller_->Command(displayId, MpvCommand::Resume)) return false;
    automaticallyPaused_.erase(displayId);
    it->second.state = WallpaperState::Playing; return true;
}

bool WallpaperService::Recover(std::uint64_t displayId) {
    const auto it = displays_.find(displayId);
    if (!running_ || !controller_ || it == displays_.end() || it->second.mediaPath.empty()) return false;
    if (!controller_->Recover(displayId)) return false;
    it->second.state = WallpaperState::Playing;
    return true;
}

bool WallpaperService::StopDisplay(std::uint64_t displayId) {
    const auto it = displays_.find(displayId);
    if (!controller_ || it == displays_.end()) return false;
    controller_->Stop(displayId);
    it->second.state = WallpaperState::Stopped;
    automaticallyPaused_.erase(displayId);
    return true;
}

std::optional<DisplayWallpaper> WallpaperService::state(std::uint64_t displayId) const {
    const auto it = displays_.find(displayId); return it == displays_.end() ? std::nullopt : std::optional<DisplayWallpaper>(it->second);
}

}  // namespace lume::wallpaper
