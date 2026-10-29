#include "platform/windows/widget_ui/icon_art.h"
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using lume::platform::windows::icon_art::Draw;
using lume::platform::windows::icon_art::Icon;
using lume::platform::windows::icon_art::IconCount;

namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

struct Rendered {
    int side;
    std::vector<Gdiplus::Color> pixels;
    int covered = 0;
    int strongest = 0;
};

Rendered Render(Icon icon, float dpi, Gdiplus::Color tint) {
    const int side = static_cast<int>(std::ceil(40 * dpi));
    Gdiplus::Bitmap bitmap(side, side, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.ScaleTransform(dpi, dpi);
    Draw(graphics, icon, 8, 8, 24, tint);
    Rendered result{side, std::vector<Gdiplus::Color>(static_cast<std::size_t>(side) * side)};
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
        auto& pixel = result.pixels[static_cast<std::size_t>(y) * side + x];
        Check(bitmap.GetPixel(x, y, &pixel) == Gdiplus::Ok, "read rendered icon pixel");
        if (!pixel.GetA()) continue;
        ++result.covered;
        result.strongest = std::max(result.strongest, static_cast<int>(pixel.GetA()));
        Check(x >= static_cast<int>(std::floor(9 * dpi)) && x < static_cast<int>(std::ceil(31 * dpi)) &&
              y >= static_cast<int>(std::floor(9 * dpi)) && y < static_cast<int>(std::ceil(31 * dpi)),
              "icon ink stays within a one-DIP inset");
    }
    return result;
}
}

int main() {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    Check(Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok, "start GDI+");
    for (int i = 0; i < IconCount; ++i) {
        const auto icon = static_cast<Icon>(i);
        int previous = 0;
        for (float dpi : {1.f, 1.25f, 1.5f, 2.f}) {
            const auto normal = Render(icon, dpi, Gdiplus::Color(255, 0, 0, 0));
            if (!(normal.covered > 18 * dpi * dpi && normal.strongest >= 100))
                std::cerr << "icon=" << i << " dpi=" << dpi << " covered=" << normal.covered << " strongest=" << normal.strongest << '\n';
            Check(normal.covered > 18 * dpi * dpi && normal.strongest >= 100,
                  "every icon has visible antialiased geometry at each DPI");
            if (previous) Check(normal.covered > previous,
                                "higher DPI increases the rendered icon footprint");
            previous = normal.covered;
            const auto selected = Render(icon, dpi, Gdiplus::Color(255, 0, 0, 0));
            const auto disabled = Render(icon, dpi, Gdiplus::Color(105, 0, 0, 0));
            Check(selected.covered == normal.covered && disabled.covered > normal.covered / 2,
                  "selected and disabled black ink remain visible");
            bool sawVisibleInk = false;
            for (std::size_t p = 0; p < normal.pixels.size(); ++p) {
                const auto a = normal.pixels[p].GetA();
                Check(a == selected.pixels[p].GetA() && disabled.pixels[p].GetA() <= a,
                      "disabled ink reduces alpha without increasing coverage");
                if (a < 120) continue;
                sawVisibleInk = true;
                Check(normal.pixels[p].GetR() == 0 && normal.pixels[p].GetG() == 0 && normal.pixels[p].GetB() == 0 &&
                      selected.pixels[p].GetR() == 0 && disabled.pixels[p].GetR() == 0,
                      "selected and disabled icon ink remains black");
                Check(disabled.pixels[p].GetA() > 0 && disabled.pixels[p].GetA() < a,
                      "disabled icon uses translucent black ink");
            }
            Check(sawVisibleInk, "every thin-line icon retains visible black ink");
        }
    }
    Gdiplus::GdiplusShutdown(token);
    std::cout << "PASS custom toolbar and menu icon DPI, inset, alpha and tint\n";
}
