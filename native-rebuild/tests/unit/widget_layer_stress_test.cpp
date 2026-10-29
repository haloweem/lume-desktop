#include "platform/windows/widget_ui/native_widget_window.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "platform/windows/calendar/native_calendar_window.h"
#include "platform/windows/clock/native_clock_window.h"
#include "platform/windows/weather/weather_tooltip.h"
#include <objbase.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace lume::platform::windows;

namespace {
void Check(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}

void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

template<class Predicate> bool WaitFor(Predicate predicate, DWORD duration) {
    const auto deadline = GetTickCount64() + duration;
    do {
        Pump();
        if (predicate()) return true;
        Sleep(5);
    } while (GetTickCount64() < deadline);
    return predicate();
}

HWND Surface(HWND owner) {
    struct Search { HWND owner, result; } search{owner, nullptr};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(data);
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process == GetCurrentProcessId() &&
            GetPropW(window, L"LumeGlassSurfaceOwner") == value.owner) {
            value.result = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

bool Below(HWND window, HWND ceiling) {
    if (!IsWindow(window) || !IsWindow(ceiling)) return false;
    for (HWND above = GetWindow(window, GW_HWNDPREV); above; above = GetWindow(above, GW_HWNDPREV))
        if (above == ceiling) return true;
    return false;
}

LRESULT CALLBACK FixtureProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
        EndPaint(window, &paint);
        return 0;
    }
    wchar_t className[64]{};
    const bool manager = GetClassNameW(window, className, 64) &&
                         wcscmp(className, L"LumeOriginalPageWindow") == 0;
    if (manager && (message == WM_MOUSEACTIVATE ||
        (message == WM_ACTIVATE && LOWORD(w) != WA_INACTIVE)))
        desktop_level::ManagerSelected();
    // The real manager sends this notification when it is shown, activated or
    // moved. Use the same asynchronous widget path, not direct Place() calls.
    if (message == WM_ACTIVATE || message == WM_SHOWWINDOW || message == WM_WINDOWPOSCHANGED)
        desktop_level::RequestAll();
    return DefWindowProcW(window, message, w, l);
}

struct FixtureWindow {
    HWND handle = nullptr;
    ~FixtureWindow() { if (IsWindow(handle)) DestroyWindow(handle); }
};

struct ForegroundRestore {
    HWND saved = GetForegroundWindow();
    ~ForegroundRestore() {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        if (process == GetCurrentProcessId() && IsWindow(saved)) SetForegroundWindow(saved);
    }
};

LRESULT CALLBACK TracePosition(HWND window, UINT message, WPARAM w, LPARAM l) {
    const auto original = reinterpret_cast<WNDPROC>(GetPropW(window, L"LumeStressOriginalProcedure"));
    if (message != WM_WINDOWPOSCHANGING)
        return CallWindowProcW(original, window, message, w, l);
    const auto before = *reinterpret_cast<const WINDOWPOS*>(l);
    const auto guard = desktop_level::placingWindow;
    const auto result = CallWindowProcW(original, window, message, w, l);
    const auto& after = *reinterpret_cast<const WINDOWPOS*>(l);
    static unsigned samples = 0;
    if (++samples <= 80) {
        wchar_t name[96]{};
        GetClassNameW(window, name, 96);
        std::wcout << L"position " << name << L" hwnd=" << window << L" after=" << before.hwndInsertAfter
                   << L" flags=" << before.flags << L"->" << after.flags << L" guard=" << guard
                   << L" afterGuard=" << desktop_level::placingWindow
                   << L" tls=" << &desktop_level::placingWindow
                   << L" host=" << desktop_level::Host()
                   << L" ceiling=" << desktop_level::ManagerCeiling()
                   << L" aboveCeiling=" << desktop_level::Above(window, desktop_level::ManagerCeiling())
                   << L'\n';
    }
    return result;
}

struct PositionTrace {
    HWND window;
    WNDPROC original = nullptr;
    explicit PositionTrace(HWND value) : window(value) {
        if (!std::getenv("LUME_WIDGET_LAYER_TRACE")) return;
        original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
        Check(SetPropW(window, L"LumeStressOriginalProcedure", reinterpret_cast<HANDLE>(original)) != FALSE,
              "save fixture position observer");
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TracePosition));
    }
    ~PositionTrace() {
        if (original && IsWindow(window)) {
            SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
            RemovePropW(window, L"LumeStressOriginalProcedure");
        }
    }
};

WNDPROC dislocationProcedure = nullptr;
LRESULT CALLBACK AllowFixtureDislocation(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_WINDOWPOSCHANGING) return DefWindowProcW(window, message, w, l);
    return CallWindowProcW(dislocationProcedure, window, message, w, l);
}

void Dislocate(HWND window) {
    // Reproduce an already-invalid order (for example an asynchronous desktop
    // transition) without granting production callers a permanent escape from
    // the widget's position guard. Subclassing is confined to our own HWND.
    dislocationProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(AllowFixtureDislocation)));
    Check(dislocationProcedure != nullptr, "temporarily observe fixture window positioning");
    const bool moved = SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
    SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dislocationProcedure));
    dislocationProcedure = nullptr;
    Check(moved, "establish displaced fixture window order");
}

WNDPROC nestedProcedure = nullptr;
HWND nestedPeer = nullptr;
bool nestedEntered = false;
LRESULT CALLBACK ReenterPlacement(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_WINDOWPOSCHANGING && !nestedEntered &&
        !(reinterpret_cast<const WINDOWPOS*>(l)->flags & SWP_NOZORDER)) {
        nestedEntered = true;
        desktop_level::Place(nestedPeer, false);
    }
    return CallWindowProcW(nestedProcedure, window, message, w, l);
}

void CheckReentrantPlacement(HWND outer, HWND peer, HWND manager) {
    Dislocate(outer);
    Dislocate(peer);
    nestedPeer = peer;
    nestedEntered = false;
    nestedProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(outer, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ReenterPlacement)));
    Check(nestedProcedure != nullptr, "observe reentrant fixture placement");
    desktop_level::Place(outer, false);
    SetWindowLongPtrW(outer, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(nestedProcedure));
    nestedProcedure = nullptr;
    nestedPeer = nullptr;
    Check(nestedEntered, "fixture reenters placement during outer WINDOWPOSCHANGING");
    Check(Below(outer, manager) && Below(peer, manager),
          "nested placement preserves the outer window positioning authorization");
}

void Register(HINSTANCE instance, const wchar_t* name) {
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.hInstance = instance;
    type.lpfnWndProc = FixtureProcedure;
    type.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    type.lpszClassName = name;
    Check(RegisterClassExW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
          "register isolated window class");
}

void CheckLayer(const std::array<HWND, 3>& owners, HWND manager, const char* stage) {
    const auto managerGlass = Surface(manager);
    const auto ceiling = managerGlass && IsWindowVisible(managerGlass) ? managerGlass : manager;
    for (const auto owner : owners) {
        const auto glass = Surface(owner);
        if (!Below(owner, ceiling) || !glass || !Below(glass, ceiling) || !Below(glass, owner)) {
            std::cerr << "layer violation at " << stage << ": widget=" << owner
                      << " glass=" << glass << " manager=" << manager
                      << " managerGlass=" << managerGlass
                      << " widgetVisible=" << IsWindowVisible(owner)
                      << " glassVisible=" << (glass ? IsWindowVisible(glass) : FALSE)
                      << " ownerBelowManager=" << Below(owner, manager)
                      << " glassBelowManager=" << Below(glass, manager)
                      << " ownerBelowManagerGlass=" << Below(owner, managerGlass)
                      << " glassBelowManagerGlass=" << Below(glass, managerGlass)
                      << " glassBelowOwner=" << Below(glass, owner) << '\n';
            for (const auto actual : owners) {
                std::cerr << "pair " << actual << " glass=" << Surface(actual)
                          << " ownerBelowManager=" << Below(actual, manager)
                          << " glassBelowManager=" << Below(Surface(actual), manager) << '\n';
            }
            for (HWND actual = GetTopWindow(nullptr); actual; actual = GetWindow(actual, GW_HWNDNEXT)) {
                DWORD process = 0;
                GetWindowThreadProcessId(actual, &process);
                if (process != GetCurrentProcessId()) continue;
                wchar_t name[96]{}; GetClassNameW(actual, name, 96);
                std::wcerr << L"z " << actual << L" " << name << L" visible=" << IsWindowVisible(actual)
                           << L" owner=" << GetWindow(actual, GW_OWNER)
                           << L" materialOwner=" << GetPropW(actual, L"LumeGlassSurfaceOwner") << L'\n';
            }
            throw std::runtime_error("a delayed widget or real glass surface crossed the manager");
        }
    }
}

void Run() {
    ForegroundRestore foreground;
    const auto instance = GetModuleHandleW(nullptr);
    Register(instance, L"LumeWidgetStressDesktopHost");
    Register(instance, L"SHELLDLL_DefView");
    Register(instance, L"LumeOriginalPageWindow");
    FixtureWindow host{CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"LumeWidgetStressDesktopHost", L"Isolated desktop host", WS_POPUP | WS_VISIBLE,
        80, 80, 1000, 680, nullptr, nullptr, instance, nullptr)};
    Check(host.handle != nullptr, "create isolated desktop host");
    Check(CreateWindowExW(0, L"SHELLDLL_DefView", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 1000, 680, host.handle, nullptr, instance, nullptr) != nullptr,
        "create isolated desktop icon view");
    Check(desktop_level::Host() == host.handle, "use only our stand-in desktop host");

    const lume::widget::Limits limits{340, 216, 320, 204, 720, 720};
    WidgetWindowPorts ports;
    ports.draw = [](WidgetCanvas& canvas, float, float) {
        canvas.Text(L"isolated glass layer regression", {20, 20, 290, 24}, 14);
    };
    NativeWidgetWindow weather(instance, L"Isolated weather", limits, ports);
    NativeWidgetWindow performance(instance, L"Isolated performance", limits, ports);
    const std::array<NativeWidgetWindow*, 2> widgets{&weather, &performance};
    for (std::size_t index = 0; index < widgets.size(); ++index) {
        auto preferences = lume::widget::Defaults(limits);
        preferences.placed = true;
        preferences.x = 140 + static_cast<int>(index) * 370;
        preferences.y = 150;
        Check(widgets[index]->Configure(preferences) && widgets[index]->Start(),
              "start real widget and asynchronous material worker");
    }
    WeatherTooltip weatherTooltip;
    Check(weatherTooltip.Attach(weather.Handle()), "attach production weather tooltip");
    NativeCalendarWindow calendar(instance, [](lume::calendar::Action) {}, [] {},
        [] { return lume::calendar::Date{2026, 10, 15}; });
    lume::calendar::Preferences calendarPreferences;
    calendarPreferences.material = lume::calendar::Material::FrostedGlass;  // This test exercises the glass layer.
    calendarPreferences.enabled = true;
    *calendarPreferences.Active() = {180, 400, 420, 340, true};
    Check(calendar.Configure(calendarPreferences) && calendar.Start(), "start real native calendar");
    const std::array<HWND, 3> owners{weather.Handle(), performance.Handle(), calendar.Handle()};
    Check(WaitFor([&] {
        for (const auto* widget : widgets) {
            const HWND glass = Surface(widget->Handle());
            if (!glass || !IsWindowVisible(glass) || widget->MaterialState() != WidgetMaterialState::Ready)
                return false;
        }
        return calendar.MaterialState() == WidgetMaterialState::Ready &&
               Surface(calendar.Handle()) && IsWindowVisible(Surface(calendar.Handle()));
    }, 10000), "real LumeGlass surfaces become ready and visible");

    FixtureWindow manager{CreateWindowExW(WS_EX_APPWINDOW, L"LumeOriginalPageWindow",
        L"Isolated manager", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        100, 100, 900, 560, nullptr, nullptr, instance, nullptr)};
    Check(manager.handle != nullptr, "create isolated manager");
    WidgetMaterialRuntime managerMaterial;
    LumeMaterialParameters managerParameters{};
    managerParameters.size = sizeof(managerParameters);
    managerParameters.owner = manager.handle;
    managerParameters.frosted = 2;
    managerParameters.opticalProfile = 2;
    managerParameters.lightForeground = 1;
    managerParameters.cornerRadius = 26;
    Check(managerMaterial.Attach(managerParameters), "attach real manager material worker");
    Check(WaitFor([&] {
        return managerMaterial.State() == WidgetMaterialState::Ready &&
               Surface(manager.handle) && IsWindowVisible(Surface(manager.handle));
    }, 10000), "real manager glass becomes ready and visible");
    desktop_level::Watcher watcher;
    Check(watcher.Start(instance), "start real desktop-level watcher");
    Check(WaitFor([&] {
        for (const auto owner : owners)
            if (!Below(owner, manager.handle) || !Below(Surface(owner), manager.handle))
                return false;
        return true;
    }, 2000), "initial manager notification settles real widget/glass pairs");

    SetActiveWindow(manager.handle);
    for (const auto owner : owners) {
        Dislocate(owner);
        Check(Below(manager.handle, owner), "fixture places widget above active manager");
    }
    PositionTrace weatherTrace(weather.Handle());
    PositionTrace performanceTrace(performance.Handle());
    PositionTrace calendarTrace(calendar.Handle());
    Check(GetActiveWindow() == manager.handle, "displaced widgets never stole manager activation");
    desktop_level::RequestAll();
    const bool repaired = WaitFor([&] {
        for (const auto owner : owners)
            if (!Below(owner, manager.handle) || !Below(Surface(owner), manager.handle))
                return false;
        return true;
    }, 2000);
    if (!repaired) CheckLayer(owners, manager.handle, "repair already-displaced real pairs");
    Check(repaired, "restack repairs already-displaced real pairs without raising manager again");
    CheckReentrantPlacement(weather.Handle(), performance.Handle(), manager.handle);

    {
        NativeClockWindow clock(instance, [](lume::clock::Action) {});
        lume::clock::Appearance appearance;
        Check(clock.Configure(lume::clock::Style::Welfare, appearance) && clock.Start(),
              "start real clock for pointer activation regression");
        const auto response = SendMessageW(clock.Handle(), WM_MOUSEACTIVATE,
            reinterpret_cast<WPARAM>(clock.Handle()), MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN));
        Check(response == MA_ACTIVATE, "clock click activates so the next manager click can take focus back");
        SetActiveWindow(clock.Handle());
        Check(Below(manager.handle, clock.Handle()), "clicked clock is above manager");
        desktop_level::RequestAll();Pump();
        Check(Below(manager.handle, clock.Handle()), "passive restack retains clicked clock");
        SetActiveWindow(manager.handle);Pump();
        Check(Below(clock.Handle(), manager.handle), "manager activation returns clock below manager");
        clock.Stop();
    }

    unsigned samples = 0;
    for (unsigned cycle = 0; cycle < 24; ++cycle) {
        const HWND clicked = owners[cycle % owners.size()];
        SendMessageW(clicked, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(clicked),
                     MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN));
        SetActiveWindow(clicked);
        Check(GetActiveWindow() == clicked, "clicked fixture widget owns activation");
        Check(WaitFor([&] {
            const HWND glass = Surface(clicked);
            return Below(manager.handle, clicked) && glass && Below(glass, clicked) &&
                   Below(manager.handle, glass);
        }, 1000), "click selects widget together with its real glass above manager");
        for (const auto owner : owners) {
            if (owner == clicked) continue;
            Check(Below(owner, clicked), "unclicked widgets stay below selected widget");
        }
        for (unsigned passive = 0; passive < 4; ++passive) {
            desktop_level::RequestAll();
            for (auto* widget : widgets) Check(widget->Paint(), "paint passive unclicked widgets");
            PostMessageW(Surface(clicked), WM_APP + 1, 0, 0);
            const auto until = GetTickCount64() + 35;
            do { Pump(); Sleep(5); } while (GetTickCount64() < until);
            Check(Below(manager.handle, clicked) && Below(manager.handle, Surface(clicked)),
                  "passive refresh cannot demote the explicitly selected widget pair");
            for (const auto owner : owners) {
                if (owner == clicked) continue;
                Check(Below(owner, manager.handle) && Below(Surface(owner), manager.handle),
                      "refresh never promotes an unclicked widget or its glass over manager");
            }
        }
        // Host replacement/order transitions used to be covered only with
        // fake one-pixel glass. Material workers keep running throughout.
        Check(SetWindowPos(host.handle, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE, "raise isolated desktop host");
        if (cycle % 4 == 0) {
            ShowWindow(manager.handle, SW_HIDE);
            ShowWindow(manager.handle, SW_SHOWNOACTIVATE);
        }
        Check(SetWindowPos(manager.handle, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) != FALSE,
            "bring clicked manager to the top");
        SendMessageW(manager.handle, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(manager.handle),
                     MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN));
        SetActiveWindow(manager.handle);
        Check(GetActiveWindow() == manager.handle, "manager owns this test thread's activation");
        desktop_level::RequestAll();
        Pump();

        for (std::size_t index = 0; index < widgets.size(); ++index) {
            auto preferences = widgets[index]->Bounds();
            preferences.frosted = (cycle + index) % 2 == 0;
            preferences.x += cycle % 2 == 0 ? 1 : -1;
            Check(widgets[index]->Configure(preferences) && widgets[index]->Paint(),
                  "configure and repaint while the manager is above widgets");
            // Exercise both asynchronous wake paths after the manager has
            // already won activation. This message is addressed only to the
            // real surface created by this fixture's material worker.
            const auto glass = Surface(widgets[index]->Handle());
            Check(glass != nullptr, "real glass remains attached during stress");
            PostMessageW(glass, WM_APP + 1, 0, 0);
        }
        calendarPreferences.material = cycle % 2 == 0 ? lume::calendar::Material::LiquidGlass :
            lume::calendar::Material::FrostedGlass;
        Check(calendar.Configure(calendarPreferences, true, true),
              "reconfigure real calendar while manager owns activation");
        PostMessageW(Surface(calendar.Handle()), WM_APP + 1, 0, 0);
        managerParameters.contactX = cycle % 2 == 0 ? -.6f : .6f;
        Check(managerMaterial.Update(managerParameters), "wake real manager glass worker");
        PostMessageW(Surface(manager.handle), WM_APP + 1, 0, 0);

        const auto deadline = GetTickCount64() + 180;
        do {
            Pump();
            CheckLayer(owners, manager.handle, "delayed material wake after manager activation");
            ++samples;
            Sleep(5);
        } while (GetTickCount64() < deadline);
    }
    // Desktop activation must restack even when no widget was selected and
    // Explorer raised an already-visible host (there is no SHOW event).
    desktop_level::ManagerSelected();
    Pump();
    Check(SetWindowPos(host.handle, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE, "raise active desktop without showing again");
    desktop_level::Watcher::ForegroundChanged(host.handle);
    Check(WaitFor([&] {
        for (const auto owner : owners)
            if (!Below(host.handle, owner) || !Below(host.handle, Surface(owner))) return false;
        return true;
    }, 2000), "Show Desktop restores every widget and real glass above desktop");
    performance.Stop();
    desktop_level::Watcher::ForegroundChanged(host.handle);
    Pump();
    Check(!performance.Handle(), "desktop recovery does not reopen a stopped widget");
    SetForegroundWindow(manager.handle);
    desktop_level::Watcher::ForegroundChanged(manager.handle);
    desktop_level::ManagerSelected();
    SetWindowPos(manager.handle, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Check(WaitFor([&] { return Below(weather.Handle(), manager.handle) &&
        Below(Surface(weather.Handle()), manager.handle) && Below(calendar.Handle(), manager.handle); }, 2000),
        "returning to manager restores widget and glass ceiling");
    watcher.Stop();
    managerMaterial.Detach();
    calendar.Stop();
    weatherTooltip.Stop();
    for (auto* widget : widgets) widget->Stop();
    std::cout << "real widget/glass layer stress passed: 24 transitions, " << samples
              << " asynchronous samples\n";
}
} // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        std::cerr << "COM initialization failed\n";
        return 1;
    }
    int result = 0;
    try { Run(); }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    CoUninitialize();
    return result;
}
