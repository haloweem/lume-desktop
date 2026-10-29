#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

namespace lume::platform::windows::icon_art {
// The first thirteen values intentionally match screenshot::Command.
enum class Icon {
    Shape, Arrow, Pen, Marker, Mosaic, Text, Eraser, Undo, Redo,
    Close, Pin, Save, Copy, Size, Position, Color, Lock, Center,
    Refresh, Create, Material, Settings, Reset, Count
};
inline constexpr int IconCount = static_cast<int>(Icon::Count);

// Draws an embedded mask from the approved transparent PNG icons.
// The caller controls ink color and alpha; Graphics transforms supply DPI scaling.
void Draw(Gdiplus::Graphics& graphics, Icon icon, float x, float y, float size,
          Gdiplus::Color ink);
}
