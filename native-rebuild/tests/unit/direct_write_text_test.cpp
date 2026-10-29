#include "platform/windows/widget_ui/direct_write_text.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace dw = lume::platform::windows::direct_write;
namespace {
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Surface {
    static constexpr int width = 320, height = 160;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    std::uint32_t* pixels = nullptr;
    Surface() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        Check(dc && bitmap && pixels, "create isolated test surface");
        previous = SelectObject(dc, bitmap);
        std::fill_n(pixels, width * height, 0u);
    }
    ~Surface() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
};
void HdcViewportAndClip() {
    Surface surface;
    SetViewportOrgEx(surface.dc, 30, 20, nullptr);
    IntersectClipRect(surface.dc, 0, 0, 85, 38);
    dw::Style style; style.size = 26;
    Check(dw::Draw(surface.dc, L"DirectWrite 中文", RECT{0, 0, 240, 38}, RGB(255,255,255), style), "HDC DirectWrite draw succeeds without caller COM initialization");
    int count = 0, partial = 0;
    for (int y = 0; y < Surface::height; ++y) for (int x = 0; x < Surface::width; ++x) {
        const auto pixel = surface.pixels[y * Surface::width + x];
        if (!pixel) continue;
        ++count;
        Check(x >= 30 && x < 115 && y >= 20 && y < 58, "HDC viewport and clipping respected");
        const auto b = pixel & 255, g = (pixel >> 8) & 255, r = (pixel >> 16) & 255;
        Check(r == g && g == b, "HDC white text has no colored fringes");
        partial += r > 0 && r < 255;
    }
    Check(count > 40 && partial > 10, "HDC text is visible and antialiased");
}
void AlphaAndTransform() {
    Gdiplus::Bitmap bitmap(320, 160, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&bitmap);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.ScaleTransform(2, 2);
        graphics.SetClip(Gdiplus::RectF(10, 10, 120, 40));
        dw::Style style; style.size = 18;
        Check(dw::Draw(graphics, L"Alpha 中文", Gdiplus::RectF(10, 10, 120, 40), Gdiplus::Color(160, 255, 255, 255), style), "transparent GDI+ DirectWrite draw succeeds");
    }
    Gdiplus::BitmapData data{};
    Gdiplus::Rect bounds(0, 0, 320, 160);
    Check(bitmap.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) == Gdiplus::Ok, "lock alpha pixels");
    int count = 0, partial = 0;
    for (int y = 0; y < 160; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const BYTE*>(data.Scan0) + y * data.Stride);
        for (int x = 0; x < 320; ++x) {
            const auto pixel = row[x];
            const auto a = pixel >> 24, r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
            Check(r == g && g == b && r <= a && a <= 160, "transparent pixels are grayscale premultiplied alpha");
            if (!a) continue;
            ++count; partial += a < 160;
            Check(x >= 20 && x < 260 && y >= 20 && y < 100, "GDI+ transform and clip respected");
        }
    }
    bitmap.UnlockBits(&data);
    Check(count > 100 && partial > 20, "scaled transparent text visible with antialiasing");
}
void Layout() {
    dw::Style style; style.size = 20;
    const auto one = dw::Measure(L"DirectWrite 中文", 1000, style);
    Check(one.Width > 50 && one.Height > 10, "DirectWrite returns actual text metrics");
    style.size = 40;
    const auto two = dw::Measure(L"DirectWrite 中文", 1000, style);
    Check(std::abs(two.Width / one.Width - 2) < .05f, "font size scales actual layout metrics");
    style.size = 20; style.flags = DT_WORDBREAK | DT_NOPREFIX;
    const auto multiline = dw::Measure(L"one two three four five six seven", 75, style);
    // Metrics include the trailing space at a wrapped line's end.
    Check(multiline.Width <= 80 && multiline.Height > one.Height * 2, "narrow layout wraps and measures multiple lines");
    style.family = L"Lume deliberately missing family"; style.weight = 700; style.italic = true;
    Check(dw::Measure(L"中文 fallback", 300, style).Width > 20, "missing family still gets DirectWrite font fallback");
    Check(dw::Measure(L"", 300, style).Width == 0, "empty text has zero measured width");
    style.flags = DT_SINGLELINE;
    Check(std::abs(dw::Measure(L"A&&B", 300, style).Width - dw::Measure(L"A&B", 300,
        dw::Style{20, 700, true, style.family, DT_SINGLELINE | DT_NOPREFIX}).Width) < .01f,
        "escaped ampersands measured consistently");
}
void FractionalBoundsDoNotResampleGlyphs() {
    for(const float scale:{1.f,1.25f,1.5f,2.f}){
        Surface first,second;
        dw::Style style;style.size=14;style.flags=DT_NOPREFIX;
        const auto draw=[&](Surface& destination,float extra){
            Gdiplus::Graphics graphics(destination.dc);
            graphics.ScaleTransform(scale,scale);
            Check(dw::Draw(graphics,L"清晰文字 ABC",{8,8,130+extra,32+extra},
                Gdiplus::Color(255,255,255,255),style),"draw fractional text bounds");
        };
        draw(first,0);draw(second,.3f);GdiFlush();
        int changed=0;
        for(int i=0;i<Surface::width*Surface::height;++i)
            changed+=(first.pixels[i]&0xffffff)!=(second.pixels[i]&0xffffff);
        Check(changed==0,"unused fractional layout space must not stretch or blur glyph pixels");
    }
}
void AlignmentTrimmingAndMultiline() {
    Surface left, right, trimmed, clipped, multiline;
    dw::Style style; style.size = 20; style.flags = DT_SINGLELINE | DT_NOPREFIX;
    const RECT bounds{10, 10, 110, 130};
    Check(dw::Draw(left.dc, L"Hi", bounds, RGB(255,255,255), style), "draw leading text");
    style.flags |= DT_RIGHT | DT_BOTTOM;
    Check(dw::Draw(right.dc, L"Hi", bounds, RGB(255,255,255), style), "draw trailing bottom text");
    auto start = [](const Surface& surface) {
        POINT point{Surface::width, Surface::height};
        for (int y = 0; y < Surface::height; ++y) for (int x = 0; x < Surface::width; ++x)
            if (surface.pixels[y * Surface::width + x] & 0xffffff) { point.x = std::min(point.x, static_cast<LONG>(x)); point.y = std::min(point.y, static_cast<LONG>(y)); }
        return point;
    };
    Check(start(right).x > start(left).x + 50 && start(right).y > start(left).y + 70, "horizontal and vertical alignment use layout bounds");
    style.flags = DT_SINGLELINE | DT_NOPREFIX;
    Check(dw::Draw(clipped.dc, L"ABCDEFGHIJKLMNOPQRSTUVWXYZ", bounds, RGB(255,255,255), style), "draw clipped overflow");
    style.flags |= DT_END_ELLIPSIS;
    Check(dw::Draw(trimmed.dc, L"ABCDEFGHIJKLMNOPQRSTUVWXYZ", bounds, RGB(255,255,255), style), "draw ellipsis overflow");
    Check(!std::equal(clipped.pixels, clipped.pixels + Surface::width * Surface::height, trimmed.pixels), "ellipsis replaces overflowing glyphs");
    style.flags = DT_WORDBREAK | DT_NOPREFIX;
    Check(dw::Draw(multiline.dc, L"one two three four five six", bounds, RGB(255,255,255), style), "draw wrapped text");
    bool belowFirstLine = false;
    for (int y = 0; y < Surface::height; ++y) for (int x = 0; x < Surface::width; ++x) {
        if (!(multiline.pixels[y * Surface::width + x] & 0xffffff)) continue;
        Check(x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom, "multiline text clips to requested rectangle");
        belowFirstLine |= y > 60;
    }
    Check(belowFirstLine, "wrapped text renders beyond first line");
    // A preceding bridge call must not leave the thread initialized as MTA.
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Check(SUCCEEDED(apartment), "bridge balances its own COM initialization");
    const bool staDraw = dw::Draw(left.dc, L"STA", bounds, RGB(255,255,255), style);
    CoUninitialize();
    Check(staDraw, "bridge also works in caller's STA apartment");
}
}
int main() {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return 1;
    int result = 0;
    try { HdcViewportAndClip(); AlphaAndTransform(); Layout(); FractionalBoundsDoNotResampleGlyphs(); AlignmentTrimmingAndMultiline(); std::cout << "DirectWrite bridge tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    Gdiplus::GdiplusShutdown(token);
    return result;
}
