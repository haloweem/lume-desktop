#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lume::ui {
enum class WallpaperAction {
    Snapshot, Refresh, Import, StopAll, Download, Cancel, Remove, Apply,
    Pause, Resume, Stop, Fit, Volume, PauseFullscreen, Preview, EndPreview, Like, ApplyAll, Stats, EndStats
};
struct WallpaperPageCommand {
    WallpaperAction action = WallpaperAction::Snapshot;
    std::string id;
    std::uint64_t display = 0;
    int volume = 0;
    bool flag = false;
};
bool ValidWallpaperId(std::string_view id) noexcept;
std::optional<WallpaperPageCommand> ParseWallpaperPageJson(std::wstring_view json) noexcept;
}
