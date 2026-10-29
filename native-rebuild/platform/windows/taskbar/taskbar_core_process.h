#pragma once
#include "core/taskbar/taskbar_state.h"
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>

namespace lume::platform::windows {
struct TaskbarCoreLaunch {
    std::filesystem::path executable;
    std::wstring arguments;
};
struct TaskbarCoreTimings {
    std::uint32_t startTimeoutMs = 8000;
    std::uint32_t readySettleMs = 1200;
    std::uint32_t waitSliceMs = 50;
    std::uint32_t stopTimeoutMs = 3000;
};
// A narrow Win32 boundary also lets isolated tests observe the real creation
// instant and inject failed waits without replacing the child process itself.
struct TaskbarCoreProcessApi {
    decltype(&CreateJobObjectW) createJob = &CreateJobObjectW;
    decltype(&CreateProcessW) createProcess = &CreateProcessW;
    decltype(&WaitForSingleObject) wait = &WaitForSingleObject;
};
// Owns only a prepared child process and its kill-on-close job. The production
// taskbar port supplies the pinned-payload verifier; tests supply an isolated
// child executable that never interacts with Explorer or user settings.
class TaskbarCoreProcess final {
public:
    using Prepare = std::function<taskbar::Error(bool recovery, TaskbarCoreLaunch&)>;
    TaskbarCoreProcess(const std::atomic_bool& cancel, Prepare, TaskbarCoreTimings = {}, TaskbarCoreProcessApi = {});
    ~TaskbarCoreProcess();
    taskbar::Error Start(bool recovery = false);
    taskbar::Error Stop(bool recoverPreviousProcess);
    bool Alive() const;
    bool Ready() const;
    bool HasProcess() const;
    std::uintptr_t ExitSignal() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace lume::platform::windows
