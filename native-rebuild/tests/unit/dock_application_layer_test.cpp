// Runs the entire scenario process (including material worker threads) on an
// isolated desktop. No input-desktop switch or running Lume is involved.
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/dock/dock_sources.h"
#include "platform/windows/dock/dock_desktop_layer.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include <objbase.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace lume::platform::windows;
using lume::dock::Item;
using lume::dock::Kind;
namespace {
char* checkpoint=nullptr;
void Check(bool ok, const char* why) {
    if(checkpoint)strncpy_s(checkpoint,512,why,_TRUNCATE);
    if (!ok) { std::fprintf(stderr, "FAIL: %s (Win32=%lu)\n", why, GetLastError()); std::fflush(stderr); std::exit(1); }
}
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_APP + 1 || message == WM_CLOSE) { DestroyWindow(window); PostQuitMessage(0); return 0; }
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
    Check(IsWindowVisible(dock.Handle())&&IsWindowVisible(dock.BarHandle()),"ordinary applications cover Dock without hiding its windows");
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
struct IdleOrder {
    HWND icons, bar;
    unsigned changes = 0, inversions = 0, iconChanges = 0, barChanges = 0;
    static LRESULT CALLBACK Observe(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        const LRESULT result = DefSubclassProc(window, message, w, l);
        if (message == WM_WINDOWPOSCHANGED) {
            auto& self = *reinterpret_cast<IdleOrder*>(data);
            ++self.changes;
            if(window==self.icons)++self.iconChanges;
            if(window==self.bar)++self.barChanges;
            const HWND glass = Surface(self.bar);
            if (desktop_level::Above(self.bar, self.icons) ||
                (glass && IsWindowVisible(glass) && desktop_level::Above(glass, self.icons))) {
                ++self.inversions;
                if (self.inversions <= 6) {
                    const auto* p = reinterpret_cast<WINDOWPOS*>(l);
                    wchar_t after[80]{};
                    GetClassNameW(p->hwndInsertAfter, after, 80);
                    std::printf("inversion: changed=%s flags=%x after=%p barAbove=%d glassAbove=%d\n",
                        window == self.icons ? "icons" : "bar", p->flags, p->hwndInsertAfter,
                        desktop_level::Above(self.bar, self.icons), desktop_level::Above(glass, self.icons));
                    std::wprintf(L"  anchor=%ls\n", after);
                }
            }
        }
        return result;
    }
};
int LiveIdle(bool fixture, bool frosted = false) {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize idle probe COM");
    const HWND previousForeground = GetForegroundWindow();
    MONITORINFO info{}; info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
    const HWND host = fixture ? Window(L"LumeProbeIdleDesktop", info.rcWork) : desktop_level::Host();
    const HWND view = fixture ? Window(L"SHELLDLL_DefView", {0, 0, 1, 1}, WS_CHILD, host) : nullptr;
    if (fixture) { ShowWindow(host, SW_SHOW); SetForegroundWindow(host); }
    Check(WaitFor([&] { return desktop_level::IsDesktopForegroundWindow(GetForegroundWindow(), host); }, fixture ? 30000 : 0),
        "activate desktop before starting live idle probe");
    desktop_level::Watcher watcher;
    Check(watcher.Start(GetModuleHandleW(nullptr)), "start idle desktop watcher");
    desktop_level::Watcher::ForegroundChanged(host);
    DockWindow dock(GetModuleHandleW(nullptr), {});
    Check(dock.Show({{Kind::Launchpad, {}, "Launchpad", false}, {Kind::Lume, {}, "Lume", false}}, frosted), "show idle Dock");
    const bool ready = WaitFor([&] { auto glass = Surface(dock.BarHandle()); return glass && IsWindowVisible(glass); }, 5000);
    if (!ready) {
        std::printf("idle startup: material=%d iconsVisible=%d barVisible=%d desktop=%d\n",
            static_cast<int>(dock.MaterialState()), IsWindowVisible(dock.Handle()), IsWindowVisible(dock.BarHandle()),
            desktop_level::DesktopForeground(host));
        EnumWindows([](HWND w, LPARAM) -> BOOL {
            if (!IsWindowVisible(w)) return TRUE;
            wchar_t name[128]{}; GetClassNameW(w, name, 128);
            std::wprintf(L"visible: %ls zoomed=%d\n", name, IsZoomed(w)); return TRUE;
        }, 0);
    }
    Check(ready, "idle glass becomes visible");
    Pump(500);
    IdleOrder order{dock.Handle(), dock.BarHandle()};
    SetWindowSubclass(order.icons, IdleOrder::Observe, 1, reinterpret_cast<DWORD_PTR>(&order));
    SetWindowSubclass(order.bar, IdleOrder::Observe, 1, reinterpret_cast<DWORD_PTR>(&order));
    unsigned samples = 0, unstable = 0;
    const auto until = GetTickCount64() + 8000;
    do {
        Pump(5);
        ++samples;
        const HWND glass = Surface(order.bar);
        if (!IsWindowVisible(order.icons) || !IsWindowVisible(order.bar) || !IsWindowVisible(glass) ||
            !desktop_level::Above(order.icons, order.bar) || !desktop_level::Above(order.bar, glass)) ++unstable;
    } while (GetTickCount64() < until);
    RemoveWindowSubclass(order.icons, IdleOrder::Observe, 1);
    RemoveWindowSubclass(order.bar, IdleOrder::Observe, 1);
    std::printf("idle Dock: frosted=%d window changes=%u transient inversions=%u samples=%u unstable=%u\n",
        frosted, order.changes, order.inversions, samples, unstable);
    watcher.Stop(); dock.Stop();
    if (fixture) { DestroyWindow(view); DestroyWindow(host); if (IsWindow(previousForeground)) SetForegroundWindow(previousForeground); }
    CoUninitialize();
    Check(order.inversions == 0, "idle restacking never places material above icons");
    Check(unstable == 0, "live material and icons remain visible and ordered while idle");
    return 0;
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
    wchar_t checkpointName[100]{};
    GetEnvironmentVariableW(L"LUME_TEST_LAYER_CHECKPOINT",checkpointName,100);
    HANDLE checkpointMap=*checkpointName?OpenFileMappingW(FILE_MAP_WRITE,FALSE,checkpointName):nullptr;
    if(checkpointMap)checkpoint=static_cast<char*>(MapViewOfFile(checkpointMap,FILE_MAP_WRITE,0,0,512));
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
    window_layer::desktopForeground=[] { return desktop_level::DesktopForeground(); };
    const std::vector<Item> items{{Kind::Launchpad, {}, "Launchpad", false}, {Kind::Lume, {}, "Lume", true},
                                  {Kind::Pinned, "C:\\fixture.exe", "Fixture", false}};
    DockWindow dock(GetModuleHandleW(nullptr), {});
    const HWND foreground = GetForegroundWindow();
    Check(dock.Show(items, true), "show Dock with raised desktop host");
    BelowApplication(dock, browser, "Dock stays below maximized browser despite raised desktop host");
    Check(GetForegroundWindow() == foreground, "initial placement preserves foreground");
    HWND settings=Window(L"LumeProbeWindowedSettings",{100,100,700,600},WS_OVERLAPPEDWINDOW);
    ShowWindow(settings,SW_SHOW);SetForegroundWindow(settings);
    desktop_level::Watcher::ForegroundChanged(settings);
    SendMessageW(dock.Handle(),desktop_level::Message(),0,0);
    Check(IsWindowVisible(dock.Handle())&&IsWindowVisible(dock.BarHandle()),
          "windowed foreground with a maximized browser in background never hides Dock");
    BelowApplication(dock,settings,"exposed desktop Dock stays beneath windowed settings");
    ShowWindow(settings,SW_HIDE);DestroyWindow(settings);
    ShowWindow(browser, SW_HIDE);  // The external application below is the ceiling now.

    // A genuine different-process application exercises the same policy.
    Child external(L"--application", DesktopName());
    HWND app = nullptr;
    Check(WaitFor([&] { app = FindWindowW(L"LumeProbeExternalApplication", nullptr); return app != nullptr; }),
          "wait for different-process application");
    DWORD appPid = 0; GetWindowThreadProcessId(app, &appPid);
    Check(appPid == external.process.dwProcessId && appPid != GetCurrentProcessId(), "fixture is a separate process");
    const auto listed = [&] {
        for (const auto& window : dock_sources::ProgramWindows())
            if (window.handle == reinterpret_cast<std::uint64_t>(app)) return true;
        return false;
    };
    // The HWND can be found while CreateWindowEx is still completing its
    // initial show. Wait for the actual taskbar provider to observe it.
    Check(WaitFor(listed), "open taskbar application is enumerated");
    SetWindowLongPtrW(app, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(host));
    Check(!listed(), "ordinary owned popup is excluded");
    SetWindowLongPtrW(app, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_NOACTIVATE);
    Check(listed(), "explicit taskbar window is included despite owner and noactivate");
    SetWindowLongPtrW(app, GWL_EXSTYLE, WS_EX_TOOLWINDOW);
    Check(!listed(), "tool window remains excluded");
    SetWindowLongPtrW(app, GWL_EXSTYLE, 0);
    SetWindowLongPtrW(app, GWLP_HWNDPARENT, 0);
    ShowWindow(app, SW_HIDE);
    Check(!listed(), "hidden application has no running entry");
    ShowWindow(app, SW_SHOWMINNOACTIVE);
    Check(listed(), "minimized application retains its running entry");
    ShowWindow(app, SW_RESTORE);
    SetWindowPos(app, HWND_BOTTOM, 0, 0, 0, 0, flags | SWP_SHOWWINDOW);
    // No Dock.Show or manual restack: WinEvents must repair this order.
    Check(WaitFor([&] { return desktop_level::Above(app, dock.Handle()); }), "application event repairs layer without refresh");
    BelowApplication(dock, app, "Dock follows external application ceiling");

    // Use an ordinary application above the desktop, before the later
    // Show Desktop scenarios deliberately raise the host over applications.
    SetWindowPos(host,HWND_BOTTOM,0,0,0,0,flags|SWP_SHOWWINDOW);
    SetWindowPos(app,HWND_TOP,0,0,0,0,flags|SWP_SHOWWINDOW);
    desktop_level::Watcher::ForegroundChanged(app);
    SendMessageW(dock.Handle(),DockDesktopLayer::Message,0,0);
    BelowApplication(dock,app,"navigation fixture begins below the real application");
    Check(desktop_level::Above(dock.BarHandle(),host),"navigation fixture begins above the desktop host");
    // Explorer's navigation sink can briefly be visible and nonempty below
    // the desktop. Old ceiling scans selected it as the last application.
    HWND sink=Window(L"CtrlNotifySink",{10,10,110,110},WS_POPUP|WS_VISIBLE);
    SetWindowPos(sink,HWND_BOTTOM,0,0,0,0,flags|SWP_SHOWWINDOW);
    RECT sinkBounds{};
    Check(IsWindowVisible(sink)&&GetWindowRect(sink,&sinkBounds)&&!IsRectEmpty(&sinkBounds)&&
          desktop_level::Above(app,host)&&desktop_level::Above(host,sink),
          "navigation sink is visible and nonempty below both the real app and desktop");
    Check(desktop_level::ApplicationCeiling(host)==app,"shared ceiling ignores navigation sink and retains the real application");
    SendMessageW(dock.Handle(),DockDesktopLayer::Message,0,0);
    BelowApplication(dock,app,"navigation sink cannot move Dock above the real application");
    Check(desktop_level::Above(dock.BarHandle(),host)&&GetWindow(dock.Handle(),GW_HWNDNEXT)==dock.BarHandle(),
          "navigation sink cannot move Dock below desktop or split its pair");
    DestroyWindow(sink);

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
    // Simulate Show Desktop's own minimization on this isolated desktop. A
    // normal application must not restore the pair; a desktop transition does.
    desktop_level::Watcher::ForegroundChanged(manager);
    ShowWindow(dock.Handle(),SW_MINIMIZE);ShowWindow(dock.BarHandle(),SW_MINIMIZE);
    SendMessageW(dock.Handle(),desktop_level::Message(),0,0);
    Check(IsIconic(dock.Handle())&&IsIconic(dock.BarHandle()),
          "ordinary application foreground does not unminimize desktop Dock layers");
    SetWindowPos(host,HWND_TOP,0,0,0,0,flags);
    desktop_level::Watcher::ForegroundChanged(host);
    Check(WaitFor([&] { return dock.LayersVisible()&&!IsIconic(dock.Handle())&&!IsIconic(dock.BarHandle()); }),
          "desktop activation unminimizes both Dock layers without toggling the component");
    Check(desktop_level::Above(dock.Handle(),dock.BarHandle())&&desktop_level::Above(dock.BarHandle(),host),
          "unminimized Dock retains pair order above the desktop host");
    ShowWindow(dock.Handle(),SW_HIDE);ShowWindow(dock.BarHandle(),SW_HIDE);
    desktop_level::RequestAll();
    Check(WaitFor([&] { return dock.LayersVisible(); }),
          "desktop restack restores a system-hidden Dock pair even when both layers were hidden");
    Check(!(GetWindowLongPtrW(dock.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&
          !(GetWindowLongPtrW(dock.BarHandle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
          "minimized and hidden recovery never promotes Dock to topmost");
    // A taskbar/overlay can be the first non-Lume window above the desktop.
    // Inserting after its HWND would silently promote the Dock to topmost;
    // the next idle sweep demotes each layer separately and exposes glass.
    HWND topmost = Window(L"LumeProbeTopmostOverlay", {0, 0, 100, 40});
    SetWindowPos(topmost, HWND_TOPMOST, 0, 0, 0, 0, flags | SWP_SHOWWINDOW);
    IdleOrder idle{dock.Handle(), dock.BarHandle()};
    SetWindowSubclass(idle.icons, IdleOrder::Observe, 1, reinterpret_cast<DWORD_PTR>(&idle));
    SetWindowSubclass(idle.bar, IdleOrder::Observe, 1, reinterpret_cast<DWORD_PTR>(&idle));
    // Settle the new overlay's band once before measuring redundant requests.
    // No message pumping in this loop: genuine app/animation transitions are
    // tested separately below and must not be mistaken for no-op restacks.
    SendMessageW(dock.Handle(),desktop_level::Message(),0,0);
    Check(IsWindowVisible(idle.icons)&&IsWindowVisible(idle.bar)&&
          GetWindow(idle.icons,GW_HWNDNEXT)==idle.bar&&desktop_level::Above(idle.bar,host),
          "repeated restack fixture begins visible and correctly ordered above desktop");
    const HWND stableAbove=GetWindow(idle.icons,GW_HWNDPREV);
    const HWND stableBelow=GetWindow(idle.bar,GW_HWNDNEXT);
    idle.changes=idle.iconChanges=idle.barChanges=0;
    for (int cycle = 0; cycle < 20; ++cycle) {
        SendMessageW(dock.Handle(), desktop_level::Message(), 0, 0);
        SendMessageW(dock.Handle(), DockDesktopLayer::Message, 0, 0);
        Check(!(GetWindowLongPtrW(dock.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) &&
              !(GetWindowLongPtrW(dock.BarHandle(), GWL_EXSTYLE) & WS_EX_TOPMOST),
              "idle desktop restack never inherits overlay topmost band");
        Check(desktop_level::Above(dock.BarHandle(), host), "idle Dock stays above desktop");
        Check(IsWindowVisible(idle.icons)&&IsWindowVisible(idle.bar)&&
              GetWindow(idle.icons,GW_HWNDNEXT)==idle.bar&&
              GetWindow(idle.icons,GW_HWNDPREV)==stableAbove&&GetWindow(idle.bar,GW_HWNDNEXT)==stableBelow,
              "repeated desktop and event restacks preserve the visible pair and surrounding order");
    }
    if(idle.iconChanges||idle.barChanges)
        std::printf("redundant restack position messages: icons=%u bar=%u\n",idle.iconChanges,idle.barChanges);
    Check(idle.iconChanges==0&&idle.barChanges==0,
          "correctly ordered repeated restacks send no unnecessary icon or bar WM_WINDOWPOSCHANGED");
    DestroyWindow(topmost);
    // The following case tests a normal application's ceiling, not the
    // fullscreen manager (which covers the Dock in the normal application band).
    ShowWindow(manager,SW_HIDE);ShowWindow(managerGlass,SW_HIDE);
    // Also exercise a downward repair: an application can reinsert itself
    // below the entire Dock group without activating or moving the pointer.
    HWND lower = Window(L"LumeProbeLoweredApplication", {0, 0, 100, 40});
    for (int cycle = 0; cycle < 10; ++cycle) {
        SetWindowPos(lower, HWND_BOTTOM, 0, 0, 0, 0, flags | SWP_SHOWWINDOW);
        desktop_level::Watcher::ForegroundChanged(lower);
        SendMessageW(dock.Handle(), desktop_level::Message(), 0, 0);
        Check(desktop_level::Above(lower, dock.Handle()), "lowered application remains a ceiling");
        ShowWindow(lower, SW_HIDE);
        desktop_level::Watcher::ForegroundChanged(host);
        SendMessageW(dock.Handle(), desktop_level::Message(), 0, 0);
    }
    DestroyWindow(lower);
    RemoveWindowSubclass(idle.icons, IdleOrder::Observe, 1);
    RemoveWindowSubclass(idle.bar, IdleOrder::Observe, 1);
    Check(idle.inversions == 0, "idle and downward restacks never cross icon and glass layers");
    ShowWindow(browser, SW_SHOWMAXIMIZED);
    SetWindowPos(browser, host, 0, 0, 0, 0, flags);
    SendMessageW(dock.Handle(), desktop_level::Message(), 0, 0);
    Check(IsWindowVisible(dock.Handle()) && desktop_level::Above(dock.BarHandle(), host),
          "maximized window behind active desktop does not hide Dock");
    ShowWindow(browser, SW_HIDE);
    // Full-monitor passive layered tools (GPU HUD/cursor overlays) are not
    // fullscreen applications. Their bounds may span every display.
    MONITORINFO overlayMonitor{sizeof(overlayMonitor)};
    Check(GetMonitorInfoW(MonitorFromWindow(dock.BarHandle(),MONITOR_DEFAULTTOPRIMARY),&overlayMonitor),"read overlay monitor");
    HWND passive=Window(L"LumeProbePassiveOverlay",overlayMonitor.rcMonitor);
    for(const DWORD extra:{DWORD{0},DWORD{WS_EX_TRANSPARENT}}){
        SetWindowLongPtrW(passive,GWL_EXSTYLE,WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|extra);
        SetLayeredWindowAttributes(passive,0,1,LWA_ALPHA);
        SetWindowPos(passive,HWND_TOP,0,0,0,0,flags|SWP_SHOWWINDOW);
        SendMessageW(dock.Handle(),desktop_level::Message(),0,0);
        Check(IsWindowVisible(dock.Handle())&&IsWindowVisible(dock.BarHandle()),
              "passive full-monitor overlay must not hide Dock");
    }
    ShowWindow(passive,SW_HIDE);DestroyWindow(passive);
    SetForegroundWindow(manager);
    desktop_level::Watcher::ForegroundChanged(manager);
    SetWindowPos(manager, HWND_TOP, 0, 0, 0, 0, flags | SWP_SHOWWINDOW);
    SendMessageW(dock.Handle(), desktop_level::Message(), 0, 0);
    Check(IsWindowVisible(dock.Handle()) && IsWindowVisible(dock.BarHandle()), "own manager covers but never hides the Dock pair");
    BelowApplication(dock, manager, "switching back to application restores Dock ceiling");
    ShowWindow(manager, SW_HIDE); ShowWindow(managerGlass, SW_HIDE);

    SetWindowPos(host, HWND_BOTTOM, 0, 0, 0, 0, flags);
    ShowWindow(browser, SW_SHOWMAXIMIZED);
    Check(dock.Show(items, true), "show below maximized browser");
    ShowWindow(browser, SW_MINIMIZE);
    Check(WaitFor([&] { return desktop_level::Above(dock.Handle(), host); }), "minimize returns Dock to desktop");
    ShowWindow(browser, SW_RESTORE);
    SetWindowPos(browser, HWND_BOTTOM, 0, 0, 0, 0, flags);
    // Maximized or restored, ordinary applications stay above a visible Dock.
    Check(WaitFor([&] { return IsWindowVisible(dock.Handle())&&IsWindowVisible(dock.BarHandle())&&desktop_level::Above(browser, dock.Handle()); }),
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
    {
        using namespace window_layer;
        Context policy;contextReader=[&]{return policy;};Start();
        Check(WaitFor([&]{return Surface(dock.BarHandle())!=nullptr;},3000),
              "shell transition fixture waits for its asynchronous material worker");
        const HWND retainedGlass=Surface(dock.BarHandle());
        Check(retainedGlass!=nullptr,"shell transition fixture starts with a live material worker");
        for(unsigned cycle=0;cycle<12;++cycle){
            // Grouped taskbar previews can cover a monitor and inherit a
            // notification state from the previously active game. Neither
            // opening the preview nor selecting a window may retire the Dock
            // pair or its asynchronous material worker.
            policy={};policy.foreignForeground=true;policy.fullscreen={info.rcMonitor};
            ApplyForegroundProtection(policy,true,cycle%2?QUNS_RUNNING_D3D_FULL_SCREEN:QUNS_PRESENTATION_MODE);
            Update(policy);SendMessageW(dock.Handle(),desktop_level::Message(),0,0);Pump(20);
            Check(IsWindowVisible(dock.Handle())&&IsWindowVisible(dock.BarHandle())&&
                  !Suppressed(dock.Handle())&&Surface(dock.BarHandle())==retainedGlass,
                  "grouped taskbar preview preserves both Dock windows and the same material worker");
            policy={};policy.foreignForeground=true;Update(policy);
            SendMessageW(dock.Handle(),desktop_level::Message(),0,0);Pump(20);
            Check(IsWindowVisible(dock.Handle())&&IsWindowVisible(dock.BarHandle())&&
                  Surface(dock.BarHandle())==retainedGlass,
                  "selecting a window from grouped taskbar preview never recreates the material");
        }
        policy.foreignForeground=true;policy.fullscreen={info.rcMonitor};Update(policy);
        SendMessageW(dock.Handle(),desktop_level::Message(),0,0);
        Check(!IsWindowVisible(dock.Handle())&&!IsWindowVisible(dock.BarHandle()),
              "foreground fullscreen protection still hides the Dock pair");
        policy={};Update(policy);
        SendMessageW(dock.Handle(),desktop_level::Message(),0,0);
        Check(IsWindowVisible(dock.Handle())&&IsWindowVisible(dock.BarHandle()),
              "returning from fullscreen restores Dock without toggling its preference");
        policy.exclusive=true;policy.allDisplays=true;Update(policy);
        SendMessageW(dock.Handle(),desktop_level::Message(),0,0);
        Check(!IsWindowVisible(dock.Handle())&&!IsWindowVisible(dock.BarHandle()),"exclusive fullscreen protection remains active");
        policy={};Update(policy);SendMessageW(dock.Handle(),desktop_level::Message(),0,0);
        Check(IsWindowVisible(dock.Handle())&&IsWindowVisible(dock.BarHandle()),"exclusive protection exit restores Dock");
        Stop();contextReader={};
    }
    Check(WaitFor([&] { return Surface(dock.BarHandle())!=nullptr; },3000),
          "real glass is recreated after fullscreen suppression");
    dock.Stop();
    Check(!dock.Visible() && !IsWindow(dock.Handle()), "stop retires windows and event hooks");
    Check(dock.Show(items, true), "restart reattaches layer watcher");
    dock.Stop();
    ShowWindow(app, SW_MINIMIZE);
    Check(dock_sources::FocusWindow(reinterpret_cast<std::uint64_t>(app)) && !IsIconic(app),
          "preview selection restores the selected external window");
    Check(dock_sources::FocusWindow(reinterpret_cast<std::uint64_t>(app)) && !IsIconic(app),
          "selecting the same preview again never minimizes it");
    std::string executable;
    for (const auto& window : dock_sources::ProgramWindows())
        if (window.handle == reinterpret_cast<std::uint64_t>(app)) executable = window.executable;
    Check(!executable.empty() && dock_sources::ProgramWindowsFor(executable).size() == 1,
          "preview provider enumerates the external program window");
    Check(dock_sources::CloseProgram(executable), "close-window posts WM_CLOSE to the external application");
    Check(WaitForSingleObject(external.process.hProcess, 1500) == WAIT_OBJECT_0, "fixture exits cleanly");
    Check(true,"cleanup browser");DestroyWindow(browser);
    Check(true,"cleanup manager glass");DestroyWindow(managerGlass);
    Check(true,"cleanup manager");DestroyWindow(manager);
    Check(true,"cleanup desktop view");DestroyWindow(view);
    Check(true,"cleanup desktop host");DestroyWindow(host);
    Check(true,"cleanup COM apartment");CoUninitialize();
    // Inactive desktops may not produce WGC frames. Live glass visibility is
    // also covered by the existing material/widget layer packaging probes.
    std::printf("dock application layer passed: 40 fullscreen/maximized cycles, real surface samples=%u, visible=%u\n",
                materialSamples, visibleMaterialSamples);
    if(checkpoint){UnmapViewOfFile(checkpoint);checkpoint=nullptr;}if(checkpointMap)CloseHandle(checkpointMap);
    return 0;
}
}  // namespace
int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc > 1 && wcscmp(argv[1], L"--live-material") == 0) return LiveMaterial();
    if (argc > 1 && wcscmp(argv[1], L"--live-idle") == 0) return LiveIdle(false);
    if (argc > 1 && wcscmp(argv[1], L"--live-idle-fixture") == 0) return LiveIdle(true);
    if (argc > 1 && wcscmp(argv[1], L"--live-idle-frosted") == 0) return LiveIdle(true, true);
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
        const auto checkpointName=L"Local\\LumeLayerCheckpoint-"+std::to_wstring(GetCurrentProcessId());
        HANDLE map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,512,checkpointName.c_str());
        char* last=map?static_cast<char*>(MapViewOfFile(map,FILE_MAP_READ,0,0,512)):nullptr;
        SetEnvironmentVariableW(L"LUME_TEST_LAYER_CHECKPOINT",checkpointName.c_str());
        Child scenario(L"--scenario", std::wstring(L"WinSta0\\") + name);
        SetEnvironmentVariableW(L"LUME_TEST_LAYER_CHECKPOINT",nullptr);
        const bool finished=WaitForSingleObject(scenario.process.hProcess, 25000) == WAIT_OBJECT_0;
        if(!finished&&last)std::fprintf(stderr,"last scenario checkpoint: %s\n",last);
        if(last)UnmapViewOfFile(last);if(map)CloseHandle(map);
        Check(finished, "scenario finishes within timeout");
        Check(GetExitCodeProcess(scenario.process.hProcess, &code), "read scenario result");
    }
    CloseDesktop(desktop);
    return static_cast<int>(code);
}
