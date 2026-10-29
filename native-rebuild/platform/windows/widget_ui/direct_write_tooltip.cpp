#include "platform/windows/widget_ui/direct_write_tooltip.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <string>

namespace lume::platform::windows::direct_write_tooltip {
namespace {
constexpr UINT_PTR kSubclassId = 0x4c445754;
struct State { bool sizing = false; };

std::wstring Text(HWND window) {
    TTTOOLINFOW current{};
    current.cbSize = TTTOOLINFOW_V2_SIZE;
    if (!SendMessageW(window, TTM_GETCURRENTTOOLW, 0, reinterpret_cast<LPARAM>(&current))) return {};
    // Modern common controls honor wParam as the output capacity. Keep callback
    // and resource-string resolution in the native control, with bounded storage.
    std::array<wchar_t, 8192> buffer{};
    current.lpszText = buffer.data();
    SendMessageW(window, TTM_GETTEXTW, buffer.size(), reinterpret_cast<LPARAM>(&current));
    buffer.back() = L'\0';
    return buffer.data();
}

direct_write::Style Style(HWND window) {
    direct_write::Style style;
    const UINT dpi = GetDpiForWindow(window);
    style.size = 12.0f * static_cast<float>(dpi ? dpi : 96) / 96.0f;
    LOGFONTW font{};
    const auto handle = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    if (handle && GetObjectW(handle, sizeof(font), &font)) {
        if (font.lfHeight) style.size = static_cast<float>(std::abs(font.lfHeight));
        style.weight = font.lfWeight;
        style.italic = font.lfItalic != FALSE;
    }
    style.flags = DT_NOPREFIX;
    if (!(GetWindowLongPtrW(window, GWL_STYLE) & TTS_NOPREFIX)) style.flags &= ~DT_NOPREFIX;
    if (SendMessageW(window, TTM_GETMAXTIPWIDTH, 0, 0) > 0) style.flags |= DT_WORDBREAK;
    return style;
}

void Resize(HWND window, WINDOWPOS& position) {
    const auto text = Text(window);
    if (text.empty()) return;
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    RECT work{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) work = monitor.rcWork;
    const LONG available = std::max(1L, work.right - work.left - 32);
    const auto maximum = static_cast<LONG>(SendMessageW(window, TTM_GETMAXTIPWIDTH, 0, 0));
    const float width = static_cast<float>(maximum > 0 ? std::min(maximum, available) : available);
    const auto measured = direct_write::Measure(text, width, Style(window));
    if (measured.Width <= 0 || measured.Height <= 0) return;
    RECT bounds{0, 0, static_cast<LONG>(std::ceil(measured.Width)) + 1,
                       static_cast<LONG>(std::ceil(measured.Height)) + 1};
    SendMessageW(window, TTM_ADJUSTRECT, TRUE, reinterpret_cast<LPARAM>(&bounds));
    position.cx = bounds.right - bounds.left;
    position.cy = bounds.bottom - bounds.top;
    position.flags &= ~SWP_NOSIZE;
    if (!(position.flags & SWP_NOMOVE)) {
        position.x = std::clamp(position.x, static_cast<int>(work.left), std::max(static_cast<int>(work.left), static_cast<int>(work.right) - position.cx));
        position.y = std::clamp(position.y, static_cast<int>(work.top), std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - position.cy));
    }
}

void Paint(HWND window, HDC dc) {
    RECT client{}; GetClientRect(window, &client);
    HTHEME theme = OpenThemeData(window, L"TOOLTIP");
    bool painted = false;
    if (theme) {
        painted = SUCCEEDED(DrawThemeBackground(theme, dc, TTP_STANDARD, TTSS_NORMAL, &client, nullptr));
        CloseThemeData(theme);
    }
    if (!painted) {
        const auto background = static_cast<COLORREF>(SendMessageW(window, TTM_GETTIPBKCOLOR, 0, 0));
        const HBRUSH brush = CreateSolidBrush(background);
        FillRect(dc, &client, brush);
        DeleteObject(brush);
        FrameRect(dc, &client, GetSysColorBrush(COLOR_WINDOWFRAME));
    }
    RECT bounds = client;
    SendMessageW(window, TTM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&bounds));
    const auto color = static_cast<COLORREF>(SendMessageW(window, TTM_GETTIPTEXTCOLOR, 0, 0));
    direct_write::Draw(dc, Text(window), bounds, color, Style(window));
}

LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                           UINT_PTR id, DWORD_PTR reference) {
    auto* state = reinterpret_cast<State*>(reference);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, Procedure, id);
        delete state;
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (message == WM_WINDOWPOSCHANGING && lParam && !state->sizing) {
        const auto result = DefSubclassProc(window, message, wParam, lParam);
        auto& position = *reinterpret_cast<WINDOWPOS*>(lParam);
        if (!(position.flags & SWP_HIDEWINDOW)) {
            state->sizing = true;
            Resize(window, position);
            state->sizing = false;
        }
        return result;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (dc) Paint(window, dc);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT || message == WM_PRINT) {
        if (wParam) Paint(window, reinterpret_cast<HDC>(wParam));
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    return DefSubclassProc(window, message, wParam, lParam);
}
}

bool Attach(HWND tooltip) {
    if (!IsWindow(tooltip)) return false;
    DWORD_PTR reference = 0;
    if (GetWindowSubclass(tooltip, Procedure, kSubclassId, &reference)) return true;
    auto* state = new (std::nothrow) State;
    if (!state) return false;
    if (!SetWindowSubclass(tooltip, Procedure, kSubclassId, reinterpret_cast<DWORD_PTR>(state))) {
        delete state;
        return false;
    }
    return true;
}
}
