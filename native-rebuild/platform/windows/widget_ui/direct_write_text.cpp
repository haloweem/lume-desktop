#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/widget_ui/ui_font.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace lume::platform::windows::direct_write {
namespace {
using Microsoft::WRL::ComPtr;
constexpr float kMaxLayout = 1000000.0f;
constexpr UINT kMaxDimension = 16384;
constexpr std::size_t kMaxPixels = 16 * 1024 * 1024;

struct Factories {
    ComPtr<ID2D1Factory> draw;
    ComPtr<IDWriteFactory> write;
    Factories() {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, draw.GetAddressOf());
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                           reinterpret_cast<IUnknown**>(write.GetAddressOf()));
    }
};
Factories& SharedFactories() {
    // Both factories are free-threaded. No layouts, font handles, bitmap sizes or
    // user strings accumulate here; each call owns its finite working set.
    static Factories factories;
    return factories;
}
struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool Ready() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};

std::wstring DisplayText(const std::wstring& text, UINT flags) {
    std::wstring result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        wchar_t character = text[i];
        if (!(flags & DT_NOPREFIX) && character == L'&') {
            if (i + 1 >= text.size() || text[i + 1] != L'&') continue;
            ++i;
        }
        if ((flags & DT_SINGLELINE) && (character == L'\r' || character == L'\n')) {
            if (character == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            character = L' ';
        }
        result.push_back(character);
    }
    return result;
}

ComPtr<IDWriteTextLayout> Layout(const std::wstring& text, float width, float height, const Style& style) {
    ComPtr<IDWriteTextLayout> layout;
    auto& factories = SharedFactories();
    if (!factories.write || !std::isfinite(style.size) || style.size <= 0 ||
        text.size() > std::numeric_limits<UINT32>::max()) return layout;
    ComPtr<IDWriteTextFormat> format;
    const wchar_t* family = style.family && *style.family ? style.family : ui_font::Family(text);
    if (FAILED(factories.write->CreateTextFormat(family, nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(std::clamp(style.weight, 1, 999)),
            style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, style.size, L"", format.GetAddressOf()))) return layout;
    format->SetTextAlignment((style.flags & DT_CENTER) ? DWRITE_TEXT_ALIGNMENT_CENTER :
                            (style.flags & DT_RIGHT) ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment((style.flags & DT_VCENTER) ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER :
                                 (style.flags & DT_BOTTOM) ? DWRITE_PARAGRAPH_ALIGNMENT_FAR : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetWordWrapping(!(style.flags & DT_SINGLELINE) && (style.flags & DT_WORDBREAK)
                            ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (style.flags & DT_RTLREADING) format->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
    if (style.flags & (DT_END_ELLIPSIS | DT_WORD_ELLIPSIS | DT_PATH_ELLIPSIS)) {
        DWRITE_TRIMMING trimming{};
        trimming.granularity = (style.flags & DT_WORD_ELLIPSIS) ? DWRITE_TRIMMING_GRANULARITY_WORD : DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        if (style.flags & DT_PATH_ELLIPSIS) { trimming.delimiter = L'\\'; trimming.delimiterCount = 1; }
        ComPtr<IDWriteInlineObject> sign;
        if (SUCCEEDED(factories.write->CreateEllipsisTrimmingSign(format.Get(), sign.GetAddressOf())))
            format->SetTrimming(&trimming, sign.Get());
    }
    const auto display = DisplayText(text, style.flags);
    factories.write->CreateTextLayout(display.data(), static_cast<UINT32>(display.size()), format.Get(),
                                      width, height, layout.GetAddressOf());
    return layout;
}

struct Raster {
    UINT width = 0, height = 0;
    std::vector<BYTE> pixels;
};
bool Render(const std::wstring& text, float width, float height, float scaleX, float scaleY,
            Gdiplus::Color color, const Style& style, Raster& raster) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(scaleX) || !std::isfinite(scaleY) ||
        width <= 0 || height <= 0 || scaleX <= 0 || scaleY <= 0) return false;
    const double pixelWidth = std::ceil(static_cast<double>(width) * scaleX);
    const double pixelHeight = std::ceil(static_cast<double>(height) * scaleY);
    if (pixelWidth > kMaxDimension || pixelHeight > kMaxDimension || pixelWidth * pixelHeight > kMaxPixels) return false;
    raster.width = static_cast<UINT>(pixelWidth);
    raster.height = static_cast<UINT>(pixelHeight);
    auto layout = Layout(text, width, height, style);
    auto& factories = SharedFactories();
    if (!layout || !factories.draw) return false;
    // WIC is apartment-bound. Keep all its objects inside the matching COM
    // scope, including when the caller has already initialized an STA.
    ComScope apartment;
    if (!apartment.Ready()) return false;
    ComPtr<IWICImagingFactory> imaging;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(imaging.GetAddressOf()))) ||
        FAILED(imaging->CreateBitmap(raster.width, raster.height, GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapCacheOnLoad, bitmap.GetAddressOf()))) return false;
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(factories.draw->CreateWicBitmapRenderTarget(bitmap.Get(), properties, target.GetAddressOf())) ||
        FAILED(target->CreateSolidColorBrush(D2D1::ColorF(color.GetR() / 255.0f, color.GetG() / 255.0f,
            color.GetB() / 255.0f, color.GetA() / 255.0f), brush.GetAddressOf()))) return false;
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->SetTransform(D2D1::Matrix3x2F::Scale(scaleX, scaleY));
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0, 0, 0, 0));
    target->DrawTextLayout(D2D1::Point2F(0, 0), layout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (FAILED(target->EndDraw())) return false;
    raster.pixels.resize(static_cast<std::size_t>(raster.width) * raster.height * 4);
    return SUCCEEDED(bitmap->CopyPixels(nullptr, raster.width * 4,
        static_cast<UINT>(raster.pixels.size()), raster.pixels.data()));
}
}

bool Draw(HDC dc, const std::wstring& text, const RECT& bounds, COLORREF color, const Style& style) {
    if (!dc) return false;
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (text.empty() || width <= 0 || height <= 0) return true;
    POINT points[]{{bounds.left, bounds.top}, {bounds.right, bounds.top}, {bounds.left, bounds.bottom}};
    if (!LPtoDP(dc, points, 3)) return false;
    const float scaleX = std::hypot(static_cast<float>(points[1].x - points[0].x), static_cast<float>(points[1].y - points[0].y)) / width;
    const float scaleY = std::hypot(static_cast<float>(points[2].x - points[0].x), static_cast<float>(points[2].y - points[0].y)) / height;
    Raster raster;
    if (!Render(text, static_cast<float>(width), static_cast<float>(height), scaleX, scaleY,
                Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)), style, raster)) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(raster.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(raster.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC source = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!source || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (source) DeleteDC(source);
        return false;
    }
    std::memcpy(pixels, raster.pixels.data(), raster.pixels.size());
    const auto previous = SelectObject(source, bitmap);
    const BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const bool result = AlphaBlend(dc, bounds.left, bounds.top, width, height, source, 0, 0,
                                  static_cast<int>(raster.width), static_cast<int>(raster.height), blend) != FALSE;
    SelectObject(source, previous);
    DeleteObject(bitmap);
    DeleteDC(source);
    return result;
}

bool Draw(Gdiplus::Graphics& graphics, const std::wstring& text, const Gdiplus::RectF& bounds,
          Gdiplus::Color color, const Style& style) {
    if (text.empty() || bounds.Width <= 0 || bounds.Height <= 0) return true;
    Gdiplus::PointF points[]{{0, 0}, {1, 0}, {0, 1}};
    if (graphics.TransformPoints(Gdiplus::CoordinateSpaceDevice, Gdiplus::CoordinateSpaceWorld, points, 3) != Gdiplus::Ok) return false;
    const float scaleX = std::hypot(points[1].X - points[0].X, points[1].Y - points[0].Y);
    const float scaleY = std::hypot(points[2].X - points[0].X, points[2].Y - points[0].Y);
    Raster raster;
    if (!Render(text, bounds.Width, bounds.Height, scaleX, scaleY, color, style, raster)) return false;
    Gdiplus::Bitmap image(static_cast<INT>(raster.width), static_cast<INT>(raster.height),
                          static_cast<INT>(raster.width * 4), PixelFormat32bppPARGB, raster.pixels.data());
    if (image.GetLastStatus() != Gdiplus::Ok) return false;
    // Axis-aligned UI text is already rasterized at device density. Fitting
    // ceil(width * scale) pixels back into fractional bounds resamples every
    // glyph and makes small text soft. Present that raster one-to-one instead.
    if(std::abs(points[1].Y-points[0].Y)<.001f&&std::abs(points[2].X-points[0].X)<.001f&&
        points[1].X>points[0].X&&points[2].Y>points[0].Y){
        Gdiplus::PointF origin(bounds.X,bounds.Y);
        if(graphics.TransformPoints(Gdiplus::CoordinateSpaceDevice,Gdiplus::CoordinateSpaceWorld,&origin,1)!=Gdiplus::Ok)return false;
        const auto saved=graphics.Save();
        graphics.ResetTransform();graphics.SetPageUnit(Gdiplus::UnitPixel);graphics.SetPageScale(1);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        const Gdiplus::Rect destination(static_cast<INT>(std::lround(origin.X)),static_cast<INT>(std::lround(origin.Y)),
            static_cast<INT>(raster.width),static_cast<INT>(raster.height));
        const auto result=graphics.DrawImage(&image,destination,0,0,static_cast<INT>(raster.width),
            static_cast<INT>(raster.height),Gdiplus::UnitPixel);
        graphics.Restore(saved);
        return result==Gdiplus::Ok;
    }
    // Rotated/skewed hosts still need their geometric transform.
    return graphics.DrawImage(&image, bounds, 0, 0, static_cast<Gdiplus::REAL>(raster.width),
                               static_cast<Gdiplus::REAL>(raster.height), Gdiplus::UnitPixel) == Gdiplus::Ok;
}

Gdiplus::RectF Measure(const std::wstring& text, float maxWidth, const Style& style) {
    if (text.empty()) return {};
    const float width = std::isfinite(maxWidth) && maxWidth > 0 ? std::min(maxWidth, kMaxLayout) : kMaxLayout;
    auto layout = Layout(text, width, kMaxLayout, style);
    DWRITE_TEXT_METRICS metrics{};
    if (!layout || FAILED(layout->GetMetrics(&metrics))) return {};
    return Gdiplus::RectF(0, 0, metrics.widthIncludingTrailingWhitespace, metrics.height);
}
}
