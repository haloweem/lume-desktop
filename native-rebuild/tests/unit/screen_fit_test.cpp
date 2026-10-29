#include "core/ui/original_window_layout.h"
#include "core/window/screen_fit.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void Expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Common displays: physical size and Windows' recommended scaling.
struct Display { const char* name; int width, height, percent; };
constexpr Display Displays[] = {
    {"1366x768 100%", 1366, 768, 100},      {"1600x900 100%", 1600, 900, 100},
    {"1920x1080 100%", 1920, 1080, 100},    {"1920x1080 125%", 1920, 1080, 125},
    {"1920x1080 150%", 1920, 1080, 150},    {"1920x1200 125%", 1920, 1200, 125},
    {"2560x1440 100%", 2560, 1440, 100},    {"2560x1440 125%", 2560, 1440, 125},
    {"2560x1440 150%", 2560, 1440, 150},    {"2560x1600 150%", 2560, 1600, 150},
    {"2560x1600 200%", 2560, 1600, 200},    {"2880x1800 200%", 2880, 1800, 200},
    {"3000x2000 200%", 3000, 2000, 200},    {"3840x2160 150%", 3840, 2160, 150},
    {"3840x2160 200%", 3840, 2160, 200},
};
constexpr int TaskbarDip = 48;

void TestFitScale() {
    Expect(lume::window::FitScale(2048, 1152) == 1.0, "the reference display keeps the tuned defaults");
    Expect(lume::window::FitScale(2560, 1440) == 1.0, "larger displays never enlarge defaults");
    Expect(std::abs(lume::window::FitScale(1536, 864) - 0.75) < 1e-9, "1920x1080 at 125% shrinks to 75%");
    Expect(lume::window::FitScale(1280, 720) == lume::window::FitMinimum, "small displays stop at the floor");
    Expect(lume::window::FitScale(0, 720) == 1.0 && lume::window::FitScale(1280, -1) == 1.0,
           "invalid monitor sizes leave defaults unchanged");
    Expect(lume::window::FitLength(820, 0.7, 320) == 574 && lume::window::FitLength(420, 0.7, 320) == 320,
           "fitted lengths respect the minimum");
}

void TestDisplays() {
    for (const auto& d : Displays) {
        const int w = d.width * 100 / d.percent, h = d.height * 100 / d.percent;
        const double fit = lume::window::FitScale(w, h);
        // Dock at the default size: the glass bar stays a modest share of the height.
        const double bar = 100 * fit;
        Expect(bar / h <= 0.105, d.name);
        // Calendar default (820x500) after the fit keeps clear of the work area edges.
        const int calendarW = lume::window::FitLength(820, fit, 320), calendarH = lume::window::FitLength(500, fit, 320);
        Expect(calendarW <= w - 64 && calendarH <= h - TaskbarDip - 64, d.name);
        // Manager window: inside the work area, and the page zoom stays readable.
        const lume::ui::PageBounds work{0, 0, w, h - TaskbarDip};
        const auto manager = lume::ui::CalculateManagerWindowBounds(work);
        Expect(manager.x >= 0 && manager.y >= 0 && manager.x + manager.width <= work.width &&
                   manager.y + manager.height <= work.height, d.name);
        Expect(lume::ui::CalculateOriginalZoom(manager.width, manager.height) >= 0.7, d.name);
        // Six standard widgets opened together never overlap and stay on screen.
        std::vector<lume::window::PhysicalRect> placed;
        const double scale = d.percent / 100.0;
        const int ww = static_cast<int>(320 * scale), wh = static_cast<int>(152 * scale);
        const lume::window::PhysicalRect physicalWork{0, 0, d.width, static_cast<int>((h - TaskbarDip) * scale)};
        for (int i = 0; i < 6; ++i) {
            const auto r = lume::window::FreeSlot(physicalWork, ww, wh, static_cast<int>(32 * scale),
                                                  static_cast<int>(16 * scale), placed);
            Expect(r.x >= 0 && r.y >= 0 && r.x + r.width <= physicalWork.width && r.y + r.height <= physicalWork.height,
                   d.name);
            for (const auto& other : placed)
                Expect(r.x >= other.x + other.width || other.x >= r.x + r.width || r.y >= other.y + other.height ||
                           other.y >= r.y + r.height,
                       d.name);
            placed.push_back(r);
        }
    }
}

void TestReferenceUnchanged() {
    // 2560x1440 at 125%: the manager keeps the historical layout.
    const lume::ui::PageBounds work{0, 0, 2048, 1104};
    const auto a = lume::ui::CalculateManagerWindowBounds(work), b = lume::ui::CalculateOriginalWindowBounds(work);
    Expect(a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height, "reference manager layout unchanged");
    // 1920x1080 at 150%: grows to the largest 16:9 that fits.
    const auto laptop = lume::ui::CalculateManagerWindowBounds({0, 0, 1280, 672});
    Expect(laptop.width == 1138 && laptop.height == 640 && laptop.y == 24, "laptop manager window grows to fit");
}

void TestFreeSlotAndFitInside() {
    const lume::window::PhysicalRect work{0, 0, 1000, 600};
    const auto first = lume::window::FreeSlot(work, 300, 150, 30, 10, {});
    Expect(first.x == 670 && first.y == 30, "first widget goes to the top-right");
    const auto second = lume::window::FreeSlot(work, 300, 150, 30, 10, {first});
    Expect(second.x == 670 && second.y == 190, "second widget goes below the first");
    const auto big = lume::window::FitInside({900, 500, 1200, 720}, work, 320, 152);
    Expect(big.x == 0 && big.y == 0 && big.width == 1000 && big.height == 600, "oversized windows shrink to the work area");
    const auto tiny = lume::window::FitInside({0, 0, 400, 400}, {0, 0, 200, 200}, 320, 152);
    Expect(tiny.width == 320 && tiny.height == 200, "never smaller than the minimum");
}
}  // namespace

int main() {
    TestFitScale();
    TestDisplays();
    TestReferenceUnchanged();
    TestFreeSlotAndFitInside();
    std::cout << "screen fit tests passed\n";
    return 0;
}
