#pragma once
#include "core/ui/page_bridge.h"
#include <windows.h>
#include <functional>

namespace lume::shell {
// UI-thread intent only. The host coordinates consent and replays the command
// against current preferences; sessions do not own Windows permission requests.
// An empty handler retains the session's isolated, local command behavior.
using MaterialSelectionHandler = std::function<void(ui::PageCommand, HWND)>;
}
