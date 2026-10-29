// Explicit probe (not in ctest): a full-screen stand-in desktop host (with a
// SHELLDLL_DefView child, so widgets level above it), the Dock at desktop
// level and a desktop widget; then the launchpad opens and every composed
// frame checks that the Dock icons and the widget's foreground stay visible.
// Captures only the probe's own synthetic background, never the real desktop.
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/dock/launchpad_window.h"
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
        if (pid != GetCurrentProcessId() || !IsWindowVisible(w)) continue;
        wchar_t c[64]{};
        GetClassNameW(w, c, 64);
        std::wcout << c << L" > ";
    }
    std::cout << std::endl;
}
}  // namespace
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const auto& work = monitor.rcWork;
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = Background;
    wc.lpszClassName = L"LumeStackingProbeHost";
    RegisterClassW(&wc);
    WNDCLASSW view{};
    view.hInstance = wc.hInstance;
    view.lpfnWndProc = DefWindowProcW;
    view.lpszClassName = L"SHELLDLL_DefView";
    RegisterClassW(&view);
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, work.left, work.top,
                                work.right - work.left, work.bottom - work.top, nullptr, nullptr, wc.hInstance, nullptr);
    CreateWindowExW(0, view.lpszClassName, L"", WS_CHILD, 0, 0, 1, 1, host, nullptr, wc.hInstance, nullptr);
    ShowWindow(host, SW_SHOWNOACTIVATE);
    Pump(150);
    // A desktop widget with white foreground strokes.
    const lume::widget::Limits limits{340, 216, 320, 204, 720, 720};
    WidgetWindowPorts ports;
    ports.draw = [](WidgetCanvas& canvas, float w, float h) {
        for (int i = 0; i < 6; ++i) canvas.Rounded({20.f, 20.f + i * 28.f, w - 40, 14}, 4, 0xffffff, 1);
        (void)h;
    };
    NativeWidgetWindow widget(wc.hInstance, L"probe widget", limits, ports);
    auto prefs = lume::widget::Defaults(limits);
    prefs.placed = true;
    prefs.x = work.left + 60;
    prefs.y = work.top + 60;
    widget.Configure(prefs);
    widget.Start();
    std::vector<lume::dock::Item> items{{lume::dock::Kind::Launchpad, {}, "L", false}, {lume::dock::Kind::Lume, {}, "Lume", false},
                                        {lume::dock::Kind::Pinned, "C:\\A\\a.exe", "A", false}};
    DockWindow dock(wc.hInstance, {});
    dock.Show(items, true);
    std::vector<lume::dock::Entry> entries;
    for (int i = 0; i < 25; ++i) entries.push_back({"C:\\I\\" + std::to_string(i), "item " + std::to_string(i), false});
    LaunchpadWindow launchpad(wc.hInstance, {[&] { return entries; }, [](const auto& all, const std::string&) { return all; },
                                             {}, {}, {}, {}, {}, {}, {}, {}, {}});
    desktop_level::Watcher watcher;
    if (!watcher.Start(wc.hInstance)) { std::cerr << "desktop watcher start failed\n"; return 1; }
    Pump(200);  // sample during the startup restack window, not after warm-up
    RECT bar = dock.ScreenBounds(), widgetRect{};
    GetWindowRect(widget.Handle(), &widgetRect);
    InflateRect(&widgetRect, -10, -10);
    const int dockBase = Ink(bar), widgetBase = Ink(widgetRect, true);
    int failures = widgetBase < 100 ? 1 : 0;
    std::cout << "baseline dockInk=" << dockBase << " widgetInk=" << widgetBase << "\n";
    for (int round = 0; round < 6; ++round) {
        if (round == 0) launchpad.Prepare(bar, true);
        Pump(round == 0 ? 1200 : 300);
        launchpad.Show(bar, true);
        int worstDock = dockBase, worstWidget = widgetBase, frames = 0;
        const auto end = GetTickCount64() + 900;
        while (GetTickCount64() < end) {
            MSG m{};
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_MOUSELEAVE || m.message == WM_ACTIVATEAPP) continue;
                DispatchMessageW(&m);
            }
            DwmFlush();
            ++frames;
            worstDock = std::min(worstDock, Ink(bar));
            worstWidget = std::min(worstWidget, Ink(widgetRect, true));
        }
        Stack("stack while open");
        std::cout << "round " << round << ": frames=" << frames << " minDockInk=" << worstDock << " minWidgetInk=" << worstWidget << "\n";
        launchpad.Hide();
        Pump(500);
        std::cout << "  after hide dockInk=" << Ink(bar) << " widgetInk=" << Ink(widgetRect, true) << std::endl;
        if (worstWidget * 2 < widgetBase) ++failures;
        Stack("stack after hide");
    }
    launchpad.Stop();
    watcher.Stop();
    dock.Stop();
    widget.Stop();
    DestroyWindow(host);
    if (failures) std::cerr << "FAIL: startup launchpad lost a widget foreground frame\n";
    return failures ? 1 : 0;
}
