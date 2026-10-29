#pragma once
#include <windows.h>
#include <optional>
#include <string>

namespace lume::platform::windows::wallpaper {
// The Windows desktop wallpaper of one monitor, matched by its bounds. Lume
// sets it to a still of its own wallpaper, so the desktop shows the right
// picture from sign-in until the live wallpaper starts. Best effort.
std::optional<std::wstring> SystemWallpaper(const RECT& monitor);
// cover: fill (crop) like Lume's cover mode; otherwise fit with bars.
bool SetSystemWallpaper(const RECT& monitor, const std::wstring& image, bool cover);
}  // namespace lume::platform::windows::wallpaper
