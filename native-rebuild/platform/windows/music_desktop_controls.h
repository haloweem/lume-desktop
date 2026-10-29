#pragma once

#include "core/ui/music_page_protocol.h"
#include "platform/windows/widget_ui/native_widget_window.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace lume::platform::windows {

// A bounded projection, not a second queue/library or playback owner.
struct MusicDesktopState {
    std::uint64_t session = 0;
    std::string trackId;
    std::wstring title, artist, album;
    music::PlaybackStatus status = music::PlaybackStatus::Empty;
    double position = 0, duration = 0;
    bool playing = false, canToggle = false, canSkip = false, canSeek = false;
};

MusicDesktopState ProjectMusicDesktopState(const music::MusicSnapshot&);
std::optional<ui::MusicPageCommand> MusicDesktopCommand(
    const MusicDesktopState&, ui::MusicAction, double position = 0);

// Resolves a track's cover (online URL or embedded local artwork) into a
// data URL on a background worker, for surfaces that only draw data URLs.
// UI-thread object; `wake` runs on the worker when a cover becomes ready.
class MusicCoverResolver final {
public:
    explicit MusicCoverResolver(std::function<void()> wake);
    ~MusicCoverResolver();
    MusicCoverResolver(const MusicCoverResolver&) = delete;
    MusicCoverResolver& operator=(const MusicCoverResolver&) = delete;
    // Returns the cover as a data URL, or empty while it is being fetched.
    std::string Resolve(const music::Track&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class MusicDesktopControls final {
public:
    using CommandHandler = std::function<void(const ui::MusicPageCommand&)>;
    using WidgetHandler = std::function<void(WidgetAction, const widget::Preferences&)>;
    using WakeHandler = std::function<void()>;
    static constexpr widget::Limits Limits{320, 152, 320, 152, 720, 720};

    // All public calls and destruction belong to the constructing UI thread.
    // The parent initializes COM, owns persistence and keeps callbacks short.
    // Callbacks may Update/Hide/Shutdown but must defer object destruction.
    // Construction/Update never shows a window or enables system media controls.
    MusicDesktopControls(HINSTANCE, CommandHandler, WidgetHandler = {}, WakeHandler = {});
    ~MusicDesktopControls();
    MusicDesktopControls(const MusicDesktopControls&) = delete;
    MusicDesktopControls& operator=(const MusicDesktopControls&) = delete;

    void Update(const music::MusicSnapshot&);
    bool Configure(const widget::Preferences&);
    bool Show();
    void Hide(); // Releases widget/material/decoded cover, not playback or SMTC.
    bool Visible() const;
    HWND Handle() const;
    widget::Preferences Bounds() const;
    const MusicDesktopState& State() const;

    // Preferred: lazily owns an invisible, nonactivating top-level HWND.
    // Independent of the manager window and desktop widget visibility.
    HRESULT EnableSystemMediaControls();
    // Explicit opt-in. owner must be this UI thread's live top-level HWND,
    // and must outlive this registration. Call Disable before destroying it.
    // A failed HRESULT is observable; widget controls remain usable.
    HRESULT EnableSystemMediaControls(HWND owner);
    void DisableSystemMediaControls();
    bool SystemMediaControlsEnabled() const;
    HRESULT SystemMediaResult() const;

    // Forward the parent's WM_APPCOMMAND here before DefWindowProc.
    // For the explicit HWND overload also forward its WM_NCDESTROY.
    // While SMTC is registered, Windows alone routes media keys (no double fire).
    std::optional<LRESULT> ProcessMessage(HWND, UINT, WPARAM, LPARAM);
    // Optional IsDialogMessage-equivalent for the widget's keyboard traversal.
    bool PreTranslateMessage(const MSG&);
    void Shutdown();

    // Production painter, also usable on an offscreen WidgetCanvas.
    void Draw(WidgetCanvas&, float width, float height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lume::platform::windows
