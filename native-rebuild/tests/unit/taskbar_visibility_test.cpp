#include "platform/windows/taskbar/taskbar_visibility.h"
#include "platform/windows/taskbar/taskbar_desktop_recovery.h"
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); }
}
void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
}
}
int main() {
    using namespace lume::platform::windows;
    {
        using namespace taskbar_desktop_recovery;
        const Candidate covered{true, true, true, false, false, false, true, true, false};
        Check(CanRepair(covered, false, false), "visible secondary tray below desktop can recover after settling");
        Check(!CanRepair(covered, true, false), "system auto-hide retains control");
        Check(!CanRepair(covered, false, true), "Lume fully-hidden and Dock auto-hide demand retain control");
        auto c = covered; c.fullScreenAboveDesktop = true;
        Check(!CanRepair(c, false, false), "fullscreen app on secondary monitor prevents restoring its taskbar");
        c = covered; c.explorerTray = false;
        Check(!CanRepair(c, false, false), "same-class window in another process is rejected");
        c = covered; c.visible = false;
        Check(!CanRepair(c, false, false), "deliberately hidden tray is never shown");
        c = covered; c.opaque = false;
        Check(!CanRepair(c, false, false), "transparent hidden tray is never revived");
        c = covered; c.iconic = true;
        Check(!CanRepair(c, false, false), "minimized tray retains state");
        c = covered; c.cloaked = true;
        Check(!CanRepair(c, false, false), "cloaked or unknown-visibility tray retains state");
        c = covered; c.topmost = true;
        Check(!CanRepair(c, false, false), "healthy topmost primary tray requires no write");
        c = covered; c.belowDesktop = false;
        Check(!CanRepair(c, false, false), "healthy tray above desktop requires no write");
        c = covered; c.exposedMonitorEdge = false;
        Check(!CanRepair(c, false, false), "offscreen or undocked tray is left alone");
        Check(!CanRepair({}, false, false), "incomplete snapshot fails closed");
        Check(ExposedMonitorEdge({-2560, 1390, 0, 1440}, {-2560, 0, 0, 1440}),
              "left secondary monitor with negative coordinates has an exposed bottom tray");
        Check(ExposedMonitorEdge({2560, -1440, 2620, 0}, {2560, -1440, 5120, 0}),
              "vertical taskbar on above-right secondary monitor is supported");
        Check(!ExposedMonitorEdge({2560, 1438, 5120, 1480}, {2560, 0, 5120, 1440}),
              "partly offscreen auto-hide or disconnected tray is rejected");
        Check(!ExposedMonitorEdge({2800, 100, 4700, 160}, {2560, 0, 5120, 1440}),
              "floating third-party tray is not repositioned above desktop");
        Recovery recovery;
        Check(recovery.Tick(nullptr, false) == 0, "missing settled desktop cannot operate Explorer");
        recovery.Reset();
    }
    for (const bool layered : {false, true}) {
        const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (layered ? WS_EX_LAYERED : 0),
            L"STATIC", L"Isolated taskbar visibility fixture", WS_POPUP,
            -30000, -30000, 160, 40, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(window != nullptr, "offscreen fixture created");
        if (layered) Check(SetLayeredWindowAttributes(window, RGB(1, 2, 3), 177, LWA_ALPHA | LWA_COLORKEY) != FALSE,
                           "preexisting layered attributes");
        ShowWindow(window, SW_SHOWNA);
        lume::taskbar::Visibility original;
        Check(taskbar_visibility::Capture(window, original) && original.visible, "capture original visibility");
        for (int i = 0; i < 3; ++i) {
            Check(taskbar_visibility::Hide(window), "full hiding applies repeatedly"); Pump();
            COLORREF key = 0; BYTE alpha = 255; DWORD flags = 0;
            Check(GetLayeredWindowAttributes(window, &key, &alpha, &flags) && alpha == 0 && flags == LWA_ALPHA,
                  "taskbar is fully transparent");
            Check((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0, "hidden taskbar is click-through");
            ShowWindow(window, SW_SHOWNA); // Simulate Explorer responding to the screen edge.
            Check(GetLayeredWindowAttributes(window, &key, &alpha, &flags) && alpha == 0,
                  "Explorer showing the window cannot reveal it");
        }
        Check(taskbar_visibility::Restore(window, original), "visibility restored"); Pump();
        Check(IsWindowVisible(window) && static_cast<std::uint32_t>(GetWindowLongPtrW(window, GWL_EXSTYLE)) == original.extendedStyle,
              "original visibility and styles restored");
        if (layered) {
            COLORREF key = 0; BYTE alpha = 0; DWORD flags = 0;
            Check(GetLayeredWindowAttributes(window, &key, &alpha, &flags) && key == RGB(1, 2, 3) &&
                  alpha == 177 && flags == (LWA_ALPHA | LWA_COLORKEY), "original layered attributes restored");
        }
        ShowWindow(window, SW_HIDE);
        Check(taskbar_visibility::Capture(window, original) && !original.visible &&
              taskbar_visibility::Hide(window) && taskbar_visibility::Restore(window, original), "initially hidden windows recover");
        Pump(); Check(!IsWindowVisible(window), "initial hidden state preserved");
        DestroyWindow(window);
    }
    {
        // A record lost while the window was hidden: capturing it again must
        // journal a visible original, so a later restore shows the taskbar.
        const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Lost record fixture", WS_POPUP,
            -30000, -30000, 160, 40, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ShowWindow(window, SW_SHOWNA);
        Check(taskbar_visibility::Hide(window) && taskbar_visibility::LooksHidden(window), "fixture fully hidden"); Pump();
        ShowWindow(window, SW_SHOWNA);  // Explorer shows it again; our marks remain.
        lume::taskbar::Visibility recaptured;
        Check(taskbar_visibility::Capture(window, recaptured) && recaptured.visible &&
              !(recaptured.extendedStyle & (WS_EX_LAYERED | WS_EX_TRANSPARENT)), "hidden state is never journaled as original");
        Check(taskbar_visibility::Restore(window, recaptured) && !taskbar_visibility::LooksHidden(window), "restore from recapture shows it");
        Pump();
        Check(IsWindowVisible(window) && !(GetWindowLongPtrW(window, GWL_EXSTYLE) & (WS_EX_LAYERED | WS_EX_TRANSPARENT)), "visible and opaque again");
        DestroyWindow(window);
    }
    Check(!taskbar_visibility::Hide(nullptr), "invalid target rejected");
    std::cout << "taskbar visibility passed; Explorer untouched\n";
}
