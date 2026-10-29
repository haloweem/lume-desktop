#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>

namespace lume::platform::windows::direct_write {
// Font size and bounds use the host's current coordinate system.
struct Style {
    float size = 13;
    int weight = 400;
    bool italic = false;
    const wchar_t* family = nullptr;
    UINT flags = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
};
bool Draw(HDC dc, const std::wstring& text, const RECT& bounds, COLORREF color, const Style& style);
bool Draw(Gdiplus::Graphics& graphics, const std::wstring& text, const Gdiplus::RectF& bounds,
          Gdiplus::Color color, const Style& style);
Gdiplus::RectF Measure(const std::wstring& text, float maxWidth, const Style& style);
}
