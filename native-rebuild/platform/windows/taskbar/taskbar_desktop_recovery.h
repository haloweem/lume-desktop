#pragma once
#include <windows.h>
#include <vector>

namespace lume::platform::windows::taskbar_desktop_recovery {
// A conservative read-only snapshot. Unknown visibility/identity is never an
// invitation to show a tray or change the user's taskbar configuration.
struct Candidate {
    bool explorerTray = false;
    bool visible = false;
    bool opaque = false;
    bool iconic = false;
    bool cloaked = true;
    bool topmost = true;
    bool belowDesktop = false;
    bool exposedMonitorEdge = false;
    bool fullScreenAboveDesktop = true;
};
inline bool CanRepair(const Candidate& c, bool autoHide, bool suppressed) {
    return !autoHide && !suppressed && c.explorerTray && c.visible && c.opaque &&
        !c.iconic && !c.cloaked && !c.topmost && c.belowDesktop &&
        c.exposedMonitorEdge && !c.fullScreenAboveDesktop;
}
bool ExposedMonitorEdge(const RECT& taskbar, const RECT& monitor);
// Uses the actual foreground and current Explorer identity, never a remembered
// foreground-desktop flag from an animation or an earlier input event.
HWND CurrentDesktop();
class Recovery final {
public:
    // Checks actual state after a queued request on the following tick. The
    // return value counts verified results, never asynchronous API acceptance.
    // Changes only Z-order, never show/position/size/opacity/settings.
    int Tick(HWND settledDesktop, bool suppressed);
    void Reset() { pending_.clear(); }
private:
    struct Request { HWND window, desktop; DWORD shell; ULONGLONG at; };
    std::vector<Request> pending_;
};
}  // namespace lume::platform::windows::taskbar_desktop_recovery
