#include "core/calendar/calendar_settings.h"
namespace lume::calendar {
Profile* Preferences::Active() noexcept {
    switch (theme) {
    case Theme::Spacious: return &profiles[0];
    case Theme::Split: return &profiles[1];
    case Theme::Todo: return &profiles[1];
    }
    return nullptr;
}
const Profile* Preferences::Active() const noexcept {
    switch (theme) {
    case Theme::Spacious: return &profiles[0];
    case Theme::Split: return &profiles[1];
    case Theme::Todo: return &profiles[1];
    }
    return nullptr;
}
std::string_view MaterialName(Material material) noexcept {
    switch (material) {
    case Material::LiquidGlass: return "liquid-glass";
    case Material::FrostedGlass: return "frosted-glass";
    case Material::Solid: return "solid";
    }
    return {};
}
std::optional<Material> ParseMaterial(std::string_view name) noexcept {
    if (name == "liquid-glass") return Material::LiquidGlass;
    if (name == "frosted-glass") return Material::FrostedGlass;
    if (name == "solid") return Material::Solid;
    return std::nullopt;
}
bool ValidRgba(Rgba value) noexcept {
    return value.rgb >= 0 && value.rgb <= 0xffffff && value.alpha >= 0 && value.alpha <= 255;
}
bool ValidProfile(const Profile& p) noexcept {
    return p.x >= -32768 && p.x <= 32767 && p.y >= -32768 && p.y <= 32767 &&
        p.width >= 320 && p.width <= 1440 && p.height >= 320 && p.height <= 720;
}
bool ValidPreferences(const Preferences& value) noexcept {
    return value.Active() && !MaterialName(value.material).empty() && !BoardViewName(value.boardView).empty() &&
        ValidRgba(value.textColor) && ValidRgba(value.highlightColor) && ValidRgba(value.panelColor) &&
        ValidProfile(value.profiles[0]) && ValidProfile(value.profiles[1]);
}
bool ParseRgba(std::string_view text, Rgba& out) noexcept {
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#') return false;
    unsigned value = 0;
    for (std::size_t i = 1; i < text.size(); ++i) {
        const char ch = text[i];
        int digit = -1;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
        if (digit < 0) return false;
        value = value * 16u + static_cast<unsigned>(digit);
    }
    out = text.size() == 7 ? Rgba{static_cast<int>(value),255} :
        Rgba{static_cast<int>(value >> 8u),static_cast<int>(value & 255u)};
    return true;
}
std::string FormatRgba(Rgba value) {
    if (!ValidRgba(value)) return {};
    constexpr char digits[] = "0123456789abcdef";
    std::string text = "#";
    for (int shift = 20; shift >= 0; shift -= 4) text += digits[(value.rgb >> shift) & 15];
    if (value.alpha != 255) {
        text += digits[value.alpha >> 4];
        text += digits[value.alpha & 15];
    }
    return text;
}
}
