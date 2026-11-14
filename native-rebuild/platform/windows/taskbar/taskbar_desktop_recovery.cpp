#include "platform/windows/taskbar/taskbar_desktop_recovery.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/shell_surface.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "core/taskbar/taskbar_store.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <cwchar>
#include <string>
#include <algorithm>

namespace lume::platform::windows::taskbar_desktop_recovery {
namespace {
bool Class(HWND window, const wchar_t* expected) {
    wchar_t name[64]{};
    return GetClassNameW(window, name, 64) && wcscmp(name, expected) == 0;
}
DWORD Process(HWND window) {
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid); return pid;
}
bool Explorer(DWORD pid) {
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{}, windows[MAX_PATH]{}; DWORD length = 32768;
    const bool read = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    const UINT count = GetWindowsDirectoryW(windows, MAX_PATH);
    if (!read || !count || count >= MAX_PATH) return false;
    const std::wstring expected = std::wstring(windows) + L"\\explorer.exe";
    return _wcsicmp(path, expected.c_str()) == 0;
}
bool Uncloaked(HWND window) {
    DWORD cloak = 1;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloak, sizeof(cloak))) && !cloak;
}
bool Opaque(HWND window) {
    if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED)) return true;
    COLORREF key = 0; BYTE alpha = 0; DWORD flags = 0;
    return GetLayeredWindowAttributes(window, &key, &alpha, &flags) &&
        (!(flags & LWA_ALPHA) || alpha != 0);
}
bool Covers(const RECT& window, const RECT& area) {
    constexpr LONG tolerance = 2;
    return window.left <= area.left + tolerance && window.top <= area.top + tolerance &&
        window.right >= area.right - tolerance && window.bottom >= area.bottom - tolerance;
}
bool Above(HWND window, HWND other) {
    for (HWND below = GetWindow(window, GW_HWNDNEXT); below; below = GetWindow(below, GW_HWNDNEXT))
        if (below == other) return true;
    return false;
}
bool AtMonitorEdge(const RECT& bar, const RECT& monitor) {
    if (IsRectEmpty(&bar) || bar.left < monitor.left || bar.top < monitor.top ||
        bar.right > monitor.right || bar.bottom > monitor.bottom) return false;
    const LONG width = bar.right - bar.left, height = bar.bottom - bar.top;
    const LONG screenWidth = monitor.right - monitor.left, screenHeight = monitor.bottom - monitor.top;
    const bool horizontal = width >= screenWidth / 2 && height < screenHeight / 2 &&
        (bar.top == monitor.top || bar.bottom == monitor.bottom);
    const bool vertical = height >= screenHeight / 2 && width < screenWidth / 2 &&
        (bar.left == monitor.left || bar.right == monitor.right);
    return horizontal || vertical;
}
bool Tray(HWND window, DWORD shell) {
    return shell && Process(window) == shell && GetAncestor(window, GA_ROOT) == window &&
        (Class(window, L"Shell_TrayWnd") || Class(window, L"Shell_SecondaryTrayWnd"));
}
bool AutoHide(DWORD shell) {
    const HWND primary = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!Tray(primary, shell) || IsHungAppWindow(primary)) return true;
    APPBARDATA data{}; data.cbSize = sizeof(data); data.hWnd = primary;
    return (SHAppBarMessage(ABM_GETSTATE, &data) & ABS_AUTOHIDE) != 0;
}
bool FullScreenAbove(HWND desktop, const RECT& monitor) {
    for (HWND window = GetTopWindow(nullptr); window && window != desktop; window = GetWindow(window, GW_HWNDNEXT)) {
        if (!IsWindowVisible(window) || IsIconic(window)) continue;
        RECT bounds{};
        if (!GetWindowRect(window, &bounds) || IsRectEmpty(&bounds) || !Covers(bounds, monitor)) continue;
        const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        // Cursor and input-transparent accessibility overlays do not constitute
        // a foreground fullscreen application. Interactive fullscreen windows do.
        if ((style & (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) == (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) continue;
        if (shell_surface::ShellSurface(window)) continue;
        if (Process(window) == GetCurrentProcessId() &&
            (GetPropW(window, lume::window_ids::DesktopWidgetProp) ||
             GetPropW(window, lume::window_ids::GlassSurfaceOwnerProp) ||
             GetPropW(window, lume::window_ids::DesktopCompanionOwnerProp))) continue;
        DWORD cloak = 0;
        if (FAILED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloak, sizeof(cloak)))) return true;
        if (cloak) continue;
        if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED) {
            COLORREF key = 0; BYTE alpha = 0; DWORD flags = 0;
            if (!GetLayeredWindowAttributes(window, &key, &alpha, &flags)) return true;
            if ((flags & LWA_ALPHA) && alpha == 0) continue;
        }
        return true;
    }
    return false;
}
Candidate Read(HWND window, HWND desktop, DWORD shell) {
    Candidate c;
    c.explorerTray = Tray(window, shell);
    c.visible = IsWindowVisible(window) != FALSE;
    c.iconic = IsIconic(window) != FALSE;
    c.opaque = Opaque(window);
    c.cloaked = !Uncloaked(window);
    c.topmost = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    c.belowDesktop = Above(desktop, window);
    RECT bar{}, host{};
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    const HMONITOR screen = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    if (screen && GetMonitorInfoW(screen, &monitor) && GetWindowRect(window, &bar) &&
        GetWindowRect(desktop, &host)) {
        c.exposedMonitorEdge = AtMonitorEdge(bar, monitor.rcMonitor) && Covers(host, bar);
        c.fullScreenAboveDesktop = FullScreenAbove(desktop, monitor.rcMonitor);
    }
    return c;
}
}  // namespace
bool ExposedMonitorEdge(const RECT& taskbar, const RECT& monitor) {
    return AtMonitorEdge(taskbar, monitor);
}
HWND CurrentDesktop() {
    const HWND foreground = GetForegroundWindow();
    if (!foreground || (!Class(foreground, L"Progman") && !Class(foreground, L"WorkerW")) ||
        GetAncestor(foreground, GA_ROOT) != foreground || !IsWindowVisible(foreground) || IsIconic(foreground)) return nullptr;
    const DWORD shell = Process(GetShellWindow());
    if (!shell || Process(foreground) != shell || !Explorer(shell) || !Uncloaked(foreground)) return nullptr;
    return foreground;
}
int Recovery::Tick(HWND settledDesktop, bool suppressed) {
    if (suppressed || !settledDesktop || CurrentDesktop() != settledDesktop) {
        Reset(); return 0;
    }
    const DWORD shell = Process(GetShellWindow());
    if (!shell || AutoHide(shell)) { Reset(); return 0; }
    int verified = 0;
    const auto now = GetTickCount64();
    for (auto it = pending_.begin(); it != pending_.end();) {
        const bool current = it->desktop == settledDesktop && it->shell == shell && Tray(it->window, shell);
        const auto actual = current ? Read(it->window, settledDesktop, shell) : Candidate{};
        const bool complete = current && actual.visible && actual.opaque && !actual.iconic && !actual.cloaked &&
            actual.topmost && actual.exposedMonitorEdge && !actual.fullScreenAboveDesktop &&
            Above(it->window, settledDesktop);
        if (complete) {
            ++verified;
            diag::Log("taskbar-desktop", "verified-tray-above-desktop tick=" + std::to_string(GetTickCount64()));
        }
        if (!current || complete) it = pending_.erase(it);
        else {
            // Keep the identity in the bounded set for this desktop episode,
            // so an unresponsive Explorer never accumulates queued writes.
            if (it->at && now - it->at >= 1500) {
                diag::Log("taskbar-desktop", "tray-order-request-unconfirmed");
                it->at = 0;
            }
            ++it;
        }
    }
    struct Scan { HWND desktop; DWORD shell; std::vector<Request>& pending; } scan{settledDesktop, shell, pending_};
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto& s = *reinterpret_cast<Scan*>(value);
        if (!Tray(window, s.shell) || IsHungAppWindow(window) ||
            s.pending.size() >= taskbar::MaxTaskbars ||
            std::any_of(s.pending.begin(), s.pending.end(), [window](const Request& r) { return r.window == window; })) return TRUE;
        const auto candidate = Read(window, s.desktop, s.shell);
        if (!CanRepair(candidate, false, false)) return TRUE;
        // Explorer and input can change between enumeration and the write. A
        // stale foreground/monitor/fullscreen snapshot must never raise a tray.
        if (CurrentDesktop() != s.desktop || Process(GetShellWindow()) != s.shell || AutoHide(s.shell)) return FALSE;
        if (IsHungAppWindow(window)) return TRUE;
        if (!CanRepair(Read(window, s.desktop, s.shell), false, false)) return TRUE;
        // Explorer owns this HWND. Never synchronously wait for its window
        // thread from Lume's UI thread; acceptance is verified on a later tick.
        constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS;
        if (SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, flags)) {
            s.pending.push_back({window, s.desktop, s.shell, GetTickCount64()});
            diag::Log("taskbar-desktop", "requested-obscured-tray-z-order tick=" + std::to_string(GetTickCount64()));
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&scan));
    return verified;
}
}  // namespace lume::platform::windows::taskbar_desktop_recovery
