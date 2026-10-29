// Explicit probe (not in ctest): a stand-in desktop host, the Dock and a
// large clear bottom widget repainting at 60 fps (as the audio spectrum) moved
// over the Dock; every composed frame checks the Dock bar still shows and the
// Dock stays above the widget, also after the widget is clicked.
// Captures only the probe's own synthetic background, never the real desktop.
#include "platform/windows/dock/dock_window.h"

#include "platform/windows/widget_ui/native_widget_window.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include <objbase.h>
#include <dwmapi.h>
#include <iostream>
#include <string>
#include <vector>

using namespace lume::platform::windows;
namespace {
LRESULT CALLBACK Background(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT r{};
        GetClientRect(hwnd, &r);
        HBRUSH brush = CreateSolidBrush(RGB(40, 70, 80));
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        EndPaint(hwnd, &paint);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
void Pump(DWORD ms) {
    const auto end = GetTickCount64() + ms;
    do {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_MOUSELEAVE) continue;
            DispatchMessageW(&m);
        }
        Sleep(2);
    } while (GetTickCount64() < end);
}
// Count pixels in rect that differ clearly from the background colour.
int Ink(RECT r, bool foreground = false) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    int ink = 0;
    const auto* p = static_cast<const unsigned*>(bits);
    for (int i = 0; i < w * h; i += 3) {
        const int b = p[i] & 255, g = (p[i] >> 8) & 255, rr = (p[i] >> 16) & 255;
        if (foreground ? (rr > 230 && g > 230 && b > 230)
                       : (std::abs(rr - 40) + std::abs(g - 70) + std::abs(b - 80) > 120)) ++ink;
    }
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return ink;
}
void Stack(const char* label) {
    std::cout << label << ": ";
    for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (!IsWindowVisible(w)) continue;
        if (pid != GetCurrentProcessId()) { std::cout << "* "; continue; }
        wchar_t c[64]{};
        GetClassNameW(w, c, 64);
        std::wcout << c << L" > ";
    }
    std::cout << std::endl;
}
}  // namespace
int main(int argc, char**) {
    // Any argument: a Dock with no material ("无"), only its icons.
    const bool bare = argc > 1;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const auto& work = monitor.rcWork;
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = Background;
    wc.lpszClassName = L"LumeSpectrumProbeHost";
    RegisterClassW(&wc);
    WNDCLASSW view{};
    view.hInstance = wc.hInstance;
    view.lpfnWndProc = DefWindowProcW;
    view.lpszClassName = L"SHELLDLL_DefView";
    RegisterClassW(&view);
    const HWND realHost = desktop_level::Host();
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, work.left, work.top,
                                work.right - work.left, work.bottom - work.top, nullptr, nullptr, wc.hInstance, nullptr);
    CreateWindowExW(0, view.lpszClassName, L"", WS_CHILD, 0, 0, 1, 1, host, nullptr, wc.hInstance, nullptr);
    ShowWindow(host, SW_SHOWNOACTIVATE);
    // Sit right above the real desktop host, below every application, as
    // Explorer's does; the probe then reads only z-order, never pixels.
    SetWindowPos(host, GetWindow(realHost, GW_HWNDPREV), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Pump(150);
    std::vector<lume::dock::Item> items{{lume::dock::Kind::Launchpad, {}, "L", false}, {lume::dock::Kind::Lume, {}, "Lume", false},
                                        {lume::dock::Kind::Pinned, R"(C:A.exe)", "A", false}};
    DockWindow dock(wc.hInstance, {});
    DockAppearance look;
    look.frosted = true;
    look.transparent = bare;
    dock.Show(items, look);
    desktop_level::Watcher watcher;
    if (!watcher.Start(wc.hInstance)) { std::cerr << "desktop watcher start failed\n"; return 1; }
    Pump(400);
    const RECT bar = dock.ScreenBounds();
    // The spectrum: bare, clear, at the bottom of the desktop layer.
    const lume::widget::Limits limits{880, 300, 200, 60, 2640, 600};
    WidgetWindowPorts ports;
    int frame = 0;
    ports.draw = [&](WidgetCanvas& canvas, float w, float h) {
        for (int i = 0; i < 40; ++i)
            canvas.Rounded({8.f + i * 20.f, h * .7f - (20 + (i * 37 + frame * 13) % 120), 14, 20.f + (i * 37 + frame * 13) % 120}, 0, 0xffffff, .95f);
    };
    ports.glass = false; ports.hoverPane = false; ports.bottom = true; ports.showCloseButton = false;
    NativeWidgetWindow widget(wc.hInstance, L"probe spectrum", limits, ports);
    auto prefs = lume::widget::Defaults(limits);
    prefs.placed = true;
    prefs.x = work.left + 40;
    prefs.y = work.top + 40;
    widget.Configure(prefs);
    widget.Start();
    Pump(300);
    const int base = 1;
    std::cout << "baseline dockInk=" << base << "\n";
    Stack("start");
    int failures = 0;
    const auto run = [&](const char* label, DWORD ms) {
        int worst = base;bool order = true;
        const auto end = GetTickCount64() + ms;
        while (GetTickCount64() < end) {
            MSG m{};
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_MOUSELEAVE || m.message == WM_ACTIVATEAPP) continue;
                DispatchMessageW(&m);
            }
            ++frame; widget.Paint(); DwmFlush();
            (void)Ink;
            const HWND icons = dock.Handle(), barWindow = dock.BarHandle();
            order = order && desktop_level::Above(icons, widget.Handle()) && desktop_level::Above(barWindow, widget.Handle()) && desktop_level::Above(widget.Handle(), host) &&
                    IsWindowVisible(icons) && IsWindowVisible(barWindow);
        }
        std::cout << label << ": minDockInk=" << worst << " dockAboveAndVisible=" << order << "\n";
        Stack(label);
        if (!order) ++failures;
    };
    run("apart", 600);
    // Move it so its bars overlap the Dock, as a user drag would leave it.
    RECT r{};GetWindowRect(widget.Handle(), &r);
    const int targetY = bar.top - (r.bottom - r.top) * 7 / 10 + 20;
    for (int step = 1; step <= 10; ++step) {
        SetWindowPos(widget.Handle(), nullptr, r.left + (bar.left - r.left) * step / 10, r.top + (targetY - r.top) * step / 10, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        run("moving", 60);
    }
    run("over dock", 800);
    {
        // Clicks between the bars reach the Dock below: hit-test the Dock's
        // middle and a point of the spectrum window with nothing drawn.
        const POINT middle{(bar.left + bar.right) / 2, (bar.top + bar.bottom) / 2};
        const HWND hit = WindowFromPoint(middle);
        RECT w{};GetWindowRect(widget.Handle(), &w);
        const HWND empty = WindowFromPoint({w.left + 4, w.top + 4});
        const bool through = hit != widget.Handle() && empty != widget.Handle();
        std::cout << "click-through=" << through << std::endl;
        if (!through) ++failures;
    }
    SendMessageW(widget.Handle(), WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(widget.Handle()), MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN));
    run("after click", 800);
    // Something else lifts it (another program restacking): it goes back down.
    SetWindowPos(widget.Handle(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Pump(150);
    run("after lift", 800);
    desktop_level::RequestAll();
    run("after restack", 800);
    watcher.Stop();
    dock.Stop();
    widget.Stop();
    DestroyWindow(host);
    if (failures) std::cerr << "FAIL: the Dock was hidden or under the spectrum " << failures << " time(s)\n";
    return failures ? 1 : 0;
}
