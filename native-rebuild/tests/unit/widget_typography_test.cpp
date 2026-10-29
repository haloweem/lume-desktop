#include "platform/windows/widget_ui/ui_font.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <dwrite.h>
#include <wrl/client.h>
#include <cmath>
#include <iostream>
using namespace lume::platform::windows;
using Microsoft::WRL::ComPtr;
namespace {
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
}
int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return 2;
    {
        Check(!ui_font::IsChinese(L"CPU 100% 12:30") && ui_font::IsChinese(L"日程 Lume 2026"), "text family classification");
        const auto fallback = ui_font::Resolve(L"Lume deliberately missing font 96342");
        Check(ui_font::Available(fallback.c_str()), "missing family resolves to installed font");
        const auto font = ui_font::GdiPlus(L"Lume deliberately missing font 96342", 14.f, Gdiplus::FontStyleBold);
        Check(font && font->GetLastStatus() == Gdiplus::Ok && font->GetSize() == 14.f && font->GetStyle() == Gdiplus::FontStyleBold,
              "missing GDI+ font retains size and bold through fallback");
        ComPtr<IDWriteFactory> factory;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) return 2;
        WidgetCanvas canvas;
        for (const unsigned dpi : {96u, 144u, 192u}) {
            Check(canvas.Begin(600, 160, dpi), "canvas begins");
            float y = 0;
            for (const auto* text : {L"Settings CPU 12345 100%", L"日历普通阅读文字", L"日程 Lume 2026年10月"}) {
                for (const auto weight : {WidgetTextWeight::Normal, WidgetTextWeight::Semibold}) {
                    ComPtr<IDWriteTextFormat> format;
                    ComPtr<IDWriteTextLayout> layout;
                    DWRITE_TEXT_METRICS metrics{};
                    Check(SUCCEEDED(factory->CreateTextFormat(ui_font::Family(text), nullptr,
                        static_cast<DWRITE_FONT_WEIGHT>(weight), DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL, 14.f, L"zh-CN", &format)), "reference font created");
                    if (!format) return 2;
                    Check(SUCCEEDED(factory->CreateTextLayout(text, static_cast<UINT32>(wcslen(text)),
                        format.Get(), 100000, 10000, &layout)) && SUCCEEDED(layout->GetMetrics(&metrics)), "reference text measured");
                    Check(std::abs(canvas.MeasureText(text, 14.f, weight) - metrics.widthIncludingTrailingWhitespace) < .01f,
                          "component measures with shared family and requested weight");
                    canvas.Text(text, {0, y, 590, 24}, 14.f, 0xffffff, 1.f, weight);
                    y += 24;
                }
            }
            WidgetBitmap bitmap;
            Check(canvas.Finish(bitmap), "canvas finishes");
            for (unsigned row = 0; row < 6; ++row) {
                unsigned ink = 0;
                for (unsigned yPixel = row * 24 * dpi / 96; yPixel < (row + 1) * 24 * dpi / 96; ++yPixel)
                    for (unsigned x = 0; x < bitmap.width; ++x)
                        if ((bitmap.pixels[yPixel * bitmap.width + x] >> 24) != 0) ++ink;
                Check(ink > 20, "Chinese/Latin/mixed text produces visible pixels");
            }
        }
    }
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    if (failures) return 1;
    std::cout << "PASS shared component families, regular/bold metrics, fallback and DPI pixels\n";
    return 0;
}
