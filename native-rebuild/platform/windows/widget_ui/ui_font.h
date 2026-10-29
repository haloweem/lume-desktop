#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
#include <string>
#include <string_view>

namespace lume::platform::windows::ui_font {
// One policy for native component labels and their measuring/editing paths.
// A mixed Chinese label uses YaHei UI as a whole to preserve shaping/spacing.
inline bool IsChinese(std::wstring_view text) {
    for (const wchar_t c : text) {
        if ((c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) ||
            (c >= 0xfe30 && c <= 0xfe4f) || (c >= 0xff00 && c <= 0xffef) ||
            (c >= 0xd840 && c <= 0xd87f)) return true;
    }
    return false;
}
inline bool Available(const wchar_t* family) {
    LOGFONTW request{};
    request.lfCharSet = DEFAULT_CHARSET;
    if (wcsncpy_s(request.lfFaceName, family, _TRUNCATE) != 0) return false;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    bool found = false;
    EnumFontFamiliesExW(dc, &request,
        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM data) -> int {
            *reinterpret_cast<bool*>(data) = true;
            return 0;
        }, reinterpret_cast<LPARAM>(&found), 0);
    DeleteDC(dc);
    return found;
}
inline std::wstring Resolve(const wchar_t* preferred) {
    for (const auto family : {preferred, L"Segoe UI", L"Microsoft YaHei UI", L"Tahoma"})
        if (family && *family && Available(family)) return family;
    LOGFONTW stock{};
    if (GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(stock), &stock) && stock.lfFaceName[0])
        return stock.lfFaceName;
    return L"Arial";
}
inline const wchar_t* Family(std::wstring_view text = {}) {
    // Cache names only, never font handles or graphics resources.
    static const auto latin = Resolve(L"Segoe UI");
    static const auto chinese = Resolve(L"Microsoft YaHei UI");
    return (IsChinese(text) ? chinese : latin).c_str();
}
inline std::unique_ptr<Gdiplus::Font> GdiPlus(const wchar_t* preferred, float size,
                                            int style = Gdiplus::FontStyleRegular) {
    for (const auto name : {preferred, Family(), Family(L"中文")}) {
        Gdiplus::FontFamily family(name);
        if (family.GetLastStatus() != Gdiplus::Ok || !family.IsStyleAvailable(style)) continue;
        auto font = std::make_unique<Gdiplus::Font>(&family, size, style, Gdiplus::UnitPixel);
        if (font->GetLastStatus() == Gdiplus::Ok) return font;
    }
    auto font = std::make_unique<Gdiplus::Font>(Gdiplus::FontFamily::GenericSansSerif(), size,
                                              style, Gdiplus::UnitPixel);
    return font->GetLastStatus() == Gdiplus::Ok ? std::move(font) : nullptr;
}
}
