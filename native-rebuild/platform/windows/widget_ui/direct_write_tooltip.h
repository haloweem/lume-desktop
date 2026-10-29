#pragma once
#include <windows.h>

namespace lume::platform::windows::direct_write_tooltip {
// Call once after creating a TOOLTIPS_CLASSW window. Native TTM messages,
// mouse tracking, delays and lifetime continue to belong to common controls.
bool Attach(HWND tooltip);
}
