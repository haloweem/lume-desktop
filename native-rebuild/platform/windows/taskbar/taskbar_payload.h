#pragma once
#include "core/taskbar/taskbar_state.h"
#include <atomic>
#include <filesystem>

namespace lume::platform::windows {
struct TaskbarPaths {
    std::string stateFile;
    std::filesystem::path payloadRoot;
    std::filesystem::path cacheRoot;
    // Production hosts opt in explicitly. Verification hosts leave this false
    // and inject an isolated PortFactory if they need functional tests.
    bool allowSystemEffects = false;
};
// Verifies a pinned manifest and every selected architecture dependency before
// returning the executable path. Writes only inside the explicit cache root.
taskbar::Error StageTaskbarPayload(const TaskbarPaths&, bool arm64,
                                  const std::atomic_bool& cancel,
                                  std::filesystem::path& executable);
taskbar::Error VerifyTaskbarPayload(const std::filesystem::path&, bool arm64);
}  // namespace lume::platform::windows
