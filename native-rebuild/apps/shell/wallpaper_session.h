#pragma once
#include "core/ui/wallpaper_page_protocol.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lume::shell {
namespace detail {
// Top-to-bottom visible window candidates after filtering hidden/cloaked and
// non-application windows. Kept separate so desktop visibility is testable
// without moving windows on the user's desktop.
struct WallpaperCoverageWindow {
    RECT bounds{};
    bool desktop=false;
    bool shellOverlay=false;
};
bool WallpaperDisplayCovered(const RECT& display,const std::vector<WallpaperCoverageWindow>& windows,
                             bool desktopForeground=false);
// Read-only native window enumeration shared by production and explicit
// fullscreen fixtures; does not move windows or alter input/desktop state.
bool WallpaperDisplayCoveredNative(const RECT& display);
}
struct WallpaperSessionOptions {
    // Required library root; verification callers must supply an isolated fixture root.
    std::string root;
    std::wstring executable;
    bool verification=false;
    HWND verificationParent=nullptr;
    // Supply the restored AppSettings value before Restore starts any player.
    bool balancedMode=false;
    // UI-thread notification after an official download is successfully registered.
    std::function<void(std::string id,std::string sourceUrl)> downloadCompleted;
    // UI-thread admission; completion is delivered on that same UI thread.
    std::function<void(std::string id,std::string sourceUrl,std::function<void(std::string)> completed)> authorizeDownload;
    // Actor-thread memory probe for isolated watchdog tests only. Ignored unless
    // verification and an owned verificationParent are both supplied.
    std::function<std::uint64_t(std::uint64_t display)> verificationPrivateBytes;
    // Isolated playback tests only; ignored outside an owned verification parent.
    std::function<bool(std::uint64_t display)> verificationFullscreen;
    // Scales only recovery/parking intervals in isolated tests, never player IPC.
    unsigned verificationRecoveryMilliseconds=0;
    // Explicit isolated diagnostics fixture, ignored without an owned parent.
    std::string verificationResourceHistoryDirectory;
    unsigned verificationResourceHistoryIntervalMilliseconds=60000;
};
class WallpaperSession final {
public:
    using Notify=std::function<void(const std::wstring&)>;
    WallpaperSession(HINSTANCE,WallpaperSessionOptions,Notify);
    ~WallpaperSession();
    WallpaperSession(const WallpaperSession&)=delete;
    WallpaperSession& operator=(const WallpaperSession&)=delete;
    bool Restore();
    // Call from a background settings worker, not the UI/actor thread.
    // An actor-confirmed unchanged policy need not wait for restored players.
    // Actual changes stay ordered: queued work expires after 2s, started work
    // waits for bounded IPC/rollback.
    // False guarantees that this request cannot change the mode later.
    void SetGameIdle(bool idle);  // UI thread, transient across all displays
    bool SetBalancedMode(bool enabled);
    void HandlePage(const ui::WallpaperPageCommand&,HWND owner=nullptr);
    // Batch-local path deduplication, at most 32 successful imports, first error retained.
    // Verification permits explicit file imports, but does not enable system playback.
    void HandleDroppedFiles(std::vector<std::wstring> paths);
    std::wstring State()const;
    bool IsOfficialId(std::string_view id)const;
    std::wstring MediaDirectory()const;
    bool HasWork()const;
    void Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
