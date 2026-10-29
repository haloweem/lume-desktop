#include "platform/windows/desktop_icons/desktop_icons_port.h"
#include <commctrl.h>
#include <objbase.h>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace lume::platform::windows;
namespace {
void Check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
}
int main(int argc, char** argv) {
    const bool child = argc == 3 && std::string(argv[1]) == "--child";
    std::wstring channel;
    if (child) {
        const std::string suffix(argv[2]);
        Check(!suffix.empty() && suffix.find_first_not_of("0123456789-") == std::string::npos,
              "child receives only a private test channel identifier");
        channel = L"Local\\LumeIconsPortTest-" + std::wstring(suffix.begin(), suffix.end());
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialized");
    INITCOMMONCONTROLSEX common{}; common.dwSize = sizeof(common); common.dwICC = ICC_LISTVIEW_CLASSES;
    Check(InitCommonControlsEx(&common), "standard native list control loaded");
    WNDCLASSEXW shellClass{}; shellClass.cbSize = sizeof(shellClass);
    shellClass.hInstance = GetModuleHandleW(nullptr); shellClass.lpszClassName = L"SHELLDLL_DefView";
    shellClass.lpfnWndProc = DefWindowProcW;
    Check(RegisterClassExW(&shellClass) != 0, "isolated process-local shell-view class");
    HWND host = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", L"Lume 图标接口验证",
                               WS_POPUP | WS_VISIBLE, 80, 80, 300, 180, nullptr, nullptr,
                               shellClass.hInstance, nullptr);
    HWND shell = CreateWindowExW(0, shellClass.lpszClassName, L"", WS_CHILD | WS_VISIBLE,
                                0, 0, 300, 180, host, nullptr, shellClass.hInstance, nullptr);
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOCOLUMNHEADER,
                               0, 0, 300, 180, shell, nullptr, shellClass.hInstance, nullptr);
    Check(host && shell && list, "private native windows created");
    LVCOLUMNW column{}; column.mask = LVCF_WIDTH; column.cx = 290;
    ListView_InsertColumn(list, 0, &column);
    wchar_t name[] = L"验证项目";
    LVITEMW item{}; item.mask = LVIF_TEXT; item.pszText = name;
    Check(ListView_InsertItem(list, &item) == 0, "native item inserted");
    UpdateWindow(host); UpdateWindow(shell); UpdateWindow(list); Pump();
    DesktopIconsTarget target{shell, list, GetCurrentProcessId(), GetCurrentThreadId()};
    if (child) {
        HANDLE mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, channel.c_str());
        HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, (channel + L"-ready").c_str());
        HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, (channel + L"-stop").c_str());
        Check(mapping && ready && stop, "child opens parent-owned test channel");
        auto* shared = static_cast<DesktopIconsTarget*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(target)));
        Check(shared != nullptr, "child maps target handles");
        *shared = target;
        Check(SetEvent(ready), "child reports real window readiness");
        for (;;) {
            const DWORD wake = MsgWaitForMultipleObjectsEx(1, &stop, 15000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wake == WAIT_OBJECT_0 || wake == WAIT_TIMEOUT) break;
            Check(wake == WAIT_OBJECT_0 + 1, "child message wait");
            Pump();
        }
        DestroyWindow(host);
        UnmapViewOfFile(shared);
        CloseHandle(mapping); CloseHandle(ready); CloseHandle(stop);
        CoUninitialize();
        return 0;
    }
    Check(ValidDesktopIconsTarget(target), "validated exact shell and child relationship");
    auto wrong = target; wrong.process = 0;
    Check(!ValidDesktopIconsTarget(wrong) &&
          SetDesktopIconsVisible(wrong, false) == DesktopVisibilityResult::Unavailable,
          "mismatched process never changes windows");
    const auto actualTargets = FindDesktopIconsTargets();
    std::cout << "Read-only real desktop target count: " << actualTargets.size() << '\n';
    if (argc == 2 && std::string(argv[1]) == "--require-desktop")
        Check(!actualTargets.empty(), "current desktop is discoverable");
    for (const auto& actual : actualTargets) {
        Check(actual.process != GetCurrentProcessId() && actual.list != list &&
              ValidDesktopIconsTarget(actual), "discovery excludes imitation windows");
    }
    POINT blank{240, 120}; ClientToScreen(list, &blank);
    POINT occupied{20, 8}; ClientToScreen(list, &occupied);
    Check(HitDesktopIcons(target, blank) == DesktopHit::Blank, "MSAA identifies blank native list area");
    Check(HitDesktopIcons(target, occupied) == DesktopHit::Item, "MSAA rejects icon or text click");
    // STATIC controls can return HTTRANSPARENT; use an ordinary window proc to
    // model an application receiving the click in front of the desktop.
    HWND covering = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, shellClass.lpszClassName, L"",
                                    WS_POPUP | WS_VISIBLE, blank.x - 5, blank.y - 5, 20, 20,
                                    nullptr, nullptr, shellClass.hInstance, nullptr);
    Check(covering != nullptr, "covering application fixture");
    SetWindowPos(covering, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Check(HitDesktopIcons(target, blank) == DesktopHit::Outside, "window over desktop never toggles icons");
    DestroyWindow(covering);
    HANDLE cancelled = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    Check(cancelled != nullptr &&
          SetDesktopIconsVisible(target, false, cancelled) == DesktopVisibilityResult::Cancelled &&
          IsWindowVisible(list), "cancelled request does not hide");
    ResetEvent(cancelled);
    Check(SetDesktopIconsVisible(target, false, cancelled) == DesktopVisibilityResult::Applied &&
          !ObserveDesktopIcons(target).visible, "native hide confirmed by observed visibility");
    Check(HitDesktopIcons(target, blank) == DesktopHit::Blank,
          "hidden list exposes shell blank area for double-click restore");
    Check(SetDesktopIconsVisible(target, true, cancelled) == DesktopVisibilityResult::Applied &&
          ObserveDesktopIcons(target).visible, "native restore confirmed");
    CloseHandle(cancelled);
    DestroyWindow(host);
    Check(!ValidDesktopIconsTarget(target) && !ObserveDesktopIcons(target).available &&
          HitDesktopIcons(target, blank) == DesktopHit::Unavailable &&
          SetDesktopIconsVisible(target, false) == DesktopVisibilityResult::Unavailable,
          "destroyed desktop handles cannot be used");

    // Verify the same adapter against a separate native process, not a fake
    // visibility port. All mutations are limited to that child's private windows.
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    channel = L"Local\\LumeIconsPortTest-" + suffix;
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(target), channel.c_str());
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, (channel + L"-ready").c_str());
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, (channel + L"-stop").c_str());
    Check(mapping && ready && stop, "create private child channels");
    auto* shared = static_cast<DesktopIconsTarget*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(target)));
    Check(shared != nullptr, "parent maps child target");
    wchar_t executable[32768]{};
    Check(GetModuleFileNameW(nullptr, executable, 32768) != 0, "resolve test executable");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --child " + suffix;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    Check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                         nullptr, nullptr, &startup, &process), "start private native child");
    CloseHandle(process.hThread);
    const HANDLE readiness[]{ready, process.hProcess};
    Check(WaitForMultipleObjects(2, readiness, FALSE, 10000) == WAIT_OBJECT_0,
          "child creates real native controls before testing");
    const auto remote = *shared;
    Check(remote.process == process.dwProcessId && ValidDesktopIconsTarget(remote),
          "cross-process target identities match live child");
    POINT remoteBlank{240, 120}; ClientToScreen(remote.list, &remoteBlank);
    POINT remoteItem{20, 8}; ClientToScreen(remote.list, &remoteItem);
    Check(HitDesktopIcons(remote, remoteBlank) == DesktopHit::Blank &&
          HitDesktopIcons(remote, remoteItem) == DesktopHit::Item, "MSAA marshals actual cross-process hit tests");
    for (int cycle = 0; cycle < 12; ++cycle) {
        Check(SetDesktopIconsVisible(remote, false) == DesktopVisibilityResult::Applied &&
              !ObserveDesktopIcons(remote).visible, "cross-process async hide actually applies");
        Check(HitDesktopIcons(remote, remoteBlank) == DesktopHit::Blank,
              "hidden remote list still has a restore hit target");
        Check(SetDesktopIconsVisible(remote, true) == DesktopVisibilityResult::Applied &&
              ObserveDesktopIcons(remote).visible, "cross-process async show actually applies");
    }
    SetEvent(stop);
    Check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "child shuts down with its message loop");
    DWORD result = 1;
    Check(GetExitCodeProcess(process.hProcess, &result) && result == 0, "child exits cleanly");
    Check(!ValidDesktopIconsTarget(remote), "terminated process cannot retain valid desktop target");
    CloseHandle(process.hProcess);
    UnmapViewOfFile(shared); CloseHandle(mapping); CloseHandle(ready); CloseHandle(stop);
    CoUninitialize();
    std::cout << "desktop icon Win32 port verified using private native controls; real shell unchanged\n";
}
