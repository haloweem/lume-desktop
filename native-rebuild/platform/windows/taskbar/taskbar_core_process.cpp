#include "platform/windows/taskbar/taskbar_core_process.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "vendor/nlohmann/json.hpp"
#include <string>
#include <windows.h>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <limits>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace lume::platform::windows {
using taskbar::Error;
Error TaskbarCoreDialogError(std::wstring_view text) {
    // The upstream uses the same restart notice for both a pinned DLL that
    // cannot be replaced and an incompatible COM API. Preserve that distinction
    // in structured diagnostics, and never retry either through a timer.
    if (text.find(L"已更新。请重启 Windows") != std::wstring_view::npos ||
        text.find(L"has been updated. Please restart Windows") != std::wstring_view::npos)
        return Error::CoreRestartRequired;
    // Only call this for a dialog owned by the verified child. Unknown or
    // unreadable text is still a core failure, never permission to relaunch it.
    // The pinned core uses this fixed English stage label; the HRESULT is
    // stable even when Windows localizes its accompanying description.
    if (text.find(L"Failed to initialize XAML Diagnostics.") == std::wstring_view::npos)
        return Error::CoreFailure;
    std::wstring lower(text);
    for (auto& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    for (const auto& [code, error] : {
             std::pair{std::wstring_view(L"0x80070005"), Error::CoreAccessDenied},
             std::pair{std::wstring_view(L"0x80070102"), Error::CoreAttachTimeout}}) {
        const auto at = lower.find(code);
        if (at != std::wstring::npos) {
            const auto end = at + code.size();
            if (end == lower.size() || !std::iswxdigit(lower[end])) return error;
        }
    }
    return Error::CoreFailure;
}
TaskbarCoreDiagnostics ReadTaskbarCoreDiagnostics(const std::filesystem::path& path, DWORD corePid, DWORD explorerPid) {
    TaskbarCoreDiagnostics result;
    result.transparency.effectStatus = "pending";
    result.transparency.stage = "awaiting-core-report";
    if (path.empty() || !path.is_absolute() || !corePid) return result;
    try {
        auto component = path.root_path();
        for (const auto& part : path.relative_path()) {
            if (part == L".." || part == L".") return result;
            component /= part;
            const auto attributes = GetFileAttributesW(component.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return result;
        }
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0 || size > 65536) return result;
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return result;
        FILETIME now{}; GetSystemTimeAsFileTime(&now);
        const auto ticks = [](FILETIME value) {
            return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
        };
        const auto current = ticks(now), written = ticks(info.ftLastWriteTime);
        if (written > current + 10000000ULL || current > written + 50000000ULL) {
            result.transparency.stage = "core-report-stale"; return result;
        }
        std::ifstream stream(path, std::ios::binary);
        std::string bytes(static_cast<std::size_t>(size), '\0');
        if (!stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return result;
        const auto json = nlohmann::json::parse(bytes, nullptr, false);
        if (!json.is_object()) return result;
        const auto integer = [&](const char* key, std::uint64_t maximum) -> std::optional<std::uint64_t> {
            const auto it = json.find(key);
            if (it == json.end() || !it->is_number_integer() || it->get<std::int64_t>() < 0) return {};
            const auto value = it->get<std::uint64_t>();
            return value <= maximum ? std::optional<std::uint64_t>{value} : std::nullopt;
        };
        if (integer("schemaVersion", 1) != 1 || integer("pid", MAXDWORD) != corePid) return result;
        const auto reportedExplorer = integer("explorerPid", MAXDWORD);
        if (!reportedExplorer || (explorerPid && *reportedExplorer != explorerPid)) return result;
        const auto registered = integer("registeredTaskbars", 32), backgrounds = integer("backgrounds", 32), applied = integer("appliedTaskbars", 32);
        if (!registered || !backgrounds || !applied || *applied > *backgrounds || *backgrounds > *registered) return result;
        const auto effect = json.find("effectStatus"), stage = json.find("stage"), hr = json.find("hresult"), ready = json.find("serviceReady");
        if (effect == json.end() || !effect->is_string() || stage == json.end() || !stage->is_string() ||
            hr == json.end() || !hr->is_number_integer() || ready == json.end() || !ready->is_boolean()) return result;
        const auto name = effect->get<std::string>();
        if (name != "inactive" && name != "initializing" && name != "pending" && name != "applied" && name != "failed") return result;
        const auto signedHr = hr->get<std::int64_t>();
        if (signedHr < (std::numeric_limits<std::int32_t>::min)() || signedHr > (std::numeric_limits<std::int32_t>::max)()) return result;
        const auto& stageText = stage->get_ref<const std::string&>();
        if (stageText.size() > 96) return result;
        for (const auto c : stageText) {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '.' || c == ':' || c == '-')) return result;
        }
        const auto type = json.find("taskbarType");
        if (type != json.end()) {
            if (!type->is_string()) return result;
            const auto value = type->get<std::string>();
            if (value != "unknown" && value != "classic" && value != "mixed" && value != "xaml") return result;
            result.transparency.taskbarType = value;
        }
        result.transparency.effectStatus = name;
        result.transparency.stage = stage->get<std::string>();
        result.transparency.hresult = static_cast<std::int32_t>(signedHr);
        result.transparency.registeredTaskbars = static_cast<std::uint32_t>(*registered);
        result.transparency.backgrounds = static_cast<std::uint32_t>(*backgrounds);
        result.transparency.appliedTaskbars = static_cast<std::uint32_t>(*applied);
        result.serviceReady = ready->get<bool>();
        if (name == "applied" && (!result.serviceReady || !explorerPid || !*registered || *applied != *registered)) {
            result.transparency.effectStatus = "pending";
            result.transparency.stage = "invalid-effect-confirmation";
            return result;
        }
        if (name == "failed") {
            switch (static_cast<std::uint32_t>(signedHr)) {
                case 0x80070005: result.error = Error::CoreAccessDenied; break;
                case 0x80070102: result.error = Error::CoreAttachTimeout; break;
                case 0x80070666: // ERROR_PRODUCT_VERSION
                case 0x80070020: result.error = Error::CoreRestartRequired; break;
                default: result.error = result.serviceReady ? Error::CoreEffectUnconfirmed : Error::CoreFailure; break;
            }
        }
        result.valid = true;
    } catch (...) { /* An incomplete or replaced file is pending, never success. */ }
    return result;
}
namespace {
DWORD CurrentExplorerPid() {
    DWORD pid = 0;
    if (const auto window = FindWindowW(L"Shell_TrayWnd", nullptr)) GetWindowThreadProcessId(window, &pid);
    return pid;
}
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
// The core exits after its fatal dialog is dismissed. Remember that first
// dialog independently of its heartbeat, so no timer or recovery launch can
// show the same failure again before an explicit user retry.
void DismissCoreDialogs(DWORD pid, std::atomic<Error>& failure) {
    struct Search { DWORD pid; std::atomic<Error>& failure; } search{pid, failure};
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
        auto expected = Error::None;
        if (state.failure.compare_exchange_strong(expected, error)) {
            std::string utf8(8192, '\0');
            const int length = WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8.data(), 8192, nullptr, nullptr);
            utf8.resize(length > 0 ? static_cast<std::size_t>(length - 1) : 0);
            diag::Log("taskbar", "dismissed core error dialog: " + utf8);
        }
        PostMessageW(window, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        PostMessageW(window, WM_CLOSE, 0, 0);
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
                        if (failure != Error::None) {
                            auto expected = Error::None;
                            dialogFailure.compare_exchange_strong(expected, failure);
                        }
                    } else DismissCoreDialogs(watched, dialogFailure);
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
    Error UnexpectedExit() {
        CaptureFailure();
        // The user may dismiss the fatal dialog before the guard sees it.
        // An unexpected exit still ends this attempt; recovery must not create
        // another copy of that dialog just to restore the background.
        if (FailureOr(Error::None) == Error::None) reportedFailure = Error::CoreFailure;
        const auto failure = FailureOr(Error::CoreFailure);
        auto expected = Error::None;
        dialogFailure.compare_exchange_strong(expected, failure);
        return failure;
    }
    Error WaitReady(bool recovery) {
        ready = false;
        for (;;) {
            if (!recovery && cancel) return Error::Cancelled;
            const auto state = api.wait(process, 0);
            if (state == WAIT_OBJECT_0) { LogExit("exited before ready"); return UnexpectedExit(); }
            if (state != WAIT_TIMEOUT) return Error::CoreStart;
            const auto elapsed = GetTickCount64() - launchedAt;
            const auto report = CaptureFailure();
            if (const auto failure = FailureOr(Error::None); taskbar::RequiresExplicitRetry(failure))
                return failure;
            const bool serviceReady = report.valid && report.serviceReady;
            if (dialogFailure == Error::None &&
                (reportedFailure == Error::None || (serviceReady && !taskbar::RequiresExplicitRetry(reportedFailure.load()))) &&
                (diagnosticsFile.empty() || serviceReady) &&
                CoreWindow(pid) && elapsed >= timings.readySettleMs) {
                ready = true; return Error::None;
            }
            if (elapsed >= timings.startTimeoutMs) {
                diag::Log("taskbar", "core not ready after " + std::to_string(elapsed) + " ms");
                if (!diagnosticsFile.empty() && FailureOr(Error::None) == Error::None)
                    reportedFailure = Error::CoreAttachTimeout;
                return FailureOr(diagnosticsFile.empty() ? Error::CoreStart : Error::CoreAttachTimeout);
            }
            const auto waited = api.wait(process, timings.waitSliceMs);
            if (waited == WAIT_OBJECT_0) { LogExit("exited before ready"); return UnexpectedExit(); }
            if (waited != WAIT_TIMEOUT) return Error::CoreStart;
        }
    }
    Error FailureOr(Error fallback) const {
        const auto dialog = dialogFailure.load();
        if (dialog != Error::None) return dialog;
        const auto failure = reportedFailure.load();
        return failure == Error::None ? fallback : failure;
    }
    TaskbarCoreDiagnostics CaptureFailure() {
        const auto report = ReadTaskbarCoreDiagnostics(diagnosticsFile, pid, CurrentExplorerPid());
        // A query of an already connected background can fail temporarily and
        // recover on the next heartbeat. Do not turn it into a sticky launch
        // failure; the independent effect status still reports that failure.
        if (report.valid && !report.serviceReady && report.error != Error::None &&
            !taskbar::RequiresExplicitRetry(reportedFailure.load())) reportedFailure = report.error;
        else if (report.valid && report.serviceReady && !taskbar::RequiresExplicitRetry(reportedFailure.load())) reportedFailure = Error::None;
        return report;
    }
    Error Start(bool recovery) {
        // A first fatal dialog or failed attach blocks both ordinary launches
        // and restoration launches. Keep the journal for a later explicit retry.
        const auto failure = FailureOr(Error::None);
        if (dialogFailure != Error::None || taskbar::RequiresExplicitRetry(failure)) return failure;
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
            DWORD code = 1;
            if (!closeRequested || !GetExitCodeProcess(process, &code) || code != 0) return UnexpectedExit();
        }
        Close();
        reportedFailure = Error::None;
        if (!recovery && cancel) return Error::Cancelled;
        // Give Explorer's XAML taskbar time to appear rather than letting the
        // core fail on it; past the limit the core still starts and retries.
        for (const auto until = GetTickCount64() + timings.xamlWaitMs; !TaskbarXamlReady() && GetTickCount64() < until;) {
            if (!recovery && cancel) return Error::Cancelled;
            Sleep(100);
        }
        if (!TaskbarXamlReady()) diag::Log("taskbar", "taskbar XAML not ready after wait; starting core anyway");
        TaskbarCoreLaunch launch;
        const auto prepared = prepare ? prepare(recovery, launch) : Error::MissingPayload;
        if (prepared != Error::None) return prepared;
        diagnosticsFile = launch.diagnosticsFile;
        launchExplorerPid = CurrentExplorerPid();
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
        if (!diagnosticsFile.empty()) command += L" --lume-status-file \"" + diagnosticsFile.wstring() + L"\"";
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
            if (!completed) UnexpectedExit();
            Close();
            recoveryRequired = !completed;
            if (completed) return Error::None;
        }
        if (!process && (previous || recoveryRequired)) {
            const auto failure = FailureOr(Error::None);
            if (dialogFailure != Error::None || taskbar::RequiresExplicitRetry(failure)) return failure;
            const auto error = Start(true);
            if (error != Error::None) return error;
        }
        if (!process) return Error::None;
        if (dialogFailure != Error::None) return dialogFailure.load();
        if (!closeRequested) {
            const auto window = CoreWindow(pid);
            if (!window || !PostMessageW(window, WM_CLOSE, 0, 0)) return FailureOr(Error::CoreStop);
            closeRequested = true; ready = false;
        }
        if (api.wait(process, timings.stopTimeoutMs) != WAIT_OBJECT_0) return FailureOr(Error::CoreStop);
        DWORD code = 1;
        if (!GetExitCodeProcess(process, &code)) return Error::CoreStop;
        if (code != 0) UnexpectedExit();
        Close();
        recoveryRequired = code != 0;
        return code == 0 ? Error::None : FailureOr(Error::CoreStop);
    }
    const std::atomic_bool& cancel;
    Prepare prepare;
    TaskbarCoreTimings timings;
    TaskbarCoreProcessApi api;
    HANDLE process = nullptr, job = nullptr;
    DWORD pid = 0;
    DWORD launchExplorerPid = 0;
    std::filesystem::path diagnosticsFile;
    ULONGLONG launchedAt = 0;
    bool ready = false, closeRequested = false, recoveryRequired = false;
    std::thread guard;
    std::atomic_bool guardStop{false};
    std::atomic<Error> reportedFailure{Error::None};
    // Separate from reportedFailure: a later heartbeat or Explorer reload must
    // not erase a dialog or fatal exit (including a manually dismissed dialog).
    std::atomic<Error> dialogFailure{Error::None};
};
TaskbarCoreProcess::TaskbarCoreProcess(const std::atomic_bool& cancel, Prepare prepare, TaskbarCoreTimings timings,
                                     TaskbarCoreProcessApi api)
    : impl_(std::make_unique<Impl>(cancel, std::move(prepare), timings, api)) {}
TaskbarCoreProcess::~TaskbarCoreProcess() = default;
Error TaskbarCoreProcess::Start(bool recovery) { return impl_->Start(recovery); }
void TaskbarCoreProcess::ResetFailure() {
    // A user retry accepts the old failure, not another reading of the dead
    // child's report during Stop(true). Join its guard before clearing locks.
    if (impl_->process && impl_->api.wait(impl_->process, 0) == WAIT_OBJECT_0) impl_->Close();
    impl_->dialogFailure = Error::None;
    impl_->reportedFailure = Error::None;
}
Error TaskbarCoreProcess::Stop(bool previous) { return impl_->Stop(previous); }
bool TaskbarCoreProcess::Alive() const { return impl_->Alive(); }
bool TaskbarCoreProcess::Ready() const { return impl_->FailureOr(Error::None) == Error::None && impl_->ready && !impl_->closeRequested && impl_->Alive(); }
bool TaskbarCoreProcess::HasProcess() const { return impl_->process != nullptr; }
taskbar::TransparencyStatus TaskbarCoreProcess::Transparency() const {
    return ReadTaskbarCoreDiagnostics(impl_->diagnosticsFile, impl_->pid, CurrentExplorerPid()).transparency;
}
bool TaskbarCoreProcess::ReloadResolved() const {
    const auto current = CurrentExplorerPid();
    return impl_->dialogFailure == Error::None && impl_->reportedFailure == Error::CoreRestartRequired && impl_->launchExplorerPid &&
        current && current != impl_->launchExplorerPid;
}
std::uintptr_t TaskbarCoreProcess::ExitSignal() const { return reinterpret_cast<std::uintptr_t>(impl_->process); }
}  // namespace lume::platform::windows
