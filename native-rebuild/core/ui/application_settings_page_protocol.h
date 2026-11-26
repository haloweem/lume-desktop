#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace lume::ui {
enum class ApplicationSettingsAction {
    Snapshot, Language, BalancedMode, Autostart, CheckUpdate, DownloadUpdate, InstallUpdate,
    ChooseWallpaperDirectory, SetWallpaperDirectory, ChooseDataDirectory, InterfaceScale, PreviewInterfaceScale, ChooseGame, RemoveGame
};
struct ApplicationSettingsPageCommand {
    ApplicationSettingsAction action = ApplicationSettingsAction::Snapshot;
    std::string language{};
    bool flag = false;
    // Only set by the host for SetWallpaperDirectory; never parsed from the page.
    std::string directory{};
    int scalePercent = 75;
    unsigned gameIndex = 0;
};
std::optional<ApplicationSettingsPageCommand> ParseApplicationSettingsPageJson(
    std::wstring_view json) noexcept;
}
