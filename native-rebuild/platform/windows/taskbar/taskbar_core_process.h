#pragma once
#include "core/taskbar/taskbar_state.h"
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

namespace lume::platform::windows {
taskbar::Error TaskbarCoreDialogError(std::wstring_view);
struct TaskbarCoreLaunch {
    std::filesystem::path executable;
    std::wstring arguments;
    // Production cores publish a per-launch, atomically replaced report. An
    // empty path is reserved for isolated lifecycle fixtures.
    std::filesystem::path diagnosticsFile;
};
struct TaskbarCoreDiagnostics {
    taskbar::TransparencyStatus transparency;
    taskbar::Error error = taskbar::Error::None;
    bool valid = false;
    bool serviceReady = false;
};
TaskbarCoreDiagnostics ReadTaskbarCoreDiagnostics(const std::filesystem::path&, DWORD corePid, DWORD explorerPid);
struct TaskbarCoreTimings {
    // The upstream Explorer handshake can itself wait 35 seconds.
    std::uint32_t startTimeoutMs = 42000;
    std::uint32_t readySettleMs = 1200;
    std::uint32_t waitSliceMs = 50;
    std::uint32_t stopTimeoutMs = 3000;
    std::uint32_t xamlWaitMs = 15000;
};
// A narrow Win32 boundary also lets isolated tests observe the real creation
// instant and inject failed waits without replacing the child process itself.
struct TaskbarCoreProcessApi {
    decltype(&CreateJobObjectW) createJob = &CreateJobObjectW;
    decltype(&CreateProcessW) createProcess = &CreateProcessW;
    decltype(&WaitForSingleObject) wait = &WaitForSingleObject;
    // Optional synthetic failure input for isolated lifecycle tests. The
    // production default reads only dialogs owned by the verified child PID.
    std::function<taskbar::Error(DWORD)> readDialogFailure;
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
    void ResetFailure();
    taskbar::Error Stop(bool recoverPreviousProcess);
    bool Alive() const;
    bool Ready() const;
    bool HasProcess() const;
    taskbar::TransparencyStatus Transparency() const;
    bool ReloadResolved() const;
    std::uintptr_t ExitSignal() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace lume::platform::windows
