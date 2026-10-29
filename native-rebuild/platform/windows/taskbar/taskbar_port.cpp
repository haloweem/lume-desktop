#include "platform/windows/taskbar/taskbar_port.h"
#include "platform/windows/taskbar/taskbar_core_process.h"
#include "platform/windows/taskbar/taskbar_visibility.h"
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <cstring>
#include <cwchar>

namespace lume::platform::windows {
using taskbar::Error;
namespace {
template<class T> T Function(HMODULE module, const char* name) {
    const auto address = module ? GetProcAddress(module, name) : nullptr;
    T result = nullptr; static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result)); return result;
}
struct Accent { DWORD state, flags, color, animation; };
struct Attribute { int attribute; void* data; SIZE_T bytes; };
using Composition = BOOL (WINAPI*)(HWND, Attribute*);
std::uint64_t Created(HANDLE process) {
    FILETIME birth{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &birth, &exit, &kernel, &user)) return 0;
    return (static_cast<std::uint64_t>(birth.dwHighDateTime) << 32) | birth.dwLowDateTime;
}
bool Identity(HWND window, taskbar::Appearance& out) {
    wchar_t name[64]{};
    if (!GetClassNameW(window, name, 64) ||
        (wcscmp(name, L"Shell_TrayWnd") && wcscmp(name, L"Shell_SecondaryTrayWnd"))) return false;
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{}; DWORD size = 32768;
    const auto created = Created(process);
    const bool explorer = QueryFullProcessImageNameW(process, 0, path, &size) &&
        _wcsicmp(std::filesystem::path(path).filename().c_str(), L"explorer.exe") == 0;
    CloseHandle(process);
    if (!explorer || !created) return false;
    out.window = reinterpret_cast<std::uintptr_t>(window); out.process = pid; out.processCreated = created;
    return true;
}
bool Current(const taskbar::Appearance& saved) {
    taskbar::Appearance current;
    return Identity(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(saved.window)), current) &&
        taskbar::SameTarget(saved, current);
}
bool Conflict() {
    DWORD session = 0; ProcessIdToSessionId(GetCurrentProcessId(), &session);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry); bool conflict = false;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, L"TranslucentTB.exe") &&
            _wcsicmp(entry.szExeFile, L"LumeTaskbarCore.exe")) continue;
        DWORD candidate = 0;
        if (ProcessIdToSessionId(entry.th32ProcessID, &candidate) && candidate == session) {
            conflict = true; break;
        }
    }
    CloseHandle(snapshot); return conflict;
}
}
struct WindowsTaskbarPort::Impl {
    Impl(TaskbarPaths value, const std::atomic_bool& c) : paths(std::move(value)), cancel(c) {
        core = std::make_unique<TaskbarCoreProcess>(cancel, [this](bool recovery, TaskbarCoreLaunch& launch) {
            if (Conflict()) return Error::Conflict;
            std::atomic_bool recoveryCancellation{false};
            return StageTaskbarPayload(paths, mode == taskbar::Mode::Windows11Arm64,
                                       recovery ? recoveryCancellation : cancel, launch.executable);
        });
    }
    ~Impl() {
        // A failed normal shutdown deliberately leaves the durable ownership
        // journal intact. Job close is only final process containment.
        core.reset();
        Release();
    }
    void Release() {
        if (ownership) { ReleaseMutex(ownership); CloseHandle(ownership); ownership = nullptr; }
    }
    Error Acquire(bool transparency, taskbar::Mode& output) {
        if (!paths.allowSystemEffects) return Error::SystemEffectsDisabled;
        const bool alreadyOwned = ownership != nullptr;
        const auto fail = [&](Error error) { if (!alreadyOwned) Release(); return error; };
        if (!ownership) {
            HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\Lume.NativeTaskbar.Owner.v1");
            if (!mutex) return Error::Busy;
            const DWORD wait = WaitForSingleObject(mutex, 0);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) { CloseHandle(mutex); return Error::Busy; }
            ownership = mutex;
        }
        using Version = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
        RTL_OSVERSIONINFOW version{}; version.dwOSVersionInfoSize = sizeof(version);
        const auto read = Function<Version>(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
        if (!read || read(&version) != 0 || version.dwMajorVersion < 10) return fail(Error::Unsupported);
        SYSTEM_INFO info{}; GetNativeSystemInfo(&info);
        const bool arm64 = info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
        if (version.dwBuildNumber < 22000) mode = taskbar::Mode::Classic;
        else if (arm64) mode = taskbar::Mode::Windows11Arm64;
        else if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) mode = taskbar::Mode::Windows11X64;
        else return fail(Error::Unsupported);
        output = mode;
        if (transparency) {
            if (!core->HasProcess() && Conflict()) return fail(Error::Conflict);
            if (mode == taskbar::Mode::Classic) {
                get = Function<Composition>(GetModuleHandleW(L"user32.dll"), "GetWindowCompositionAttribute");
                set = Function<Composition>(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
                if (!get || !set) return fail(Error::Unsupported);
            } else {
                std::filesystem::path executable;
                const auto error = StageTaskbarPayload(paths, arm64, cancel, executable);
                if (error != Error::None) return fail(error);
            }
        }
        return Error::None;
    }
    Error StartCore() {
        if (!paths.allowSystemEffects) return Error::SystemEffectsDisabled;
        return core->Start();
    }
    Error StopCore(bool previous) {
        if (!paths.allowSystemEffects) return Error::SystemEffectsDisabled;
        return core->Stop(previous);
    }
    TaskbarPaths paths;
    const std::atomic_bool& cancel;
    taskbar::Mode mode = taskbar::Mode::Unknown;
    Composition get = nullptr, set = nullptr;
    HANDLE ownership = nullptr;
    std::unique_ptr<TaskbarCoreProcess> core;
};
WindowsTaskbarPort::WindowsTaskbarPort(TaskbarPaths p, const std::atomic_bool& c)
    : impl_(std::make_unique<Impl>(std::move(p), c)) {}
WindowsTaskbarPort::~WindowsTaskbarPort() = default;
Error WindowsTaskbarPort::Acquire(bool transparency, taskbar::Mode& mode) { return impl_->Acquire(transparency, mode); }
void WindowsTaskbarPort::Release() { impl_->Release(); }
Error WindowsTaskbarPort::ReadAutoHide(bool& value) {
    if (!FindWindowW(L"Shell_TrayWnd", nullptr)) return Error::ReadAutoHide;
    APPBARDATA data{}; data.cbSize = sizeof(data);
    value = (SHAppBarMessage(ABM_GETSTATE, &data) & ABS_AUTOHIDE) != 0;
    return Error::None;
}
Error WindowsTaskbarPort::SetAutoHide(bool value) {
    if (!impl_->paths.allowSystemEffects) return Error::SystemEffectsDisabled;
    if (!impl_->ownership) return Error::Busy;
    const auto window = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!window) return Error::ApplyAutoHide;
    APPBARDATA data{}; data.cbSize = sizeof(data); data.hWnd = window;
    const auto current = SHAppBarMessage(ABM_GETSTATE, &data);
    data.lParam = static_cast<LPARAM>(value ? current | ABS_AUTOHIDE : current & ~static_cast<UINT_PTR>(ABS_AUTOHIDE));
    SHAppBarMessage(ABM_SETSTATE, &data);
    data.lParam = 0;
    // Windows 11 applies the change asynchronously: an immediate read can still
    // report the old state although the taskbar does change. Wait briefly.
    for (int i = 0; i < 20; ++i) {
        if (((SHAppBarMessage(ABM_GETSTATE, &data) & ABS_AUTOHIDE) != 0) == value) return Error::None;
        if (impl_->cancel) return Error::Cancelled;
        Sleep(50);
    }
    return Error::ApplyAutoHide;
}
Error WindowsTaskbarPort::ReadVisibility(std::vector<taskbar::Visibility>& output) {
    struct State { std::vector<taskbar::Visibility> found; bool ok = true; } state;
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& state = *reinterpret_cast<State*>(data);
        taskbar::Visibility original;
        if (!Identity(window, original)) return TRUE;
        if (!taskbar_visibility::Capture(window, original) || state.found.size() >= taskbar::MaxTaskbars) {
            state.ok = false; return FALSE;
        }
        state.found.push_back(original); return TRUE;
    }, reinterpret_cast<LPARAM>(&state));
    if (!state.ok) return Error::ReadVisibility;
    if (state.found.empty()) return Error::Discover;
    output = std::move(state.found); return Error::None;
}
Error WindowsTaskbarPort::Hide(const taskbar::Visibility& original) {
    if (!impl_->paths.allowSystemEffects) return Error::SystemEffectsDisabled;
    if (!impl_->ownership) return Error::Busy;
    if (!Current(original)) return Error::ApplyVisibility;
    return taskbar_visibility::Hide(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(original.window)))
        ? Error::None : Error::ApplyVisibility;
}
taskbar::RestoreResult WindowsTaskbarPort::RestoreVisibility(const taskbar::Visibility& original) {
    if (!impl_->paths.allowSystemEffects || !impl_->ownership) return taskbar::RestoreResult::Failed;
    if (!Current(original)) return taskbar::RestoreResult::Gone;
    return taskbar_visibility::Restore(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(original.window)), original)
        ? taskbar::RestoreResult::Restored : taskbar::RestoreResult::Failed;
}
void WindowsTaskbarPort::RepairHidden() {
    if (impl_->paths.allowSystemEffects) taskbar_visibility::RepairOrphans();
}
Error WindowsTaskbarPort::ReadClassic(std::vector<taskbar::Appearance>& output) {
    struct State { Composition get; std::vector<taskbar::Appearance> found; Error error = Error::None; } state{impl_->get, {}, Error::None};
    if (!state.get) return Error::ReadAppearance;
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& state = *reinterpret_cast<State*>(data); taskbar::Appearance original;
        if (!Identity(window, original)) return TRUE;
        Accent accent{}; Attribute attribute{19, &accent, sizeof(accent)};
        if (!state.get(window, &attribute)) {
            // Win10 19045 accepts SetWindowCompositionAttribute for the
            // classic taskbar but rejects the corresponding read with
            // ERROR_INVALID_PARAMETER (87). A zero AccentPolicy is the
            // safe disabled baseline for restoration; every other error
            // remains a strict failure.
            if (GetLastError() != ERROR_INVALID_PARAMETER) {
                state.error = Error::ReadAppearance; return FALSE;
            }
            accent = {};
        }
        original.accent = accent.state; original.flags = accent.flags;
        original.color = accent.color; original.animation = accent.animation;
        state.found.push_back(original);
        return state.found.size() <= taskbar::MaxTaskbars;
    }, reinterpret_cast<LPARAM>(&state));
    if (state.error != Error::None) return state.error;
    if (state.found.empty() || state.found.size() > taskbar::MaxTaskbars) return Error::Discover;
    output = std::move(state.found); return Error::None;
}
Error WindowsTaskbarPort::MakeTransparent(const taskbar::Appearance& target) {
    if (!impl_->paths.allowSystemEffects) return Error::SystemEffectsDisabled;
    if (!impl_->ownership) return Error::Busy;
    if (!Current(target) || !impl_->set) return Error::ApplyAppearance;
    // ACCENT_ENABLE_TRANSPARENTGRADIENT uses the zero gradient color only with
    // flag 2; flag 0 falls back to the theme tint (same flags as TranslucentTB).
    Accent accent{2, 2, 0, 0}; Attribute attribute{19, &accent, sizeof(accent)};
    return impl_->set(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(target.window)), &attribute)
        ? Error::None : Error::ApplyAppearance;
}
taskbar::RestoreResult WindowsTaskbarPort::RestoreAppearance(const taskbar::Appearance& original) {
    if (!impl_->paths.allowSystemEffects || !impl_->ownership) return taskbar::RestoreResult::Failed;
    if (!Current(original)) return taskbar::RestoreResult::Gone;
    if (!impl_->set) impl_->set = Function<Composition>(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    Accent accent{original.accent, original.flags, original.color, original.animation};
    Attribute attribute{19, &accent, sizeof(accent)};
    return impl_->set && impl_->set(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(original.window)), &attribute)
        ? taskbar::RestoreResult::Restored : taskbar::RestoreResult::Failed;
}
Error WindowsTaskbarPort::StartCore() { return impl_->StartCore(); }
bool WindowsTaskbarPort::CoreAlive() const { return impl_->core->Ready(); }
Error WindowsTaskbarPort::StopCore(bool recover) { return impl_->StopCore(recover); }
std::uintptr_t WindowsTaskbarPort::ExitSignal() const { return impl_->core->ExitSignal(); }
}  // namespace lume::platform::windows
