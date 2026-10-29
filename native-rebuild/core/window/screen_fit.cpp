#include "core/window/screen_fit.h"

#include <algorithm>
#include <cmath>

namespace lume::window {
namespace {
bool Overlaps(const PhysicalRect& a, const PhysicalRect& b, int gap) noexcept {
    return a.x < b.x + b.width + gap && b.x < a.x + a.width + gap &&
           a.y < b.y + b.height + gap && b.y < a.y + a.height + gap;
}
}  // namespace

double FitScale(double monitorWidthDip, double monitorHeightDip) noexcept {
    if (!(monitorWidthDip > 0) || !(monitorHeightDip > 0)) return 1.0;
    const double scale = std::min(monitorWidthDip / FitReferenceWidth, monitorHeightDip / FitReferenceHeight);
    return std::clamp(scale, FitMinimum, 1.0);
}

int FitLength(int length, double scale, int minimum) noexcept {
    return std::max(minimum, static_cast<int>(std::lround(length * std::clamp(scale, 0.0, 1.0))));
}

PhysicalRect FitInside(PhysicalRect bounds, const PhysicalRect& work, int minWidth, int minHeight) noexcept {
    if (work.width <= 0 || work.height <= 0) return bounds;
    bounds.width = std::max(std::min(bounds.width, work.width), std::min(minWidth, bounds.width));
    bounds.height = std::max(std::min(bounds.height, work.height), std::min(minHeight, bounds.height));
    bounds.x = std::clamp(bounds.x, work.x, std::max(work.x, work.x + work.width - bounds.width));
    bounds.y = std::clamp(bounds.y, work.y, std::max(work.y, work.y + work.height - bounds.height));
    return bounds;
}

PhysicalRect FreeSlot(const PhysicalRect& work, int width, int height, int margin, int gap,
                      const std::vector<PhysicalRect>& occupied) {
    const int right = work.x + work.width - margin, bottom = work.y + work.height - margin;
    const int top = work.y + margin;
    // Columns from the right edge, then just left of each occupied rectangle;
    // in each column, the top margin or just below an occupied rectangle.
    std::vector<int> columns{right - width};
    for (const auto& r : occupied) columns.push_back(r.x - gap - width);
    std::sort(columns.begin(), columns.end(), std::greater<>());
    std::vector<int> rows{top};
    for (const auto& r : occupied) rows.push_back(r.y + r.height + gap);
    std::sort(rows.begin(), rows.end());
    for (const int x : columns) {
        if (x < work.x + margin || x + width > right) continue;
        for (const int y : rows) {
            if (y < top || y + height > bottom) continue;
            const PhysicalRect candidate{x, y, width, height};
            if (std::none_of(occupied.begin(), occupied.end(),
                             [&](const PhysicalRect& r) { return Overlaps(candidate, r, gap - 1); }))
                return candidate;
        }
    }
    const int step = std::max(1, gap * 2), count = static_cast<int>(occupied.size());
    return FitInside({right - width - step * count, top + step * count, width, height}, work, width, height);
}

}  // namespace lume::window
