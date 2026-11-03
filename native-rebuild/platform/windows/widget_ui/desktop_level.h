#pragma once
#include <windows.h>
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/widget_ui/window_layer_policy.h"
#include <cwchar>

namespace lume::platform::windows::desktop_level {
// Desktop widgets sit directly above Explorer's desktop host. That host can
// change after a widget was shown: a late desktop during boot, a wallpaper's
// 0x052C WorkerW split on Win10/early Win11, or an Explorer restart. Widgets
// restack themselves when they receive Message().
inline UINT Message() {
    static const UINT message = RegisterWindowMessageW(L"Lume.DesktopLevel.v1");
    return message;
}
inline HWND Host() {
    HWND host = nullptr;
    EnumWindows([](HWND window, LPARAM out) -> BOOL {
        if (!FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr)) return TRUE;
        *reinterpret_cast<HWND*>(out) = window;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&host));
    // Before the icon view exists, stay above the shell window: HWND_BOTTOM
    // would leave the widget behind Progman once Explorer finishes starting.
    return host ? host : GetShellWindow();
}
inline bool IsDesktopWidget(HWND window) {
    if (GetPropW(window, lume::window_ids::DesktopWidgetProp)) return true;
    wchar_t name[64]{};
    return GetClassNameW(window, name, 64) && lume::window_ids::HasDesktopClassPrefix(name);
}
// The widget a glass surface or click-through display window draws for.
inline HWND SurfaceOwner(HWND window) {
    if (const auto owner = GetPropW(window, lume::window_ids::GlassSurfaceOwnerProp)) return reinterpret_cast<HWND>(owner);
    return reinterpret_cast<HWND>(GetPropW(window, lume::window_ids::DesktopCompanionOwnerProp));
}
// Lume's desktop layer: its widgets, their glass surfaces and display
// windows, which sit right below each owner. Hidden windows do not separate the layer.
inline bool InDesktopLayer(HWND window) {
    if (!IsWindowVisible(window)) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) return false;
    if (IsDesktopWidget(window)) return true;
    const HWND owner = SurfaceOwner(window);
    return owner && IsDesktopWidget(owner);
}
inline bool AtBottom(HWND window) { return GetPropW(window, lume::window_ids::DesktopBottomProp) != nullptr; }
// Fixed bands inside the desktop layer, bottom to top: decorations that only
// show (the spectrum), ordinary widgets, the icon organizer, the Dock. A click
// reorders windows within a band only, so a widget never covers the icons or
// the Dock it happens to overlap. Surfaces share their owner's band.
enum class Band { Bottom = 0, Widget = 1, Shelf = 2, Dock = 3 };
inline Band BandOf(HWND window) {
    if (const HWND owner = SurfaceOwner(window)) window = owner;
    if (AtBottom(window)) return Band::Bottom;
    if (lume::window_ids::IsClass(window, lume::window_ids::DockIconsClass) ||
        lume::window_ids::IsClass(window, lume::window_ids::DockBarClass)) return Band::Dock;
    if (GetPropW(window, lume::window_ids::DesktopShelfProp)) return Band::Shelf;
    return Band::Widget;
}
// The window itself or one of its own surfaces.
inline bool SameFamily(HWND window, HWND other) { return other == window || SurfaceOwner(other) == window; }
// The lowest visible window of a higher band in the layer above `host`: a
// window of `window`'s band belongs directly below it (and its surfaces).
inline HWND BandCeiling(HWND window, HWND host) {
    if (!host) return nullptr;
    const auto band = BandOf(window);
    for (HWND above = GetWindow(host, GW_HWNDPREV); above && InDesktopLayer(above); above = GetWindow(above, GW_HWNDPREV)) {
        if (!IsWindowVisible(above) || SameFamily(window, above)) continue;
        if (BandOf(above) > band) return above;
    }
    return nullptr;
}
// A window of a higher band lies below `window` inside the layer.
inline bool BandViolated(HWND window, HWND host) {
    if (!host) return false;
    const auto band = BandOf(window);
    for (HWND above = GetWindow(host, GW_HWNDPREV); above && above != window && InDesktopLayer(above);
         above = GetWindow(above, GW_HWNDPREV))
        if (IsWindowVisible(above) && !SameFamily(window, above) && BandOf(above) > band) return true;
    return false;
}
// An application window (Lume's manager included) lies over part of `window`.
inline bool CoveredByApplication(HWND window) {
    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return false;
    for (HWND above = GetWindow(window, GW_HWNDPREV); above; above = GetWindow(above, GW_HWNDPREV)) {
        if (!IsWindowVisible(above) || IsIconic(above) || InDesktopLayer(above)) continue;
        if (GetWindowLongW(above, GWL_EXSTYLE) & (WS_EX_TOPMOST | WS_EX_TRANSPARENT)) continue;
        DWORD pid = 0;
        GetWindowThreadProcessId(above, &pid);
        // A widget's own popups (menu, editor) and glass do not cover it.
        if (pid == GetCurrentProcessId() && (GetWindow(above, GW_OWNER) == window || SurfaceOwner(above))) continue;
        RECT other{}, overlap{};
        if (GetWindowRect(above, &other) && IntersectRect(&overlap, &bounds, &other)) return true;
    }
    return false;
}
inline bool Above(HWND window, HWND other) {
    if (!window || !other) return false;
    for (HWND below = GetWindow(window, GW_HWNDNEXT); below; below = GetWindow(below, GW_HWNDNEXT))
        if (below == other) return true;
    return false;
}
inline thread_local HWND foregroundDesktop = nullptr;
inline bool IsDesktopForegroundWindow(HWND window, HWND host) {
    if (!window || !host) return false;
    if (window == host || GetAncestor(window, GA_ROOT) == host) return true;
    DWORD shellPid = 0, hostPid = 0, windowPid = 0;
    GetWindowThreadProcessId(GetShellWindow(), &shellPid);
    GetWindowThreadProcessId(host, &hostPid);
    GetWindowThreadProcessId(window, &windowPid);
    wchar_t name[32]{};
    return shellPid && shellPid == hostPid && shellPid == windowPid &&
        GetClassNameW(window, name, 32) && (!wcscmp(name, L"Progman") || !wcscmp(name, L"WorkerW"));
}
inline bool DesktopForeground(HWND host = Host()) {
    const HWND foreground = GetForegroundWindow();
    if (!host) return false;
    return foregroundDesktop == host || IsDesktopForegroundWindow(foreground, host);
}
// Explorer can temporarily rise above the manager during desktop transitions.
// The manager remains a ceiling even when it is below the selected host.
inline HWND ManagerCeiling() {
    HWND result = nullptr;
    EnumWindows([](HWND window, LPARAM out) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(window) || IsIconic(window)) return TRUE;
        wchar_t name[64]{};
        if (GetClassNameW(window, name, 64) && wcscmp(name, lume::window_ids::ManagerClass) == 0)
            *reinterpret_cast<HWND*>(out) = window;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    if (!result) return nullptr;
    const HWND manager = result;
    for (HWND below = GetWindow(manager, GW_HWNDNEXT); below; below = GetWindow(below, GW_HWNDNEXT))
        if (IsWindowVisible(below) && GetPropW(below, lume::window_ids::GlassSurfaceOwnerProp) == manager) result = below;
    return result;
}
// Every normal application is a ceiling, including one Explorer temporarily
// moved below the desktop host. Never exempt an entire process: manager windows
// in this process must still cover its desktop widgets.
inline HWND ApplicationCeiling(HWND host) {
    struct Scan { HWND host, last; bool desktop; } scan{host, nullptr, DesktopForeground(host)};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Scan*>(data);
        if (s.desktop && window == s.host) return FALSE;
        if (window == s.host || !IsWindowVisible(window) || IsIconic(window) || InDesktopLayer(window) ||
            window_layer::DesktopFamily(window) || window_layer::ShellDesktop(window) ||
            (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST)) return TRUE;
        DWORD cloak = 0; RECT bounds{};
        if (!GetWindowRect(window, &bounds) || IsRectEmpty(&bounds) ||
            (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloak, sizeof(cloak))) && cloak)) return TRUE;
        s.last = window; return TRUE;
    }, reinterpret_cast<LPARAM>(&scan));
    return scan.last;
}
// Directly below a widget: its display window, then its glass.
inline void KeepOwnedGlassBelow(HWND owner) {
    struct Search { HWND owner, glass, display; } search{owner, nullptr, nullptr};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& result = *reinterpret_cast<Search*>(data);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        if (!result.glass && GetPropW(window, lume::window_ids::GlassSurfaceOwnerProp) == result.owner) result.glass = window;
        if (!result.display && GetPropW(window, lume::window_ids::DesktopCompanionOwnerProp) == result.owner)
            result.display = window;
        return !(result.glass && result.display);
    }, reinterpret_cast<LPARAM>(&search));
    HWND above = owner;
    for (const HWND surface : {search.display, search.glass}) {
        if (!surface || !IsWindowVisible(surface)) continue;
        if (GetWindow(above, GW_HWNDNEXT) != surface)
            SetWindowPos(surface, above, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        above = surface;
    }
}
// Set while Place() restacks, so Changing() lets that move through.
inline thread_local HWND placingWindow = nullptr;
// Retained for existing callers; selection never grants cross-application order.
inline thread_local HWND selectedWidget = nullptr;
inline void RequestAll();
// Ignore implicit activation/owned-popup reorders. Place() and a direct
// Select() authorize only their own HWND, including during nested messages.
inline void Changing(LPARAM l) {
    auto* position = reinterpret_cast<WINDOWPOS*>(l);
    if (position && position->hwnd != placingWindow && !(position->flags & SWP_NOZORDER))
        position->flags |= SWP_NOZORDER;
}
// An owned popup (search box, title editor) sits directly above its owner,
// not at the top of the Z-order where it would float over other programs.
inline void KeepAbove(HWND popup, HWND owner) {
    const HWND above = GetWindow(owner, GW_HWNDPREV);
    if (above != popup)
        SetWindowPos(popup, above ? above : HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}
struct PlacementScope {
    HWND previous;
    explicit PlacementScope(HWND current) : previous(placingWindow) { placingWindow = current; }
    ~PlacementScope() { placingWindow = previous; }
};
// raise: move to the top of the window's band even when it already sits in it.
inline void Place(HWND window, bool show, bool raise = false);
// A click only changes order inside the desktop band, never above applications.
inline void Select(HWND window) {
    if (!IsDesktopWidget(window) || !IsWindowVisible(window) || AtBottom(window)) return;
    selectedWidget = window;
    Place(window, false, true);
}
// A click on a Lume desktop surface that is not a widget (the Dock): a widget
// lifted over an application returns to the desktop layer.
inline void Deselect() {
    if (!selectedWidget) return;
    selectedWidget = nullptr;
    RequestAll();
}
inline void ManagerSelected() {
    selectedWidget = nullptr;
    RequestAll();
}
inline void Forget(HWND window) {
    if (selectedWidget == window) selectedWidget = nullptr;
}
// Placement is a z-order request. Windows refuses it when the insert-after
// window belongs to a higher-integrity or protected process (a system overlay
// sitting right above the desktop), and the refusal covers the whole call --
// including its show -- with ERROR_ACCESS_DENIED. Fall back to the desktop
// host only. If that also fails, hide instead of trying higher applications.
inline bool RequestPlacement(HWND window, HWND anchor, UINT flags, HWND host) {
    if(anchor&&anchor!=HWND_BOTTOM&&anchor!=HWND_TOPMOST&&anchor!=HWND_NOTOPMOST&&
       (GetWindowLongPtrW(anchor,GWL_EXSTYLE)&WS_EX_TOPMOST))anchor=HWND_TOP;
    if (SetWindowPos(window, anchor, 0, 0, 0, 0, flags)) return true;
    // The desktop host keeps the window in its own layer (below applications),
    // so it is the correct fallback; walking up would insert the widget under
    // some application and leave it above the ones below that.
    if (host && host != anchor && SetWindowPos(window, host, 0, 0, 0, 0, flags)) return true;
    // Failure cannot grant permission to rise above a protected/system anchor.
    SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, (flags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW);
    return false;
}
inline void Place(HWND window, bool show, bool raise) {
    PlacementScope scope(window);
    if(show&&window_layer::Suppressed(window))window_layer::WantVisible(window);
    if (window_layer::Suppressed(window)) return;
    // Calendar/weather own even-hidden topmost tooltip windows. Windows must
    // not reorder that owner group as a side effect of moving a glass/widget.
    const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                       (show ? SWP_SHOWWINDOW : 0);
    const HWND host = Host();
    if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST)
        SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, flags & ~SWP_SHOWWINDOW);
    HWND ceiling = ApplicationCeiling(host);
    if(selectedWidget && selectedWidget!=window && IsWindowVisible(selectedWidget) &&
       BandOf(selectedWidget)==BandOf(window) && (!ceiling || !Above(selectedWidget,ceiling)))ceiling=selectedWidget;
    // Show Desktop raises Explorer above still-visible application windows.
    // Those covered windows are no longer a ceiling until an app is restored.
    if (DesktopForeground(host) && Above(host, ceiling)) ceiling = nullptr;
    if (!host) { RequestPlacement(window, HWND_BOTTOM, flags, nullptr); KeepOwnedGlassBelow(window); return; }
    if (AtBottom(window)) {
        // Right below the lowest of Lume's other desktop windows (Dock, widgets,
        // their glass), found walking up from the host. Inserting after one of
        // our own windows cannot be refused; inserting after a foreign window
        // above the host (a wallpaper or overlay) can be, and the usual
        // walk-up fallback would then lift this clear window over the Dock and
        // even over applications. Stop at the first application window.
        HWND anchor = nullptr, first = nullptr;
        for (HWND above = GetWindow(host, GW_HWNDPREV); above; above = GetWindow(above, GW_HWNDPREV)) {
            if (SameFamily(window, above)) continue;
            if (!IsWindowVisible(above)) continue;
            if (!first) first = above;
            DWORD pid = 0;
            GetWindowThreadProcessId(above, &pid);
            if (pid == GetCurrentProcessId()) {
                if (InDesktopLayer(above)) { anchor = above; break; }
                continue;
            }
            if ((GetWindowLongW(above, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) break;
        }
        if (!anchor) anchor = first;
        if (ceiling && (!anchor || Above(anchor, ceiling))) anchor = ceiling;
        // Already directly below the anchor: leave the order alone.
        HWND below = anchor ? GetWindow(anchor, GW_HWNDNEXT) : nullptr;
        while (below && below != window && !IsWindowVisible(below)) below = GetWindow(below, GW_HWNDNEXT);
        if (!anchor || below == window) {
            if (show) RequestPlacement(window, nullptr, flags | SWP_NOZORDER, host);
            KeepOwnedGlassBelow(window);
            return;
        }
        // Never fall back to a higher anchor: keep the order rather than rise.
        RequestPlacement(window, anchor, flags, host);
        KeepOwnedGlassBelow(window);
        return;
    }
    // Already in the layer directly above the host, under every window of a
    // higher band: leave the order alone. Inserting after the window above
    // the host would put the widget below its own glass surface, and the
    // surface would then jump back (flicker).
    const bool reorder = raise || BandViolated(window, host);
    HWND after = GetWindow(host, GW_HWNDPREV);
    for (; after && InDesktopLayer(after); after = GetWindow(after, GW_HWNDPREV)) {
        if (after != window || Above(window, ceiling) || reorder) continue;
        if (show && !IsWindowVisible(window)) {
            // ShowWindow(SW_SHOWNA) raises a hidden top-level window to the
            // top of the normal band. That would put a recovered Dock above
            // the manager even though its layer slot was below it. Show it
            // explicitly below the ceiling (or its previous upper neighbor)
            // instead, so a recovery never changes the layer.
            const HWND anchor = ceiling ? ceiling : GetWindow(window, GW_HWNDPREV);
            RequestPlacement(window, anchor, flags | SWP_SHOWWINDOW, host);
        } else if (show) {
            RequestPlacement(window, nullptr, flags | SWP_NOZORDER, host);
        }
        KeepOwnedGlassBelow(window);
        return;
    }
    // Below a higher band (the organizer, the Dock): directly under its
    // lowest window. Inserting after one of our own windows cannot be refused.
    if (const HWND upper = BandCeiling(window, host); upper && (!ceiling || !Above(upper, ceiling))) {
        RequestPlacement(window, upper, flags, host);
        KeepOwnedGlassBelow(window);
        return;
    }
    // Otherwise join the top of that layer, above every glass surface in it.
    // A non-topmost window cannot be inserted after a topmost one (Windows
    // ignores that); the top of the normal band is the same place.
    if (ceiling && (!after || Above(after, ceiling))) after = ceiling;
    const bool band = !after || (GetWindowLongW(after, GWL_EXSTYLE) & WS_EX_TOPMOST);
    // While the desktop is shown (Win+D, taskbar corner) Explorer holds the
    // foreground and Windows ignores HWND_TOP from a background process, so
    // the widget would stay under the raised desktop. Inserting after the
    // window directly above the host is still honoured.
    RequestPlacement(window, band ? HWND_TOP : after, flags, host);
    KeepOwnedGlassBelow(window);
}
inline void RequestAll() {
    EnumWindows([](HWND window, LPARAM) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(window) && IsDesktopWidget(window))
            PostMessageW(window, Message(), 0, 0);
        return TRUE;
    }, 0);
}

// Watches Explorer for desktop host changes and asks widgets to restack.
// Owned by the UI thread; it relies on that thread's message loop.
class Watcher {
public:
    Watcher() = default;
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;
    ~Watcher() { Stop(); }
    bool Start(HINSTANCE instance) {
        if (window_) return true;
        if (current_) return false;
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = instance;
        wc.lpfnWndProc = Procedure; wc.lpszClassName = L"LumeDesktopLevelWatch";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        // HWND_MESSAGE would not receive Explorer's TaskbarCreated broadcast.
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP,
                                  0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (!window_) return false;
        current_ = this;
        window_layer::applicationCeiling=[] { return ApplicationCeiling(Host()); };
        window_layer::Start();
        SetTimer(window_, PolicyTimer, 250, nullptr);
        Hook();
        // A clicked widget is lifted above other windows. Once the user moves on
        // to anything else, it returns to the desktop layer; otherwise closing or
        // minimizing that other window would reveal the widget over the user's work.
        foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, Foreground, 0, 0,
                                          WINEVENT_OUTOFCONTEXT);
        locationHook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, Location, 0, 0,
                                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        Sweep();
        return true;
    }
    void Stop() {
        if (current_ == this) window_layer::Stop();
        foregroundDesktop = nullptr;
        if (hook_) { UnhookWinEvent(hook_); hook_ = nullptr; }
        if (foregroundHook_) { UnhookWinEvent(foregroundHook_); foregroundHook_ = nullptr; }
        if (locationHook_) { UnhookWinEvent(locationHook_); locationHook_ = nullptr; }
        if (window_) { DestroyWindow(window_); window_ = nullptr; }
        if (current_ == this) current_ = nullptr;
    }
private:
    static constexpr UINT_PTR SweepTimer = 1, SettleTimer = 2, DesktopTimer = 3, PolicyTimer = 4;
    static void CALLBACK Location(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG, DWORD, DWORD) {
        if (current_ && !current_->policyPending_ && object == OBJID_WINDOW && window == GetForegroundWindow())
            current_->policyPending_=PostMessageW(current_->window_, WM_APP + 1, 0, 0)!=FALSE;
    }
    // While the desktop is in front, Explorer raises it again without a new
    // foreground change: after Win+D's animation and on every click on the
    // desktop (any monitor). Widgets restack this often until it leaves.
    static constexpr UINT DesktopMs = 250;
    static constexpr UINT SweepMs = 2000, SettleMs = 300, SweepCount = 30;
    static DWORD ShellProcess() {
        DWORD pid = 0;
        if (const auto shell = GetShellWindow()) GetWindowThreadProcessId(shell, &pid);
        return pid;
    }
    void Hook() {
        if (hook_) { UnhookWinEvent(hook_); hook_ = nullptr; }
        hookProcess_ = ShellProcess();
        hook_ = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_REORDER, nullptr, Event, hookProcess_, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }
    // Startup and Explorer restarts: restack periodically for one minute in
    // case the desktop host appears without a visible top-level show event.
    void Sweep() {
        sweeps_ = 0;
        SetTimer(window_, SweepTimer, SweepMs, nullptr);
        RequestAll();
    }
    static void CALLBACK Event(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD) {
        if (!current_ || object != OBJID_WINDOW || child != CHILDID_SELF || !window) return;
        if (GetAncestor(window, GA_PARENT) != GetDesktopWindow()) return;
        wchar_t name[16]{};
        if (!GetClassNameW(window, name, 16) || (wcscmp(name, L"WorkerW") && wcscmp(name, L"Progman"))) return;
        SetTimer(current_->window_, SettleTimer, SettleMs, nullptr);
    }
    static void CALLBACK Foreground(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG, DWORD, DWORD) {
        if (current_) ForegroundChanged(window);
    }
public:
    // Called for every foreground change; public for tests.
    static void ForegroundChanged(HWND window) {
        window_layer::Refresh();
        const bool wasDesktop = foregroundDesktop != nullptr;
        const HWND host = Host();
        foregroundDesktop = IsDesktopForegroundWindow(window, host) ? host : nullptr;
        if (foregroundDesktop) {
            selectedWidget = nullptr;
            RequestAll();
            if (current_) {
                SetTimer(current_->window_, SettleTimer, SettleMs, nullptr);
                SetTimer(current_->window_, DesktopTimer, DesktopMs, nullptr);
            }
            return;
        }
        if (current_) KillTimer(current_->window_, DesktopTimer);
        if (wasDesktop) RequestAll();
        if (!selectedWidget || !window) return;
        // The selected widget itself, its glass and its popups (menus, editors) keep it selected.
        for (HWND w = window; w; w = GetWindow(w, GW_OWNER)) {
            if (w == selectedWidget || InDesktopLayer(w)) return;
            const HWND owner = SurfaceOwner(w);
            if (owner && (owner == selectedWidget || IsDesktopWidget(owner))) return;
        }
        selectedWidget = nullptr;
        RequestAll();
    }
private:
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Watcher*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Watcher*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, w, l);
        if (message == WM_APP + 1 || (message == WM_TIMER && w == PolicyTimer)) {
            self->policyPending_=false;
            window_layer::Refresh(); return 0;
        }
        if (message == self->taskbarCreated_ && self->taskbarCreated_) {
            self->Hook(); self->Sweep(); return 0;
        }
        if (message == WM_TIMER && w == DesktopTimer) {
            // Stop if the foreground left the desktop without an event reaching us.
            if (IsDesktopForegroundWindow(GetForegroundWindow(), Host())) RequestAll();
            else { foregroundDesktop = nullptr; KillTimer(window, DesktopTimer); RequestAll(); }
            return 0;
        }
        if (message == WM_TIMER && w == SettleTimer) {
            KillTimer(window, SettleTimer); RequestAll(); return 0;
        }
        if (message == WM_TIMER && w == SweepTimer) {
            if (!self->hookProcess_ && ShellProcess()) self->Hook();
            RequestAll();
            if (++self->sweeps_ >= SweepCount) KillTimer(window, SweepTimer);
            return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
    static inline Watcher* current_ = nullptr;
    HWND window_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr, foregroundHook_ = nullptr, locationHook_ = nullptr;
    DWORD hookProcess_ = 0;
    UINT taskbarCreated_ = 0;
    unsigned sweeps_ = 0;
    bool policyPending_ = false;
};
}  // namespace lume::platform::windows::desktop_level
