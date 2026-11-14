#pragma once
#include <windows.h>
#include <functional>
#include <string>

namespace lume::shell {
enum class NativeMaterialTarget { Note, MusicDesktop };
// UI-thread intent, never a complete preference snapshot. The host owns consent
// and source-window lifetime; the target reads current preferences on replay.
struct NativeMaterialRequest {
    NativeMaterialTarget target = NativeMaterialTarget::Note;
    bool frosted = false;
    std::string id; // Exact note id; empty for the singleton music widget.
    HWND source = nullptr;
};
using NativeMaterialSelectionHandler = std::function<void(NativeMaterialRequest)>;
}
