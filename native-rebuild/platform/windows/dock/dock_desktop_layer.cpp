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
            OwnOverlay(window) || ShellDesktop(window, result.host, result.shellPid)) return TRUE;
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
// Any maximized application on the monitor, focused or not, covers the Dock.
bool MaximizedCovers(HWND bar) {
    struct Search { HWND host; HMONITOR monitor; DWORD shellPid; bool found, desktop; } search{
        desktop_level::Host(), MonitorFromWindow(bar, MONITOR_DEFAULTTOPRIMARY), 0, false, false};
    search.desktop = desktop_level::DesktopForeground(search.host);
    GetWindowThreadProcessId(GetShellWindow(), &search.shellPid);
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& result = *reinterpret_cast<Search*>(data);
        if (result.desktop && window == result.host) return FALSE;
        if (!IsWindowVisible(window) || IsIconic(window) || !IsZoomed(window) ||
            OwnOverlay(window) || ShellDesktop(window, result.host, result.shellPid) ||
            MonitorFromWindow(window, MONITOR_DEFAULTTONULL) != result.monitor) return TRUE;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
        result.found = true;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
constexpr UINT Flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
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
    for (auto& hook : hooks_) { if (hook) UnhookWinEvent(hook); hook = nullptr; }
    if (icons_) KillTimer(icons_, FallbackTimer);
    watchers.erase(std::remove(watchers.begin(), watchers.end(), this), watchers.end());
    icons_ = bar_ = nullptr;
    pending_ = covered_ = fallback_ = false;
}
void DockDesktopLayer::Request() {
    if (!pending_ && icons_ && (covered_ || IsWindowVisible(icons_) || IsWindowVisible(bar_)))
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
    if (const bool covered = MaximizedCovers(bar_); covered != covered_) {
        covered_ = covered;
        // Restoring a maximized window raises no hooked event: poll gently while hidden.
        if (covered_) SetTimer(icons_, FallbackTimer, 700, nullptr);
        else if (fallback_) SetTimer(icons_, FallbackTimer, 1500, nullptr);
        else KillTimer(icons_, FallbackTimer);
    }
    // Show or hide without moving first: the ordering below compares visible windows.
    for (const HWND window : {icons_, bar_}) {
        desktop_level::PlacementScope placement(window);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, Flags | SWP_NOZORDER | (covered_ ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
    }
    if (covered_) return;
    Demote(icons_);
    Demote(bar_);
    const HWND host = desktop_level::Host();
    const HWND ceiling = ApplicationCeiling(host);
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
        if (above) anchor = above;
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
        if (!SetWindowPos(icons_, anchor, 0, 0, 0, 0, flags) &&
            !(host && SetWindowPos(icons_, host, 0, 0, 0, 0, flags)))
            ShowWindow(icons_, SW_SHOWNA);
    }
    {
        desktop_level::PlacementScope placement(bar_);
        SetWindowPos(bar_, icons_, 0, 0, 0, 0, Flags | SWP_SHOWWINDOW);
    }
    desktop_level::KeepOwnedGlassBelow(bar_);
}
}  // namespace lume::platform::windows
