#pragma once
#include "core/dock/dock_model.h"
#include "core/settings/file_store.h"
#include "core/ui/page_bridge.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace lume::shell {
struct NowPlaying {
    std::string app;  // AppUserModelID of the playing program (often its exe name)
    std::shared_ptr<const platform::windows::WidgetBitmap> cover;
};
struct DockSessionOptions {
    std::string preferencesPath;
    bool verification = false;
    // Music playing elsewhere: its minimized window shows the cover.
    std::function<std::optional<NowPlaying>()> nowPlaying;
};
// The Dock bar and its launchpad: pinned and running programs, the Lume
// manager, and a searchable grid of desktop items plus user-added paths.
class DockSession final {
public:
    using Notify = std::function<void(const std::wstring&, bool attention)>;
    DockSession(HINSTANCE, DockSessionOptions, Notify, std::function<void()> openManager,
                std::function<void()> openSettings, std::function<bool()> managerVisible,
                settings::IFileStore* files = nullptr);
    ~DockSession();
    bool Restore();
    std::optional<std::wstring> HandlePage(ui::PageCommand);
    std::wstring State() const;
    bool Running() const;
    HWND Handle() const;
    // Re-reads running programs and the manager state (Lume's dot).
    void Refresh();
    // Opens or closes the launchpad; false when the Dock is not running.
    bool ToggleLaunchpad();
    const dock::Preferences& Snapshot() const;
    void Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace lume::shell
