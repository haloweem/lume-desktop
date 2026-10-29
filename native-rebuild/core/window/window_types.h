#pragma once

#include <cstdint>

namespace lume::window {

using WindowId = std::uint64_t;

enum class WindowRole {
    Main,
    Settings,
    Widget,
    Music,
    Wallpaper,
};

struct LogicalRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct PhysicalRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct WorkArea {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct WindowSpec {
    WindowRole role = WindowRole::Widget;
    LogicalRect logicalBounds{};
    unsigned int dpi = 96;
    bool clickThrough = false;
    bool visible = false;
};

struct WindowSnapshot {
    WindowId id = 0;
    WindowRole role = WindowRole::Widget;
    LogicalRect logicalBounds{};
    PhysicalRect physicalBounds{};
    unsigned int dpi = 96;
    bool clickThrough = false;
    bool visible = false;
};

}  // namespace lume::window
