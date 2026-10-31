#include "platform/windows/taskbar/taskbar_frameworks.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include <windows.h>
#include <roapi.h>
#include <shlwapi.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.System.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace lume::platform::windows {
using taskbar::Error;
namespace {
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Management::Deployment;
struct Apartment {
    Apartment() : result(RoInitialize(RO_INIT_MULTITHREADED)) {
        // The read-only query can also run on an already initialized UI thread.
        if (FAILED(result) && result != RPC_E_CHANGED_MODE) winrt::throw_hresult(result);
    }
    ~Apartment() { if (SUCCEEDED(result)) RoUninitialize(); }
    HRESULT result;
};
std::string Hex(std::int32_t code) {
    std::ostringstream text;
    text << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
         << static_cast<std::uint32_t>(code);
    return text.str();
}
void LogFailure(const std::string& operation, std::int32_t code, const std::string& detail) {
    diag::Log("taskbar", "framework " + operation + " failed: " + Hex(code) + " " + detail);
}
std::uint64_t Version(const winrt::Windows::ApplicationModel::PackageVersion& version) {
    return TaskbarPackageVersion(version.Major, version.Minor, version.Build, version.Revision);
}
TaskbarFrameworksStatus Query(bool arm64) {
    TaskbarFrameworksStatus status;
    using winrt::Windows::System::ProcessorArchitecture;
    const auto architecture = arm64 ? ProcessorArchitecture::Arm64 : ProcessorArchitecture::X64;
    // Empty user SID means the current user, without requiring elevation.
    for (const auto& package : PackageManager().FindPackagesForUser(L"")) {
        const auto id = package.Id();
        if (!package.IsFramework() || id.Architecture() != architecture) continue;
        const auto family = id.FamilyName();
        if (family != L"Microsoft.VCLibs.140.00_8wekyb3d8bbwe" &&
            family != L"Microsoft.UI.Xaml.2.8_8wekyb3d8bbwe") continue;
        if (!package.Status().VerifyIsOK()) continue;
        auto& found = family == L"Microsoft.VCLibs.140.00_8wekyb3d8bbwe" ? status.vclibsVersion : status.winuiVersion;
        found = std::max(found, Version(id.Version()));
    }
    return status;
}
Uri PackageUri(const std::filesystem::path& file) {
    std::vector<wchar_t> url(file.native().size() * 3 + 32);
    auto size = static_cast<DWORD>(url.size());
    winrt::check_hresult(UrlCreateFromPathW(file.c_str(), url.data(), &size, 0));
    return Uri(url.data());
}
class Deployment final : public TaskbarFrameworkDeployment {
public:
    explicit Deployment(const std::filesystem::path& file)
        : operation_(PackageManager().AddPackageAsync(PackageUri(file), nullptr, DeploymentOptions::None)) {}
    TaskbarFrameworkDeploymentResult Poll() override {
        const auto state = operation_.Status();
        if (state == AsyncStatus::Started) return {};
        if (state == AsyncStatus::Canceled)
            return {TaskbarFrameworkDeploymentState::Cancelled, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANCELLED)), "deployment cancelled"};
        try {
            const auto result = operation_.GetResults();
            const auto code = static_cast<std::int32_t>(result.ExtendedErrorCode().value);
            const auto detail = winrt::to_string(result.ErrorText()) +
                " activity=" + winrt::to_string(winrt::to_hstring(result.ActivityId()));
            return {state == AsyncStatus::Completed && code >= 0 ? TaskbarFrameworkDeploymentState::Succeeded :
                    TaskbarFrameworkDeploymentState::Failed, code, detail};
        } catch (const winrt::hresult_error& error) {
            return {TaskbarFrameworkDeploymentState::Failed, error.code().value, winrt::to_string(error.message())};
        }
    }
    void Cancel() override { operation_.Cancel(); }
private:
    IAsyncOperationWithProgress<DeploymentResult, DeploymentProgress> operation_;
};
void CancelDeployment(TaskbarFrameworkDeployment& operation) noexcept {
    try { operation.Cancel(); }
    catch (const winrt::hresult_error& error) { LogFailure("cancel", error.code().value, winrt::to_string(error.message())); }
    catch (...) { diag::Log("taskbar", "framework cancellation request failed"); }
}
struct DeploymentGuard {
    TaskbarFrameworkDeployment& operation;
    bool pending = true;
    ~DeploymentGuard() { if (pending) CancelDeployment(operation); }
};
}  // namespace

TaskbarFrameworksStatus QueryTaskbarFrameworks(bool arm64) {
    try {
        Apartment apartment;
        return Query(arm64);
    } catch (const winrt::hresult_error& error) {
        return {0, 0, error.code().value, winrt::to_string(error.message())};
    } catch (const std::exception& error) {
        return {0, 0, static_cast<std::int32_t>(E_FAIL), error.what()};
    }
}

Error EnsureTaskbarFrameworks(const std::filesystem::path& directory, bool arm64,
                             const std::atomic_bool& cancel, TaskbarFrameworkTimings timings,
                             TaskbarFrameworkApi api) {
    if (cancel) return Error::Cancelled;
    try {
        // Keep COM initialized until every deployment operation has been released.
        Apartment apartment;
        if (!api.query) api.query = QueryTaskbarFrameworks;
        if (!api.begin) api.begin = [](const std::filesystem::path& file) { return std::make_unique<Deployment>(file); };
        if (!api.nowMilliseconds) api.nowMilliseconds = [] { return GetTickCount64(); };
        if (!api.waitMilliseconds) api.waitMilliseconds = [](std::uint32_t ms) { Sleep(ms); };
        const auto started = api.nowMilliseconds();
        auto status = api.query(arm64);
        if (status.hresult < 0) {
            LogFailure("query", status.hresult, status.detail);
            return Error::FrameworkInstall;
        }
        if (cancel) return Error::Cancelled;
        if (status.Ready()) return Error::None;
        struct Requirement { const wchar_t* file; std::uint64_t TaskbarFrameworksStatus::*version; std::uint64_t minimum; };
        const Requirement requirements[] = {
            {L"VCLibs.appx", &TaskbarFrameworksStatus::vclibsVersion, TaskbarVCLibsMinimum},
            {L"Microsoft.UI.Xaml.2.8.appx", &TaskbarFrameworksStatus::winuiVersion, TaskbarWinUIMinimum}
        };
        for (const auto& requirement : requirements) {
            if (cancel) return Error::Cancelled;
            if (status.*requirement.version >= requirement.minimum) continue;
            const auto file = directory / requirement.file;
            const auto name = file.filename().u8string();
            std::error_code fileError;
            if (!std::filesystem::is_regular_file(file, fileError)) {
                LogFailure("payload " + name, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)), fileError.message());
                return Error::FrameworkInstall;
            }
            if (api.nowMilliseconds() - started >= timings.timeoutMs) {
                LogFailure("deploy " + name, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_TIMEOUT)), "total deployment time limit reached");
                return Error::FrameworkInstall;
            }
            diag::Log("taskbar", "deploying current-user framework " + name + (arm64 ? " (arm64)" : " (x64)"));
            auto operation = api.begin(std::filesystem::absolute(file));
            if (!operation) {
                LogFailure("deploy " + name, static_cast<std::int32_t>(E_FAIL), "no deployment operation");
                return Error::FrameworkInstall;
            }
            DeploymentGuard guard{*operation};
            for (;;) {
                if (cancel) {
                    CancelDeployment(*operation);
                    guard.pending = false;
                    diag::Log("taskbar", "framework deployment cancellation requested: " + name);
                    return Error::Cancelled;
                }
                const auto result = operation->Poll();
                if (result.state != TaskbarFrameworkDeploymentState::Pending) guard.pending = false;
                if (result.state == TaskbarFrameworkDeploymentState::Cancelled) return Error::Cancelled;
                if (result.state != TaskbarFrameworkDeploymentState::Pending) {
                    // Another installer can win the race. Re-query before
                    // treating an already-present/newer-package HRESULT as failure.
                    status = api.query(arm64);
                    if (status.hresult >= 0 && status.*requirement.version >= requirement.minimum) break;
                    LogFailure("deploy " + name, result.hresult < 0 ? result.hresult :
                        (status.hresult < 0 ? status.hresult : static_cast<std::int32_t>(E_FAIL)),
                        result.detail + " " + status.detail + " required package is not registered and healthy");
                    return Error::FrameworkInstall;
                }
                const auto elapsed = api.nowMilliseconds() - started;
                if (elapsed >= timings.timeoutMs) {
                    CancelDeployment(*operation);
                    guard.pending = false;
                    LogFailure("deploy " + name, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_TIMEOUT)), "deployment timed out; cancellation requested");
                    return Error::FrameworkInstall;
                }
                api.waitMilliseconds(std::min(std::max(timings.pollMs, 1u),
                    timings.timeoutMs - static_cast<std::uint32_t>(elapsed)));
            }
            diag::Log("taskbar", "current-user framework verified: " + name);
        }
        if (cancel) return Error::Cancelled;
        // Recheck the complete set, not just the last completed installation.
        status = api.query(arm64);
        if (status.Ready()) return Error::None;
        LogFailure("final verification", status.hresult < 0 ? status.hresult : static_cast<std::int32_t>(E_FAIL), status.detail);
    } catch (const winrt::hresult_error& error) {
        LogFailure("setup", error.code().value, winrt::to_string(error.message()));
    } catch (const std::exception& error) {
        LogFailure("setup", static_cast<std::int32_t>(E_FAIL), error.what());
    }
    return cancel ? Error::Cancelled : Error::FrameworkInstall;
}
}  // namespace lume::platform::windows
