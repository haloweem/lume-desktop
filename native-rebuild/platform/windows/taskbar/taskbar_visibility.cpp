#include "platform/windows/taskbar/taskbar_visibility.h"

namespace lume::platform::windows::taskbar_visibility {
namespace {
bool SetStyle(HWND window, LONG_PTR style) {
    SetLastError(ERROR_SUCCESS);
    return SetWindowLongPtrW(window, GWL_EXSTYLE, style) != 0 || GetLastError() == ERROR_SUCCESS;
}
}
bool LooksHidden(HWND window) {
    if (!IsWindow(window)) return false;
    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & (WS_EX_LAYERED | WS_EX_TRANSPARENT)) != (WS_EX_LAYERED | WS_EX_TRANSPARENT)) return false;
    COLORREF key = 0; BYTE alpha = 255; DWORD flags = 0;
    return GetLayeredWindowAttributes(window, &key, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0;
}
bool Capture(HWND window, taskbar::Visibility& out) {
    if (!IsWindow(window)) return false;
    out.extendedStyle = static_cast<std::uint32_t>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    out.visible = IsWindowVisible(window) != FALSE;
    if (LooksHidden(window)) {
        // Already hidden (a record was lost): never journal that as the
        // original, or restoring would leave the taskbar invisible.
        out.extendedStyle &= ~static_cast<std::uint32_t>(WS_EX_LAYERED | WS_EX_TRANSPARENT);
        out.visible = true; out.colorKey = 0; out.alpha = 255; out.layeredFlags = 0;
        return true;
    }
    if (out.extendedStyle & WS_EX_LAYERED) {
        COLORREF key = 0; BYTE alpha = 255; DWORD flags = 0;
        if (!GetLayeredWindowAttributes(window, &key, &alpha, &flags)) return false;
        out.colorKey = key; out.alpha = alpha; out.layeredFlags = flags;
    }
    return true;
}
bool Hide(HWND window) {
    if (!IsWindow(window)) return false;
    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const auto hiddenStyle = style | WS_EX_LAYERED | WS_EX_TRANSPARENT;
    if (style != hiddenStyle && !SetStyle(window, hiddenStyle)) return false;
    COLORREF key = 0; BYTE alpha = 255; DWORD flags = 0;
    if ((!GetLayeredWindowAttributes(window, &key, &alpha, &flags) || alpha != 0 || flags != LWA_ALPHA) &&
        !SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA)) return false;
    // Opacity and click-through prevent edge activation from revealing or
    // intercepting the desktop even before Explorer handles this async request.
    return !IsWindowVisible(window) || ShowWindowAsync(window, SW_HIDE) != FALSE;
}
bool Restore(HWND window, const taskbar::Visibility& original) {
    if (!IsWindow(window)) return false;
    constexpr LONG_PTR mask = WS_EX_LAYERED | WS_EX_TRANSPARENT;
    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (!SetStyle(window, (style & ~mask) | (original.extendedStyle & mask))) return false;
    if ((original.extendedStyle & WS_EX_LAYERED) &&
        !SetLayeredWindowAttributes(window, original.colorKey, original.alpha, original.layeredFlags)) return false;
    if (!ShowWindowAsync(window, original.visible ? SW_SHOWNA : SW_HIDE)) return false;
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
    return true;
}
int RepairOrphans() {
    struct State { int repaired = 0; } state;
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        wchar_t name[64]{};
        if (!GetClassNameW(window, name, 64) ||
            (wcscmp(name, L"Shell_TrayWnd") && wcscmp(name, L"Shell_SecondaryTrayWnd")) || !LooksHidden(window)) return TRUE;
        taskbar::Visibility normal;
        normal.extendedStyle = static_cast<std::uint32_t>(GetWindowLongPtrW(window, GWL_EXSTYLE)) &
            ~static_cast<std::uint32_t>(WS_EX_LAYERED | WS_EX_TRANSPARENT);
        normal.visible = true;
        if (Restore(window, normal)) ++reinterpret_cast<State*>(data)->repaired;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));
    return state.repaired;
}
}
