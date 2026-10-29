#include "platform/windows/widget_ui/direct_write_tooltip.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
std::uint64_t Pixels(HWND tooltip) {
    RECT rect{}; GetClientRect(tooltip, &rect);
    Check(rect.right > 40 && rect.bottom > 25, "DirectWrite tooltip size includes multiline text");
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.right; info.bmiHeader.biHeight = -rect.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    std::uint32_t* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
    Check(bitmap != nullptr, "create tooltip pixel capture");
    const auto previous = SelectObject(dc, bitmap);
    std::fill_n(pixels, rect.right * rect.bottom, 0u);
    SendMessageW(tooltip, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    int visible = 0, smooth = 0, lower = 0;
    std::uint64_t fingerprint = 1469598103934665603ull;
    for (LONG y = 4; y < rect.bottom - 4; ++y) for (LONG x = 4; x < rect.right - 4; ++x) {
        const auto pixel = pixels[y * rect.right + x];
        const auto r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
        Check(r == g && g == b, "tooltip text uses grayscale DirectWrite without colored fringes");
        visible += r > 0; smooth += r > 0 && r < 255; lower += r > 0 && y > rect.bottom / 2;
        fingerprint = (fingerprint ^ pixel) * 1099511628211ull;
    }
    SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
    Check(visible > 30 && smooth > 10 && lower > 10, "tooltip has visible smooth text on multiple lines");
    return fingerprint;
}
}
int main() {
    HWND owner = nullptr, tooltip = nullptr;
    int result = 0;
    try {
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES}; InitCommonControlsEx(&common);
        owner = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 20, 20, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        tooltip = CreateWindowExW(WS_EX_NOACTIVATE, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            0, 0, 0, 0, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(owner && tooltip, "create native tooltip");
        SetWindowTheme(tooltip, L"", L"");
        SendMessageW(tooltip, TTM_SETTIPBKCOLOR, RGB(0,0,0), 0);
        SendMessageW(tooltip, TTM_SETTIPTEXTCOLOR, RGB(255,255,255), 0);
        SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 160);
        Check(lume::platform::windows::direct_write_tooltip::Attach(tooltip), "attach shared DirectWrite tooltip adapter");
        Check(lume::platform::windows::direct_write_tooltip::Attach(tooltip), "tooltip attach is idempotent");
        std::wstring text = L"DirectWrite 提示\n第二行文字";
        TTTOOLINFOW tool{}; tool.cbSize = TTTOOLINFOW_V2_SIZE; tool.hwnd = owner; tool.uId = 1;
        tool.uFlags = TTF_TRACK | TTF_ABSOLUTE; tool.lpszText = text.data();
        Check(SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool)) != FALSE, "native TTM_ADDTOOL unchanged");
        SendMessageW(tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(10, 10));
        SendMessageW(tooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
        const auto firstPixels = Pixels(tooltip);
        text = L"更新后的文字\nNative API works"; tool.lpszText = text.data();
        SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
        SendMessageW(tooltip, TTM_UPDATE, 0, 0);
        Check(Pixels(tooltip) != firstPixels, "TTM_UPDATETIPTEXT changes actual rendered glyph pixels");
        Check(SendMessageW(tooltip, TTM_GETTOOLCOUNT, 0, 0) == 1, "native tooltip tool lifecycle retained");
        SendMessageW(tooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
        std::cout << "DirectWrite tooltip tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (tooltip) DestroyWindow(tooltip);
    if (owner) DestroyWindow(owner);
    return result;
}
