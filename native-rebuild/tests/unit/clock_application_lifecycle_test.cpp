#include "core/clock/clock_controller.h"
#include "core/settings/settings_types.h"
#include "core/ui/clock_page_state.h"
#include "platform/windows/clock_runtime.h"
#include "platform/windows/webview_page_window.h"

#include <objbase.h>
#include <windows.h>
#include <cwchar>
#include <iostream>
#include <utility>

namespace {
using lume::clock::ClockController;
using lume::platform::windows::NativeClockRuntime;
using lume::platform::windows::WebViewPageWindow;

struct WindowSearch {
    const wchar_t* className;
    HWND found = nullptr;
};

HWND FindOwnWindow(const wchar_t* className) {
    WindowSearch search{className};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM context) -> BOOL {
        auto& search = *reinterpret_cast<WindowSearch*>(context);
        wchar_t name[128]{};
        if (GetClassNameW(window, name, 128) && std::wcscmp(name, search.className) == 0) {
            search.found = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

struct LifecycleProbe {
    ClockController* controller;
    WebViewPageWindow* manager;
    HWND clockWindow;
    ULONGLONG deadline;
    ULONGLONG closeAt = 0;
    int polls = 0;
    bool continuous = true;
    bool timedOut = false;
};

LifecycleProbe* activeProbe = nullptr;

void CALLBACK CheckLifecycle(HWND, UINT, UINT_PTR, DWORD) {
    auto* probe = activeProbe;
    if (!probe) return;
    ++probe->polls;
    if (!probe->controller->Running() || !IsWindow(probe->clockWindow) ||
        FindOwnWindow(L"LumeNativeDesktopClock") != probe->clockWindow) {
        probe->continuous = false;
        probe->manager->RequestClose();
    }
    const auto now = GetTickCount64();
    if (now >= probe->deadline) {
        probe->timedOut = true;
        probe->manager->RequestClose();
    }
    if (probe->closeAt && now >= probe->closeAt) probe->manager->RequestClose();
}

int RunLifecycleTest() {
    const auto instance = GetModuleHandleW(nullptr);
    ClockController* actionController = nullptr;
    NativeClockRuntime runtime(instance, [&](lume::clock::Action action) {
        if (actionController) actionController->Handle(action);
    });
    lume::settings::SettingsSnapshot persisted;
    int saves = 0;
    ClockController controller(persisted.clock, runtime, [&](const lume::settings::ClockSettings& value) {
        auto next = persisted;
        next.clock = value;
        persisted = std::move(next);
        ++saves;
        return true;
    });
    actionController = &controller;
    if (!Expect(controller.Restore(), "restore disabled native runtime") ||
        !Expect(controller.Handle({lume::clock::ActionType::Enable, 1}), "enable native clock")) return 1;

    const HWND clockWindow = FindOwnWindow(L"LumeNativeDesktopClock");
    if (!Expect(clockWindow && controller.Running(), "native clock HWND exists before manager opens")) return 1;
    WebViewPageWindow manager;
    LifecycleProbe probe{&controller, &manager, clockWindow, GetTickCount64() + 40000};
    activeProbe = &probe;
    const UINT_PTR timer = SetTimer(nullptr, 0, 50, CheckLifecycle);
    if (!timer) {
        activeProbe = nullptr;
        std::cerr << "FAIL: create lifecycle observation timer\n";
        return 1;
    }

    bool passed = true;
    int totalReady = 0;
    for (int round = 0; round < 3 && passed; ++round) {
        int readyCount = 0;
        const int priorPolls = probe.polls;
        probe.closeAt = 0;
        const int result = manager.Run(instance, [&](lume::ui::PageCommand command) {
            if (command == lume::ui::PageCommand::Ready) {
                ++readyCount;
                ++totalReady;
                probe.continuous = probe.continuous && controller.Running() && IsWindow(clockWindow) &&
                    FindOwnWindow(L"LumeNativeDesktopClock") == clockWindow;
                probe.closeAt = GetTickCount64() + 100;
            }
            return lume::ui::ClockPageState(controller);
        }, true);
        passed = Expect(result == 0 && readyCount == 1, "each manager opens, reaches ready, and closes cleanly") && passed;
        passed = Expect(probe.polls > priorPolls && probe.continuous && !probe.timedOut,
            "clock remains alive while the manager message loop runs") && passed;
        passed = Expect(!FindOwnWindow(L"LumeOriginalPageWindow"), "manager HWND is destroyed after close") && passed;
        passed = Expect(controller.Running() && IsWindow(clockWindow) &&
            FindOwnWindow(L"LumeNativeDesktopClock") == clockWindow,
            "the same native clock HWND survives manager close") && passed;
    }
    KillTimer(nullptr, timer);
    activeProbe = nullptr;
    passed = Expect(totalReady == 3, "three real WebView ready/close/reopen rounds complete") && passed;
    passed = Expect(saves == 1 && persisted.clock.enabled, "manager lifetime does not change in-memory preferences") && passed;
    controller.Shutdown();
    actionController = nullptr;
    passed = Expect(!controller.Running() && !IsWindow(clockWindow) &&
        !FindOwnWindow(L"LumeNativeDesktopClock"), "shutdown destroys the native clock HWND") && passed;
    if (passed) std::cout << "Three real WebView lifetimes preserve the native clock; shutdown releases its HWND\n";
    return passed ? 0 : 1;
}
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!Expect(SUCCEEDED(initialized), "initialize the shared STA apartment")) return 1;
    const int result = RunLifecycleTest();
    CoUninitialize();
    return result;
}
