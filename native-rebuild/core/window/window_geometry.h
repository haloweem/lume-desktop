#pragma once

#include "core/window/window_types.h"

namespace lume::window {

int ScaleLogicalToPhysical(int logicalPixels, unsigned int dpi);
int ScalePhysicalToLogical(int physicalPixels, unsigned int dpi);
PhysicalRect ScaleLogicalToPhysical(const LogicalRect& logical, unsigned int dpi);
LogicalRect ScalePhysicalToLogical(const PhysicalRect& physical, unsigned int dpi);
LogicalRect ClampToWorkArea(const LogicalRect& bounds, const WorkArea& workArea);
bool Contains(const LogicalRect& bounds, int x, int y) noexcept;

}  // namespace lume::window
