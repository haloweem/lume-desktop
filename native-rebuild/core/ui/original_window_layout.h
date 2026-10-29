#pragma once

namespace lume::ui {

struct PageBounds {
    int x;
    int y;
    int width;
    int height;
};

// All coordinates and sizes are device-independent pixels (DIP).
PageBounds CalculateOriginalWindowBounds(PageBounds workArea) noexcept;
double CalculateOriginalZoom(int widthDip, int heightDip) noexcept;
// The manager window's first size. The historical 75% window leaves small work
// areas (laptops at 125-150%) with a page zoom near 0.6; there it grows to
// 1152x648 (zoom 0.75), or the largest 16:9 that fits.
PageBounds CalculateManagerWindowBounds(PageBounds workArea) noexcept;

// Restore a saved physical-pixel rectangle relative to the current work area.
PageBounds RestoreOriginalWindowBounds(PageBounds savedPixels, PageBounds savedWorkAreaPixels,
                                      PageBounds currentWorkAreaPixels, int savedDpi,
                                      int currentDpi) noexcept;

}  // namespace lume::ui
