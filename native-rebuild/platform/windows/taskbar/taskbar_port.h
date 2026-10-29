#pragma once
#include "core/taskbar/taskbar_controller.h"
#include "platform/windows/taskbar/taskbar_payload.h"
#include <memory>

namespace lume::platform::windows {
class WindowsTaskbarPort final : public taskbar::ITaskbarPort {
public:
    WindowsTaskbarPort(TaskbarPaths, const std::atomic_bool& cancel);
    ~WindowsTaskbarPort() override;
    taskbar::Error Acquire(bool transparency, taskbar::Mode&) override;
    void Release() override;
    taskbar::Error ReadAutoHide(bool&) override;
    taskbar::Error SetAutoHide(bool) override;
    taskbar::Error ReadVisibility(std::vector<taskbar::Visibility>&) override;
    taskbar::Error Hide(const taskbar::Visibility&) override;
    taskbar::RestoreResult RestoreVisibility(const taskbar::Visibility&) override;
    void RepairHidden() override;
    taskbar::Error ReadClassic(std::vector<taskbar::Appearance>&) override;
    taskbar::Error MakeTransparent(const taskbar::Appearance&) override;
    taskbar::RestoreResult RestoreAppearance(const taskbar::Appearance&) override;
    taskbar::Error StartCore() override;
    bool CoreAlive() const override;
    taskbar::Error StopCore(bool recoverPreviousProcess) override;
    std::uintptr_t ExitSignal() const override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace lume::platform::windows
