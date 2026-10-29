#pragma once

#include "core/glass/glass_types.h"

namespace lume::glass {

MaterialCoordinates ComputeMaterialCoordinates(const PhysicalRect& owner,
                                               const PhysicalRect& background,
                                               int padding);

}  // namespace lume::glass
