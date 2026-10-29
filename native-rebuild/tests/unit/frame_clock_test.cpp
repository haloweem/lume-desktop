#include "platform/windows/dock/frame_clock.h"
#include <windows.h>
#include <atomic>
#include <cstdio>

using lume::platform::windows::FrameClock;

namespace {
std::atomic<int> frames{0};
std::atomic<HWND> target{nullptr};
FrameClock* frameClockRef = nullptr;

LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_APP + 1) {
        if (frameClockRef) frameClockRef->Consumed();
        if (window == target.load()) ++frames;
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

HWND Make() {
    return CreateWindowExW(0, L"LumeFrameClockTest", L"", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
}

void Pump(int milliseconds) {
    const DWORD end = GetTickCount() + static_cast<DWORD>(milliseconds);
    MSG message;
    while (GetTickCount() < end) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
}

bool ExpectFrames(const char* stage, HWND window, int milliseconds) {
    target = window;
    frames = 0;
    Pump(milliseconds);
    const int seen = frames.load();
    std::printf("%s: frames=%d\n", stage, seen);
    return seen > 0;
}
}  // namespace

int main() {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = Procedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"LumeFrameClockTest";
    if (!RegisterClassW(&windowClass)) return 2;

    FrameClock frameClock;
    frameClockRef = &frameClock;
    int failures = 0;
    HWND first = Make();
    if (!first) return 2;
    frameClock.Start(first, WM_APP + 1);
    if (!ExpectFrames("initial", first, 250)) ++failures;

    // Leave a frame message queued, then stop and destroy the old target. The
    // destroyed window can never consume it; Start must not inherit that latch.
    Sleep(40);
    frameClock.Pause();
    DestroyWindow(first);
    HWND second = Make();
    if (!second) return 2;
    frameClock.Start(second, WM_APP + 1);
    if (!ExpectFrames("restart after destroyed target", second, 300)) ++failures;

    // Retarget while the clock is already running. The old window may already
    // have a frame in flight; the new target must still receive frames.
    HWND third = Make();
    if (!third) return 2;
    target = third;
    frameClock.Start(third, WM_APP + 1);
    if (!ExpectFrames("retarget while running", third, 300)) ++failures;

    // Pause and start the same live target again.
    frameClock.Pause();
    frameClock.Start(third, WM_APP + 1);
    if (!ExpectFrames("restart same target", third, 300)) ++failures;

    frameClock.Pause();
    DestroyWindow(second);
    DestroyWindow(third);
    if (failures) std::fprintf(stderr, "FAIL: %d frame-clock lifecycle stage(s) stalled\n", failures);
    return failures ? 1 : 0;
}

