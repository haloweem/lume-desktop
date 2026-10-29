#pragma once

#include "core/window/window_types.h"

#include <vector>

namespace lume::window {

// Lume's default sizes were tuned on a 2048x1152 DIP display (2560x1440 at
// 125%). Smaller displays, most laptops, shrink those defaults by FitScale;
// larger displays keep them. The floor keeps text and icons legible.
inline constexpr double FitReferenceWidth = 2048, FitReferenceHeight = 1152, FitMinimum = 0.7;
// Monitor size in DIP (not the work area, so the taskbar does not change it).
double FitScale(double monitorWidthDip, double monitorHeightDip) noexcept;
// A default size shrunk by `scale`, never below the minimum.
int FitLength(int length, double scale, int minimum) noexcept;

// Shrinks a size to the work area, keeping it no smaller than the minimum.
PhysicalRect FitInside(PhysicalRect bounds, const PhysicalRect& work, int minWidth, int minHeight) noexcept;

// First place for a new desktop widget of the given size: the top-right of
// the work area, then below or left of the occupied rectangles, so widgets
// opened for the first time do not land on top of each other. Falls back to
// a cascade from the top-right when the work area is full.
PhysicalRect FreeSlot(const PhysicalRect& work, int width, int height, int margin, int gap,
                      const std::vector<PhysicalRect>& occupied);

}  // namespace lume::window
