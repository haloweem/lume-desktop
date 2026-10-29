#include <windows.h>
#include <shellapi.h>
#include <cwchar>
#include <iostream>

namespace {
HWND tray = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(1));
UINT_PTR shellState = ABS_ALWAYSONTOP;
unsigned writes = 0;
bool refuseWrite = false;
bool validRequests = true;
HWND writtenWindow = nullptr;
int failures = 0;

HWND WINAPI TestFindWindow(LPCWSTR name, LPCWSTR) {
    return name && !std::wcscmp(name, L"Shell_TrayWnd") ? tray : nullptr;
}
UINT_PTR WINAPI TestAppBarMessage(DWORD message, PAPPBARDATA data) {
    if (!data || data->cbSize != sizeof(APPBARDATA)) {
        validRequests = false;
        return 0;
    }
    if (message == ABM_GETSTATE) return shellState;
    if (message == ABM_SETSTATE) {
        ++writes;
        writtenWindow = data->hWnd;
        // SETSTATE requires the target HWND and always returns TRUE. Only a
        // GETSTATE read-back can prove that Explorer accepted the requested bit.
        if (tray && data->hWnd == tray && !refuseWrite)
            shellState = static_cast<UINT_PTR>(data->lParam);
        return TRUE;
    }
    validRequests = false;
    return 0;
}
HANDLE WINAPI TestCreateMutex(LPSECURITY_ATTRIBUTES attributes, BOOL owner, LPCWSTR) {
    // Never contend with the running application's named ownership mutex.
    return CreateMutexW(attributes, owner, nullptr);
}
void Check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}

// Compile the production port against an isolated Shell API boundary. No
// message in this test reaches Explorer; no transparency core is launched.
#define FindWindowW TestFindWindow
#define SHAppBarMessage TestAppBarMessage
#define CreateMutexW TestCreateMutex
#include "platform/windows/taskbar/taskbar_port.cpp"
#undef CreateMutexW
#undef SHAppBarMessage
#undef FindWindowW

int main() {
    using namespace lume::platform::windows;
    using lume::taskbar::Error;
    std::atomic_bool cancelled{false};
    TaskbarPaths paths{};
    paths.allowSystemEffects = true;
    WindowsTaskbarPort port(paths, cancelled);
    Check(port.SetAutoHide(true) == Error::Busy && writes == 0,
          "unowned port cannot change taskbar state");
    lume::taskbar::Mode mode{};
    if (port.Acquire(false, mode) != Error::None) {
        std::cerr << "FAIL: acquire isolated ownership without loading a payload\n";
        return 1;
    }
    bool hidden = true;
    Check(port.ReadAutoHide(hidden) == Error::None && !hidden,
          "read current auto-hide without changing state");
    Check(port.SetAutoHide(true) == Error::None,
          "enable auto-hide through the actual taskbar window");
    Check(writtenWindow == tray,
          "ABM_SETSTATE receives the discovered taskbar HWND");
    Check(shellState == (ABS_AUTOHIDE | ABS_ALWAYSONTOP),
          "enabling auto-hide preserves always-on-top");
    shellState = ABS_AUTOHIDE | ABS_ALWAYSONTOP;
    Check(port.SetAutoHide(false) == Error::None && shellState == ABS_ALWAYSONTOP,
          "disable auto-hide while preserving unrelated state bits");
    shellState = ABS_ALWAYSONTOP;
    refuseWrite = true;
    Check(port.SetAutoHide(true) == Error::ApplyAutoHide && shellState == ABS_ALWAYSONTOP,
          "successful message return does not mask a rejected setting");
    refuseWrite = false;
    tray = nullptr;
    const auto previousWrites = writes;
    Check(port.ReadAutoHide(hidden) == Error::ReadAutoHide,
          "missing Explorer taskbar fails the read");
    Check(port.SetAutoHide(true) == Error::ApplyAutoHide && writes == previousWrites,
          "missing Explorer taskbar sends no mutation");
    Check(validRequests, "all appbar requests specify their structure size");
    TaskbarPaths disabled{};
    WindowsTaskbarPort protectedPort(disabled, cancelled);
    Check(protectedPort.SetAutoHide(true) == Error::SystemEffectsDisabled && writes == previousWrites,
          "system effects remain guarded");
    port.Release();
    Check(port.SetAutoHide(true) == Error::Busy && writes == previousWrites,
          "released port cannot mutate state");
    if (failures) return 1;
    std::cout << "taskbar Windows port auto-hide regressions passed; Explorer untouched\n";
}
