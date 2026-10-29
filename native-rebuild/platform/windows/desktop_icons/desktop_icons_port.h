#pragma once
#include "core/desktop_icons/desktop_icons_policy.h"
#include <windows.h>
#include <vector>

namespace lume::platform::windows {

struct DesktopIconsTarget {
    HWND shell = nullptr;
    HWND list = nullptr;
    DWORD process = 0;
    DWORD thread = 0;
};
enum class DesktopHit { Outside, Blank, Item, Unavailable };
enum class DesktopVisibilityResult { Applied, Unavailable, Cancelled, Failed };

// Production discovery only returns the actual shell process's desktop views.
// A target is revalidated before each operation, including after Explorer restart.
std::vector<DesktopIconsTarget> FindDesktopIconsTargets();
bool ValidDesktopIconsTarget(const DesktopIconsTarget&);
desktop_icons::Observation ObserveDesktopIcons(const DesktopIconsTarget&);
DesktopHit HitDesktopIcons(const DesktopIconsTarget&, POINT screenPoint);
DesktopVisibilityResult SetDesktopIconsVisible(const DesktopIconsTarget&, bool visible,
                                              HANDLE cancelled = nullptr);
}
