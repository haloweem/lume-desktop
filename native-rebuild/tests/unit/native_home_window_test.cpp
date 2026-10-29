#include "platform/windows/native_home_window.h"
#include <cstdlib>
#include <iostream>

void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main() {
    lume::ui::HomeScreenModel model;
    lume::platform::windows::NativeHomeWindow window;
    unsigned changes = 0;
    Expect(window.Create(GetModuleHandleW(nullptr), model, [&](const auto&) { ++changes; }), "creates test-owned window");
    for (int id : {100, 101, 102, 110, 120, 130, 131, 132, 140}) {
        Expect(GetDlgItem(window.Handle(), id) != nullptr, "creates every button with the correct parent");
    }
    // Exercise the application's Win32 command boundary inside this test process.
    SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(101, BN_CLICKED), 0);
    Expect(model.Snapshot().page == lume::ui::HomePage::DesktopBeauty, "beauty navigation reaches model");
    SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(110, BN_CLICKED), 0);
    Expect(model.Snapshot().page == lume::ui::HomePage::ClockSettings, "clock navigation reaches model");
    SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(120, BN_CLICKED), 0);
    SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(132, BN_CLICKED), 0);
    Expect(!model.Snapshot().clockEnabled && model.Snapshot().clockStyle == lume::ui::ClockStyle::InsideDream,
           "clock commands change state");
    Expect(changes == 4, "commands reach composition callback");
    for (const auto size : {SIZE{900, 700}, SIZE{1280, 720}, SIZE{1600, 900}}) {
        SetWindowPos(window.Handle(), nullptr, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT client{};
        GetClientRect(window.Handle(), &client);
        RECT previous{};
        for (int id : {130, 131, 132}) {
            RECT button{};
            GetWindowRect(GetDlgItem(window.Handle(), id), &button);
            MapWindowPoints(nullptr, window.Handle(), reinterpret_cast<POINT*>(&button), 2);
            Expect(button.left >= 304 && button.right <= client.right - 42 && button.bottom < client.bottom - 42,
                   "style controls fit inside settings content at supported window sizes");
            Expect(button.left >= previous.right, "style controls never overlap");
            previous = button;
        }
    }
    window.Close();
    Expect(!IsWindow(window.Handle()), "close releases native window");
    return 0;
}
