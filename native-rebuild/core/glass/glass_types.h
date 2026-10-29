#pragma once

#include <cstdint>

namespace lume::glass {

using BackgroundToken = std::uint64_t;
using BackendHandle = std::uint64_t;

struct PhysicalRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

enum class GlassMode {
    Full,
    Reduced,
    TransparentFallback,
};

struct MaterialCoordinates {
    PhysicalRect lens{};
    PhysicalRect sample{};
};

struct GlassSnapshot {
    std::uint64_t windowId = 0;
    PhysicalRect ownerBounds{};
    PhysicalRect backgroundBounds{};
    int samplePadding = 0;
    MaterialCoordinates coordinates{};
};

}  // namespace lume::glass
