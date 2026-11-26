#include "platform/windows/calendar/calendar_renderer.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace lume::platform::windows;
using lume::calendar::Material;

namespace {
void Check(bool ok, const char* why) {
    if (!ok) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}

void CheckCalendarBackground() {
    CalendarRenderer renderer;
    lume::calendar::CalendarModel model({2026, 11, 15});
    lume::calendar::Preferences prefs;
    for (unsigned dpi : {96u, 120u, 144u, 192u}) {
        for (auto material : {Material::LiquidGlass, Material::FrostedGlass, Material::Solid}) {
            prefs.material = material;
            for (bool ready : {false, true}) {
                CalendarRenderState view;
                view.materialReady = ready;
                CalendarBitmap frame;
                Check(renderer.Render(prefs, model, dpi, frame, view), "calendar renders");
                const auto blank = static_cast<std::size_t>(frame.height / 2) * frame.width + 3 * dpi / 96;
                const auto alpha = frame.pixels[blank] >> 24;
                Check(alpha == (material == Material::Solid ? 255u : 1u), "calendar blank area has expected opacity");
                Check(frame.pixels.front() == 0, "rounded corner stays transparent");
                if (material != Material::Solid) {
                    Check(std::any_of(frame.pixels.begin(), frame.pixels.end(),
                                      [](auto pixel) { return (pixel >> 24) == 255; }),
                          "calendar content remains visible over the transparent background");
                }
            }
        }
    }
}
}

int main() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "initialize renderer apartment");
    CheckCalendarBackground();
    CoUninitialize();
    std::cout << "Calendar glass and solid backgrounds passed at four DPI settings\n";
}
