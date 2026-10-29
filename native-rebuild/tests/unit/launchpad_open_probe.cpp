// Explicit probe (not in ctest): over a uniform stand-in desktop, the Dock
// prepares the launchpad, then the launchpad opens; every composed frame
// samples how far the launchpad area differs from the background, so a glass
// flash before the opening animation shows as a jump in the first frames.
// Captures only the probe's own synthetic background, never the real desktop.
// LUME_PROBE_REAL=1 opens over the real desktop instead and takes no screen
// samples; with LUME_GLASS_LOG=<path> and LUME_GLASS_TIMING=1 the glass then
// records each presented frame per worker (<path>.<thread id>).
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/dock/launchpad_window.h"
#include "platform/windows/widget_ui/native_widget_window.h"
#include "platform/windows/spectrum/spectrum_view.h"
#include "platform/windows/ui_stall_trace.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "platform/windows/dock/dock_sources.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/organizer/organizer_text.h"
#include "launchpad_screen_capture.h"
#include <cmath>
#include <cstring>
#include <objbase.h>
#include <dwmapi.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <thread>

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
// Best integer shift (|d| <= 6) of a crop of `b` against `a`, by mean absolute difference.
void BestShift(const ScreenFrame& a, const ScreenFrame& b, int w, int h, RECT crop, int& dx, int& dy, double& error) {
    error = 1e9; dx = dy = 0;
    for (int sy = -6; sy <= 6; ++sy)
        for (int sx = -6; sx <= 6; ++sx) {
            double sum = 0; int n = 0;
            for (int y = crop.top; y < crop.bottom; y += 2)
                for (int x = crop.left; x < crop.right; x += 2) {
                    const int bx = x + sx, by = y + sy;
                    if (bx < 0 || by < 0 || bx >= w || by >= h) continue;
                    sum += std::abs(int(a.luma[y * w + x]) - int(b.luma[by * w + bx])); ++n;
                }
            if (n && sum / n < error) { error = sum / n; dx = sx; dy = sy; }
        }
}
// LUME_PROBE_CAPTURE: per presented frame, the difference to the previous and
// to the final frame, and how three crops moved since the previous frame
// (centre, left, right). Uniform scaling about the centre moves the left and
// right crops by equal and opposite amounts; a jolt shows as an outlier.
void SaveCapture(const char* dir, int round, int w, int h, const std::vector<ScreenFrame>& shots) {
    if (shots.empty()) return;
    const auto& last = shots.back();
    const RECT crops[3] = {{w / 2 - 120, h / 2 - 80, w / 2 + 120, h / 2 + 80},
                           {w / 5 - 80, h / 2 - 80, w / 5 + 80, h / 2 + 80},
                           {w * 4 / 5 - 80, h / 2 - 80, w * 4 / 5 + 80, h / 2 + 80}};
    for (std::size_t k = 0; k < shots.size(); ++k) {
        double final = 0, previous = 0;
        for (std::size_t i = 0; i < shots[k].luma.size(); i += 7) {
            final += std::abs(int(shots[k].luma[i]) - int(last.luma[i]));
            if (k) previous += std::abs(int(shots[k].luma[i]) - int(shots[k - 1].luma[i]));
        }
        const double n = static_cast<double>((shots[k].luma.size() + 6) / 7);
        // Change from the first frame in the search-field band and in the grid
        // below it: the field showing ahead of the panel reads as band >> grid.
        const auto band = [&](int top, int bottom) {
            double sum = 0; int count = 0;
            for (int y = h * top / 100; y < h * bottom / 100; ++y)
                for (int x = w * 18 / 100; x < w * 78 / 100; x += 2) {
                    sum += std::abs(int(shots[k].luma[y * w + x]) - int(shots[0].luma[y * w + x])); ++count;
                }
            return count ? sum / count : 0.0;
        };
        std::printf("C r=%d k=%zu t=%.2f dt=%.1f final=%.2f prev=%.2f search=%.2f grid=%.2f", round, k, shots[k].ms - shots[0].ms,
                    k ? shots[k].ms - shots[k - 1].ms : 0.0, final / n, previous / n, band(4, 22), band(30, 90));
        if (k) for (const RECT& crop : crops) {
            int dx, dy; double error;
            BestShift(shots[k - 1], shots[k], w, h, crop, dx, dy, error);
            std::printf(" %+d,%+d/%.1f", dx, dy, error);
        }
        std::printf("\n");
        char path[512];
        std::snprintf(path, sizeof(path), "%s/r%d_%03zu.bmp", dir, round, k);
        BITMAPINFOHEADER header{sizeof(header), w, -h, 1, 24, BI_RGB};
        const int stride = (w * 3 + 3) & ~3;
        std::vector<unsigned char> bits(static_cast<std::size_t>(stride) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) std::memset(&bits[y * stride + x * 3], shots[k].luma[y * w + x], 3);
        BITMAPFILEHEADER file{0x4d42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(header) + bits.size()), 0, 0,
                              sizeof(BITMAPFILEHEADER) + sizeof(header)};
        if (FILE* out = std::fopen(path, "wb")) {
            std::fwrite(&file, sizeof(file), 1, out);
            std::fwrite(&header, sizeof(header), 1, out);
            std::fwrite(bits.data(), 1, bits.size(), out);
            std::fclose(out);
        }
    }
}
// Real mode: every launchpad frame message (WM_APP + 0x7a5) is logged as
// F t=<steady ms when handled> ms=<time spent>.
bool logFrames = false;
void Dispatch(const MSG& m) {
    if (!logFrames || m.message != WM_APP + 0x7a5) { ui_stall::Run(m); return; }
    const auto now = [] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    const double start = now();
    DispatchMessageW(&m);
    std::printf("F t=%.2f ms=%.2f\n", start, now() - start);
}
void Pump(DWORD ms) {
    const auto end = GetTickCount64() + ms;
    do {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_MOUSELEAVE) continue;
            Dispatch(m);
        }
        // Wake on the next message: Sleep(2) lasts a 15.6 ms timer tick and
        // would itself hold the launchpad's frames to about 64 per second.
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 2, QS_ALLINPUT);
    } while (GetTickCount64() < end);
}
}  // namespace
// Mean absolute difference from the background colour, per sampled pixel.
double Change(RECT r) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    double sum = 0; int n = 0;
    const auto* p = static_cast<const unsigned*>(bits);
    for (int i = 0; i < w * h; i += 7) {
        const int b = p[i] & 255, g = (p[i] >> 8) & 255, rr = (p[i] >> 16) & 255;
        sum += std::abs(rr - 40) + std::abs(g - 70) + std::abs(b - 80); ++n;
    }
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
    return n ? sum / n : 0;
}
// Bounding box of pixels clearly off the background colour (above the Dock).
RECT Box(RECT r) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    RECT box{w, h, -1, -1};
    const auto* p = static_cast<const unsigned*>(bits);
    for (int y = 0; y < h * 3 / 4; y += 2) for (int x = 0; x < w; x += 2) {
        const unsigned v = p[y * w + x];
        const int b = v & 255, g = (v >> 8) & 255, rr = (v >> 16) & 255;
        if (std::abs(rr - 40) + std::abs(g - 70) + std::abs(b - 80) > 12) {
            box.left = std::min<LONG>(box.left, x); box.right = std::max<LONG>(box.right, x);
            box.top = std::min<LONG>(box.top, y); box.bottom = std::max<LONG>(box.bottom, y);
        }
    }
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
    return {box.left + r.left, box.top + r.top, box.right + r.left, box.bottom + r.top};
}
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
    wc.lpszClassName = L"LumeLaunchpadOpenProbeHost";
    RegisterClassW(&wc);
    WNDCLASSW view{};
    view.hInstance = wc.hInstance; view.lpfnWndProc = DefWindowProcW; view.lpszClassName = L"SHELLDLL_DefView";
    RegisterClassW(&view);
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, work.left, work.top,
                                work.right - work.left, work.bottom - work.top, nullptr, nullptr, wc.hInstance, nullptr);
    CreateWindowExW(0, view.lpszClassName, L"", WS_CHILD, 0, 0, 1, 1, host, nullptr, wc.hInstance, nullptr);
    // LUME_PROBE_REAL: open over the real desktop so the glass samples real
    // sources; the probe then takes no screen samples (glass timing only).
    const bool real = getenv("LUME_PROBE_REAL") != nullptr;
    logFrames = real;
    if (const char* log = getenv("LUME_PROBE_FRAME_LOG")) ui_stall::SetLogPath(std::wstring(log, log + strlen(log)));
    if (!real) ShowWindow(host, SW_SHOWNOACTIVATE);
    Pump(150);
    std::vector<lume::dock::Item> items{{lume::dock::Kind::Launchpad, {}, "L", false}, {lume::dock::Kind::Pinned, "C:/A/a.exe", "A", false}};
    DockWindow dock(wc.hInstance, {});
    DockAppearance look; look.frosted = true;
    dock.Show(items, look);
    std::vector<lume::dock::Entry> entries;
    for (int i = 0; i < 25; ++i) entries.push_back({"C:/I/" + std::to_string(i), "item " + std::to_string(i), false});
    // LUME_PROBE_DESKTOP (with LUME_PROBE_REAL): the user's real desktop
    // entries and Shell icons, loaded as the Dock session does: each show
    // requests the icons, and loaded ones repaint the launchpad.
    const bool desktop = real && getenv("LUME_PROBE_DESKTOP") != nullptr;
    if (desktop)
        entries = lume::dock::LaunchpadItems(dock_sources::DesktopPaths(), {}, {}, dock_sources::PathKey,
                                             dock_sources::SearchKey);
    OrganizerIconCache icons;
    static LaunchpadWindow* iconTarget = nullptr;
    WNDCLASSW helperClass{};
    helperClass.hInstance = wc.hInstance;
    helperClass.lpszClassName = L"LumeLaunchpadOpenProbeIcons";
    helperClass.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_APP + 1 && iconTarget) {
            const double start = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
            iconTarget->Invalidate();
            std::printf("I t=%.2f ms=%.2f\n", start, std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now().time_since_epoch()).count() - start);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    };
    RegisterClassW(&helperClass);
    const HWND helper = CreateWindowExW(0, helperClass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    LaunchpadPorts ports{[&] { return entries; }, [](const auto& all, const std::string&) { return all; }};
    if (desktop) {
        ports.icon = [&](const std::string& path) { return icons.Find(OrganizerWide(path)); };
        ports.visibility = [&] {
            std::vector<std::wstring> paths;
            for (const auto& entry : lume::dock::LaunchpadItems(dock_sources::DesktopPaths(), {}, {}, dock_sources::PathKey,
                                                                dock_sources::SearchKey))
                paths.push_back(OrganizerWide(entry.path));
            icons.SetPaths(paths, [helper] { PostMessageW(helper, WM_APP + 1, 0, 0); },
                           std::min(256u, static_cast<unsigned>(std::ceil(52.f * GetDpiForSystem() / 96))));
        };
        std::cout << "desktop entries: " << entries.size() << std::endl;
    }
    // LUME_PROBE_SPECTRUM (with LUME_PROBE_REAL): a stand-in audio spectrum
    // as in the app while sound plays: the same bottom, glass-free widget at
    // its default size above the Dock, redrawn on the UI thread every 10 ms.
    std::vector<float> levels(96, 0.f);
    WidgetWindowPorts spectrumPorts{
        [&](WidgetCanvas& c, float w, float h) {
            DrawSpectrumPulse(c, w, h, levels, lume::spectrum::BarStyle{9, 1});
        },
        {}, {}, {}};
    spectrumPorts.glass = false;
    spectrumPorts.hoverPane = false;
    spectrumPorts.bottom = true;
    spectrumPorts.showCloseButton = false;
    NativeWidgetWindow spectrumWindow(wc.hInstance, L"probe spectrum", lume::spectrum::WindowLimits, spectrumPorts);
    std::atomic<bool> playing{false};
    std::thread audio;
    if (real && getenv("LUME_PROBE_SPECTRUM")) {
        spectrumPorts.update = [&] {
            const double t = GetTickCount64() / 1000.0;
            for (std::size_t i = 0; i < levels.size(); ++i)
                levels[i] = static_cast<float>(.5 + .45 * std::sin(t * 7 + i * .37) * std::cos(t * 3 + i * .11));
            spectrumWindow.Paint();
        };
        auto prefs = lume::widget::Defaults(lume::spectrum::WindowLimits);
        prefs.enabled = true;
        prefs.placed = true;
        prefs.x = (work.left + work.right - prefs.width) / 2;
        prefs.y = work.bottom - prefs.height - 120;
        spectrumWindow.Configure(prefs);
        spectrumWindow.Start();
        playing = true;
        audio = std::thread([&] {
            while (playing) {
                PostMessageW(spectrumWindow.Handle(), NativeWidgetWindow::UpdateMessage, 0, 0);
                Sleep(10);
            }
        });
        std::cout << "spectrum stand-in running" << std::endl;
    }
    LaunchpadWindow launchpad(wc.hInstance, ports);
    iconTarget = &launchpad;
    Pump(300);
    const RECT bar = dock.ScreenBounds();
    // Sample a band of the launchpad area above the Dock, clear of the Dock.
    const RECT area{work.left + (work.right - work.left) / 4, work.top + (work.bottom - work.top) / 4,
                    work.right - (work.right - work.left) / 4, bar.top - 40};
    int failures = 0;
    // Real mode: a separate thread prints the launchpad window size after
    // each composed frame (W t=<steady ms> size=<w>x<h>), for comparison with
    // the glass frame log on the same clock.
    std::atomic<bool> sampling{real};
    std::thread sampler;
    if (real) sampler = std::thread([&] {
        while (sampling) {
            DwmFlush();
            RECT r{};
            GetWindowRect(launchpad.Handle(), &r);
            std::printf("W t=%.2f size=%ldx%ld\n", std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now().time_since_epoch()).count(), r.right - r.left, r.bottom - r.top);
        }
    });
    const int rounds = getenv("LUME_PROBE_ROUNDS") ? std::max(1, atoi(getenv("LUME_PROBE_ROUNDS"))) : 4;
    for (int round = 0; round < rounds; ++round) {
        if (!getenv("LUME_PROBE_RAPID") || round == 0) launchpad.Prepare(bar, !getenv("LUME_PROBE_LIQUID"));
        if (!getenv("LUME_PROBE_RAPID") || round == 0) Pump(round == 0 ? 1500 : 600);
        const double base = real ? 0 : Change(area);
        if (real) std::printf("S t=%.2f\n", std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        // LUME_PROBE_CAPTURE=<dir>: record the launchpad rect as shown on screen,
        // every presented frame, through the opening and a while at rest.
        ScreenCapture capture;
        const char* captureDir = real ? getenv("LUME_PROBE_CAPTURE") : nullptr;
        if (captureDir) {
            RECT r{};
            GetWindowRect(launchpad.Handle(), &r);
            if (!capture.Start(r)) { std::cerr << "capture failed\n"; captureDir = nullptr; }
        }
        launchpad.Show(bar, !getenv("LUME_PROBE_LIQUID"));
        std::vector<double> frames;
        // LUME_PROBE_MIDCLOSE=<frames>: close after that many frames of the opening.
        const int frameCount = getenv("LUME_PROBE_MIDCLOSE") ? std::max(1, atoi(getenv("LUME_PROBE_MIDCLOSE"))) : 24;
        for (int i = 0; i < frameCount; ++i) {
            MSG m{};
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_MOUSELEAVE || m.message == WM_ACTIVATEAPP) continue;
                LARGE_INTEGER a{}, b{}, q{}; QueryPerformanceCounter(&a);
                Dispatch(m);
                QueryPerformanceCounter(&b); QueryPerformanceFrequency(&q);
                const double ms = (b.QuadPart - a.QuadPart) * 1000.0 / q.QuadPart;
                // Real mode: slow UI messages on the steady clock of the W/T logs.
                if (ms > 4 && real)
                    std::printf("U t=%.2f ms=%.2f msg=0x%x\n", std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now().time_since_epoch()).count() - ms, ms, m.message);
                else if (ms > 4 && getenv("LUME_PROBE_GEOMETRY")) std::cout << "  slow message 0x" << std::hex << m.message << std::dec << " " << ms << " ms in frame " << i << std::endl;
            }
            DwmFlush();
            // LUME_PROBE_ICONS: as in the app, icons finish loading and the
            // desktop list refreshes while the launchpad is still opening.
            if (getenv("LUME_PROBE_ICONS") && i == 8) launchpad.Invalidate();
            if (getenv("LUME_PROBE_ICONS") && i == 12) launchpad.Refresh();
            frames.push_back(real ? 0 : Change(area));
            // LUME_PROBE_SNAPSHOT=<dir>: save the launchpad's final rect as it
            // shows on screen in round 0, mid-opening (frame 4) and at rest (23).
            // LUME_PROBE_TURN: at frame 18 turn to page 2, as the wheel does.
            if (getenv("LUME_PROBE_TURN") && round == 0 && i == 14) launchpad.Turn(1);
            if (const char* dir = getenv("LUME_PROBE_SNAPSHOT"); dir && round == 0 && (i == 4 || i == 23)) {
                RECT r{};
                GetWindowRect(launchpad.Handle(), &r);
                const int w = r.right - r.left, h = r.bottom - r.top;
                HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
                HBITMAP shot = CreateCompatibleBitmap(screen, w, h);
                HGDIOBJ previous = SelectObject(memory, shot);
                BitBlt(memory, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
                SelectObject(memory, previous);
                BITMAPINFOHEADER header{sizeof(header), w, h, 1, 24, BI_RGB};
                const int stride = (w * 3 + 3) & ~3;
                std::vector<unsigned char> bits(static_cast<std::size_t>(stride) * h);
                GetDIBits(memory, shot, 0, h, bits.data(), reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS);
                BITMAPFILEHEADER file{0x4d42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(header) + bits.size()), 0, 0,
                                      sizeof(BITMAPFILEHEADER) + sizeof(header)};
                const std::string path = std::string(dir) + (i == 4 ? "/opening.bmp" : "/settled.bmp");
                std::cout << "snapshot " << path << " " << w << "x" << h << std::endl;
                if (FILE* out = std::fopen(path.c_str(), "wb")) {
                    std::fwrite(&file, sizeof(file), 1, out);
                    std::fwrite(&header, sizeof(header), 1, out);
                    std::fwrite(bits.data(), 1, bits.size(), out);
                    std::fclose(out);
                }
                DeleteObject(shot); DeleteDC(memory); ReleaseDC(nullptr, screen);
            }
            if (!real && getenv("LUME_PROBE_GEOMETRY")) {
                RECT win{}; GetWindowRect(launchpad.Handle(), &win);
                const RECT glass = Box(work);
                std::cout << "  f" << i << " window=" << win.left << "," << win.top << "-" << win.right << "," << win.bottom
                          << " seen=" << glass.left << "," << glass.top << "-" << glass.right << "," << glass.bottom << std::endl;
            }
        }
        if (!real && getenv("LUME_PROBE_SETTLE")) {
            // After the spring: the hand-off to the live search field. Prints
            // each frame's interval and the change in the panel and its search band.
            RECT win{}; GetWindowRect(launchpad.Handle(), &win);
            const RECT band{win.left + (win.right - win.left) / 4, win.top, win.right - (win.right - win.left) / 4,
                            win.top + (win.bottom - win.top) / 8};
            LARGE_INTEGER q{}, last{}; QueryPerformanceFrequency(&q); QueryPerformanceCounter(&last);
            for (int i = 0; i < 50; ++i) {
                MSG m{};
                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                    if (m.message == WM_MOUSELEAVE || m.message == WM_ACTIVATEAPP) continue;
                    LARGE_INTEGER a{}, b{}; QueryPerformanceCounter(&a);
                    DispatchMessageW(&m);
                    QueryPerformanceCounter(&b);
                    const double ms = (b.QuadPart - a.QuadPart) * 1000.0 / q.QuadPart;
                    if (ms > 2) std::cout << "  slow message 0x" << std::hex << m.message << std::dec << " " << ms << " ms" << std::endl;
                }
                DwmFlush();
                LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
                std::cout << "  s" << i << " dt=" << (now.QuadPart - last.QuadPart) * 1000.0 / q.QuadPart
                          << " area=" << Change(area) << " band=" << Change(band) << std::endl;
                last = now;
            }
        }
        const double final = frames.back();
        std::cout << "round " << round << " base=" << base << " frames:";
        for (const double v : frames) std::cout << ' ' << static_cast<int>(v);
        std::cout << "\n";
        // A smooth open: no full-strength flash in the first frames, no long
        // stall before anything shows, and no single-frame jump (the glass
        // arriving after the icons). Values are skipped when something else
        // covered the sample area before the open.
        double jump = 0; int stall = 0;
        for (std::size_t i = 1; i < frames.size(); ++i) jump = std::max(jump, frames[i] - frames[i - 1]);
        while (stall < static_cast<int>(frames.size()) && frames[stall] - base < 2) ++stall;
        if (base < 4 && final > base + 4 && (jump > (final - base) * .45 || stall > 5)) ++failures;
        if (final > base + 4 && (frames[0] - base > (final - base) * .6 || frames[1] - base > (final - base) * .8)) ++failures;
        // LUME_PROBE_HOVER (real mode): once opened, move the pointer onto the
        // panel and print the panel's mean brightness each frame (M lines), so
        // a flash on hover shows as a jump. Samples only that number.
        if (real && getenv("LUME_PROBE_HOVER")) {
            Pump(getenv("LUME_PROBE_HOVER_MS") ? static_cast<DWORD>(atoi(getenv("LUME_PROBE_HOVER_MS"))) : 700);
            POINT restore{};
            GetCursorPos(&restore);
            RECT r{};
            GetWindowRect(launchpad.Handle(), &r);
            const RECT inner{r.left + 40, r.top + 40, r.right - 40, r.bottom - 40};
            std::string trace = "M round " + std::to_string(round) + ":";
            for (int k = 0; k < 60; ++k) {
                if (k == 5) SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
                if (k == 20) SetCursorPos(r.left + (r.right - r.left) / 5, r.top + (r.bottom - r.top) / 3);
                MSG m{};
                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) Dispatch(m);
                DwmFlush();
                const RECT a{inner.left, inner.top, inner.right, inner.bottom};
                HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
                const int w = a.right - a.left, h = a.bottom - a.top;
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
                info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
                void* bits = nullptr;
                HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
                HGDIOBJ old = SelectObject(memory, bitmap);
                BitBlt(memory, 0, 0, w, h, screen, a.left, a.top, SRCCOPY | CAPTUREBLT);
                double sum = 0; int n = 0;
                const auto* p = static_cast<const unsigned*>(bits);
                for (int i = 0; i < w * h; i += 13) { sum += (p[i] & 255) + ((p[i] >> 8) & 255) + ((p[i] >> 16) & 255); ++n; }
                SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
                char value[48]{}; std::snprintf(value, sizeof(value), " %.0f:%.1f", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(), n ? sum / n / 3 : 0); trace += value;
            }
            std::printf("%s\n", trace.c_str());
            SetCursorPos(restore.x, restore.y);
        }
        if (real && !getenv("LUME_PROBE_MIDCLOSE")) Pump(700);  // let the opening finish before closing
        // LUME_PROBE_TYPE: type into the search field at rest (round 0), then clear it.
        if (real && getenv("LUME_PROBE_TYPE") && round == 0) {
            for (const wchar_t c : std::wstring(L"Bl")) { SendMessageW(launchpad.SearchHandle(), WM_CHAR, c, 0); Pump(150); }
            Pump(400);
        }
        if (captureDir) {
            const int w = capture.Width(), h = capture.Height();
            const auto shots = capture.Stop();
            SaveCapture(captureDir, round, w, h, shots);
        }
        if (real) std::printf("H t=%.2f\n", std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        launchpad.Hide();
        // LUME_PROBE_REOPEN: reopen while the close is still running, as when
        // the Dock's launchpad icon is clicked twice quickly.
        if (real && getenv("LUME_PROBE_REOPEN")) {
            Pump(90);
            std::printf("R t=%.2f\n", std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            launchpad.Show(bar, !getenv("LUME_PROBE_LIQUID"));
            Pump(700);
            launchpad.Hide();
        }
        // LUME_PROBE_SNAPSHOT: one frame of the close as well (round 0, ~6 frames in).
        if (const char* dir = getenv("LUME_PROBE_SNAPSHOT"); dir && round == 0) {
            for (int k = 0; k < 6; ++k) { MSG m{}; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m); DwmFlush(); }
            RECT r{};
            GetWindowRect(launchpad.Handle(), &r);
            const int w = r.right - r.left, h = r.bottom - r.top;
            HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
            HBITMAP shot = CreateCompatibleBitmap(screen, w, h);
            HGDIOBJ previous = SelectObject(memory, shot);
            BitBlt(memory, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
            SelectObject(memory, previous);
            BITMAPINFOHEADER header{sizeof(header), w, h, 1, 24, BI_RGB};
            const int stride = (w * 3 + 3) & ~3;
            std::vector<unsigned char> bits(static_cast<std::size_t>(stride) * h);
            GetDIBits(memory, shot, 0, h, bits.data(), reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS);
            BITMAPFILEHEADER file{0x4d42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(header) + bits.size()), 0, 0,
                                  sizeof(BITMAPFILEHEADER) + sizeof(header)};
            if (FILE* out = std::fopen((std::string(dir) + "/closing.bmp").c_str(), "wb")) {
                std::fwrite(&file, sizeof(file), 1, out);
                std::fwrite(&header, sizeof(header), 1, out);
                std::fwrite(bits.data(), 1, bits.size(), out);
                std::fclose(out);
            }
            DeleteObject(shot); DeleteDC(memory); ReleaseDC(nullptr, screen);
        }
        // LUME_PROBE_RAPID=<ms>: reopen that soon after closing (a user clicking
        // the Dock icon again and again), without the Dock's prepare.
        Pump(getenv("LUME_PROBE_RAPID") ? static_cast<DWORD>(atoi(getenv("LUME_PROBE_RAPID"))) : 700);
    }
    sampling = false;
    playing = false;
    if (audio.joinable()) audio.join();
    spectrumWindow.Stop();
    if (sampler.joinable()) sampler.join();
    launchpad.Stop();
    dock.Stop();
    DestroyWindow(host);
    if (failures) std::cerr << "FAIL: the launchpad glass appeared at full strength before opening " << failures << " time(s)\n";
    return failures ? 1 : 0;
}
