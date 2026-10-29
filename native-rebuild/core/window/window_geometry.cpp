#include "core/window/window_geometry.h"

#include <algorithm>
#include <cmath>

namespace lume::window {
namespace {

unsigned int NormalizeDpi(unsigned int dpi) noexcept {
    return dpi == 0 ? 96U : dpi;
}

int Scale(int pixels, unsigned int dpi) {
    const double scaled = static_cast<double>(pixels) * static_cast<double>(NormalizeDpi(dpi)) / 96.0;
    return static_cast<int>(std::lround(scaled));
}

}  // namespace

int ScaleLogicalToPhysical(int logicalPixels, unsigned int dpi) {
    return Scale(logicalPixels, dpi);
}

int ScalePhysicalToLogical(int physicalPixels, unsigned int dpi) {
    const double scaled = static_cast<double>(physicalPixels) * 96.0 / static_cast<double>(NormalizeDpi(dpi));
    return static_cast<int>(std::lround(scaled));
}

PhysicalRect ScaleLogicalToPhysical(const LogicalRect& logical, unsigned int dpi) {
    return {
        ScaleLogicalToPhysical(logical.x, dpi),
        ScaleLogicalToPhysical(logical.y, dpi),
        ScaleLogicalToPhysical(logical.width, dpi),
        ScaleLogicalToPhysical(logical.height, dpi),
    };
}

LogicalRect ScalePhysicalToLogical(const PhysicalRect& physical, unsigned int dpi) {
    return {
        ScalePhysicalToLogical(physical.x, dpi),
        ScalePhysicalToLogical(physical.y, dpi),
        ScalePhysicalToLogical(physical.width, dpi),
        ScalePhysicalToLogical(physical.height, dpi),
    };
}

LogicalRect ClampToWorkArea(const LogicalRect& bounds, const WorkArea& workArea) {
    LogicalRect result = bounds;
    const int maxX = workArea.x + std::max(0, workArea.width - std::max(0, result.width));
    const int maxY = workArea.y + std::max(0, workArea.height - std::max(0, result.height));
    result.x = std::clamp(result.x, workArea.x, maxX);
    result.y = std::clamp(result.y, workArea.y, maxY);
    return result;
}

bool Contains(const LogicalRect& bounds, int x, int y) noexcept {
    return x >= bounds.x && y >= bounds.y && x < bounds.x + bounds.width && y < bounds.y + bounds.height;
}

}  // namespace lume::window
