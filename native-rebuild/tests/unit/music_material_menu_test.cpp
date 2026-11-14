// Real controls/menu on an isolated desktop which is never switched to.
// No SMTC registration, user preferences, audio, capture or consent request.
#include "platform/windows/music_desktop_controls.h"
#include "platform/windows/lume_window_ids.h"
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {
using lume::platform::windows::MusicDesktopControls;
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
HWND Menu(DWORD thread, HWND owner) {
    struct Search { HWND owner, result; } search{owner,nullptr};
    EnumThreadWindows(thread,[](HWND window,LPARAM context) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(context);
        wchar_t name[64]{}; GetClassNameW(window,name,64);
        if (std::wstring(name) != L"LumeWidgetMenuWindow" ||
            GetWindow(window,GW_OWNER) != state.owner) return TRUE;
        state.result = window; return FALSE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
void Choose(MusicDesktopControls& controls,bool frosted) {
    const auto owner = controls.Handle();
    const auto uiThread = GetCurrentThreadId();
    const auto desktop = GetThreadDesktop(uiThread);
    std::atomic<bool> returned = false, attached = false, expired = false;
    std::atomic<unsigned> stage = 0;
    // WM_TIMER is a low-priority queue message and can be starved by the real
    // widget/menu paint and layer traffic. Drive the actual popup HWNDs from a
    // second thread on the same never-switched desktop instead. No focus or
    // input-desktop activation is needed for posted window messages.
    std::thread driver([&] {
        attached = SetThreadDesktop(desktop) != FALSE;
        if (!attached) { PostThreadMessageW(uiThread,WM_QUIT,1,0); return; }
        const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
        auto retry = std::chrono::steady_clock::time_point{};
        while (!returned && std::chrono::steady_clock::now() < deadline) {
            const auto root = Menu(uiThread,owner);
            if (root && stage < 3) {
                stage = 1;
                if (const auto child = Menu(uiThread,root)) {
                    stage = 2;
                    PostMessageW(child,WM_KEYDOWN,VK_HOME,0);
                    if (frosted) PostMessageW(child,WM_KEYDOWN,VK_DOWN,0);
                    PostMessageW(child,WM_KEYDOWN,VK_RETURN,0);
                    stage = 3;
                } else if (std::chrono::steady_clock::now() >= retry) {
                    // Material is the first action for the real music widget.
                    PostMessageW(root,WM_KEYDOWN,VK_HOME,0);
                    PostMessageW(root,WM_KEYDOWN,VK_RIGHT,0);
                    retry = std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (returned) return;
        expired = true;
        std::cerr << "FAIL material menu driver deadline, stage=" << stage.load() << std::endl;
        // Unwind both nested real popup loops, so a missing choice is an
        // ordinary failing assertion rather than a CTest-wide timeout.
        if (const auto root = Menu(uiThread,owner)) {
            if (const auto child = Menu(uiThread,root)) PostMessageW(child,WM_CLOSE,0,0);
            PostMessageW(root,WM_CLOSE,0,0);
        }
        PostThreadMessageW(uiThread,WM_QUIT,1,0);
    });
    std::cout << "material menu open: " << (frosted ? "frosted" : "liquid") << std::endl;
    SendMessageW(owner,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(owner),MAKELPARAM(20,20));
    returned = true;
    driver.join();
    Expect(attached,"attach native menu driver to the isolated desktop");
    Expect(!expired && stage == 3 && !Menu(uiThread,owner),"real material submenu completed and closed");
}
}

int main() {
    const auto original = GetThreadDesktop(GetCurrentThreadId());
    const auto name = L"LumeMusicMaterial-" + std::to_wstring(GetCurrentProcessId());
    const auto desktop = CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    Expect(desktop && SetThreadDesktop(desktop),"create isolated material-test desktop");
    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"initialize test apartment");
    std::cout << "isolated desktop and apartment ready" << std::endl;
    {
        unsigned requests = 0, applied = 0; HWND source = nullptr; bool selected = true;
        MusicDesktopControls controls(GetModuleHandleW(nullptr),{},
            [&](auto action,const auto&) {
                if (action == lume::platform::windows::WidgetAction::Liquid ||
                    action == lume::platform::windows::WidgetAction::Frosted) ++applied;
            });
        auto prefs = lume::widget::Defaults(MusicDesktopControls::Limits);
        prefs.enabled = true; prefs.frosted = true;
        // This is a native menu/intent integration test. Retain the real
        // material submenu and Configure path while keeping its background a
        // solid fixture, so no unrelated LumeGlass capture worker is started
        // on a desktop that deliberately has no interactive display.
        prefs.solid = true;
        Expect(controls.Configure(prefs) && controls.Show(),"show music controls only on the isolated desktop");
        Expect(!GetPropW(controls.Handle(),lume::window_ids::GlassOwnerProp),
            "native menu fixture does not start a background capture worker");
        std::cout << "real music controls ready" << std::endl;
        controls.SetMaterialSelectionHandler([&](bool frosted,HWND owner) {
            ++requests; selected = frosted; source = owner;
        });
        Choose(controls,false);
        Expect(requests == 1 && !selected && source == controls.Handle() && controls.Bounds().frosted &&
            controls.Bounds().solid && applied == 0,
            "real native choice reaches host before controls mutate or notify applied preferences");
        std::cout << "host intent arrived before material application" << std::endl;
        auto replay = controls.Bounds(); replay.frosted = selected;
        Expect(controls.Configure(replay) && !controls.Bounds().frosted && controls.Bounds().solid && requests == 1,
            "host replay applies once without reentering the material request");
        std::cout << "host replay applied once" << std::endl;
        controls.SetMaterialSelectionHandler({});
        Choose(controls,true);
        Expect(controls.Bounds().frosted && controls.Bounds().solid && requests == 1 && applied == 1,
            "standalone controls retain local application after the host port is cleared");
        Expect(!GetPropW(controls.Handle(),lume::window_ids::GlassOwnerProp),
            "standalone material choice preserves the fixture without capture");
        std::cout << "standalone material applied; shutting down controls" << std::endl;
        controls.Shutdown();
        std::cout << "controls shutdown returned; destroying controls" << std::endl;
    }
    std::cout << "controls destroyed; uninitializing apartment" << std::endl;
    CoUninitialize();
    std::cout << "apartment released; restoring thread desktop" << std::endl;
    Expect(SetThreadDesktop(original) && CloseDesktop(desktop),"release the isolated test desktop");
    std::cout << "PASS real music material intent, deferred application and standalone fallback\n";
}
