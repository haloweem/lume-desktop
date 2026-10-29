#pragma once

#include "platform/windows/material/widget_material_runtime.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <windows.h>
#include <algorithm>
#include <cstring>

namespace lume::platform::windows {

// Shared transparent-surface presenter for native overlays. It is deliberately
// independent of Dock state or layout so other desktop surfaces can reuse it.
class LayeredSurface final {
public:
    LayeredSurface() = default;
    LayeredSurface(const LayeredSurface&) = delete;
    LayeredSurface& operator=(const LayeredSurface&) = delete;
    ~LayeredSurface() { Release(); }
    bool Upload(const WidgetBitmap& bitmap) {
        if (!dc_ || width_ != bitmap.width || height_ != bitmap.height) {
            Release();
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(bitmap.width);
            info.bmiHeader.biHeight = -static_cast<LONG>(bitmap.height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            dc_ = CreateCompatibleDC(nullptr);
            dib_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels_, nullptr, 0);
            if (!dc_ || !dib_ || !pixels_) { Release(); return false; }
            original_ = SelectObject(dc_, dib_);
            width_ = bitmap.width;
            height_ = bitmap.height;
        }
        std::memcpy(pixels_, bitmap.pixels.data(), bitmap.pixels.size() * 4);
        return true;
    }
    bool Compose(HWND window, POINT position, SIZE size, POINT source = {}, BYTE opacity = 255) {
        if (!dc_) return false;
        size.cx = std::clamp(size.cx, 1L, static_cast<LONG>(width_) - source.x);
        size.cy = std::clamp(size.cy, 1L, static_cast<LONG>(height_) - source.y);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
        return UpdateLayeredWindow(window, nullptr, &position, &size, dc_, &source, 0,
                                   &blend, ULW_ALPHA) != FALSE;
    }
    bool Present(HWND window, const WidgetBitmap& bitmap, const POINT* position = nullptr,
                 BYTE opacity = 255) {
        if (!Upload(bitmap)) return false;
        SIZE size{static_cast<LONG>(width_), static_cast<LONG>(height_)};
        POINT source{};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
        POINT destination = position ? *position : POINT{};
        return UpdateLayeredWindow(window, nullptr, position ? &destination : nullptr, &size,
                                   dc_, &source, 0, &blend, ULW_ALPHA) != FALSE;
    }
    unsigned Width() const { return width_; }
    unsigned Height() const { return height_; }
    void Release() {
        if (dc_) {
            if (original_) SelectObject(dc_, original_);
            DeleteDC(dc_);
        }
        if (dib_) DeleteObject(dib_);
        dc_ = nullptr; dib_ = nullptr; original_ = nullptr; pixels_ = nullptr;
        width_ = height_ = 0;
    }
private:
    HDC dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ original_ = nullptr;
    void* pixels_ = nullptr;
    unsigned width_ = 0;
    unsigned height_ = 0;
};

inline void SubmitMaterial(WidgetMaterialRuntime& material,
                           const LumeMaterialParameters& parameters,
                           LumeMaterialParameters& last) {
    if (material.State() == WidgetMaterialState::Detached) {
        material.Attach(parameters);
        last = parameters;
        return;
    }
    if (std::memcmp(&parameters, &last, sizeof(parameters)) == 0) return;
    material.Update(parameters);
    last = parameters;
}

inline void MaterialContact(HWND window, float width, float height, unsigned dpi,
                            float& x, float& y) {
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(window, &cursor);
    const float px = static_cast<float>(cursor.x) * 96 / dpi;
    const float py = static_cast<float>(cursor.y) * 96 / dpi;
    if (px < 0 || py < 0 || px >= width || py >= height) {
        x = -.62f; y = -.85f; return;
    }
    x = px / (width > 1 ? width : 1) * 2 - 1;
    y = py / (height > 1 ? height : 1) * 2 - 1;
}

}  // namespace lume::platform::windows
