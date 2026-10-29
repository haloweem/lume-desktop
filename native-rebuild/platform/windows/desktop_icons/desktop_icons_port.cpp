#include "platform/windows/desktop_icons/desktop_icons_port.h"

#include <oleacc.h>
#include <shellapi.h>
#include <string>

namespace lume::platform::windows {
namespace {
constexpr wchar_t ShellViewClass[] = L"SHELLDLL_DefView";
constexpr wchar_t ListViewClass[] = L"SysListView32";

bool IsClass(HWND hwnd, const wchar_t* expected) {
    wchar_t actual[64]{};
    return hwnd && GetClassNameW(hwnd, actual, static_cast<int>(std::size(actual))) &&
           std::wstring_view(actual) == expected;
}

bool ChildOf(HWND parent, HWND child) {
    return parent == child || (parent && child && IsChild(parent, child));
}

DesktopIconsTarget TargetForShell(HWND shell) {
    DesktopIconsTarget target{};
    target.shell = shell;
    target.list = FindWindowExW(shell, nullptr, ListViewClass, nullptr);
    if (!target.list) {
        EnumChildWindows(
            shell,
            [](HWND child, LPARAM data) -> BOOL {
                if (!*reinterpret_cast<HWND*>(data) && IsClass(child, ListViewClass)) {
                    *reinterpret_cast<HWND*>(data) = child;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&target.list));
    }
    target.thread = GetWindowThreadProcessId(shell, &target.process);
    return target;
}

bool Cancelled(HANDLE handle) {
    return handle && WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
}

}  // namespace

bool ValidDesktopIconsTarget(const DesktopIconsTarget& target) {
    if (!IsClass(target.shell, ShellViewClass) || !IsClass(target.list, ListViewClass) ||
        !ChildOf(target.shell, target.list)) return false;
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(target.shell, &process);
    return thread != 0 && process == target.process && thread == target.thread &&
           GetWindowThreadProcessId(target.list, &process) == thread &&
           process == target.process;
}

std::vector<DesktopIconsTarget> FindDesktopIconsTargets() {
    struct Search { DWORD process = 0; std::vector<DesktopIconsTarget> targets; } search;
    const HWND shellWindow = GetShellWindow();
    if (!shellWindow || !GetWindowThreadProcessId(shellWindow, &search.process)) return {};
    EnumWindows(
        [](HWND hwnd, LPARAM data) -> BOOL {
            auto& output = *reinterpret_cast<Search*>(data);
            DWORD process = 0;
            if (!GetWindowThreadProcessId(hwnd, &process) || process != output.process ||
                (!IsClass(hwnd, L"Progman") && !IsClass(hwnd, L"WorkerW"))) return TRUE;
            HWND shell = FindWindowExW(hwnd, nullptr, ShellViewClass, nullptr);
            if (shell) {
                const auto target = TargetForShell(shell);
                if (target.process == output.process && ValidDesktopIconsTarget(target))
                    output.targets.push_back(target);
            }
            return output.targets.size() < 8;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.targets;
}

desktop_icons::Observation ObserveDesktopIcons(const DesktopIconsTarget& target) {
    desktop_icons::Observation output{};
    output.available = ValidDesktopIconsTarget(target);
    if (!output.available) return output;
    output.visible = IsWindowVisible(target.list) != FALSE;
    LASTINPUTINFO input{};
    input.cbSize = sizeof(input);
    output.inputKnown = GetLastInputInfo(&input) != FALSE;
    output.now = GetTickCount();
    output.lastInput = input.dwTime;
    POINT cursor{};
    GetCursorPos(&cursor);
    output.mouseX = cursor.x;
    output.mouseY = cursor.y;
    QUERY_USER_NOTIFICATION_STATE state{};
    output.fullscreen = FAILED(SHQueryUserNotificationState(&state)) ||
        state == QUNS_NOT_PRESENT || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_BUSY;
    return output;
}

DesktopHit HitDesktopIcons(const DesktopIconsTarget& target, POINT screenPoint) {
    if (!ValidDesktopIconsTarget(target)) return DesktopHit::Unavailable;
    HWND hit = WindowFromPoint(screenPoint);
    if (!hit || (!ChildOf(target.list, hit) && hit != target.shell)) return DesktopHit::Outside;
    if (!IsWindowVisible(target.list)) return hit == target.shell ? DesktopHit::Blank : DesktopHit::Outside;
    if (!ChildOf(target.list, hit)) return DesktopHit::Outside;
    // MSAA marshals the cross-process hit test. Passing a local LVHITTESTINFO to
    // Explorer is invalid; remote buffers also cannot be freed safely on timeout.
    IAccessible* accessible = nullptr;
    if (FAILED(AccessibleObjectFromWindow(target.list, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
                                         reinterpret_cast<void**>(&accessible))) || !accessible)
        return DesktopHit::Unavailable;
    VARIANT result;
    VariantInit(&result);
    const auto status = accessible->accHitTest(screenPoint.x, screenPoint.y, &result);
    DesktopHit classification = DesktopHit::Unavailable;
    if (status == S_FALSE) classification = DesktopHit::Outside;
    else if (SUCCEEDED(status)) {
        if (result.vt == VT_I4)
            classification = result.lVal == CHILDID_SELF ? DesktopHit::Blank : DesktopHit::Item;
        else if (result.vt == VT_DISPATCH) classification = DesktopHit::Item;
    }
    VariantClear(&result);
    accessible->Release();
    return ValidDesktopIconsTarget(target) ? classification : DesktopHit::Unavailable;
}

DesktopVisibilityResult SetDesktopIconsVisible(const DesktopIconsTarget& target,
                                               bool visible, HANDLE cancelled) {
    if (!ValidDesktopIconsTarget(target)) return DesktopVisibilityResult::Unavailable;
    if (Cancelled(cancelled)) return DesktopVisibilityResult::Cancelled;
    if ((IsWindowVisible(target.list) != FALSE) == visible) return DesktopVisibilityResult::Applied;
    if (target.thread == GetCurrentThreadId()) {
        ShowWindow(target.list, visible ? SW_SHOWNA : SW_HIDE);
    } else if (!ShowWindowAsync(target.list, visible ? SW_SHOWNA : SW_HIDE)) {
        return DesktopVisibilityResult::Failed;
    }
    // An accepted asynchronous request is not confirmation that Explorer applied it.
    const auto start = GetTickCount64();
    for (;;) {
        if (!ValidDesktopIconsTarget(target)) return DesktopVisibilityResult::Unavailable;
        if ((IsWindowVisible(target.list) != FALSE) == visible) return DesktopVisibilityResult::Applied;
        if (Cancelled(cancelled)) return DesktopVisibilityResult::Cancelled;
        if (GetTickCount64() - start >= 200) return DesktopVisibilityResult::Failed;
        if (cancelled) WaitForSingleObject(cancelled, 5);
        else Sleep(5);
    }
}

}  // namespace lume::platform::windows
