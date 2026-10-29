#pragma once
#include "platform/windows/taskbar/taskbar_runtime.h"
#include <windows.h>
#include <functional>
#include <memory>

namespace lume::shell {
class TaskbarSession final {
public:
    using Notify = std::function<void(const std::wstring&, bool)>;
    TaskbarSession(HINSTANCE, platform::windows::TaskbarPaths, Notify,
                   settings::IFileStore* = nullptr,
                   platform::windows::TaskbarRuntime::PortFactory = {});
    ~TaskbarSession();
    bool Restore();
    bool Apply(taskbar::Preferences);
    taskbar::Preferences Preferences() const;
    std::wstring State() const;
    bool Running() const;
    bool HasWork() const;
    bool HasRecovery() const;
    bool Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace lume::shell
