#include "platform/windows/taskbar/taskbar_core_process.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include <string>
#include <windows.h>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <thread>
#include <utility>
#include <vector>

namespace lume::platform::windows {
using taskbar::Error;
Error TaskbarCoreDialogError(std::wstring_view text) {
    // The pinned core uses this fixed English stage label; the HRESULT is
    // stable even when Windows localizes its accompanying description.
    if (text.find(L"Failed to initialize XAML Diagnostics.") == std::wstring_view::npos)
        return Error::None;
    std::wstring lower(text);
    for (auto& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    constexpr std::wstring_view code = L"0x80070005";
    const auto at = lower.find(code);
    if (at == std::wstring::npos) return Error::None;
    const auto end = at + code.size();
    return end == lower.size() || !std::iswxdigit(lower[end]) ? Error::CoreAccessDenied : Error::None;
}
namespace {
HWND CoreWindow(DWORD pid) {
    struct Search { DWORD pid; HWND window; } search{pid, nullptr};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& found = *reinterpret_cast<Search*>(data); DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner != found.pid) return TRUE;
        wchar_t name[64]{}; GetClassNameW(window, name, 64);
        if (!_wcsicmp(name, L"TrayWindow")) { found.window = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
}
// The core reports a fatal error (for example "Failed to initialize XAML
// Diagnostics", 0x800401E3, when Explorer's taskbar XAML is not up yet) in a
// message box and exits once it is dismissed. Lume restarts the core with
// backoff, so the box is dismissed here instead of being left to the user.
void DismissCoreDialogs(DWORD pid, std::atomic<Error>& failure) {
    struct Search { DWORD pid; std::atomic<Error>& failure; bool found; } search{pid, failure, false};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(data); DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner != state.pid || !IsWindowVisible(window)) return TRUE;
        wchar_t name[16]{}; GetClassNameW(window, name, 16);
        if (wcscmp(name, L"#32770") != 0) return TRUE;
        // Read and classify before posting close: the message box can disappear
        // as soon as its owner handles that message.
        wchar_t message[2048]{};
        const auto text = GetDlgItem(window, 0xFFFF);
        if (text) GetWindowTextW(text, message, 2048);
        const auto error = TaskbarCoreDialogError(message);
        if (error != Error::None) state.failure = error;
        if (!state.found) {
            std::string utf8(8192, '\0');
            const int length = WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8.data(), 8192, nullptr, nullptr);
            utf8.resize(length > 0 ? static_cast<std::size_t>(length - 1) : 0);
            diag::Log("taskbar", "dismissed core error dialog: " + utf8);
        }
        PostMessageW(window, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        PostMessageW(window, WM_CLOSE, 0, 0);
        state.found = true;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
}
// Windows 11 hosts the taskbar in XAML; the core attaches to it through XAML
// Diagnostics, which fails until that island exists (sign-in, Explorer restart).
bool TaskbarXamlReady() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW version{}; version.dwOSVersionInfoSize = sizeof(version);
    const auto ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto get = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    if (!get || get(&version) != 0 || version.dwBuildNumber < 22000) return true;
    const auto tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    return tray && FindWindowExW(tray, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
}
}
struct TaskbarCoreProcess::Impl {
    Impl(const std::atomic_bool& cancellation, Prepare factory, TaskbarCoreTimings limits, TaskbarCoreProcessApi calls)
        : cancel(cancellation), prepare(std::move(factory)), timings(limits), api(calls) {}
    ~Impl() { Close(); }
    void StopGuard() {
        guardStop = true;
        if (guard.joinable()) guard.join();
        guardStop = false;
    }
    void StartGuard() {
        StopGuard();
        const auto watched = pid;
        HANDLE copy = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &copy, SYNCHRONIZE, FALSE, 0)) return;
        try {
            guard = std::thread([this, watched, copy] {
                while (!guardStop && WaitForSingleObject(copy, 500) == WAIT_TIMEOUT) {
                    if (api.readDialogFailure) {
                        const auto failure = api.readDialogFailure(watched);
                        if (failure != Error::None) reportedFailure = failure;
                    } else DismissCoreDialogs(watched, reportedFailure);
                }
                CloseHandle(copy);
            });
        } catch (...) { CloseHandle(copy); }
    }
    void Close() {
        StopGuard();
        if (process) { CloseHandle(process); process = nullptr; }
        if (job) { CloseHandle(job); job = nullptr; }
        pid = 0; ready = false; closeRequested = false;
    }
    bool Alive() const { return process && api.wait(process, 0) == WAIT_TIMEOUT; }
    void LogExit(const char* when) const {
        DWORD code = 0;
        const bool known = process && GetExitCodeProcess(process, &code);
        diag::Log("taskbar", std::string("core ") + when + ", code " + (known ? diag::ExitCodeText(code) : std::string("unknown")));
    }
    Error WaitReady(bool recovery) {
        ready = false;
        for (;;) {
            if (!recovery && cancel) return Error::Cancelled;
            const auto state = api.wait(process, 0);
            if (state == WAIT_OBJECT_0) { LogExit("exited before ready"); return FailureOr(Error::CoreExited); }
            if (state != WAIT_TIMEOUT) return Error::CoreStart;
            const auto elapsed = GetTickCount64() - launchedAt;
            if (reportedFailure == Error::None && CoreWindow(pid) && elapsed >= timings.readySettleMs) {
                ready = true; return Error::None;
            }
            if (elapsed >= timings.startTimeoutMs) { diag::Log("taskbar", "core not ready after " + std::to_string(elapsed) + " ms"); return FailureOr(Error::CoreStart); }
            const auto waited = api.wait(process, timings.waitSliceMs);
            if (waited == WAIT_OBJECT_0) { LogExit("exited before ready"); return FailureOr(Error::CoreExited); }
            if (waited != WAIT_TIMEOUT) return Error::CoreStart;
        }
    }
    Error FailureOr(Error fallback) const {
        const auto failure = reportedFailure.load();
        return failure == Error::None ? fallback : failure;
    }
    Error Start(bool recovery) {
        // A known access failure will also fail a recovery launch. Keep its
        // journal, and wait for an explicit user retry instead of another box.
        if (reportedFailure != Error::None) return reportedFailure.load();
        if (process) {
            const auto state = api.wait(process, 0);
            if (state == WAIT_TIMEOUT) {
                if (closeRequested) return Error::CoreStop;
                // A surviving process can still be waiting for initialization
                // or displaying an error. Reuse still checks readiness.
                return WaitReady(recovery);
            }
            // An unsuccessful wait is not evidence of process exit. Preserve
            // the actual handles and do not launch a second owned core.
            if (state != WAIT_OBJECT_0) return Error::CoreStart;
        }
        Close();
        if (!recovery && cancel) return Error::Cancelled;
        // Give Explorer's XAML taskbar time to appear rather than letting the
        // core fail on it; past the limit the core still starts and retries.
        for (const auto until = GetTickCount64() + XamlWaitMs; !TaskbarXamlReady() && GetTickCount64() < until;) {
            if (!recovery && cancel) return Error::Cancelled;
            Sleep(100);
        }
        if (!TaskbarXamlReady()) diag::Log("taskbar", "taskbar XAML not ready after wait; starting core anyway");
        TaskbarCoreLaunch launch;
        const auto prepared = prepare ? prepare(recovery, launch) : Error::MissingPayload;
        if (prepared != Error::None) return prepared;
        job = api.createJob(nullptr, nullptr);
        if (!job) return Error::CoreStart;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            Close(); return Error::CoreStart;
        }
        SIZE_T attributeBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
        if (!attributeBytes) { Close(); return Error::CoreStart; }
        std::vector<unsigned char> storage(attributeBytes);
        const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes)) {
            Close(); return Error::CoreStart;
        }
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                       &job, sizeof(job), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(attributes); Close(); return Error::CoreStart;
        }
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW; startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION created{};
        auto command = std::wstring(L"\"") + launch.executable.wstring() + L"\"";
        if (!launch.arguments.empty()) command += L" " + launch.arguments;
        // The kernel assigns the child to the kill-on-close job as part of
        // creation, including while suspended and before this call returns.
        const bool started = api.createProcess(launch.executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                            launch.executable.parent_path().c_str(), &startup.StartupInfo, &created) != FALSE;
        DeleteProcThreadAttributeList(attributes);
        if (!started) {
            Close(); return Error::CoreStart;
        }
        process = created.hProcess; pid = created.dwProcessId;
        const bool resumed = ResumeThread(created.hThread) != static_cast<DWORD>(-1);
        CloseHandle(created.hThread);
        if (!resumed) { CloseHandle(job); job = nullptr; return Error::CoreStart; }
        launchedAt = GetTickCount64();
        diag::Log("taskbar", std::string("core started, pid ") + std::to_string(pid) + (recovery ? " (recovery)" : ""));
        StartGuard();
        return WaitReady(recovery);
    }
    Error Stop(bool previous) {
        const auto state = process ? api.wait(process, 0) : WAIT_OBJECT_0;
        if (state != WAIT_OBJECT_0 && state != WAIT_TIMEOUT) return Error::CoreStop;
        if (process && state == WAIT_OBJECT_0) {
            if (!closeRequested) LogExit("exited unexpectedly");
            DWORD code = 1;
            const bool completed = closeRequested && GetExitCodeProcess(process, &code) && code == 0;
            Close();
            recoveryRequired = !completed;
            if (completed) return Error::None;
        }
        if (!process && (previous || recoveryRequired)) {
            if (reportedFailure != Error::None) return reportedFailure.load();
            const auto error = Start(true);
            if (error != Error::None) return error;
        }
        if (!process) return Error::None;
        if (!closeRequested) {
            const auto window = CoreWindow(pid);
            if (!window || !PostMessageW(window, WM_CLOSE, 0, 0)) return Error::CoreStop;
            closeRequested = true; ready = false;
        }
        if (api.wait(process, timings.stopTimeoutMs) != WAIT_OBJECT_0) return Error::CoreStop;
        DWORD code = 1;
        if (!GetExitCodeProcess(process, &code)) return Error::CoreStop;
        Close();
        recoveryRequired = code != 0;
        return code == 0 ? Error::None : Error::CoreStop;
    }
    const std::atomic_bool& cancel;
    Prepare prepare;
    TaskbarCoreTimings timings;
    TaskbarCoreProcessApi api;
    HANDLE process = nullptr, job = nullptr;
    DWORD pid = 0;
    ULONGLONG launchedAt = 0;
    bool ready = false, closeRequested = false, recoveryRequired = false;
    static constexpr ULONGLONG XamlWaitMs = 15000;
    std::thread guard;
    std::atomic_bool guardStop{false};
    std::atomic<Error> reportedFailure{Error::None};
};
TaskbarCoreProcess::TaskbarCoreProcess(const std::atomic_bool& cancel, Prepare prepare, TaskbarCoreTimings timings,
                                     TaskbarCoreProcessApi api)
    : impl_(std::make_unique<Impl>(cancel, std::move(prepare), timings, api)) {}
TaskbarCoreProcess::~TaskbarCoreProcess() = default;
Error TaskbarCoreProcess::Start(bool recovery) { return impl_->Start(recovery); }
void TaskbarCoreProcess::ResetFailure() { impl_->reportedFailure = Error::None; }
Error TaskbarCoreProcess::Stop(bool previous) { return impl_->Stop(previous); }
bool TaskbarCoreProcess::Alive() const { return impl_->Alive(); }
bool TaskbarCoreProcess::Ready() const { return impl_->reportedFailure == Error::None && impl_->ready && !impl_->closeRequested && impl_->Alive(); }
bool TaskbarCoreProcess::HasProcess() const { return impl_->process != nullptr; }
std::uintptr_t TaskbarCoreProcess::ExitSignal() const { return reinterpret_cast<std::uintptr_t>(impl_->process); }
}  // namespace lume::platform::windows
