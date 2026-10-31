#pragma once
#include "core/taskbar/taskbar_state.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace lume::platform::windows {
constexpr std::uint64_t TaskbarPackageVersion(std::uint16_t major, std::uint16_t minor,
                                             std::uint16_t build, std::uint16_t revision) {
    return (static_cast<std::uint64_t>(major) << 48) |
           (static_cast<std::uint64_t>(minor) << 32) |
           (static_cast<std::uint64_t>(build) << 16) | revision;
}
inline constexpr auto TaskbarVCLibsMinimum = TaskbarPackageVersion(14, 0, 33519, 0);
inline constexpr auto TaskbarWinUIMinimum = TaskbarPackageVersion(8, 2310, 30001, 0);
struct TaskbarFrameworksStatus {
    // Highest healthy current-user framework of the requested architecture.
    // Ordinary CRT DLLs and packages registered only for another user do not count.
    std::uint64_t vclibsVersion = 0;
    std::uint64_t winuiVersion = 0;
    std::int32_t hresult = 0;
    std::string detail;
    bool Ready() const {
        return hresult >= 0 && vclibsVersion >= TaskbarVCLibsMinimum && winuiVersion >= TaskbarWinUIMinimum;
    }
};
enum class TaskbarFrameworkDeploymentState { Pending, Succeeded, Failed, Cancelled };
struct TaskbarFrameworkDeploymentResult {
    TaskbarFrameworkDeploymentState state = TaskbarFrameworkDeploymentState::Pending;
    std::int32_t hresult = 0;
    std::string detail;
};
class TaskbarFrameworkDeployment {
public:
    virtual ~TaskbarFrameworkDeployment() = default;
    virtual TaskbarFrameworkDeploymentResult Poll() = 0;
    virtual void Cancel() = 0;
};
struct TaskbarFrameworkTimings {
    // One total budget for both frameworks; never blocks the settings UI thread.
    std::uint32_t timeoutMs = 60000;
    std::uint32_t pollMs = 50;
};
struct TaskbarFrameworkApi {
    std::function<TaskbarFrameworksStatus(bool arm64)> query;
    std::function<std::unique_ptr<TaskbarFrameworkDeployment>(const std::filesystem::path&)> begin;
    std::function<std::uint64_t()> nowMilliseconds;
    std::function<void(std::uint32_t)> waitMilliseconds;
};
// Read-only; queries registration, architecture, minimum version and package health.
TaskbarFrameworksStatus QueryTaskbarFrameworks(bool arm64);
// Run on the taskbar worker, after verifying these payload files against pinned
// hashes: VCLibs.appx and Microsoft.UI.Xaml.2.8.appx. Never downgrades packages,
// never changes all-user registration, and never launches PowerShell/installers.
taskbar::Error EnsureTaskbarFrameworks(const std::filesystem::path& frameworkDirectory,
                                     bool arm64, const std::atomic_bool& cancel,
                                     TaskbarFrameworkTimings = {}, TaskbarFrameworkApi = {});
}  // namespace lume::platform::windows
