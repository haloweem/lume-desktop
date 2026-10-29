// Opens the real notes window over sample notes in a throwaway directory,
// captures it from the screen, then closes it through the save handshake.
// Usage: lume-notes-window-preview <output directory>. Not part of ctest.
#include "apps/shell/notes_session.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/material/material_api.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <cstring>
#include <vector>

namespace {
void Require(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
void Pump(DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    MSG message{};
    while (GetTickCount64() < end) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}
LRESULT CALLBACK PatternProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const HBRUSH orange = CreateSolidBrush(RGB(245, 106, 34));
        const HBRUSH blue = CreateSolidBrush(RGB(31, 117, 238));
        for (int y = 0; y < client.bottom; y += 64)
            for (int x = 0; x < client.right; x += 64) {
                const RECT tile{x, y, std::min<LONG>(x + 64, client.right), std::min<LONG>(y + 64, client.bottom)};
                FillRect(dc, &tile, GetWindowLongPtrW(window, GWLP_USERDATA) ? blue : ((x + y) / 64) % 2 ? orange : blue);
            }
        DeleteObject(orange);
        DeleteObject(blue);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
HWND CreatePatternWindow() {
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpfnWndProc = PatternProc;
    type.lpszClassName = L"LumeNotesPreviewPattern";
    Require(RegisterClassW(&type) != 0, "pattern window class");
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    Require(GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor) != FALSE, "monitor bounds");
    const RECT area = monitor.rcWork;
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, type.lpszClassName, L"Preview pattern",
                                  WS_POPUP, area.left + (area.right - area.left - width) / 2,
                                  area.top + (area.bottom - area.top - height) / 2, width, height,
                                  nullptr, nullptr, type.hInstance, nullptr);
    Require(window != nullptr, "pattern window created");
    ShowWindow(window, SW_SHOWNOACTIVATE);
    return window;
}
struct Frame {
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
};
Frame Capture(HWND window, const std::filesystem::path &path) {
    RECT r{};
    GetWindowRect(window, &r);
    const int w = r.right - r.left, h = r.bottom - r.top;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    BITMAPFILEHEADER file{0x4d42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + w * h * 4), 0, 0,
                          sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(&file), sizeof(file));
    out.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(static_cast<const char *>(bits), static_cast<std::streamsize>(w) * h * 4);
    Frame result{w, h, std::vector<unsigned char>(static_cast<const unsigned char *>(bits),
                                              static_cast<const unsigned char *>(bits) + w * h * 4)};
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return result;
}
double EmptyAreaDifference(const Frame &a, const Frame &b) {
    Require(a.width == b.width && a.height == b.height, "stable capture dimensions");
    double total = 0;
    unsigned count = 0;
    for (int y = a.height * 7 / 10; y < a.height * 9 / 10; ++y)
        for (int x = a.width * 6 / 10; x < a.width * 9 / 10; ++x)
            for (int channel = 0; channel < 3; ++channel) {
                const auto index = static_cast<std::size_t>((y * a.width + x) * 4 + channel);
                total += std::abs(static_cast<int>(a.pixels[index]) - b.pixels[index]);
                ++count;
            }
    return total / count;
}
unsigned RedLevels(const Frame &frame) {
    std::array<bool, 256> used{};
    const int y = frame.height * 8 / 10;
    for (int x = frame.width * 6 / 10; x < frame.width * 9 / 10; ++x)
        used[frame.pixels[static_cast<std::size_t>((y * frame.width + x) * 4 + 2)]] = true;
    return static_cast<unsigned>(std::count(used.begin(), used.end(), true));
}
HWND FindMaterialSurface(HWND owner) {
    struct Context { HWND owner, found = nullptr; } context{owner};
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto &data = *reinterpret_cast<Context *>(value);
        if (reinterpret_cast<HWND>(GetPropW(window, L"LumeGlassSurfaceOwner")) == data.owner) {
            data.found = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.found;
}
HWND FindNotesWindow() {
    HWND found = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) -> BOOL {
        wchar_t name[64]{};
        GetClassNameW(window, name, static_cast<int>(std::size(name)));
        if (std::wstring(name) == L"LumeNotesWindow") {
            *reinterpret_cast<HWND *>(value) = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}
std::vector<HWND> StickyWindows() {
    std::vector<HWND> result;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) -> BOOL {
        wchar_t name[64]{}; GetClassNameW(window, name, 64);
        if (std::wstring(name) == L"LumeStickyNoteWindow") reinterpret_cast<std::vector<HWND> *>(value)->push_back(window);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
void Key(WORD key, bool up = false) {
    INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key; input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    Require(SendInput(1, &input, sizeof input) == 1, "isolated key input"); Pump(50);
}
void ClickAt(HWND window, int x, int y, unsigned count = 1) {
    POINT point{MulDiv(x, static_cast<int>(GetDpiForWindow(window)), 96), MulDiv(y, static_cast<int>(GetDpiForWindow(window)), 96)};
    ClientToScreen(window, &point); SetCursorPos(point.x, point.y);
    for (unsigned i = 0; i < count; ++i) {
        INPUT inputs[2]{}; inputs[0].type = inputs[1].type = INPUT_MOUSE;
        inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        Require(SendInput(2, inputs, sizeof(INPUT)) == 2, "isolated note click"); Pump(70);
    }
}
void TypeText(const std::wstring &value) {
    for (wchar_t ch : value) {
        INPUT inputs[2]{}; inputs[0].type = inputs[1].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = inputs[1].ki.wScan = ch;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE; inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        Require(SendInput(2, inputs, sizeof(INPUT)) == 2, "isolated text input"); Pump(25);
    }
    Pump(100);
}
unsigned menuSelection = 0;
bool menuDriven = false;
void CALLBACK SelectNoteMenu(HWND, UINT, UINT_PTR, DWORD) {
    if (menuDriven) return;
    HWND menu = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) -> BOOL {
        wchar_t name[64]{}; GetClassNameW(window, name, 64);
        if (std::wstring(name) == L"LumeWidgetMenuWindow" && IsWindowVisible(window)) {
            *reinterpret_cast<HWND *>(value) = window; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&menu));
    if (!menu) return;
    menuDriven = true;
    PostMessageW(menu, WM_KEYDOWN, VK_HOME, 0);
    for (unsigned i = 0; i < menuSelection; ++i) PostMessageW(menu, WM_KEYDOWN, VK_DOWN, 0);
    PostMessageW(menu, WM_KEYDOWN, VK_RETURN, 0);
}
void ChooseNoteMenu(HWND window, unsigned item) {
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(window); Pump(300);
    ClickAt(window, 90, 140);
    menuSelection = item; menuDriven = false;
    const auto timer = SetTimer(nullptr, 0, 150, SelectNoteMenu);
    Key(VK_APPS); Key(VK_APPS, true); Pump(800);
    KillTimer(nullptr, timer);
    Require(menuDriven, "compact context menu opens through WebView");
}
template<class Function> Function Resolve(HMODULE module, const char *name) {
    const auto address = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}
HWND interactionWindow = nullptr;
unsigned interactionStep = 0;
POINT interactionStart{};
bool interactionHeld = false;
void ReleaseInteraction() {
    if (!interactionHeld)
        return;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(input));
    interactionHeld = false;
}
void CALLBACK DrivePointerInteraction(HWND, UINT, UINT_PTR, DWORD) {
    if (interactionStep++ < 5)
        SetCursorPos(interactionStart.x + static_cast<int>(interactionStep) * 8, interactionStart.y);
    else {
        ReleaseInteraction();
        PostMessageW(interactionWindow, WM_LBUTTONUP, 0, 0);
        if (interactionStep > 9)
            PostMessageW(interactionWindow, WM_CANCELMODE, 0, 0);
    }
}
void ExerciseNativeInteraction(HWND window, bool resize) {
    RECT before{}, after{};
    POINT cursor{};
    Require(GetWindowRect(window, &before) && GetCursorPos(&cursor), "native interaction initial geometry");
    interactionWindow = window;
    interactionStep = 0;
    const int dpi = static_cast<int>(GetDpiForWindow(window));
    interactionStart = resize ? POINT{before.right - 3, (before.top + before.bottom) / 2} :
                               POINT{before.left + (before.right - before.left) * 7 / 10, before.top + MulDiv(26, dpi, 96)};
    Require(!(GetAsyncKeyState(VK_LBUTTON) & 0x8000), "test does not move the pointer during a user drag");
    SetCursorPos(interactionStart.x, interactionStart.y);
    Require(GetAncestor(WindowFromPoint(interactionStart), GA_ROOT) == window && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000),
            "test pointer is over its isolated notes window");
    const auto timer = SetTimer(nullptr, 0, 100, DrivePointerInteraction);
    Require(timer != 0, "native interaction timer");
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    interactionHeld = SendInput(1, &input, sizeof(input)) == 1;
    Require(interactionHeld, "test pointer presses the component control");
    Pump(1100);
    ReleaseInteraction();
    KillTimer(nullptr, timer);
    SetCursorPos(cursor.x, cursor.y);
    Require(GetWindowRect(window, &after) != FALSE, "native interaction final geometry");
    const auto change = resize ? (after.right - after.left) - (before.right - before.left) : after.left - before.left;
    std::cout << (resize ? "resize_delta=" : "drag_delta=") << change << " interaction_steps=" << interactionStep << '\n';
    Require(change > 0, resize ? "component resize changes its real window bounds" : "component drag changes its real window position");
    SetWindowPos(window, nullptr, before.left, before.top, before.right - before.left, before.bottom - before.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    Pump(200);
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    Require(argc == 2 || (argc == 3 && std::wstring(argv[2]) == L"--fallback"), "output directory [--fallback]");
    const bool fallback = argc == 3;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
    const std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output);
    const auto data = output / L"data";
    std::filesystem::create_directories(data);
    const auto notesFile = (data / L"notes.json").u8string();
    {
        std::ofstream sample(data / L"notes.json", std::ios::binary);
        sample << u8R"({"version":1,"pinnedId":"","notes":[)"
               << u8R"({"id":"n-1","title":"治愈心情","body":"1.我从没觉得孤独，说的浪漫些，我完全自由\n\n2.夕阳总会落在你身上，你也会快乐一场。\n\n3.路过全世界，只为走向你。\n\n4.于千万人中获你惊鸿，从此连生命也愿笑奉。","createdAt":1780000000000,"updatedAt":1790000000000},)"
               << u8R"({"id":"n-2","title":"Markdown 示例","body":"# 标题\n\n**粗体** 与 *斜体*\n\n- [x] 已完成\n- [ ] 待办","createdAt":1780000000000,"updatedAt":1789000000000},)"
               << u8R"({"id":"n-3","title":"keyi","body":"keyi","createdAt":1780000000000,"updatedAt":1788000000000}]})";
    }
    lume::platform::windows::LocalSettingsFileStore files;
    lume::shell::NotesSessionOptions options;
    options.notesPath = notesFile;
    options.widgetPath = (data / L"notes-widget.json").u8string();
    options.files = &files;
    options.webviewProfile = (output / L"webview").wstring();
    const auto module = fallback ? nullptr : LoadLibraryW(L"LumeGlass.dll");
    Require(fallback || module, "production material module");
    const auto statistics = module ? Resolve<LumeMaterialStatisticsFn>(module, "LumeMaterialStatistics") : nullptr;
    Require(fallback || statistics, "material statistics export");
    const auto assertReleased = [&] {
        if (!statistics) return;
        LumeMaterialStatisticsData stats{};
        stats.size = sizeof(stats);
        Require(statistics(&stats) && !stats.users && !stats.devices && !stats.captures, "closed notes release all material resources");
    };
    assertReleased();
    HWND pattern = CreatePatternWindow();
    Pump(300);
    Capture(pattern, output / L"pattern.bmp");
    {
        lume::shell::NotesSession session(GetModuleHandleW(nullptr), options);
        Require(session.Restore(), "sample notes load");
        Require(session.OpenWindow(), "notes window opens");
        HWND window = FindNotesWindow();
        Require(window != nullptr, "notes window exists");
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Pump(6000);
        Require(session.Active(), "window stays open");
        window = FindNotesWindow();
        Require(window != nullptr, "notes window exists");
        Require((GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION) == 0, "notes window has no system caption or buttons");
        const auto extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        Require((extendedStyle & WS_EX_TOOLWINDOW) != 0 && (extendedStyle & WS_EX_APPWINDOW) == 0,
                "notes uses the component window style without a taskbar item");
        RECT bounds{};
        GetWindowRect(window, &bounds);
        Require(SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(bounds.left + 2, (bounds.top + bounds.bottom) / 2)) == HTLEFT,
                "component left edge retains native resizing");
        Require((GetPropW(window, L"LumeGlassOwner") != nullptr) == !fallback, "notes window material ownership matches availability");
        Require((FindMaterialSurface(window) != nullptr) == !fallback, "notes material surface matches availability");
        SetForegroundWindow(window);
        Pump(800);
        const auto patterned = Capture(window, output / L"notes-window.bmp");
        SetWindowLongPtrW(pattern, GWLP_USERDATA, 1);
        RedrawWindow(pattern, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        Pump(800);
        const auto solid = Capture(window, output / L"notes-window-solid.bmp");
        const auto difference = EmptyAreaDifference(patterned, solid);
        const auto levels = RedLevels(patterned);
        std::cout << "material_visible_difference=" << difference << " red_levels=" << levels << '\n';
        Require(fallback ? difference < 1 : difference > 8, "page pixels respond to captured background only when glass is ready");
        Require(fallback ? levels < 4 : levels > 8, "frosted glass smooths hard checker boundaries");
        ExerciseNativeInteraction(window, false);
        ExerciseNativeInteraction(window, true);
        SendMessageW(window, WM_CLOSE, 0, 0);
        SendMessageW(window, WM_CLOSE, 0, 0);
        Require(IsWindow(window) != FALSE, "repeated close waits for the page save handshake");
        for (int i = 0; i < 300 && session.Active(); ++i)
            Pump(10);
        Require(!session.Active(), "close handshake completes");
        Require(FindMaterialSurface(window) == nullptr, "close removes notes material surface");
        assertReleased();
        Require(session.OpenWindow(), "notes window reopens after releasing the final material lease");
        HWND reopened = FindNotesWindow();
        Require(reopened != nullptr, "reopened notes window exists");
        SetWindowPos(reopened, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Pump(2000);
        Require(session.Active(), "reopened notes stays open");
        reopened = FindNotesWindow();
        Require(reopened != nullptr && (GetPropW(reopened, L"LumeGlassOwner") != nullptr) == !fallback,
                "reopened notes material ownership matches availability");
        PostMessageW(reopened, WM_CLOSE, 0, 0);
        for (int i = 0; i < 300 && session.Active(); ++i)
            Pump(10);
        Require(!session.Active(), "reopened window closes through handshake");
        assertReleased();
        Require(session.HandleMessage(L"{\"version\":1,\"scope\":\"notes\",\"action\":\"pin\",\"id\":\"n-1\",\"value\":true}").has_value(), "pin opens compact note");
        Pump(2500);
        auto sticky = StickyWindows();
        Require(sticky.size() == 1 && !FindNotesWindow(), "pin only opens a stickyWindow note");
        const HWND stickyWindow = sticky.front();
        RECT smallBefore{}, smallAfter{}; GetWindowRect(stickyWindow, &smallBefore);
        const auto dpi = static_cast<int>(GetDpiForWindow(stickyWindow));
        Require(MulDiv(smallBefore.right - smallBefore.left, 96, dpi) == 280, "compact default width retained");
        SetWindowPos(stickyWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(stickyWindow); Pump(400);
        Capture(stickyWindow, output / L"sticky-read.bmp");
        ClickAt(stickyWindow, 70, 150, 2);
        Pump(500);
        Capture(stickyWindow, output / L"sticky-editing.bmp");
        Key(VK_CONTROL); Key('A'); Key('A', true); Key(VK_CONTROL, true);
        TypeText(L"Edited directly in the desktop note");
        Capture(stickyWindow, output / L"sticky-typed.bmp");
        Key(VK_CONTROL); Key(VK_RETURN); Key(VK_RETURN, true); Key(VK_CONTROL, true);
        Pump(1200);
        std::cout << "compact_body=" << lume::notes::Find(session.Snapshot(), "n-1")->body << '\n';
        Require(lume::notes::Find(session.Snapshot(), "n-1")->body == "Edited directly in the desktop note", "double click edits and persists within the compact window");
        GetWindowRect(stickyWindow, &smallAfter);
        Require(EqualRect(&smallBefore, &smallAfter) && !FindNotesWindow(), "editing preserves HWND bounds without opening manager");
        Capture(stickyWindow, output / L"sticky-edited.bmp");
        Require((GetPropW(stickyWindow, L"LumeGlassOwner") != nullptr) == !fallback, "compact note uses production glass material");
        ChooseNoteMenu(stickyWindow, 0);
        Pump(2000);
        Require(StickyWindows().size() == 2 && !FindNotesWindow(), "new desktop note coexists with the existing note");
        ChooseNoteMenu(stickyWindow, 2);
        Pump(1200);
        Require(FindNotesWindow() && StickyWindows().size() == 2, "settings does not replace compact windows");
        SendMessageW(FindNotesWindow(), WM_CLOSE, 0, 0); Pump(800);
        SendMessageW(stickyWindow, WM_CLOSE, 0, 0); Pump(800);
        Require(StickyWindows().size() == 1 && session.Snapshot().notes.size() == 4 &&
                session.Snapshot().desktopNotes.size() == 1, "closing a compact note hides only itself and preserves text");
        session.Shutdown();
        Pump(300);
        assertReleased();
    }
    DestroyWindow(pattern);
    if (module)
        FreeLibrary(module);
    std::cout << "NOTES_WINDOW_PREVIEW_PASS\n";
    CoUninitialize();
    return 0;
}
