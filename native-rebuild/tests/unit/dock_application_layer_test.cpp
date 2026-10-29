// Runs the entire scenario process (including material worker threads) on an
// isolated desktop. No input-desktop switch or running Lume is involved.
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
    if (!ok) { std::fprintf(stderr, "FAIL: %s (Win32=%lu)\n", why, GetLastError()); std::fflush(stderr); std::exit(1); }
}
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_APP + 1) { DestroyWindow(window); PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
HWND Window(const wchar_t* name, const RECT& bounds, DWORD style = WS_POPUP, HWND owner = nullptr) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = Procedure; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = name;
    Check(RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "register fixture class");
    HWND window = CreateWindowExW(0, name, name, style, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top, owner, nullptr, wc.hInstance, nullptr);
    Check(window != nullptr, "create fixture window");
    return window;
}
void Pump(DWORD milliseconds = 0) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (milliseconds) Sleep(2);
    } while (GetTickCount64() < end);
}
template<class Predicate> bool WaitFor(Predicate predicate, DWORD timeout = 1500) {
    const auto end = GetTickCount64() + timeout;
    do { Pump(5); if (predicate()) return true; } while (GetTickCount64() < end);
    return predicate();
}
std::wstring DesktopName() {
    wchar_t name[256]{}; DWORD bytes = 0;
    Check(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, name, sizeof(name), &bytes),
          "read isolated desktop name");
    return std::wstring(L"WinSta0\\") + name;
}
struct Child {
    PROCESS_INFORMATION process{};
    HANDLE job = nullptr;
    Child(const wchar_t* mode, const std::wstring& desktop) {
        wchar_t executable[32768]{};
        Check(GetModuleFileNameW(nullptr, executable, 32768) != 0, "find fixture executable");
        std::wstring command = std::wstring(L"\"") + executable + L"\" " + mode;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.lpDesktop = const_cast<wchar_t*>(desktop.c_str());
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        job = CreateJobObjectW(nullptr, nullptr);
        Check(job != nullptr, "create fixture lifetime job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        Check(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)), "bound fixture lifetime");
        Check(CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process), "start isolated fixture process");
        Check(AssignProcessToJobObject(job, process.hProcess), "assign fixture lifetime job");
        Check(ResumeThread(process.hThread) != static_cast<DWORD>(-1), "resume fixture process");
    }
    ~Child() {
        if (job) CloseHandle(job);
        if (process.hThread) CloseHandle(process.hThread);
        if (process.hProcess) { WaitForSingleObject(process.hProcess, 1000); CloseHandle(process.hProcess); }
    }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
};
void BelowApplication(const DockWindow& dock, HWND app, const char* context) {
    Check(dock.LayersVisible(), "both native layers stay available");
    if (IsZoomed(app)) {
        // A maximized application hides the Dock instead of only covering it.
        Check(!IsWindowVisible(dock.Handle()) && !IsWindowVisible(dock.BarHandle()), "maximized application hides Dock");
        return;
    }
    Check(desktop_level::Above(app, dock.Handle()), context);
    Check(desktop_level::Above(app, dock.BarHandle()), "bar remains below application");
    Check(desktop_level::Above(dock.Handle(), dock.BarHandle()), "icons remain above bar");
    Check(!(GetWindowLongPtrW(dock.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST), "icons never become topmost");
    Check(!(GetWindowLongPtrW(dock.BarHandle(), GWL_EXSTYLE) & WS_EX_TOPMOST), "bar never becomes topmost");
}
HWND Surface(HWND owner) {
    struct Search { HWND owner, surface; } search{owner, nullptr};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(data);
        if (GetPropW(window, L"LumeGlassSurfaceOwner") == s.owner) { s.surface = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.surface;
}
// Explicit live probe for the material renderer; inactive isolated desktops
// cannot reliably produce WGC frames. No Explorer or existing app is moved.
int LiveMaterial() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize live probe COM");
    MONITORINFO info{}; info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
    RECT bounds{info.rcWork.left, info.rcWork.top, info.rcWork.left + 100, info.rcWork.top + 100};
    HWND manager = Window(L"LumeProbeLiveApplication", bounds);
    SetWindowPos(manager, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    const std::vector<Item> items{{Kind::Launchpad, {}, "Launchpad", false}, {Kind::Lume, {}, "Lume", false}};
    DockWindow dock(GetModuleHandleW(nullptr), {});
    Check(dock.Show(items, true), "show live material Dock");
    Check(WaitFor([&] {
        HWND surface = Surface(dock.BarHandle());
        return surface && IsWindowVisible(surface) && dock.MaterialState() == WidgetMaterialState::Ready;
    }, 5000), "live real material becomes ready and visible");
    unsigned samples = 0;
    for (int cycle = 0; cycle < 30; ++cycle) {
        const HWND foreground = GetForegroundWindow();
        if (cycle % 3 == 0) {
            ShowWindow(dock.Handle(), SW_HIDE);
            SendMessageW(dock.BarHandle(), desktop_level::Message(), 0, 0);
        } else Check(dock.Show(items, cycle % 2 == 0), "refresh live material");
        Check(GetForegroundWindow() == foreground, "live placement does not change foreground");
        BelowApplication(dock, manager, "live icons/bar stay below application");
        Pump(20);
        if (HWND surface = Surface(dock.BarHandle()); surface && IsWindowVisible(surface)) {
            ++samples;
            Check(desktop_level::Above(manager, surface) && desktop_level::Above(dock.BarHandle(), surface),
                  "visible rendered material stays below application and bar");
            Check(!(GetWindowLongPtrW(surface, GWL_EXSTYLE) & WS_EX_TOPMOST), "live glass never becomes topmost");
        }
    }
    Check(samples > 0, "visible live material was sampled");
    dock.Stop(); DestroyWindow(manager); CoUninitialize();
    std::printf("live Dock material layer passed: visible samples=%u\n", samples);
    return 0;
}
int Scenario() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize isolated COM");
    MONITORINFO info{}; info.cbSize = sizeof(info);
    Check(GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info), "read monitor");
    const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    HWND host = Window(L"LumeDockApplicationHost", info.rcMonitor);
    HWND view = Window(L"SHELLDLL_DefView", {0, 0, 1, 1}, WS_CHILD, host);
    ShowWindow(host, SW_SHOWNOACTIVATE);
    HWND browser = Window(L"LumeProbeBrowser", info.rcWork, WS_OVERLAPPEDWINDOW);
    ShowWindow(browser, SW_SHOWMAXIMIZED);
    SetWindowPos(host, HWND_TOP, 0, 0, 0, 0, flags);
    Check(desktop_level::Host() == host, "scenario uses its own desktop host");
    const std::vector<Item> items{{Kind::Launchpad, {}, "Launchpad", false}, {Kind::Lume, {}, "Lume", true},
                                  {Kind::Pinned, "C:\\fixture.exe", "Fixture", false}};
    DockWindow dock(GetModuleHandleW(nullptr), {});
    const HWND foreground = GetForegroundWindow();
    Check(dock.Show(items, true), "show Dock with raised desktop host");
    BelowApplication(dock, browser, "Dock stays below maximized browser despite raised desktop host");
    Check(GetForegroundWindow() == foreground, "initial placement preserves foreground");
    ShowWindow(browser, SW_HIDE);  // The external application below is the ceiling now.

    // A genuine different-process application exercises the same policy.
    Child external(L"--application", DesktopName());
    HWND app = nullptr;
    Check(WaitFor([&] { app = FindWindowW(L"LumeProbeExternalApplication", nullptr); return app != nullptr; }),
          "wait for different-process application");
    DWORD appPid = 0; GetWindowThreadProcessId(app, &appPid);
    Check(appPid == external.process.dwProcessId && appPid != GetCurrentProcessId(), "fixture is a separate process");
    SetWindowPos(app, HWND_BOTTOM, 0, 0, 0, 0, flags | SWP_SHOWWINDOW);
    // No Dock.Show or manual restack: WinEvents must repair this order.
    Check(WaitFor([&] { return desktop_level::Above(app, dock.Handle()); }), "application event repairs layer without refresh");
    BelowApplication(dock, app, "Dock follows external application ceiling");

    Check(WaitFor([&] { return Surface(dock.BarHandle()) != nullptr; }, 3000), "real asynchronous glass surface exists on test desktop");
    unsigned materialSamples = 0, visibleMaterialSamples = 0;
    for (int cycle = 0; cycle < 40; ++cycle) {
        ShowWindow(app, SW_RESTORE);
        if (cycle % 2 == 0) {
            SetWindowLongPtrW(app, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(app, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
                SWP_NOACTIVATE | SWP_FRAMECHANGED);
        } else {
            SetWindowLongPtrW(app, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            ShowWindow(app, SW_SHOWMAXIMIZED);
        }
        SetWindowPos(host, cycle % 3 ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0, flags);
        if (cycle % 4 == 0) {
            ShowWindow(dock.Handle(), SW_HIDE);
            // A message to the remaining bar must restore the whole pair.
            SendMessageW(dock.BarHandle(), desktop_level::Message(), 0, 0);
        } else {
            const HWND before = GetForegroundWindow();
            Check(dock.Show(items, cycle % 2 != 0), "background Dock refresh");
            Check(GetForegroundWindow() == before, "refresh never steals foreground");
        }
        BelowApplication(dock, app, "refresh/recovery stays below fullscreen/maximized external app");
        Pump(20);
        BelowApplication(dock, app, "asynchronous events preserve fullscreen ceiling");
        if (HWND glass = Surface(dock.BarHandle())) {
            ++materialSamples;
            Check(!(GetWindowLongPtrW(glass, GWL_EXSTYLE) & WS_EX_TOPMOST), "real glass is not topmost");
            if (IsWindowVisible(glass)) {
                ++visibleMaterialSamples;
                Check(desktop_level::Above(app, glass) && desktop_level::Above(dock.BarHandle(), glass),
                      "asynchronous real glass stays below bar and application");
            }
        }
    }
    // Existing implicit-raise guards must cover both windows.
    for (HWND window : {dock.Handle(), dock.BarHandle()}) {
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, flags);
        BelowApplication(dock, app, "implicit raise is rejected");
    }
    HWND manager = Window(L"LumeOriginalPageWindow", info.rcMonitor);
    ShowWindow(manager, SW_SHOWNOACTIVATE);
    HWND managerGlass = Window(L"LumeProbeManagerGlass", info.rcMonitor);
    SetPropW(managerGlass, L"LumeGlassSurfaceOwner", manager);
    SetWindowPos(managerGlass, manager, 0, 0, 0, 0, flags | SWP_SHOWWINDOW);
    ShowWindow(browser, SW_HIDE);
    ShowWindow(app, SW_HIDE);
    SetWindowPos(host, HWND_TOP, 0, 0, 0, 0, flags);
    Check(dock.Show(items, false), "refresh below own fullscreen music/manager window");
    BelowApplication(dock, managerGlass, "same-process manager and its glass are ceilings too");
    // Win+D may raise Explorer without minimizing every visible application.
    // Only desktop activation permits ignoring those covered app ceilings.
    SetWindowPos(host, HWND_TOP, 0, 0, 0, 0, flags);
    desktop_level::Watcher::ForegroundChanged(host);
    Check(desktop_level::DesktopForeground(host), "receive isolated Show Desktop foreground event");
    Check(WaitFor([&] { return IsWindowVisible(dock.Handle()) && desktop_level::Above(dock.BarHandle(), host); }),
          "desktop activation restores both Dock layers above raised host");
    Check(desktop_level::Above(dock.Handle(), dock.BarHandle()), "Show Desktop preserves Dock pair order");
    ShowWindow(browser, SW_SHOWMAXIMIZED);
    SetWindowPos(browser, host, 0, 0, 0, 0, flags);
    SendMessageW(dock.Handle(), desktop_level::Message(), 0, 0);
    Check(IsWindowVisible(dock.Handle()) && desktop_level::Above(dock.BarHandle(), host),
          "maximized window behind active desktop does not hide Dock");
    ShowWindow(browser, SW_HIDE);
    SetForegroundWindow(manager);
    desktop_level::Watcher::ForegroundChanged(manager);
    SetWindowPos(manager, HWND_TOP, 0, 0, 0, 0, flags);
    SendMessageW(dock.Handle(), desktop_level::Message(), 0, 0);
    Check(IsWindowVisible(dock.Handle()) && IsWindowVisible(dock.BarHandle()), "restored application retains visible Dock pair");
    BelowApplication(dock, manager, "switching back to application restores Dock ceiling");
    ShowWindow(manager, SW_HIDE); ShowWindow(managerGlass, SW_HIDE);

    SetWindowPos(host, HWND_BOTTOM, 0, 0, 0, 0, flags);
    ShowWindow(browser, SW_SHOWMAXIMIZED);
    Check(dock.Show(items, true), "show below maximized browser");
    ShowWindow(browser, SW_MINIMIZE);
    Check(WaitFor([&] { return desktop_level::Above(dock.Handle(), host); }), "minimize returns Dock to desktop");
    ShowWindow(browser, SW_RESTORE);
    SetWindowPos(browser, HWND_BOTTOM, 0, 0, 0, 0, flags);
    // Restored to maximized, the browser hides the Dock; otherwise it is the ceiling.
    Check(WaitFor([&] { return IsZoomed(browser) ? !IsWindowVisible(dock.Handle()) : desktop_level::Above(browser, dock.Handle()); }),
          "restore event reapplies application ceiling");
    ShowWindow(browser, SW_HIDE);
    ShowWindow(app, SW_SHOWNOACTIVATE);
    SetWindowPos(app, HWND_TOPMOST, 0, 0, 0, 0, flags);
    Check(dock.Show(items, false), "refresh with topmost application");
    BelowApplication(dock, app, "topmost application never makes Dock topmost");
    ShowWindow(app, SW_HIDE);

    HWND launchpad = Window(L"LumeDockLaunchpad", info.rcWork);
    SetWindowPos(launchpad, HWND_TOP, 0, 0, 0, 0, flags | SWP_SHOWWINDOW);
    HWND search = Window(L"LumeProbeLaunchpadSearch", {0, 0, 200, 40}, WS_POPUP | WS_VISIBLE, launchpad);
    Check(dock.Show(items, true), "show during launchpad interaction");
    Check(desktop_level::Above(dock.Handle(), launchpad) && desktop_level::Above(dock.Handle(), search),
          "launchpad and its owned search do not suppress Dock interaction");
    DestroyWindow(search); DestroyWindow(launchpad);
    DestroyWindow(view); DestroyWindow(host);
    ShowWindow(browser, SW_SHOWNOACTIVATE);
    Check(dock.Show(items, false), "refresh while desktop host is missing");
    BelowApplication(dock, browser, "missing host cannot raise Dock over application");
    host = Window(L"LumeDockApplicationHost", info.rcMonitor);
    view = Window(L"SHELLDLL_DefView", {0, 0, 1, 1}, WS_CHILD, host);
    ShowWindow(host, SW_SHOWNOACTIVATE);
    SetWindowPos(host, HWND_BOTTOM, 0, 0, 0, 0, flags);
    ShowWindow(browser, SW_HIDE);
    Check(WaitFor([&] { return desktop_level::Above(dock.Handle(), host); }), "recreated desktop host restores Dock");
    Check(dock.LayersVisible(), "Dock remains available on desktop");
    Check(materialSamples > 0, "real asynchronous surface was observed during fullscreen cycles");
    dock.Stop();
    Check(!dock.Visible() && !IsWindow(dock.Handle()), "stop retires windows and event hooks");
    Check(dock.Show(items, true), "restart reattaches layer watcher");
    dock.Stop();
    PostMessageW(app, WM_APP + 1, 0, 0);
    Check(WaitForSingleObject(external.process.hProcess, 1500) == WAIT_OBJECT_0, "fixture exits cleanly");
    DestroyWindow(browser); DestroyWindow(managerGlass); DestroyWindow(manager); DestroyWindow(view); DestroyWindow(host);
    CoUninitialize();
    // Inactive desktops may not produce WGC frames. Live glass visibility is
    // also covered by the existing material/widget layer packaging probes.
    std::printf("dock application layer passed: 40 fullscreen/maximized cycles, real surface samples=%u, visible=%u\n",
                materialSamples, visibleMaterialSamples);
    return 0;
}
}  // namespace
int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc > 1 && wcscmp(argv[1], L"--live-material") == 0) return LiveMaterial();
    if (argc > 1 && wcscmp(argv[1], L"--application") == 0) {
        Window(L"LumeProbeExternalApplication", {0, 0, 800, 600}, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
        return 0;
    }
    if (argc > 1 && wcscmp(argv[1], L"--scenario") == 0) return Scenario();
    const std::wstring name = L"LumeDockApplicationTest-" + std::to_wstring(GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    Check(desktop != nullptr, "create isolated desktop");
    DWORD code = 1;
    {
        Child scenario(L"--scenario", std::wstring(L"WinSta0\\") + name);
        Check(WaitForSingleObject(scenario.process.hProcess, 25000) == WAIT_OBJECT_0, "scenario finishes within timeout");
        Check(GetExitCodeProcess(scenario.process.hProcess, &code), "read scenario result");
    }
    CloseDesktop(desktop);
    return static_cast<int>(code);
}
