#include "core/ui/original_window_layout.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void Expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void TestBoundsMatchOriginalJavaScript() {
    using lume::ui::PageBounds;
    struct Case { PageBounds workArea; PageBounds expected; };
    // Golden outputs from history/lume1.0.1/src/lume-electron-shell/host/windowState.js,
    // calculateMainWindowBounds; DIP inputs preserve display-origin offsets.
    const Case cases[] = {
        {{0, 0, 1920, 1040}, {266, 130, 1387, 780}},
        {{0, 0, 1536, 824}, {218, 103, 1099, 618}},
        {{-1920, -200, 1920, 1040}, {-1654, -70, 1387, 780}},
        {{0, 0, 800, 600}, {100, 131, 600, 338}},
        {{0, 0, 640, 360}, {80, 45, 480, 270}},
        {{0, 0, 1280, 720}, {160, 90, 960, 540}},
        {{10, 20, 1000, 600}, {30, 50, 960, 540}},
        {{0, 0, 1024, 768}, {32, 114, 960, 540}},
        {{0, 0, 341, 199}, {42, 27, 256, 144}},
        {{0, 0, 100, 31}, {29, 24, 41, 23}},
        {{0, 0, 1282, 722}, {160, 90, 962, 541}},
        {{0, 0, 2000, 400}, {733, 50, 533, 300}},
        {{0, 0, 900, 2000}, {112, 810, 675, 380}},
    };
    for (const auto& item : cases) {
        const auto actual = lume::ui::CalculateOriginalWindowBounds(item.workArea);
        Expect(actual.x == item.expected.x && actual.y == item.expected.y &&
                   actual.width == item.expected.width && actual.height == item.expected.height,
               "window bounds match historical JavaScript golden output");
    }
}

void TestZoomLimitsAndInvalidDimensions() {
    struct Case { int width; int height; double expected; };
    const Case cases[] = {
        {1536, 864, 1.0}, {768, 432, 0.5}, {3072, 1728, 2.0},
        {100, 100, 0.5}, {6144, 3456, 2.0}, {1536, 432, 0.5},
        {768, 1728, 0.5}, {1152, 648, 0.75}, {1920, 1040, 1040.0 / 864},
        {1387, 780, 780.0 / 864}, {1099, 618, 618.0 / 864},
        {0, 864, 1.0}, {1536, 0, 1.0}, {-1, 864, 1.0},
        {1536, -1, 1.0}, {std::numeric_limits<int>::min(), 864, 1.0},
        {std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 2.0},
    };
    for (const auto& item : cases) {
        Expect(std::abs(lume::ui::CalculateOriginalZoom(item.width, item.height) - item.expected) < 1e-12,
               "zoom uses smaller DIP ratio, clamp, and invalid-input fallback");
    }
}

void TestRestoreAcrossDpiAndWorkAreaChanges() {
    using lume::ui::PageBounds;
    struct Case {
        PageBounds saved;
        PageBounds oldArea;
        PageBounds currentArea;
        int oldDpi;
        int currentDpi;
        PageBounds expected;
    };
    const Case cases[] = {
        {{100, 100, 960, 540}, {0, 0, 1920, 1040}, {0, 0, 1920, 1040}, 96, 96,
         {100, 100, 960, 540}},
        {{100, 100, 960, 540}, {0, 0, 1920, 1040}, {0, 0, 1920, 1040}, 96, 144,
         {150, 150, 1440, 810}},
        {{150, 150, 1440, 810}, {0, 0, 1920, 1040}, {0, 0, 1920, 1040}, 144, 96,
         {100, 100, 960, 540}},
        {{-1820, -980, 960, 540}, {-1920, -1080, 1920, 1040}, {0, 0, 1920, 1040}, 96, 144,
         {150, 150, 1440, 810}},
        {{100, 100, 960, 540}, {0, 0, 1920, 1040}, {-1920, -1080, 1920, 1040}, 96, 144,
         {-1770, -930, 1440, 810}},
        {{100, 100, 1440, 810}, {0, 0, 1920, 1040}, {0, 0, 800, 600}, 96, 96,
         {0, 100, 800, 450}},
        {{100, 100, 1440, 810}, {0, 0, 1920, 1040}, {0, 0, 1000, 400}, 96, 96,
         {100, 0, 711, 400}},
        {{1500, 800, 960, 540}, {0, 0, 2560, 1440}, {-1280, 0, 1280, 720}, 96, 96,
         {-960, 180, 960, 540}},
        {{100, 100, 960, 540}, {0, 0, 1920, 1040}, {0, 0, 1920, 1040}, 0, 144,
         {150, 150, 1440, 810}},
        {{150, 150, 1440, 810}, {0, 0, 1920, 1040}, {0, 0, 1920, 1040}, 144, -1,
         {100, 100, 960, 540}},
    };
    for (const auto& item : cases) {
        const auto actual = lume::ui::RestoreOriginalWindowBounds(
            item.saved, item.oldArea, item.currentArea, item.oldDpi, item.currentDpi);
        Expect(actual.x == item.expected.x && actual.y == item.expected.y &&
                   actual.width == item.expected.width && actual.height == item.expected.height,
               "restored bounds follow DPI, monitor origin, and current work-area limits");
        Expect(actual.x >= item.currentArea.x && actual.y >= item.currentArea.y &&
                   actual.x + actual.width <= item.currentArea.x + item.currentArea.width &&
                   actual.y + actual.height <= item.currentArea.y + item.currentArea.height,
               "restored rectangle remains within the current work area");
    }
}
}  // namespace

int main() {
    TestBoundsMatchOriginalJavaScript();
    TestZoomLimitsAndInvalidDimensions();
    TestRestoreAcrossDpiAndWorkAreaChanges();
    std::cout << "original_window_layout_test: passed\n";
    return 0;
}
