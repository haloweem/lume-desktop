#pragma once
#include "core/calendar/calendar_types.h"
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace lume::calendar {
// Solid is an opaque dark panel without glass ("普通"), the default.
enum class Material { LiquidGlass, FrostedGlass, Solid };
struct Rgba { int rgb = 0xffffff; int alpha = 255; };
struct Profile {
    int x = 0;
    int y = 0;
    int width = 820;
    int height = 500;
    bool hasPosition = false;
};
struct Preferences {
    bool enabled = false;
    Theme theme = Theme::Spacious;
    Material material = Material::Solid;
    BoardView boardView = BoardView::All;
    Rgba textColor{};
    Rgba highlightColor{0x0a84ff,255};
    // Background of the solid material; choosing it switches to that material.
    Rgba panelColor{0x2c2c2e,255};
    bool locked = false;
    std::array<Profile,2> profiles{};
    Profile* Active() noexcept;
    const Profile* Active() const noexcept;
};
std::string_view MaterialName(Material material) noexcept;
std::optional<Material> ParseMaterial(std::string_view name) noexcept;
bool ValidRgba(Rgba value) noexcept;
bool ValidProfile(const Profile& value) noexcept;
bool ValidPreferences(const Preferences& value) noexcept;
bool ParseRgba(std::string_view text, Rgba& out) noexcept;
std::string FormatRgba(Rgba value);
}  // namespace lume::calendar
