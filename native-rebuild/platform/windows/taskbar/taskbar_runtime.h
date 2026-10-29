#pragma once
#include "platform/windows/taskbar/taskbar_payload.h"
#include "core/taskbar/taskbar_controller.h"
#include <functional>
#include <memory>

namespace lume::platform::windows {
struct TaskbarRuntimeStatus {
    taskbar::Status state;
    taskbar::LoadState loaded = taskbar::LoadState::Fresh;
    bool applying = false;
    bool worker = false;
    std::uint64_t revision = 0;
};
class TaskbarRuntime final {
public:
    // Runs on the worker; marshal UI notifications and do not synchronously
    // call Shutdown from this callback. TaskbarSession supplies that bridge.
    using Notify = std::function<void()>;
    using PortFactory = std::function<std::unique_ptr<taskbar::ITaskbarPort>(const TaskbarPaths&, const std::atomic_bool&)>;
    TaskbarRuntime(TaskbarPaths, Notify = {}, settings::IFileStore* = nullptr, PortFactory = {});
    ~TaskbarRuntime();
    // Requests are asynchronous and coalesced. Return means accepted, not that
    // Windows has applied the change; completion is reported in Snapshot().
    bool Apply(taskbar::Preferences);
    bool Restore();
    // Keeps Explorer's taskbar auto-hidden while true, independently of the
    // user's taskbar preferences; false hands control back to them.
    bool RequireAutoHide(bool);
    bool Shutdown();
    TaskbarRuntimeStatus Snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace lume::platform::windows
