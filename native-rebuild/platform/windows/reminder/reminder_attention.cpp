#include "platform/windows/reminder/reminder_attention.h"
#include <shellapi.h>
#include <cwchar>

namespace lume::platform::windows {
ReminderAttention QueryReminderAttention() {
    ReminderAttention result;
    QUERY_USER_NOTIFICATION_STATE state{};
    result.notificationsAccepted = SUCCEEDED(SHQueryUserNotificationState(&state)) &&
                                   state == QUNS_ACCEPTS_NOTIFICATIONS;
    const HWND foreground = GetForegroundWindow();
    if (!foreground || !IsWindowVisible(foreground) || IsIconic(foreground)) return result;
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    if (!pid) return result;
    RECT bounds{};
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (!GetWindowRect(foreground, &bounds) ||
        !GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST), &monitor)) return result;
    result.foregroundKnown = true;
    DWORD shellPid = 0;
    const HWND shell = GetShellWindow();
    if (shell) GetWindowThreadProcessId(shell, &shellPid);
    wchar_t name[64]{};
    GetClassNameW(foreground, name, 64);
    const bool desktop = shellPid && pid == shellPid &&
        (std::wcscmp(name, L"Progman") == 0 || std::wcscmp(name, L"WorkerW") == 0);
    result.desktopOrLume = desktop || pid == GetCurrentProcessId();
    // The desktop itself covers a monitor; it is not a fullscreen application.
    result.fullscreen = !desktop && bounds.left <= monitor.rcMonitor.left && bounds.top <= monitor.rcMonitor.top &&
        bounds.right >= monitor.rcMonitor.right && bounds.bottom >= monitor.rcMonitor.bottom;
    return result;
}
}
