#pragma once
#include <windows.h>
#include <array>
#include <cstdio>
#include <string>

// Diagnoses dropped animation frames on the UI thread, where the Dock, the
// launchpad, the island and every desktop widget share one message loop.
// The loop times each dispatch and keeps the recent slow ones; an animation
// that dropped frames appends them, with its own frame gaps, to a local log.
// UI-thread only; costs two clock reads per message.
namespace lume::platform::windows::ui_stall {
inline double Now() {
    static const double frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return static_cast<double>(f.QuadPart); }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart * 1000.0 / frequency;
}
struct Dispatch {
    double at = 0, ms = 0;
    UINT message = 0;
    wchar_t windowClass[40]{};
};
inline std::array<Dispatch, 64> recent{};
inline std::size_t next = 0;
inline std::wstring logPath;  // empty: no log
inline void SetLogPath(std::wstring path) { logPath = std::move(path); }
// Dispatches one message, remembering it when it took more than 6 ms.
inline void Run(const MSG& message) {
    const double start = Now();
    DispatchMessageW(&message);
    const double ms = Now() - start;
    if (ms <= 6) return;
    auto& slot = recent[next++ % recent.size()];
    slot = {};
    slot.at = start;
    slot.ms = ms;
    slot.message = message.message;
    if (message.hwnd) GetClassNameW(message.hwnd, slot.windowClass, static_cast<int>(std::size(slot.windowClass)));
}
// Appends one line: what the animation was, its dropped-frame gaps (offset
// and length in ms from `start`) and the slow dispatches since `start`.
inline void Report(const char* what, double start, double refreshMs, const std::string& gaps) {
    if (logPath.empty() || gaps.empty()) return;
    FILE* file = _wfopen(logPath.c_str(), L"ab");
    if (!file) return;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u %s refresh=%.1fms gaps:%s | slow:", time.wYear, time.wMonth, time.wDay,
                 time.wHour, time.wMinute, time.wSecond, what, refreshMs, gaps.c_str());
    for (std::size_t i = 0; i < recent.size(); ++i) {
        const auto& d = recent[(next + i) % recent.size()];
        if (d.ms <= 0 || d.at < start - 50) continue;
        char name[40]{};
        WideCharToMultiByte(CP_UTF8, 0, d.windowClass, -1, name, sizeof(name), nullptr, nullptr);
        std::fprintf(file, " %.0fms@%.0f(%s:0x%x)", d.ms, d.at - start, name, d.message);
    }
    std::fputc('\n', file);
    std::fclose(file);
}
}  // namespace lume::platform::windows::ui_stall
