#pragma once
#include "core/taskbar/taskbar_state.h"
#include <windows.h>

namespace lume::platform::windows::taskbar_visibility {
// The port validates Explorer identity and journals Capture before calling Hide.
bool Capture(HWND, taskbar::Visibility&);
bool Hide(HWND);
bool Restore(HWND, const taskbar::Visibility&);
// True when the window carries exactly the fully-hidden marks Hide() applies:
// layered, click-through and zero alpha. Explorer never sets these itself.
bool LooksHidden(HWND);
// Shows Explorer taskbars left fully hidden without a journal entry (a lost
// record, an older build or the removed menu bar). Returns how many it repaired.
int RepairOrphans();
}
