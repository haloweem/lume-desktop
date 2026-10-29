#pragma once
#include <windows.h>
#include "../lume_window_ids.h"
#include <commctrl.h>

namespace lume {

inline bool IsDesktopHost(HWND window) {
    wchar_t name[64]{};
    GetClassNameW(window, name, ARRAYSIZE(name));
    if (wcscmp(name, L"Progman") != 0 && wcscmp(name, L"WorkerW") != 0) return false;
    DWORD shellProcess = 0, windowProcess = 0;
    GetWindowThreadProcessId(GetShellWindow(), &shellProcess);
    GetWindowThreadProcessId(window, &windowProcess);
    return shellProcess && shellProcess == windowProcess;
}

// Keep widgets above Explorer's desktop, but below ordinary application
// windows. HWND_BOTTOM alone can put them behind the wallpaper host.
inline HWND DesktopInsertAfter(HWND widget) {
    HWND lastApplication = HWND_TOP;
    for (HWND window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT)) {
        if (window == widget || !IsWindowVisible(window)) continue;
        if (IsDesktopHost(window)) break;
        if (GetPropW(window, lume::window_ids::DesktopWidgetProp) || GetPropW(window, lume::window_ids::GlassOwnerProp)) continue;
        wchar_t name[64]{};
        GetClassNameW(window, name, ARRAYSIZE(name));
        if (wcscmp(name, L"LumeNativeGlassSurfaceV3") == 0) continue;
        if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST)) lastApplication = window;
    }
    return lastApplication;
}

inline LRESULT CALLBACK DesktopWindowProc(HWND window, UINT message, WPARAM wParam,
                                          LPARAM lParam, UINT_PTR id, DWORD_PTR) {
    if (message == WM_NCDESTROY) {
        RemovePropW(window, lume::window_ids::DesktopWidgetProp);
        RemoveWindowSubclass(window, DesktopWindowProc, id);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_WINDOWPOSCHANGING) {
        auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
        if (!(position->flags & SWP_NOZORDER)) {
            position->hwndInsertAfter = DesktopInsertAfter(window);
            position->flags |= SWP_NOACTIVATE;
        }
    }
    return result;
}

inline bool ConfigureDesktopWindow(HWND window) {
    if (GetPropW(window, lume::window_ids::DesktopWidgetProp)) return true;
    if (!SetWindowSubclass(window, DesktopWindowProc, 1, 0)) return false;
    if (!SetPropW(window, lume::window_ids::DesktopWidgetProp, reinterpret_cast<HANDLE>(1))) {
        RemoveWindowSubclass(window, DesktopWindowProc, 1);
        return false;
    }
    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    SetWindowLongPtrW(window, GWL_EXSTYLE, (style | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
    SetWindowPos(window, DesktopInsertAfter(window), 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return true;
}

} // namespace lume
