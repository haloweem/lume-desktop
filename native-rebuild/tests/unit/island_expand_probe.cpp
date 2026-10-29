// Explicit probe (not in ctest): the island with liquid or frosted glass
// expands and collapses a few times over the real desktop. Run it with
// LUME_GLASS_LOG=<path> and LUME_GLASS_TIMING=1 to record, per glass worker
// (<path>.<thread id>), each presented frame and when it read the lens size.
// The probe itself prints, on the same steady clock:
//   W t=<ms> size=<w>x<h> at=<x>,<y>   the island window after each composed frame
//   U t=<ms> ms=<ms> msg=   a UI-thread message that took over 8 ms
// Takes no screen samples. Argument: "liquid" (default) or "frosted".
#include "platform/windows/dynamic_island/island_window.h"
#include "launchpad_screen_capture.h"
#include <dwmapi.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

using namespace lume;
namespace {
double Now() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void Pump(unsigned ms) {
    const auto end = GetTickCount64() + ms;
    do {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            const double start = Now();
            DispatchMessageW(&m);
            const double spent = Now() - start;
            if (spent > 8) std::printf("U t=%.2f ms=%.2f msg=0x%x\n", start, spent, m.message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < end);
}
}  // namespace
int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    platform::windows::IslandWindow island(GetModuleHandleW(nullptr), {});
    platform::windows::IslandDisplay display;
    display.material = argc > 1 && !std::strcmp(argv[1], "frosted") ? platform::windows::IslandMaterial::Frosted
                     : argc > 1 && !std::strcmp(argv[1], "plain") ? platform::windows::IslandMaterial::None
                                                                    : platform::windows::IslandMaterial::Liquid;
    display.autoHide = false;
    widget::Preferences prefs;
    prefs.enabled = true;
    if (!island.Start(prefs, display)) { std::cerr << "FAIL: island starts\n"; return 1; }
    std::atomic<bool> sampling{true};
    const HWND window = island.Handle();
    std::thread sampler([&] {
        while (sampling) {
            DwmFlush();
            RECT r{};
            GetWindowRect(window, &r);
            std::printf("W t=%.2f size=%ldx%ld at=%ld,%ld\n", Now(), r.right - r.left, r.bottom - r.top, r.left, r.top);
        }
    });
    Pump(1500);
    // LUME_PROBE_CAPTURE: one screen row through the expanded capsule, every
    // presented frame: S t=<present ms> left=<px> right=<px> where it differs
    // from the same row before expanding (the capsule as actually shown).
    ScreenCapture capture;
    RECT row{};
    const bool capturing = getenv("LUME_PROBE_CAPTURE") != nullptr;
    if (capturing) {
        RECT r{};
        GetWindowRect(window, &r);
        const int middle = (r.left + r.right) / 2, y = r.top + MulDiv(60, GetDpiForWindow(window), 96);
        row = {middle - 500, y, middle + 500, y + 1};
        if (!capture.Start(row)) { std::cerr << "capture failed\n"; return 1; }
    }
    for (int round = 0; round < 4; ++round) {
        island.Expand(true);
        Pump(900);
        island.Expand(false);
        Pump(900);
    }
    if (capturing) {
        const auto frames = capture.Stop();
        const int w = row.right - row.left;
        for (const auto& frame : frames) {
            int left = -1, right = -1;
            for (int x = 0; x < w; ++x)
                if (std::abs(int(frame.luma[x]) - int(frames.front().luma[x])) > 6) { if (left < 0) left = x; right = x; }
            std::printf("S t=%.2f left=%d right=%d\n", frame.ms, left < 0 ? -1 : row.left + left, right < 0 ? -1 : row.left + right + 1);
        }
    }
    sampling = false;
    sampler.join();
    island.Stop();
    std::cout << "island expand probe done\n";
    return 0;
}
