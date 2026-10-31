#include "platform/windows/taskbar/taskbar_frameworks.h"
#include "platform/windows/taskbar/taskbar_payload.h"
#include "platform/windows/taskbar/taskbar_port.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include <windows.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace lume::platform::windows;
using lume::taskbar::Error;
namespace {
void Check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); } }
std::string VersionText(std::uint64_t v) {
    return std::to_string(v >> 48) + "." + std::to_string((v >> 32) & 65535) + "." +
        std::to_string((v >> 16) & 65535) + "." + std::to_string(v & 65535);
}
void PrintStatus(bool arm64) {
    const auto status = QueryTaskbarFrameworks(arm64);
    std::cout << (arm64 ? "arm64" : "x64") << " current-user frameworks: VCLibs=" << VersionText(status.vclibsVersion)
              << " WinUI=" << VersionText(status.winuiVersion) << " ready=" << status.Ready()
              << " HRESULT=" << status.hresult << " " << status.detail << '\n';
}
std::filesystem::path TemporaryDirectory() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"lume-taskbar-framework-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Check(root.is_absolute() && std::filesystem::equivalent(root.parent_path(), std::filesystem::temp_directory_path()) &&
          std::filesystem::create_directory(root), "create an exclusively owned temporary directory");
    return root;
}
struct Fixture {
    TaskbarFrameworksStatus status;
    std::atomic_bool cancelled{false};
    std::vector<std::wstring> installed;
    std::vector<bool> architectures;
    std::uint64_t now = 0;
    std::uint32_t delay = 0;
    int queryCount = 0, cancelledOperations = 0;
    bool fail = false, pending = false, omitRegistration = false, race = false, cancelOnWait = false;
    bool failQuery = false, throwPoll = false;
    struct Operation final : TaskbarFrameworkDeployment {
        Fixture& fixture;
        std::filesystem::path file;
        std::uint64_t started;
        Operation(Fixture& f, std::filesystem::path value) : fixture(f), file(std::move(value)), started(f.now) {}
        TaskbarFrameworkDeploymentResult Poll() override {
            if (fixture.throwPoll) throw std::runtime_error("injected poll failure");
            if (fixture.pending || fixture.now - started < fixture.delay) return {};
            if ((!fixture.fail || fixture.race) && !fixture.omitRegistration) {
                if (file.filename() == L"VCLibs.appx") fixture.status.vclibsVersion = TaskbarVCLibsMinimum;
                else fixture.status.winuiVersion = TaskbarWinUIMinimum;
            }
            return {fixture.fail ? TaskbarFrameworkDeploymentState::Failed : TaskbarFrameworkDeploymentState::Succeeded,
                    fixture.fail ? static_cast<std::int32_t>(0x80073CF9u) : 0, "simulated deployment"};
        }
        void Cancel() override { ++fixture.cancelledOperations; }
    };
    TaskbarFrameworkApi Api() {
        TaskbarFrameworkApi api;
        api.query = [this](bool arm64) {
            ++queryCount; architectures.push_back(arm64);
            if (failQuery) return TaskbarFrameworksStatus{0, 0, static_cast<std::int32_t>(0x80070005u), "query denied"};
            return status;
        };
        api.begin = [this](const std::filesystem::path& file) {
            Check(file.is_absolute(), "deployment always receives an absolute verified-payload path");
            installed.push_back(file.filename().wstring());
            return std::make_unique<Operation>(*this, file);
        };
        api.nowMilliseconds = [this] { return now; };
        api.waitMilliseconds = [this](std::uint32_t ms) { now += ms; if (cancelOnWait) cancelled = true; };
        return api;
    }
    Error Ensure(const std::filesystem::path& root, bool arm64 = false, TaskbarFrameworkTimings timing = {200, 10}) {
        return EnsureTaskbarFrameworks(root, arm64, cancelled, timing, Api());
    }
};
void UnitTests() {
    const auto root = TemporaryDirectory();
    { std::ofstream f(root / L"VCLibs.appx"); f << "test-only: fake deployment never loads this file"; }
    { std::ofstream f(root / L"Microsoft.UI.Xaml.2.8.appx"); f << "test-only"; }
    {
        Fixture f;
        Check(f.Ensure(root) == Error::None && f.status.Ready(), "install both missing frameworks and verify registration");
        Check(f.installed == std::vector<std::wstring>{L"VCLibs.appx", L"Microsoft.UI.Xaml.2.8.appx"},
              "install CRT before the WinUI framework that needs it");
    }
    {
        Fixture f; f.status.vclibsVersion = TaskbarPackageVersion(14, 9, 1, 0);
        Check(f.Ensure(root, true) == Error::None && f.installed == std::vector<std::wstring>{L"Microsoft.UI.Xaml.2.8.appx"},
              "ARM64 path installs only the missing framework and leaves a newer CRT alone");
        for (const bool arm64 : f.architectures) Check(arm64, "architecture is preserved through every query");
    }
    {
        Fixture f; f.status = {TaskbarPackageVersion(15, 0, 0, 0), TaskbarPackageVersion(9, 0, 0, 0)};
        Check(f.Ensure(root / L"absent") == Error::None && f.installed.empty(),
              "already-satisfied higher versions need neither payload files nor deployment");
    }
    {
        Fixture f; f.status = {TaskbarPackageVersion(14, 0, 33518, 65535), TaskbarWinUIMinimum};
        Check(f.Ensure(root) == Error::None && f.installed == std::vector<std::wstring>{L"VCLibs.appx"},
              "too-old CRT is repaired, exactly sufficient WinUI is retained");
    }
    {
        Fixture f; f.failQuery = true;
        Check(f.Ensure(root) == Error::FrameworkInstall && f.installed.empty(),
              "unknown registration status never triggers a speculative installation");
    }
    {
        Fixture f; f.fail = true;
        Check(f.Ensure(root) == Error::FrameworkInstall && f.installed.size() == 1 && !f.status.Ready(),
              "failed CRT deployment prevents WinUI deployment and core startup");
    }
    {
        Fixture f; f.omitRegistration = true;
        Check(f.Ensure(root) == Error::FrameworkInstall && f.installed.size() == 1,
              "reported deployment success without current-user registration is rejected");
    }
    {
        Fixture f; f.fail = true; f.race = true;
        Check(f.Ensure(root) == Error::None && f.status.Ready(),
              "a concurrent installer is accepted only after the required registration is verified");
    }
    {
        Fixture f; f.cancelled = true;
        Check(f.Ensure(root) == Error::Cancelled && f.queryCount == 0 && f.installed.empty(),
              "pre-cancelled work never queries or starts deployment");
    }
    {
        Fixture f; f.pending = true; f.cancelOnWait = true;
        Check(f.Ensure(root) == Error::Cancelled && f.cancelledOperations == 1 && f.installed.size() == 1,
              "cancellation reaches the current operation without beginning the second dependency");
    }
    {
        Fixture f; f.pending = true;
        Check(f.Ensure(root) == Error::FrameworkInstall && f.cancelledOperations == 1 && f.now == 200,
              "a stalled deployment is cancelled at the finite deadline");
    }
    {
        Fixture f; f.delay = 150;
        Check(f.Ensure(root) == Error::FrameworkInstall && f.installed.size() == 2 && f.now == 200,
              "two deployments share one deadline rather than each receiving a full timeout");
    }
    {
        Fixture f; f.throwPoll = true;
        Check(f.Ensure(root) == Error::FrameworkInstall && f.cancelledOperations == 1,
              "an exception while polling cancels the outstanding operation before returning");
    }
    {
        Fixture f;
        Check(f.Ensure(root / L"missing") == Error::FrameworkInstall && f.installed.empty(),
              "missing offline payload fails before any package operation");
    }
    Check(std::filesystem::equivalent(root.parent_path(), std::filesystem::temp_directory_path()), "cleanup stays inside owned temporary directory");
    std::filesystem::remove_all(root);
    std::cout << "taskbar framework dependency regressions passed; no real packages were deployed\n";
}
int ExplicitRuntime(const std::string& action, const std::filesystem::path& payload, bool arm64) {
    const auto root = TemporaryDirectory();
    diag::Initialize(root / L"logs");
    std::cout << "verification artifacts=" << root.u8string() << '\n';
    std::atomic_bool cancel{false};
    TaskbarPaths paths{"", payload, root / L"cache", true};
    Error error = Error::None;
    if (action == "--deploy") {
        std::filesystem::path executable;
        error = StageTaskbarPayload(paths, arm64, cancel, executable);
        if (error == Error::None)
            error = EnsureTaskbarFrameworks(executable.parent_path() / L"frameworks", arm64, cancel);
        std::cout << "verified deployment result=" << lume::taskbar::ErrorName(error) << '\n';
        PrintStatus(arm64);
    } else {
        // Explicit opt-in only: exercises the same production port without
        // changing persisted settings, auto-hide or taskbar visibility.
        WindowsTaskbarPort port(paths, cancel);
        lume::taskbar::Mode mode = lume::taskbar::Mode::Unknown;
        bool autoHideBefore = false, autoHideAfter = false;
        const auto before = port.ReadAutoHide(autoHideBefore);
        error = port.Acquire(true, mode);
        std::cout << "acquire=" << lume::taskbar::ErrorName(error) << " mode=" << lume::taskbar::ModeName(mode) << '\n';
        if (error == Error::None && (mode == lume::taskbar::Mode::Windows11X64 || mode == lume::taskbar::Mode::Windows11Arm64)) {
            error = port.StartCore();
            std::cout << "start=" << lume::taskbar::ErrorName(error) << " ready=" << port.CoreAlive() << '\n';
            if (error == Error::None) {
                Sleep(1500);
                if (!port.CoreAlive()) error = Error::CoreExited;
            }
            const auto stopped = port.StopCore(false);
            std::cout << "stop=" << lume::taskbar::ErrorName(stopped) << " ready=" << port.CoreAlive() << '\n';
            if (error == Error::None) error = stopped;
            if (error == Error::None) {
                // A previous-process recovery must remain possible at shutdown
                // when frameworks are already installed, even after cancellation.
                cancel = true;
                const auto recovered = port.StopCore(true);
                std::cout << "cancelled registered-framework recovery=" << lume::taskbar::ErrorName(recovered)
                          << " ready=" << port.CoreAlive() << '\n';
                if (recovered != Error::None) error = recovered;
            }
        } else if (error == Error::None) error = Error::Unsupported;
        port.Release();
        const auto after = port.ReadAutoHide(autoHideAfter);
        const bool unchanged = before == Error::None && after == Error::None && autoHideBefore == autoHideAfter;
        std::cout << "auto-hide unchanged=" << unchanged << '\n';
        if (!unchanged && error == Error::None) error = Error::ReadAutoHide;
    }
    return error == Error::None ? 0 : 1;
}
}  // namespace
int main(int count, char** args) {
    if (count == 1) { UnitTests(); return 0; }
    const std::string action = args[1];
    if (action == "--query" && (count == 2 || (count == 3 && std::string(args[2]) == "--arm64"))) {
        PrintStatus(count == 3);
        return 0;
    }
    if ((action == "--deploy" || action == "--live") &&
        (count == 3 || (count == 4 && std::string(args[3]) == "--arm64"))) {
        return ExplicitRuntime(action, std::filesystem::absolute(std::filesystem::u8path(args[2])), count == 4);
    }
    std::cerr << "Usage: taskbar-frameworks-test [--query [--arm64] | --deploy PINNED_PAYLOAD_ROOT [--arm64] | --live PINNED_PAYLOAD_ROOT]\n";
    return 2;
}
