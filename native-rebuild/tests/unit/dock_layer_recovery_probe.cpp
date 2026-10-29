// Explicit probe (not in ctest): isolates the Dock/manager layer in its own
// desktop, so it never touches the running Lume. It reproduces the recovery
// path where Explorer has hidden the icon layer: the layer must reappear below
// a visible manager ceiling instead of being raised to the top by ShowWindow.
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include <objbase.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace lume::platform::windows;
using lume::dock::Item;
using lume::dock::Kind;

namespace {
void Check(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    return DefWindowProcW(window, message, w, l);
}
bool Register(const wchar_t* name, HINSTANCE instance) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = name;
    return RegisterClassW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
bool Above(HWND upper, HWND lower) {
    for (HWND window = GetWindow(upper, GW_HWNDNEXT); window; window = GetWindow(window, GW_HWNDNEXT))
        if (window == lower) return true;
    return false;
}
bool DirectlyAboveVisible(HWND upper, HWND lower) {
    HWND below = GetWindow(upper, GW_HWNDNEXT);
    while (below && !IsWindowVisible(below)) below = GetWindow(below, GW_HWNDNEXT);
    return below == lower;
}
}  // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // A separate desktop keeps EnumWindows()/ManagerCeiling() from seeing the
    // running Lume manager or the real Dock.
    HDESK desktop = CreateDesktopW(L"LumeDockLayerProbe", nullptr, nullptr, 0,
                                   GENERIC_ALL, nullptr);
    Check(desktop != nullptr, "create isolated desktop");
    Check(SetThreadDesktop(desktop), "switch probe thread to isolated desktop");
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    Check(Register(L"LumeDockLayerHost", instance), "register host class");
    Check(Register(L"SHELLDLL_DefView", instance), "register shell view class");
    Check(Register(L"LumeOriginalPageWindow", instance), "register manager class");

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT& work = monitor.rcWork;
    const RECT& screen = monitor.rcMonitor;

    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"LumeDockLayerHost", L"",
                                WS_POPUP, work.left, work.top, work.right - work.left, work.bottom - work.top,
                                nullptr, nullptr, instance, nullptr);
    Check(host != nullptr, "create synthetic desktop host");
    CreateWindowExW(0, L"SHELLDLL_DefView", L"", WS_CHILD, 0, 0, 1, 1, host, nullptr, instance, nullptr);
    ShowWindow(host, SW_SHOWNOACTIVATE);

    HWND manager = CreateWindowExW(0, L"LumeOriginalPageWindow", L"Lume probe manager",
                                   WS_POPUP | WS_VISIBLE, screen.left, screen.top,
                                   screen.right - screen.left, screen.bottom - screen.top,
                                   nullptr, nullptr, instance, nullptr);
    Check(manager != nullptr, "create synthetic fullscreen manager");
    ShowWindow(manager, SW_SHOW);
    SetWindowPos(manager, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Check(IsWindowVisible(manager) && !IsIconic(manager), "manager is visible and not minimized");

    std::vector<Item> items{{Kind::Launchpad, {}, "Launchpad", false},
                                  {Kind::Lume, {}, "Lume", true},
                                  {Kind::Pinned, "C:\\Apps\\a.exe", "A", false}};
    DockWindow dock(instance, {});
    Check(dock.Show(items, false), "show Dock in isolated desktop");
    HWND icons = dock.Handle();
    HWND bar = dock.BarHandle();
    Check(Above(manager, icons), "initial Dock icons are below the manager");
    Check(Above(icons, bar), "initial glass bar is below the icons");

    // Explorer can hide the icon layer while keeping the bar. Showing it with
    // ShowWindow(SW_SHOWNA) raises a hidden top-level to the top of the normal
    // band, above the manager; the recovery must preserve the layer below it.
    ShowWindow(icons, SW_HIDE);
    Check(!IsWindowVisible(icons), "icon layer is hidden");
    Check(dock.Show(items, false), "recover hidden Dock layers");
    Check(IsWindowVisible(icons) && IsWindowVisible(bar), "both Dock layers are visible again");
    Check(Above(manager, icons), "recovered icons remain below the manager");
    Check(Above(icons, bar) && DirectlyAboveVisible(icons, bar),
          "recovered bar remains directly below the icons");

    WINDOWPOS implicit{};
    implicit.hwnd = bar;
    SendMessageW(bar, WM_WINDOWPOSCHANGING, 0, reinterpret_cast<LPARAM>(&implicit));
    Check((implicit.flags & SWP_NOZORDER) != 0, "glass bar rejects implicit z-order changes");

    // An ordinary application has a different class and must have the same
    // protection as Lume's manager, even while Explorer raises its host.
    ShowWindow(manager, SW_HIDE);
    Check(Register(L"LumeProbeBrowser", instance), "register browser class");
    HWND browser = CreateWindowExW(0, L"LumeProbeBrowser", L"Browser fixture",
                                   WS_OVERLAPPEDWINDOW | WS_VISIBLE, work.left, work.top,
                                   work.right - work.left, work.bottom - work.top,
                                   nullptr, nullptr, instance, nullptr);
    Check(browser != nullptr, "create ordinary application");
    ShowWindow(browser, SW_SHOWMAXIMIZED);
    SetWindowPos(browser, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(host, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Check(dock.Show(items, false), "refresh Dock while desktop host is above browser");
    Check(Above(browser, icons), "Dock stays below maximized browser when desktop host rises");
    Check(Above(browser, bar), "glass bar stays below maximized browser");

    dock.Stop();
    DestroyWindow(browser);
    DestroyWindow(manager);
    DestroyWindow(host);
    CoUninitialize();
    CloseDesktop(desktop);
    std::printf("dock layer recovery passed\n");
    return 0;
}

