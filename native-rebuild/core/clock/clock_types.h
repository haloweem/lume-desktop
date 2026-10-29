#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace lume::clock {
enum class Style { Welfare, Mond, InsideDream };
struct Appearance {
    int x = 789;
    int y = 348;
    int scale = 100;
    int color = 0xffffff;
    int alpha = 255;
    bool customColor = false;
    bool locked = false;
};
enum class ActionType { Enable, SelectStyle, Scale, Position, Color, Lock, Reset, Center, OpenSettings };
struct Action { ActionType type; int value = 0; int secondary = 0; };
using Profiles = std::array<Appearance, 3>;
constexpr std::size_t Index(Style style) { return static_cast<std::size_t>(style); }
inline Profiles DefaultProfiles() {
    return {{{789,348,100,0xffffff,255,false,false}, {532,288,100,0xffffff,255,false,false},
             {660,220,100,0xffffff,255,false,false}}};
}
inline Style ParseStyle(std::string_view value) {
    return value == "mond" ? Style::Mond : value == "insideDream" ? Style::InsideDream : Style::Welfare;
}
inline const char* StyleName(Style style) {
    return style == Style::Mond ? "mond" : style == Style::InsideDream ? "insideDream" : "welfare";
}
inline bool ValidAppearance(const Appearance& value) {
    return value.x >= -32768 && value.x <= 32767 && value.y >= -32768 && value.y <= 32767 &&
        value.scale >= 50 && value.scale <= 300 && value.scale % 10 == 0 &&
        value.color >= 0 && value.color <= 0xffffff && value.alpha >= 0 && value.alpha <= 255;
}
}  // namespace lume::clock
