#include "platform/windows/installer_shutdown.h"
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace lume::platform::windows;
bool passed = true;
void Check(bool value, const char* reason) {
    if (!value) { passed = false; std::cerr << "FAIL: " << reason << '\n'; }
}
std::wstring Quoted(const fs::path& value) { return L"\"" + value.wstring() + L"\""; }
struct Child {
    HANDLE process = nullptr;
    Child() = default;
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    ~Child() {
        if (!process) return;
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process, 99);
            WaitForSingleObject(process, 3000);
        }
        CloseHandle(process);
    }
    void Start(const fs::path& executable, const std::wstring& arguments) {
        std::wstring command = Quoted(executable) + L" " + arguments;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(), &startup, &info))
            throw std::runtime_error("start isolated fixture");
        process = info.hProcess;
        CloseHandle(info.hThread);
    }
    bool Live() const { return WaitForSingleObject(process, 0) == WAIT_TIMEOUT; }
};
bool Ready(const fs::path& marker) {
    const auto until = GetTickCount64() + 5000;
    while (GetTickCount64() < until) {
        if (fs::exists(marker)) return true;
        Sleep(20);
    }
    return false;
}
int Fixture(const std::wstring& mode, const fs::path& marker) {
    if (mode == L"stubborn") {
        std::ofstream(marker) << "ready";
        Sleep(INFINITE);
        return 9;
    }
    WNDCLASSW klass{}; klass.lpfnWndProc = DefWindowProcW;
    klass.hInstance = GetModuleHandleW(nullptr); klass.lpszClassName = L"LumeNativeApplicationTray";
    if (!RegisterClassW(&klass)) return 3;
    const auto window = CreateWindowW(klass.lpszClassName, L"isolated", WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, klass.hInstance, nullptr);
    if (!window) return 4;
    std::ofstream(marker) << "ready";
    // Older shells consume the first WM_QUIT in WebView and retain a native loop.
    for (int loop = 0; loop < (mode == L"legacy" ? 2 : 1); ++loop) {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    DestroyWindow(window);
    std::ofstream(fs::path(marker.wstring() + L".clean")) << "clean";
    return 0;
}
void CopyFixture(const fs::path& self, const fs::path& target) {
    fs::create_directories(target.parent_path());
    fs::copy_file(self, target);
}
void Scenarios(const fs::path& self, const fs::path& helper, const fs::path& root) {
    const auto workerStarted = GetTickCount64();
    Check(RunInstallShutdownWorker(self, root, 600) == InstallShutdownResult::Failed,
        "outer watchdog rejects a stalled path-query worker");
    Check(fs::exists(root / L"worker.ready") && GetTickCount64() - workerStarted < 2000,
        "outer deadline covers a worker that never returns from its operation");
    DWORD workerId = 0;
    std::ifstream(root / L"worker.ready") >> workerId;
    const auto worker = OpenProcess(SYNCHRONIZE, FALSE, workerId);
    Check(workerId != 0 && (!worker || WaitForSingleObject(worker, 0) == WAIT_OBJECT_0),
        "timed-out worker does not remain running");
    if (worker) CloseHandle(worker);
    const auto abandoned = root / L"supervisor stopped";
    fs::create_directories(abandoned);
    Child supervisor;
    supervisor.Start(self, L"--supervisor " + Quoted(abandoned));
    Check(Ready(abandoned / L"worker.ready"), "supervised fixture becomes ready");
    workerId = 0;
    std::ifstream(abandoned / L"worker.ready") >> workerId;
    const auto supervisedWorker = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, workerId);
    Check(supervisedWorker != nullptr, "retain only the owned worker handle");
    Check(TerminateProcess(supervisor.process, 99) != FALSE, "simulate supervisor exiting unexpectedly");
    Check(WaitForSingleObject(supervisor.process, 2000) == WAIT_OBJECT_0, "isolated supervisor exited");
    if (supervisedWorker) {
        const bool exited = WaitForSingleObject(supervisedWorker, 2000) == WAIT_OBJECT_0;
        Check(exited, "supervisor exit closes the job and reclaims its worker");
        if (!exited) {
            TerminateProcess(supervisedWorker, 99);
            WaitForSingleObject(supervisedWorker, 2000);
        }
        CloseHandle(supervisedWorker);
    }
    const auto quotedDirectory = root / L"worker quoted \u6d4b\u8bd5";
    fs::create_directories(quotedDirectory);
    for (const auto code : {0, 10, 2, 3, 99}) {
        std::ofstream(quotedDirectory / L"worker.exit-code") << code;
        const auto expected = code == 0 ? InstallShutdownResult::Stopped :
            code == 10 ? InstallShutdownResult::Forced :
            code == 2 ? InstallShutdownResult::InvalidDirectory : InstallShutdownResult::Failed;
        Check(RunInstallShutdownWorker(self, fs::path(quotedDirectory.wstring() + L"\\"), 2000) == expected,
            "supervisor preserves known results, rejects unknown codes and quotes directory arguments");
    }
    const auto target = root / L"installed Lume";
    const auto foreign = root / L"installed Lume-other";
    CopyFixture(self, target / L"LumeShell.exe");
    CopyFixture(self, target / L"wallpaper-runtime" / L"mpv.exe");
    CopyFixture(self, foreign / L"LumeShell.exe");
    CopyFixture(self, foreign / L"mpv.exe");
    Child otherShell, otherPlayer;
    otherShell.Start(foreign / L"LumeShell.exe", L"--fixture stubborn " + Quoted(root / L"foreign-shell.ready"));
    otherPlayer.Start(foreign / L"mpv.exe", L"--fixture stubborn " + Quoted(root / L"foreign-player.ready"));
    Check(Ready(root / L"foreign-shell.ready") && Ready(root / L"foreign-player.ready"), "foreign fixtures ready");
    for (const auto* mode : {L"normal", L"legacy"}) {
        const auto marker = root / (std::wstring(mode) + L".ready");
        Child shell;
        shell.Start(target / L"LumeShell.exe", L"--fixture " + std::wstring(mode) + L" " + Quoted(marker));
        Check(Ready(marker), "owned shell fixture ready");
        const auto started = GetTickCount64();
        const auto result = StopInstalledLume(target / L".", {1200, 1000});
        Check(result == InstallShutdownResult::Stopped, "responsive and legacy loops exit without force");
        Check(!shell.Live() && fs::exists(marker.wstring() + L".clean"), "complete graceful fixture cleanup ran");
        Check(GetTickCount64() - started < 2600, "graceful operation is bounded");
        Check(otherShell.Live() && otherPlayer.Live(), "same-name outside-directory processes stay alive");
    }
    {
        Child shell, player;
        shell.Start(target / L"LumeShell.exe", L"--fixture stubborn " + Quoted(root / L"stubborn.ready"));
        player.Start(target / L"wallpaper-runtime" / L"mpv.exe", L"--fixture stubborn " + Quoted(root / L"player.ready"));
        Check(Ready(root / L"stubborn.ready") && Ready(root / L"player.ready"), "unresponsive fixtures ready");
        const auto started = GetTickCount64();
        Check(StopInstalledLume(target, {300, 1000}) == InstallShutdownResult::Forced, "unresponsive owned processes receive bounded fallback");
        Check(!shell.Live() && !player.Live(), "shell and installed player release their image files");
        Check(GetTickCount64() - started < 1800, "fallback shares one deadline instead of waiting per process");
        Check(otherShell.Live() && otherPlayer.Live(), "fallback does not terminate other Lume or mpv");
    }
    Check(StopInstalledLume(target) == InstallShutdownResult::Stopped, "repeat shutdown is idempotent");
    Check(StopInstalledLume(root / L"not-created") == InstallShutdownResult::Stopped, "first installation has no previous process");
    for (const auto& missing : {root / L"new Lume", root / L"new parent" / L"Lume \u6d4b\u8bd5"}) {
        for (const auto& path : {missing, missing / L".", fs::path(missing.wstring() + L"\\")}) {
            Check(StopInstalledLume(path) == InstallShutdownResult::Stopped,
                "absent install directory accepts installer dot and trailing separator forms");
            Check(RunInstallShutdownWorker(helper, path) == InstallShutdownResult::Stopped,
                "packaged worker accepts first-install directory before Inno creates it");
            Check(!fs::exists(missing), "pre-install shutdown does not create the install directory");
        }
    }
    Check(StopInstalledLume({}) == InstallShutdownResult::InvalidDirectory, "empty directory rejected");
    Check(StopInstalledLume(L".") == InstallShutdownResult::InvalidDirectory, "relative directory rejected");
    Check(StopInstalledLume(root.root_path()) == InstallShutdownResult::InvalidDirectory, "volume root rejected");
    Child cli;
    cli.Start(helper, L"--directory " + Quoted(target / L"."));
    Check(WaitForSingleObject(cli.process, 3000) == WAIT_OBJECT_0, "packaged helper command returns");
    DWORD code = 99;
    GetExitCodeProcess(cli.process, &code);
    Check(code == 0, "packaged helper accepts the explicit directory argument");
    Child invalid;
    invalid.Start(helper, L"--directory . --unexpected");
    Check(WaitForSingleObject(invalid.process, 3000) == WAIT_OBJECT_0, "invalid command does not hang");
    GetExitCodeProcess(invalid.process, &code);
    Check(code == 2, "unknown helper arguments are rejected");
    Check(otherShell.Live() && otherPlayer.Live(), "all checks preserve unrelated fixtures");
}
}
int main() {
    int count = 0;
    const auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 2;
    std::vector<std::wstring> values(args, args + count);
    LocalFree(args);
    if (values.size() == 3 && values[1] == L"--supervisor")
        return static_cast<int>(RunInstallShutdownWorker(fs::absolute(values[0]), values[2]));
    if (values.size() == 3 && values[1] == L"--worker") {
        int exitCode = 0;
        if (std::ifstream(fs::path(values[2]) / L"worker.exit-code") >> exitCode) return exitCode;
        std::ofstream(fs::path(values[2]) / L"worker.ready") << GetCurrentProcessId();
        Sleep(INFINITE);
        return 3;
    }
    if (values.size() == 4 && values[1] == L"--fixture") return Fixture(values[2], values[3]);
    if (values.size() != 2) return 2;
    const auto root = fs::current_path() /
        (L"installer-shutdown-fixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(root);
        Scenarios(fs::absolute(values[0]), fs::absolute(values[1]), root);
    } catch (const std::exception& error) {
        Check(false, error.what());
    }
    if (passed) std::cout << "PASS: isolated installer shutdown, legacy loops, timeout and path ownership\n";
    return passed ? 0 : 1;
}
