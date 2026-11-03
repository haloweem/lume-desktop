#include "platform/windows/application_tray.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/widget_ui/window_layer_policy.h"
#include <cstdio>
#include <vector>

using namespace lume::platform::windows;
namespace {
int failures = 0;
void Check(bool value, const char* why) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; }
}
enum class Operation { Select, Escape, Outside, HideOwner, Fullscreen };
struct Driver {
    HWND owner = nullptr;
    bool application = false;
    Operation operation = Operation::Select;
    int down = 0;
    bool seen = false;
    window_layer::Context* context = nullptr;
};
thread_local Driver* driver = nullptr;
thread_local unsigned createdMenus = 0, destroyedMenus = 0, cancelledMenus = 0;
LRESULT CALLBACK PreventActivation(int code, WPARAM w, LPARAM l) {
    // The fixture desktop is deliberately never made the input desktop.
    // Suppress OS activation there; visibility, capture, policy and selection
    // are still exercised through real HWNDs and the production callback.
    if (code == HCBT_ACTIVATE) return 1;
    return CallNextHookEx(nullptr, code, w, l);
}
LRESULT CALLBACK Trace(int code, WPARAM w, LPARAM l) {
    if (code >= 0) {
        const auto& message = *reinterpret_cast<const CWPSTRUCT*>(l);
        if (lume::window_ids::IsClass(message.hwnd, lume::window_ids::WidgetMenuClass)) {
            if (message.message == WM_CREATE) { ++createdMenus; std::puts("menu created"); }
            if (message.message == WM_DESTROY) ++destroyedMenus;
            if (message.message == WM_CANCELMODE) ++cancelledMenus;
            if (message.message == WM_SHOWWINDOW) std::puts("menu show requested");
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}
void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
void CALLBACK Drive(HWND, UINT, UINT_PTR timer, DWORD) {
    std::puts("menu driver timer");
    KillTimer(nullptr, timer);
    HWND menu = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        if (!lume::window_ids::IsClass(window, lume::window_ids::WidgetMenuClass) ||
            GetWindow(window, GW_OWNER) != driver->owner) return TRUE;
        *reinterpret_cast<HWND*>(data) = window;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&menu));
    Check(menu != nullptr, "modal menu remains alive until the user acts");
    if (!menu) { PostQuitMessage(1); return; }
    driver->seen = true;
    Check(IsWindowVisible(menu) && !window_layer::Suppressed(menu), "menu is actually visible and not suppressed");
    Check(((GetWindowLongPtrW(menu, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) == driver->application,
          "application menu overlays apps; desktop menu retains desktop policy");
    Check(GetCapture() == menu, "visible menu receives mouse input");
    switch (driver->operation) {
    case Operation::Select:
        PostMessageW(menu, WM_KEYDOWN, VK_HOME, 1);
        for (int i = 0; i < driver->down; ++i) PostMessageW(menu, WM_KEYDOWN, VK_DOWN, 1);
        PostMessageW(menu, WM_KEYDOWN, VK_RETURN, 1);
        break;
    case Operation::Escape: PostMessageW(menu, WM_KEYDOWN, VK_ESCAPE, 1); break;
    case Operation::Outside: PostMessageW(menu, WM_LBUTTONDOWN, 0, MAKELPARAM(-10, -10)); break;
    case Operation::HideOwner:
        ShowWindow(driver->owner, SW_HIDE); window_layer::Refresh(); break;
    case Operation::Fullscreen:
        driver->context->allDisplays = true; window_layer::Refresh(); break;
    }
}
UINT_PTR Begin(Driver& state) {
    driver = &state;
    const auto timer = SetTimer(nullptr, 0, 40, Drive);
    Check(timer != 0, "arm bounded menu driver");
    return timer;
}
void End(UINT_PTR timer) {
    KillTimer(nullptr, timer); driver = nullptr; Pump();
    Check(GetCapture() == nullptr, "closing the menu releases mouse capture");
}
void Scenario() {
    window_layer::Context context;
    window_layer::contextReader = [&] { return context; };
    window_layer::Start();
    HHOOK trace = SetWindowsHookExW(WH_CALLWNDPROC, Trace, nullptr, GetCurrentThreadId());
    HHOOK activation = SetWindowsHookExW(WH_CBT, PreventActivation, nullptr, GetCurrentThreadId());
    Check(trace && activation, "install isolated menu observation and activation hooks");
    std::vector<TrayAction> actions;
    ApplicationTray tray;
    // Explorer is absent on the isolated desktop. The actual callback HWND is
    // enough to test the production menu without adding an icon to the user's tray.
    tray.Create(GetModuleHandleW(nullptr), [&](TrayAction action) { actions.push_back(action); });
    Check(tray.Window() && !IsWindowVisible(tray.Window()), "real tray callback owner remains hidden");
    if (!tray.Window()) return;
    const auto callback = [&](UINT message, int down, Operation operation, TrayAction expected) {
        std::printf("tray begin message=%u selection=%d operation=%d fullscreen=%d\n",
                    message, down, static_cast<int>(operation), context.allDisplays);
        Driver state{tray.Window(), true, operation, down, false, &context};
        const auto count = actions.size();
        const auto created = createdMenus, destroyed = destroyedMenus, cancelled = cancelledMenus;
        const auto timer = Begin(state);
        SendMessageW(tray.Window(), WM_APP + 41, 1, message);
        End(timer);
        Check(state.seen, "right click opens the actual tray menu with layer policy enabled");
        Check(createdMenus == created + 1 && destroyedMenus == destroyed + 1 && cancelledMenus == cancelled,
              "tray menu is created and destroyed once without policy cancellation");
        if (operation == Operation::Select)
            Check(actions.size() == count + 1 && actions.back() == expected, "tray selection dispatches exactly the chosen action");
        else Check(actions.size() == count, "dismissal does not dispatch a tray action");
        Check(!IsWindowVisible(tray.Window()), "menu does not show the hidden background owner");
        Check(window_layer::windows.empty(), "destroyed tray menus leave no policy entries");
        std::puts("tray end");
    };
    callback(WM_RBUTTONUP, 0, Operation::Select, TrayAction::Open);
    HWND app = CreateWindowExW(0, L"STATIC", L"Ordinary application fixture", WS_POPUP | WS_VISIBLE,
                              0, 0, 800, 500, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(app != nullptr, "create ordinary application ceiling");
    context.foreignForeground = true; context.foreground = app;
    window_layer::applicationCeiling = [=] { return app; };
    callback(WM_CONTEXTMENU, 1, Operation::Select, TrayAction::ExportDiagnostics);
    callback(WM_RBUTTONUP, 2, Operation::Select, TrayAction::Exit);
    callback(WM_RBUTTONUP, 0, Operation::Escape, TrayAction::Open);
    callback(WM_CONTEXTMENU, 0, Operation::Outside, TrayAction::Open);
    // This explicit user action remains reachable even when desktop widgets are
    // protected, so the user can still open or exit the application from its tray.
    context.allDisplays = true;
    callback(WM_RBUTTONUP, 0, Operation::Select, TrayAction::Open);
    context = {}; window_layer::applicationCeiling = {}; DestroyWindow(app);

    HWND widget = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Desktop widget fixture", WS_POPUP,
                                 60, 60, 240, 160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(widget != nullptr, "create desktop widget fixture");
    window_layer::Register(widget, window_layer::Kind::Desktop);
    int suppressed = 0;
    window_layer::observer = [&](HWND, window_layer::Kind kind, bool hidden) {
        if (kind == window_layer::Kind::Popup && hidden) ++suppressed;
    };
    const auto desktopMenu = [&](Operation operation, bool visible, UINT expected) {
        std::printf("desktop begin operation=%d expectedVisible=%d\n", static_cast<int>(operation), visible);
        Driver state{widget, false, operation, 0, false, &context};
        const auto created = createdMenus, destroyed = destroyedMenus;
        const auto timer = Begin(state);
        const auto result = LumeWidgetMenu::ShowItems(widget, {100, 100}, {{L"Action", 7}}, true);
        End(timer);
        Check(state.seen == visible && result == expected, "desktop menu keeps its normal visibility and cancellation rules");
        Check(createdMenus == created + 1 && destroyedMenus == destroyed + 1, "desktop menu always cleans up its window");
        std::puts("desktop end");
    };
    desktopMenu(Operation::Select, false, 0);
    Check(suppressed == 1, "a hidden desktop owner still suppresses its popup");
    ShowWindow(widget, SW_SHOWNOACTIVATE);
    desktopMenu(Operation::Select, true, 7);
    desktopMenu(Operation::HideOwner, true, 0);
    Check(suppressed == 2, "hiding a desktop owner cancels its open menu");
    ShowWindow(widget, SW_SHOWNOACTIVATE);
    desktopMenu(Operation::Fullscreen, true, 0);
    Check(suppressed == 3 && !IsWindowVisible(widget), "fullscreen still cancels desktop menus and hides their owner");
    context = {}; window_layer::Refresh(); Pump();
    Check(IsWindowVisible(widget), "desktop owner restores when fullscreen protection ends");
    desktopMenu(Operation::Select, true, 7);
    DestroyWindow(widget);
    window_layer::Stop(); window_layer::contextReader = {};
    Check(window_layer::windows.empty(), "fixture cleanup removes policy entries");
    UnhookWindowsHookEx(trace);
    UnhookWindowsHookEx(activation);
}
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    wchar_t name[80]{}; swprintf_s(name, L"LumeTrayMenuTest-%lu", GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop) { std::fprintf(stderr, "FAIL: create isolated desktop\n"); return 2; }
    const auto original = GetThreadDesktop(GetCurrentThreadId());
    if (!SetThreadDesktop(desktop)) { CloseDesktop(desktop); return 2; }
    Scenario();
    Check(SetThreadDesktop(original) != FALSE, "restore the test thread desktop after closing all windows");
    Check(CloseDesktop(desktop) != FALSE, "release the isolated desktop");
    if (!failures) std::puts("PASS tray right-click/context menus: Open, Export, Exit, dismiss, app ceiling, fullscreen; desktop menu protections retained");
    return failures ? 1 : 0;
}
