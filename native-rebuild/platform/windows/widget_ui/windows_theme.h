#pragma once

#include <dwmapi.h>
#include <windows.h>
#include <algorithm>
#include <cstdint>

namespace lume::platform::windows {

struct WindowsThemeColors {
    std::uint32_t background = 0x202124;
    std::uint32_t panel = 0x202124;
    std::uint32_t foreground = 0xffffff;
    std::uint32_t secondary = 0xb8b8bd;
    std::uint32_t hover = 0xffffff;
    std::uint32_t divider = 0xffffff;
    std::uint32_t accent = 0x0a84ff;
    bool dark = true;
    bool transparency = true;
};

inline std::uint32_t ThemeRegistryValue(const wchar_t* name, std::uint32_t fallback) {
    DWORD value = fallback;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) != ERROR_SUCCESS)
        return fallback;
    return value;
}

inline std::uint32_t BlendThemeColor(std::uint32_t a, std::uint32_t b, float amount) {
    const auto channel = [amount](std::uint32_t x, std::uint32_t y) {
        const float from = static_cast<float>(x), to = static_cast<float>(y);
        return static_cast<std::uint32_t>(std::clamp(from + (to - from) * amount, 0.f, 255.f));
    };
    return (channel((a >> 16) & 255, (b >> 16) & 255) << 16) |
           (channel((a >> 8) & 255, (b >> 8) & 255) << 8) |
           channel(a & 255, b & 255);
}

// Reads the same user theme switches Explorer uses for the taskbar. DWM's
// colorization value supplies the user's accent without taking ownership of
// any system settings.
inline WindowsThemeColors WindowsTheme() {
    WindowsThemeColors result;
    result.dark = ThemeRegistryValue(L"SystemUsesLightTheme", 0) == 0;
    const bool colorPrevalence = ThemeRegistryValue(L"ColorPrevalence", 0) != 0;
    result.transparency = ThemeRegistryValue(L"EnableTransparency", 1) != 0;
    const auto accent = [&] {
        DWORD color = 0;
        BOOL opaque = FALSE;
        return SUCCEEDED(DwmGetColorizationColor(&color, &opaque))
            ? (color & 0xffffffu) : result.accent;
    }();
    result.accent = accent;
    const std::uint32_t base = result.dark ? 0x202124u : 0xf3f3f3u;
    // Explorer only applies the accent to the taskbar when ColorPrevalence is
    // enabled. Otherwise keep the same neutral light/dark surface.
    result.background = colorPrevalence ? BlendThemeColor(base, accent, result.dark ? .08f : .04f) : base;
    // Shell flyouts use a neutral surface even when the taskbar is accented.
    result.panel = result.dark ? 0x282828u : 0xf9f9f9u;
    result.foreground = result.dark ? 0xffffffu : 0x1f1f1fu;
    result.secondary = result.dark ? 0xb8b8bdu : 0x5f6368u;
    result.hover = result.dark ? 0xffffffu : 0x000000u;
    result.divider = result.dark ? 0xffffffu : 0x000000u;
    return result;
}

}  // namespace lume::platform::windows
