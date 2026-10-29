#include "platform/windows/widget_ui/icon_art.h"
#include "platform/windows/widget_ui/icon_art_data.h"
#include <array>
#include <cstdint>

namespace lume::platform::windows::icon_art {
void Draw(Gdiplus::Graphics& graphics, Icon icon, float x, float y, float size,
          Gdiplus::Color ink) {
    const int index = static_cast<int>(icon);
    if (index < 0 || index >= IconCount || size <= 0 || !ink.GetA()) return;
    constexpr int side = generated::MaskSide;
    std::array<std::uint32_t, side * side> pixels{};
    const auto range = generated::Ranges[index];
    std::size_t at = 0;
    for (std::uint32_t i = range.offset; i < range.offset + range.length; i += 2) {
        const auto count = generated::Runs[i];
        const auto coverage = generated::Runs[i + 1];
        const auto alpha = static_cast<std::uint32_t>((coverage * ink.GetA() + 127) / 255);
        const auto red = ink.GetR() * alpha / 255;
        const auto green = ink.GetG() * alpha / 255;
        const auto blue = ink.GetB() * alpha / 255;
        const std::uint32_t pixel = alpha << 24 | red << 16 | green << 8 | blue;
        for (std::uint16_t p = 0; p < count; ++p) pixels[at++] = pixel;
    }
    Gdiplus::Bitmap bitmap(side, side, side * 4, PixelFormat32bppPARGB,
                           reinterpret_cast<BYTE*>(pixels.data()));
    const auto state = graphics.Save();
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.DrawImage(&bitmap, Gdiplus::RectF(x, y, size, size), 0, 0, side, side,
                       Gdiplus::UnitPixel);
    graphics.Restore(state);
}
}
