#pragma once

#include <windows.h>

namespace lume::platform::windows {

// Trims only descendants of the shell process after its WebView is suspended.
// The shell and native component processes remain untouched.
void TrimWebViewProcessWorkingSets(DWORD rootProcessId) noexcept;

}  // namespace lume::platform::windows
