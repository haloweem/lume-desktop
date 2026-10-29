#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace lume::platform::windows {
// Drags local files and folders out of Lume the way Explorer's desktop does:
// the target program receives the shell's own data object (file list,
// shortcuts, thumbnails). Call from the UI thread while the left button is
// down; it returns when the drop finishes. Moving is not offered, so a drop
// copies or links and the original stays where Lume refers to it. Missing
// paths are skipped; returns the effect performed, or DROPEFFECT_NONE.
DWORD DragPathsOut(HWND source, const std::vector<std::wstring>& paths);

struct DragOutResult {
    DWORD effect = 0;  // DROPEFFECT_NONE (oleidl.h)
    // Released over the Windows desktop: nothing was dropped there.
    bool desktop = false;
};
// As above, but a release over the Windows desktop drops nothing (Explorer
// would copy or link the file next to itself) and reports `desktop`, so the
// caller can treat it as handing the icon back to the desktop.
DragOutResult DragPathsOutToDesktop(HWND source, const std::vector<std::wstring>& paths);
// The screen point is over Explorer's desktop (its icon view or wallpaper
// host), not over a program window or the taskbar.
bool OverDesktop(POINT screen);
// The screen point is over another program's window, or an icon organizer
// (which takes dropped files). Lume's other desktop surfaces (clock, Dock,
// widgets), the taskbar and the desktop itself do not count.
bool OverProgram(POINT screen);
}
