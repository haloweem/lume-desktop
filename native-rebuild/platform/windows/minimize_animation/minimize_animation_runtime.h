#pragma once
#include "core/minimize_animation/minimize_animation.h"
#include <windows.h>
#include <memory>
#include <string>

namespace lume::platform::windows {
enum class MinimizeAnimationError {
    None, InvalidSettings, MissingHost, AlreadyRunning, Mapping, StartProcess, HookInstall,
    StopTimeout, HostExited, UnsupportedArchitecture
};
struct MinimizeAnimationScope {
    // Zero means the current desktop. Tests must pass their fixture GUI thread.
    DWORD threadId = 0;
    DWORD processId = 0;
    bool requireBothArchitectures = true;
    std::wstring directory;
};
struct MinimizeAnimationSnapshot {
    bool running = false;
    bool x64 = false;
    bool x86 = false;
    unsigned hostCount = 0, hooks = 0, workers = 0, entries = 0, pendingCleanup = 0;
    unsigned minimized = 0, restored = 0, launched = 0, completed = 0, cancelled = 0;
    unsigned failed = 0, frames = 0, captures = 0;
    DWORD lastTargetPid = 0;
    MinimizeAnimationError error = MinimizeAnimationError::None;
    DWORD systemError = 0;
};
struct MinimizeAnimationTimings {
    DWORD readyTimeoutMs = 5000, stopTimeoutMs = 7000, finalExitTimeoutMs = 2000;
    DWORD waitSliceMs = 10;
};
struct MinimizeAnimationProcessApi {
    decltype(&CreateJobObjectW) createJob = CreateJobObjectW;
    decltype(&CreateProcessW) createProcess = CreateProcessW;
    decltype(&WaitForSingleObject) wait = WaitForSingleObject;
};
class MinimizeAnimationRuntime final {
public:
    explicit MinimizeAnimationRuntime(MinimizeAnimationScope = {}, MinimizeAnimationTimings = {},
                                      MinimizeAnimationProcessApi = {});
    ~MinimizeAnimationRuntime();
    MinimizeAnimationRuntime(const MinimizeAnimationRuntime&) = delete;
    MinimizeAnimationRuntime& operator=(const MinimizeAnimationRuntime&) = delete;
    bool Start(const minimize_animation::Preferences&);
    bool Configure(const minimize_animation::Preferences&);
    bool Stop();
    MinimizeAnimationSnapshot Snapshot() const;
    // Keeps timed-out destruction observable without a permanent polling thread.
    // A record is released only after both Host exit and target lease cleanup.
    static unsigned PendingCleanup();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
const char* MinimizeAnimationErrorName(MinimizeAnimationError) noexcept;
}
