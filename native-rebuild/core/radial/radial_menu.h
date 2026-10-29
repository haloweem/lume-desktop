#pragma once

#include <string>

namespace lume::radial {

inline constexpr float Diameter = 320.0f;
inline constexpr float OuterRadius = 152.0f;
inline constexpr float InnerRadius = 76.0f;
inline constexpr int ScreenshotSlot = 6;

struct Point {
    float x = 0;
    float y = 0;
};

int HitSlot(float x, float y);
// Invalid indices return the inactive center (160, 160).
Point SlotPosition(int slot);
// Both annulus boundaries are included; non-finite coordinates are rejected.
bool InRing(float x, float y);

struct Preferences {
    bool enabled = false;
    bool frosted = false;
    unsigned shortcutCode = 9;
};

// VK + 256 * modifiers, with Alt=1, Ctrl=2, Shift=4, Win=8.
bool ValidShortcut(unsigned code);
bool Valid(const Preferences& preferences);
bool Encode(const Preferences& preferences, std::string& output);
bool Decode(const std::string& input, Preferences& output);
std::wstring ShortcutLabel(unsigned code);

}  // namespace lume::radial
