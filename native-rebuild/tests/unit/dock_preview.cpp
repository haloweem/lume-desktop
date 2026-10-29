// Explicit visual probe for the Dock and launchpad over an isolated, topmost
// background. Writes BMP captures; not part of ctest.
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/dock/launchpad_window.h"
#include "platform/windows/dock/dock_spring.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/organizer/organizer_text.h"
#include <objbase.h>
#include <dwmapi.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace lume::platform::windows;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Pump(DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG m{};
        // Synthetic hover: drop the leave notice the real cursor triggers.
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_MOUSELEAVE) continue;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(5);
    } while (GetTickCount64() < end);
}
LRESULT CALLBACK Background(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT r{};
        GetClientRect(hwnd, &r);
        for (LONG y = 0; y < r.bottom; y += 96)
            for (LONG x = 0; x < r.right; x += 96) {
                const COLORREF colors[]{RGB(40, 44, 52), RGB(196, 196, 196), RGB(90, 90, 96), RGB(150, 150, 154)};
                HBRUSH brush = CreateSolidBrush(colors[(x / 96 + y / 96) % 4]);
                RECT cell{x, y, x + 96, y + 96};
                FillRect(dc, &cell, brush);
                DeleteObject(brush);
            }
        EndPaint(hwnd, &paint);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
void Save(const std::filesystem::path& path, RECT region) {
    const int width = region.right - region.left, height = region.bottom - region.top;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HGDIOBJ old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, region.left, region.top, SRCCOPY | CAPTUREBLT);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(width * height * 4);
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
    stream.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    stream.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(width) * height * 4);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}
}  // namespace
int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HWND background = nullptr;
    try {
        Require(argc == 2, "output directory argument");
        const auto output = std::filesystem::u8path(argv[1]);
        std::filesystem::create_directories(output);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpfnWndProc = Background;
        wc.lpszClassName = L"LumeDockIsolatedPreview";
        RegisterClassW(&wc);
        const auto& work = monitor.rcWork;
        background = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP,
                                     work.left, work.top, work.right - work.left, work.bottom - work.top, nullptr, nullptr,
                                     wc.hInstance, nullptr);
        Require(background != nullptr, "background");
        ShowWindow(background, SW_SHOWNOACTIVATE);
        Pump(100);

        std::vector<std::string> pinned{"C:\\Windows\\explorer.exe", "C:\\Windows\\System32\\notepad.exe",
                                        "C:\\Windows\\System32\\cmd.exe"};
        auto items = lume::dock::DockItems(pinned, pinned, {{"C:\\Windows\\System32\\notepad.exe", "Notepad"},
                                                            {"C:\\Windows\\System32\\mspaint.exe", "Paint"}},
                                           OrganizerPathKey);
        std::vector<std::wstring> paths;
        for (const auto& item : items) if (!item.path.empty()) paths.push_back(OrganizerWide(item.path));
        std::vector<lume::dock::Entry> entries;
        for (const auto* name : {"C:\\Windows\\System32\\calc.exe", "C:\\Windows\\System32\\notepad.exe",
                                 "C:\\Windows\\System32\\mspaint.exe", "C:\\Windows\\System32\\cmd.exe",
                                 "C:\\Windows\\explorer.exe", "C:\\Windows\\regedit.exe", "C:\\Windows\\System32\\taskmgr.exe",
                                 "C:\\Windows\\System32\\charmap.exe", "C:\\Windows\\System32\\magnify.exe",
                                 "C:\\Windows\\System32\\osk.exe", "C:\\Windows\\System32\\snippingtool.exe",
                                 "C:\\Windows\\System32\\mstsc.exe", "C:\\Windows", "C:\\Windows\\System32\\drivers\\etc\\hosts"}) {
            entries.push_back({name, lume::dock::EntryName(name), false});
            paths.push_back(OrganizerWide(name));
        }
        for (int i = 0; i < 12; ++i) entries.push_back({"C:\\Windows\\Long" + std::to_string(i) + ".exe", "Long application name number " + std::to_string(i), false});
        OrganizerIconCache icons;
        icons.SetPaths(paths, [] {}, 128);
        const auto deadline = GetTickCount64() + 8000;
        while (icons.CachedPaths() < paths.size() && GetTickCount64() < deadline) Pump(50);
        std::cout << "icons cached " << icons.CachedPaths() << " of " << paths.size() << '\n';

        DockWindow dock(wc.hInstance, {[&](const lume::dock::Item& item) { return icons.Find(OrganizerWide(item.path)); }, {}, {}});
        LaunchpadWindow launchpad(wc.hInstance, {[&] { return entries; },
            [](const std::vector<lume::dock::Entry>& all, const std::string&) { return all; },
            [&](const std::string& path) { return icons.Find(OrganizerWide(path)); }, {}, {}, {}, {}});
        // Both dock layers above the isolated background, icon layer on top.
        const auto raise = [&] {
            SetWindowPos(dock.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowPos(dock.BarHandle(), dock.Handle(), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        };
        // No material, icon reflections, and the icon size range.
        for (const int scale : {100, 50, 120}) {
            DockAppearance look;
            look.transparent = true;
            look.reflection = true;
            look.iconScale = scale;
            Require(dock.Show(items, look), "transparent dock with reflections shows");
            // Desktop widgets refuse outside restacking, so show the real
            // desktop behind the Dock instead of lifting it over the background.
            ShowWindow(background, SW_HIDE);
            Pump(900);
            DwmFlush();
            RECT crop=dock.ScreenBounds();
            crop.top=std::max(work.top,crop.top-100);crop.bottom=std::min(work.bottom,crop.bottom+24);
            crop.left=std::max(work.left,crop.left-64);crop.right=std::min(work.right,crop.right+64);
            Save(output / ("dock-none-reflection-" + std::to_string(scale) + ".bmp"), crop);
            ShowWindow(background, SW_SHOWNOACTIVATE);
        }
        for (const bool frosted : {true, false}) {
            if(getenv("LUME_DOCK_REFLECTION_ONLY"))break;
            const std::string material = frosted ? "frosted" : "liquid";
            DockAppearance shown;
            shown.frosted = frosted;
            // Lift above any other dock on the desktop when asked.
            if (getenv("LUME_DOCK_LIFT")) shown.bottomOffset = 160;
            Require(dock.Show(items, shown), "dock shows");
            raise();
            const auto ready = GetTickCount64() + 5000;
            while (dock.MaterialState() != WidgetMaterialState::Ready && GetTickCount64() < ready) Pump(25);
            // Hover the fourth icon: it magnifies most, neighbours less.
            RECT layer{}, bar{};
            GetWindowRect(dock.Handle(), &layer);
            GetWindowRect(dock.BarHandle(), &bar);
            const auto slot = DockWindow::Slot(items, 3);
            const float scale = static_cast<float>(GetDpiForWindow(dock.Handle())) / 96;
            SendMessageW(dock.Handle(), WM_MOUSEMOVE, 0, MAKELPARAM(
                static_cast<int>(bar.left - layer.left + (slot.x + slot.width / 2) * scale),
                static_cast<int>(bar.top - layer.top + 40 * scale)));
            Pump(700);
            raise();
            Pump(100);
            DwmFlush();
            Save(output / ("dock-hover-" + material + ".bmp"), work);
            // Smoothness probe: sweep the pointer across the Dock at ~120 Hz and
            // record, per composed frame, whether the glass bar moved and how
            // long message handling took.
            {
                LARGE_INTEGER frequency{}, a{}, b{};
                QueryPerformanceFrequency(&frequency);
                double worst = 0, total = 0;
                int frames = 0, moved = 0, events = 0;
                RECT last{};
                GetWindowRect(dock.BarHandle(), &last);
                const float span = DockWindow::Width(items);
                const auto start = GetTickCount64();
                while (GetTickCount64() - start < 1500) {
                    const float t = static_cast<float>(GetTickCount64() - start) / 1500.f;
                    const float x = span * (t < .5f ? t * 2 : 2 - t * 2);
                    QueryPerformanceCounter(&a);
                    SendMessageW(dock.Handle(), WM_MOUSEMOVE, 0, MAKELPARAM(
                        static_cast<int>(bar.left - layer.left + x * scale), static_cast<int>(bar.top - layer.top + 40 * scale)));
                    ++events;
                    MSG m{};
                    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                        if (m.message == WM_MOUSELEAVE) continue;
                        DispatchMessageW(&m);
                    }
                    QueryPerformanceCounter(&b);
                    const double ms = static_cast<double>(b.QuadPart - a.QuadPart) * 1000 / static_cast<double>(frequency.QuadPart);
                    worst = std::max(worst, ms);
                    total += ms;
                    DwmFlush();
                    ++frames;
                    RECT now{};
                    GetWindowRect(dock.BarHandle(), &now);
                    if (now.left != last.left || now.right != last.right) ++moved;
                    last = now;
                }
                std::cout << "dock sweep " << material << ": frames=" << frames << " barMoves=" << moved << " avgMs="
                          << total / events << " worstMs=" << worst << "\n";
            }
            SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0);
            Pump(600);
            Require(launchpad.Prepare(dock.ScreenBounds(), frosted), "launchpad prepares");
            Pump(800);
            Require(launchpad.Show(dock.ScreenBounds(), frosted), "launchpad shows");
            SetWindowPos(launchpad.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            {
                // Opening probe: distinct frame rectangles per composed frame.
                int frames = 0, changes = 0;
                RECT last{};
                GetWindowRect(launchpad.Handle(), &last);
                const double start = AnimationSeconds();
                int hoverTick = 0;
                while (AnimationSeconds() - start < .7) {
                    // Synthetic hover over the grid while it opens (as a pointer resting there would).
                    if (getenv("LUME_DOCK_HOVER") && ++hoverTick % 3 == 0) {
                        RECT lp{}; GetClientRect(launchpad.Handle(), &lp);
                        SendMessageW(launchpad.Handle(), WM_MOUSEMOVE, 0, MAKELPARAM(lp.right / 3 + hoverTick, lp.bottom / 2));
                    }
                    MSG m{};
                    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                        const double before = AnimationSeconds();
                        DispatchMessageW(&m);
                        const double spent = (AnimationSeconds() - before) * 1000;
                        if (spent > 4) std::cout << "[msg 0x" << std::hex << m.message << std::dec << " " << static_cast<int>(spent) << "ms] ";
                    }
                    const double flush = AnimationSeconds();
                    DwmFlush();
                    const double waited = (AnimationSeconds() - flush) * 1000;
                    if (waited > 12) std::cout << "[flush " << static_cast<int>(waited) << "ms] ";
                    ++frames;
                    RECT now{};
                    GetWindowRect(launchpad.Handle(), &now);
                    if (now.left != last.left || now.top != last.top || now.right != last.right) {
                        ++changes;
                        std::cout << static_cast<int>((AnimationSeconds() - start) * 1000) << ":" << (now.right - now.left) << " ";
                    } else {
                        std::cout << "(" << static_cast<int>((AnimationSeconds() - start) * 1000) << " p" << launchpad.Presence() << ") ";
                    }
                    last = now;
                    if (frames == 4) Save(output / ("launchpad-opening-" + material + ".bmp"), work);
                }
                std::cout << "launchpad open " << material << ": frames=" << frames << " frameChanges=" << changes << "\n";
                {
                    const double paintStart = AnimationSeconds();
                    for (int i = 0; i < 20; ++i) launchpad.Paint();
                    std::cout << "launchpad full paint avgMs=" << (AnimationSeconds() - paintStart) * 1000 / 20 << std::endl;
                }
            }
            Pump(500);
            if (launchpad.SearchHandle())
                SetWindowPos(launchpad.SearchHandle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            const auto glass = GetTickCount64() + 5000;
            while (launchpad.MaterialState() != WidgetMaterialState::Ready && GetTickCount64() < glass) Pump(25);
            Pump(300);
            DwmFlush();
            Save(output / ("launchpad-" + material + ".bmp"), work);
            launchpad.Hide();
            Pump(600);
            std::cout << "after close: visible=" << IsWindowVisible(launchpad.Handle()) << " material=" << static_cast<int>(launchpad.MaterialState()) << " animating=" << launchpad.Animating() << std::endl;
        }
        dock.Stop();
        launchpad.Stop();
        icons.Stop();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (background) DestroyWindow(background);
        return 1;
    }
    if (background) DestroyWindow(background);
    std::cout << "dock preview written\n";
    return 0;
}
