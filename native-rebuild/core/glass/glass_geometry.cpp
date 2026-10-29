#include "core/glass/glass_geometry.h"

#include <algorithm>

namespace lume::glass {
namespace {

int Right(const PhysicalRect& rect) noexcept { return rect.x + rect.width; }
int Bottom(const PhysicalRect& rect) noexcept { return rect.y + rect.height; }

}  // namespace

MaterialCoordinates ComputeMaterialCoordinates(const PhysicalRect& owner,
                                               const PhysicalRect& background,
                                               int padding) {
    const int safePadding = std::max(0, padding);
    const int left = std::max(background.x, owner.x - safePadding);
    const int top = std::max(background.y, owner.y - safePadding);
    const int right = std::min(Right(background), Right(owner) + safePadding);
    const int bottom = std::min(Bottom(background), Bottom(owner) + safePadding);
    return {
        owner,
        {left, top, std::max(0, right - left), std::max(0, bottom - top)},
    };
}

}  // namespace lume::glass
