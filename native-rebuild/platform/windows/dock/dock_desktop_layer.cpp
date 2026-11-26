#include "platform/windows/dock/dock_desktop_layer.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include <dwmapi.h>
#include <algorithm>
#include <cwchar>
#include <vector>

namespace lume::platform::windows {
namespace {
thread_local std::vector<DockDesktopLayer*> watchers;
bool OwnOverlay(HWND window) {
    // Do not exempt the whole process: Lume's management/music window must
    // cover the Dock just like a browser or game. Only desktop surfaces and
    // the launchpad's own window family are part of the Dock interaction.
    for (unsigned depth = 0; window && depth < 16; ++depth) {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != GetCurrentProcessId()) return false;
        if (desktop_level::IsDesktopWidget(window)) return true;
        wchar_t name[64]{};
        if (GetClassNameW(window, name, 64) && (wcscmp(name, lume::window_ids::LaunchpadClass) == 0 ||
            wcscmp(name, lume::window_ids::CollectionPopupClass) == 0)) return true;
        if (const HWND owner = desktop_level::SurfaceOwner(window)) window = owner;
        else window = GetWindow(window, GW_OWNER);
    }
    return false;
}
bool ShellDesktop(HWND window, HWND host, DWORD shellPid) {
    if (window == host) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!shellPid || pid != shellPid) return false;
    wchar_t name[32]{};
    return GetClassNameW(window, name, 32) &&
           (wcscmp(name, L"WorkerW") == 0 || wcscmp(name, L"Progman") == 0);
}
HWND ApplicationCeiling(HWND host) {
    struct Search { HWND host, last; DWORD shellPid; bool desktop; } search{
        host, nullptr, 0, desktop_level::DesktopForeground(host)};
    GetWindowThreadProcessId(GetShellWindow(), &search.shellPid);
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& result = *reinterpret_cast<Search*>(data);
        if (result.desktop && window == result.host) return FALSE;
        if (!IsWindowVisible(window) || IsIconic(window) ||
            (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) ||
            OwnOverlay(window) || ShellDesktop(window, result.host, result.shellPid) ||
            desktop_level::NotificationSink(window)) return TRUE;
        RECT bounds{};
        if (!GetWindowRect(window, &bounds) || IsRectEmpty(&bounds)) return TRUE;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
        // EnumWindows visits top to bottom. The lowest normal application
        // (including its glass/owned popups) is the strict upper boundary.
        result.last = window;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.last;
}
// Ordinary applications cover a resident Dock through z-order, never SW_HIDE.
// Only the shared foreground fullscreen/exclusive policy suppresses its monitor.
bool FullscreenProtected(HWND bar) {
    RECT area{};GetWindowRect(bar,&area);
    return window_layer::enabled && (window_layer::balancedIdle || window_layer::Protected(area,window_layer::ReadContext()));
}
constexpr UINT Flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
HWND NextVisible(HWND window) {
    for (window=GetWindow(window,GW_HWNDNEXT);window&&!IsWindowVisible(window);window=GetWindow(window,GW_HWNDNEXT)) {}
    return window;
}
bool OverlayAbove(HWND icons) {
    for (HWND window=GetWindow(icons,GW_HWNDPREV);window;window=GetWindow(window,GW_HWNDPREV))
        if (IsWindowVisible(window) && !(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST) &&
            OwnOverlay(window)) return true;
    return false;
}
void Demote(HWND window) {
    if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) {
        desktop_level::PlacementScope placement(window);
        SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, Flags);
    }
}
}  // namespace

void DockDesktopLayer::Start(HWND icons, HWND bar) {
    Stop();
    icons_ = icons;
    bar_ = bar;
    watchers.push_back(this);
    constexpr DWORD ranges[][2]{{EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_REORDER},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED}};
    for (std::size_t i = 0; i < hooks_.size(); ++i)
        hooks_[i] = SetWinEventHook(ranges[i][0], ranges[i][1], nullptr, Event, 0, 0, WINEVENT_OUTOFCONTEXT);
    // The normal path is event-driven, with no per-frame window enumeration.
    // Keep a bounded fallback only if Windows could not install every hook.
    fallback_ = std::find(hooks_.begin(), hooks_.end(), nullptr) != hooks_.end();
    if (fallback_) SetTimer(icons_, FallbackTimer, 1500, nullptr);
}
void DockDesktopLayer::Stop() {
    if (summoned_) Restore();
    for (auto& hook : hooks_) { if (hook) UnhookWinEvent(hook); hook = nullptr; }
    if (icons_) KillTimer(icons_, FallbackTimer);
    watchers.erase(std::remove(watchers.begin(), watchers.end(), this), watchers.end());
    icons_ = bar_ = nullptr;
    pending_ = covered_ = fallback_ = summoned_ = false;
}
void DockDesktopLayer::Request() {
    if (!pending_ && icons_ && (covered_ || IsWindowVisible(icons_) || IsWindowVisible(bar_) || window_layer::WantsDesktop(icons_)))
        pending_ = PostMessageW(icons_, Message, 0, 0) != FALSE;
}
void CALLBACK DockDesktopLayer::Event(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object,
                                      LONG child, DWORD, DWORD) {
    if (event >= EVENT_OBJECT_CREATE) {
        if (object != OBJID_WINDOW || child != CHILDID_SELF) return;
        if (IsWindow(window) && window != GetDesktopWindow() && GetAncestor(window, GA_ROOT) != window) return;
        if (OwnOverlay(window)) return;  // our own placements cannot feed back
    } else if (event == EVENT_SYSTEM_FOREGROUND && window && OwnOverlay(window)) {
        // The launchpad (or a widget) taking the foreground does not move any
        // application. A restack here enumerated every window mid-opening and
        // cost the launchpad's first frames; the next real change still lands.
        return;
    }
    for (auto* watcher : watchers)
        if (std::find(watcher->hooks_.begin(), watcher->hooks_.end(), hook) != watcher->hooks_.end())
            watcher->Request();
}
void DockDesktopLayer::Place() {
    if (!icons_ || !bar_) return;
    if (summoned_) {
        covered_ = false;
        KillTimer(icons_, FallbackTimer);
        for (const HWND window : {bar_, icons_}) {
            desktop_level::PlacementScope placement(window);
            SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, Flags | SWP_SHOWWINDOW);
        }
        desktop_level::KeepOwnedGlassBelow(bar_);
        return;
    }
    if(window_layer::balancedIdle) {
        // The shared foreground watcher resumes every display. An invisible
        // Dock needs neither its own recovery timer nor a desktop Z-order scan.
        KillTimer(icons_,FallbackTimer);
        covered_=true;
        window_layer::Apply(icons_);
        window_layer::Apply(bar_);
        return;
    }
    window_layer::RestoreDesktop(icons_);
    // The bar follows the icon root, including its minimized state. Its
    // application ceiling and visibility are restored as the same pair below.
    if (desktop_level::DesktopForeground() && !window_layer::Suppressed(icons_) && IsIconic(bar_))
        ShowWindow(bar_, SW_SHOWNOACTIVATE);
    if (const bool covered = FullscreenProtected(bar_); covered != covered_) {
        covered_ = covered;
        // Recheck while protected so a missed foreground event cannot leave it hidden.
        if (covered_) SetTimer(icons_, FallbackTimer, 700, nullptr);
        else if (fallback_) SetTimer(icons_, FallbackTimer, 1500, nullptr);
        else KillTimer(icons_, FallbackTimer);
    }
    const HWND host = desktop_level::Host();
    const HWND ceiling = covered_ ? nullptr : ApplicationCeiling(host);
    // Explorer destroys many child/IME HWNDs while navigating. A queued
    // destroy event no longer tells us whether its HWND was a top-level app.
    // Reconcile real changes, but never move an already-correct Dock pair:
    // moving its UI-owned bar makes the worker-owned glass follow later.
    if (!covered_ && host && IsWindowVisible(icons_) && IsWindowVisible(bar_) &&
        !IsIconic(icons_) && !IsIconic(bar_) &&
        !(GetWindowLongPtrW(icons_,GWL_EXSTYLE)&WS_EX_TOPMOST) &&
        !(GetWindowLongPtrW(bar_,GWL_EXSTYLE)&WS_EX_TOPMOST) &&
        NextVisible(icons_)==bar_ && desktop_level::Above(bar_,host) &&
        (!ceiling || desktop_level::Above(ceiling,icons_)) && !OverlayAbove(icons_)) {
        const HWND below=NextVisible(bar_);
        if (!below || desktop_level::SurfaceOwner(below)!=bar_)
            desktop_level::KeepOwnedGlassBelow(bar_);
        return;
    }
    // Show or hide only when visibility changes; even an unchanged show can
    // cause Windows to reorder owned input-method windows.
    for (const HWND window : {icons_, bar_}) {
        if ((IsWindowVisible(window)!=FALSE)==!covered_) continue;
        desktop_level::PlacementScope placement(window);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, Flags | SWP_NOZORDER | (covered_ ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
    }
    if (covered_) return;
    Demote(icons_);
    Demote(bar_);
    // A raised/rebuilding Explorer host may be above applications. In that
    // inconsistent state prefer being covered by the desktop over covering an
    // application. Missing hosts likewise fall back to the bottom, never TOP.
    HWND anchor = ceiling ? ceiling : (host ? HWND_TOP : HWND_BOTTOM);
    // While the desktop is shown (Win+D, taskbar corner) Explorer holds the
    // foreground and Windows ignores HWND_TOP from this background process:
    // insert right after the window above the desktop host instead.
    if (!ceiling && host && desktop_level::DesktopForeground(host)) {
        HWND above = GetWindow(host, GW_HWNDPREV);
        while (above && (above == icons_ || above == bar_ || desktop_level::InDesktopLayer(above)))
            above = GetWindow(above, GW_HWNDPREV);
        // Inserting after a topmost taskbar/overlay promotes an unowned
        // window into its topmost band. The next idle sweep then demotes
        // icons and bar separately, briefly leaving the glass above icons,
        // before promoting them again. Stay in the normal band instead.
        if (above && !(GetWindowLongPtrW(above, GWL_EXSTYLE) & WS_EX_TOPMOST)) anchor = above;
        // Only topmost windows (the island, the taskbar) above the raised
        // desktop: HWND_TOP would be ignored and the Dock would stay under it.
        // Join the shared placement anchor at the top of the normal band.
        else if (const HWND sibling = desktop_level::desktopPlacementAnchor.AboveHost(host)) anchor = sibling;
    }
    // When lowering the group below an application, move the background and
    // its worker-owned glass first. Lowering icons first exposes the glass
    // above them until the later calls finish (and can capture the icons).
    // Raising the group retains the opposite order below: icons, bar, glass.
    if (anchor != HWND_TOP && anchor != HWND_BOTTOM && desktop_level::Above(icons_, anchor)) {
        desktop_level::PlacementScope placement(bar_);
        if (SetWindowPos(bar_, anchor, 0, 0, 0, 0, Flags | SWP_SHOWWINDOW))
            desktop_level::KeepOwnedGlassBelow(bar_);
    }
    {
        desktop_level::PlacementScope placement(icons_);
        UINT flags = Flags | SWP_SHOWWINDOW;
        if ((ceiling && desktop_level::Above(ceiling, icons_) &&
             (!host || desktop_level::Above(icons_, host))) ||
            (anchor == HWND_TOP && (!GetWindow(icons_, GW_HWNDPREV) ||
             (GetWindowLongPtrW(GetWindow(icons_, GW_HWNDPREV), GWL_EXSTYLE) & WS_EX_TOPMOST))))
            flags |= SWP_NOZORDER;
        // A lower-integrity process cannot insert a window relative to a
        // higher-integrity or protected one: the ceiling or anchor here can be
        // such a window (a system overlay right above the desktop), and the
        // refused call would leave the Dock hidden. Retry against the desktop
        // host, then show it without a z-order request.
        desktop_level::RequestPlacement(icons_,anchor,flags,host);
    }
    {
        desktop_level::PlacementScope placement(bar_);
        SetWindowPos(bar_, icons_, 0, 0, 0, 0, Flags | SWP_SHOWWINDOW);
    }
    desktop_level::KeepOwnedGlassBelow(bar_);
}
bool DockDesktopLayer::Summon() {
    if (!icons_ || !bar_) return false;
    if (!window_layer::SetFullscreenBypass(icons_, true)) return false;
    if (!window_layer::SetFullscreenBypass(bar_, true)) {
        window_layer::SetFullscreenBypass(icons_, false);
        return false;
    }
    summoned_ = true;
    covered_ = false;
    KillTimer(icons_, FallbackTimer);
    bool ok = true;
    for (const HWND window : {bar_, icons_}) {
        desktop_level::PlacementScope placement(window);
        ok = SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, Flags | SWP_SHOWWINDOW) != FALSE && ok;
    }
    if (!ok) { Restore(); return false; }
    desktop_level::KeepOwnedGlassBelow(bar_);
    return true;
}
void DockDesktopLayer::Restore() {
    summoned_ = false;
    if (bar_) window_layer::SetFullscreenBypass(bar_, false);
    if (icons_) window_layer::SetFullscreenBypass(icons_, false);
    Place();
}
}  // namespace lume::platform::windows
