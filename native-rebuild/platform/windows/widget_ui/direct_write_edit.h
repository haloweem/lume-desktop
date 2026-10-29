#pragma once
#include <windows.h>

namespace lume::platform::windows::direct_write_edit {
// Attach before installing a host WNDPROC that calls its saved predecessor for
// WM_PRINTCLIENT. The HWND continues to own text, undo, accessibility and IME.
// Text-only STATIC and push BUTTON controls are also supported.
// A failure is explicit; callers must not expose an unadapted control.
bool Attach(HWND control);
}
