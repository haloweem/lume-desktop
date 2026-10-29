#include "core/clock/clock_controller.h"
#include "core/ui/clock_page_state.h"
#include "platform/windows/clock_runtime.h"
#include "platform/windows/webview_page_window.h"
#include "process_memory_snapshot.h"

#include <objbase.h>
#include <windows.h>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
using lume::clock::ClockController;
using lume::platform::windows::NativeClockRuntime;
using lume::platform::windows::WebViewPageWindow;

HWND FindOwnWindow(const wchar_t* className) {
    struct Search { const wchar_t* name; HWND found; } search{className, nullptr};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM context) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(context);
        wchar_t name[128]{};
        if (GetClassNameW(window, name, 128) && std::wcscmp(name, search.name) == 0) {
            search.found = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

struct Probe {
    std::filesystem::path phasePath;
    ClockController* controller = nullptr;
    WebViewPageWindow* manager = nullptr;
    HWND clockWindow = nullptr;
    ULONGLONG stageAt = 0;
    ULONGLONG deadline = 0;
    DWORD dwellMs = 4000;
    int round = 0;
    int stage = 0;
    int readyCount = 0;
    bool passed = true;
    bool clockEnabled = true;

    bool Check(bool value, const char* reason) {
        if (!value) { passed = false; std::cerr << "FAIL: " << reason << std::endl; }
        return value;
    }
    void Phase(const char* name) {
        stageAt = GetTickCount64();
        const auto temporary = phasePath.wstring() + L".tmp";
        {
            std::ofstream stream{std::filesystem::path(temporary)};
            stream << "{\"phase\":\"" << name << "\",\"round\":" << round
                   << ",\"pid\":" << GetCurrentProcessId() << ",\"tickMs\":" << stageAt << "}";
            Check(stream.good(), "write phase marker");
        }
        bool published = false;
        DWORD error = ERROR_SUCCESS;
        // A sampler or file scanner may briefly hold the destination open.
        // Retry only sharing/access conflicts, with a strict one-second bound.
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (MoveFileExW(temporary.c_str(), phasePath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                published = true;
                break;
            }
            error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) break;
            Sleep(10);
        }
        if (!published) std::cerr << "phase publication error=" << error << std::endl;
        Check(published, "publish phase marker");
        std::cout << "PHASE " << name << " round=" << round << " tick=" << stageAt << std::endl;
    }
    bool ClockAlive() {
        if (!clockEnabled) return Check(!controller->Running() &&
            !FindOwnWindow(L"LumeNativeDesktopClock"), "manager-only mode has no clock");
        return Check(controller->Running() && IsWindow(clockWindow) &&
            FindOwnWindow(L"LumeNativeDesktopClock") == clockWindow, "same native clock remains alive");
    }
    void Snapshot(const char* name) {
        // Move the externally sampled phase away from the steady state before diagnosis.
        Phase("diagnostic");
        Check(WriteProcessMemorySnapshot(phasePath.parent_path() / "memory-snapshots.jsonl", name, round),
              "write process memory snapshot");
    }
};
Probe* active = nullptr;

void PumpFor(DWORD duration) {
    const auto end = GetTickCount64() + duration;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
}

void CALLBACK ObserveManager(HWND, UINT, UINT_PTR, DWORD) {
    auto& probe = *active;
    if (!probe.ClockAlive() || GetTickCount64() >= probe.deadline) {
        probe.Check(false, "manager observation or timeout");
        probe.manager->RequestClose();
        return;
    }
    if (probe.stage == 0 || GetTickCount64() - probe.stageAt < probe.dwellMs) return;
    if (probe.stage == 1) {
        probe.Snapshot("manager_open");
        const auto managerWindow = FindOwnWindow(L"LumeOriginalPageWindow");
        ShowWindow(managerWindow, SW_MINIMIZE);
        probe.Check(managerWindow && IsIconic(managerWindow), "manager minimizes");
        probe.stage = 2;
        probe.Phase("manager_minimized");
    } else if (probe.stage == 2) {
        probe.Snapshot("manager_minimized");
        probe.stage = 3;
        probe.manager->RequestClose();
    }
}

int Run(const std::filesystem::path& phasePath, int rounds, DWORD dwellMs,
        const std::string& mode, DWORD cooldownMs) {
    const auto instance = GetModuleHandleW(nullptr);
    int unexpectedActions = 0;
    NativeClockRuntime runtime(instance, [&](lume::clock::Action action) {
        ++unexpectedActions;
        std::cerr << "UNEXPECTED_ACTION type=" << static_cast<int>(action.type)
                  << " value=" << action.value << " secondary=" << action.secondary
                  << " tick=" << GetTickCount64() << std::endl;
    });
    int saves = 0;
    ClockController controller(lume::settings::ClockSettings{}, runtime,
        [&](const lume::settings::ClockSettings&) { ++saves; return true; });
    WebViewPageWindow manager;
    Probe probe;
    probe.phasePath = phasePath;
    probe.controller = &controller;
    probe.manager = &manager;
    probe.dwellMs = dwellMs;
    probe.clockEnabled = mode != "manager-only";
    active = &probe;
    probe.Check(controller.Restore() && !controller.Running(), "isolated disabled initial settings");
    probe.Phase("initial_disabled");
    PumpFor(dwellMs);
    probe.Snapshot("initial_disabled");
    for (int round = 1; round <= rounds && probe.passed; ++round) {
        probe.round = round;
        if (probe.clockEnabled) {
            probe.Check(controller.Handle({lume::clock::ActionType::Enable, 1}), "enable clock");
            probe.clockWindow = FindOwnWindow(L"LumeNativeDesktopClock");
            probe.ClockAlive();
            // Resource sampling is intentionally noninteractive; input acceptance has its own test.
            // Disable only this probe's HWND, preserving visibility, rendering, and minute timers.
            EnableWindow(probe.clockWindow, FALSE);
            probe.Check(!IsWindowEnabled(probe.clockWindow), "isolate probe clock from user input");
            probe.Phase("clock_only_before");
            PumpFor(dwellMs);
            probe.Snapshot("clock_only_before");
        }
        if (mode != "clock-only") {
            probe.stage = 0;
            probe.readyCount = 0;
            probe.deadline = GetTickCount64() + 25000 + 2 * dwellMs;
            const UINT_PTR timer = SetTimer(nullptr, 0, 50, ObserveManager);
            if (!probe.Check(timer != 0, "create manager observation timer")) break;
            probe.Phase("manager_starting");
            const int result = manager.Run(instance, [&](lume::ui::PageCommand command) {
                if (command == lume::ui::PageCommand::Ready) {
                    ++probe.readyCount;
                    const HWND managerWindow = FindOwnWindow(L"LumeOriginalPageWindow");
                    EnableWindow(managerWindow, FALSE);
                    probe.Check(managerWindow && !IsWindowEnabled(managerWindow), "isolate probe manager from user input");
                    probe.stage = 1;
                    probe.Phase("manager_open");
                }
                return lume::ui::ClockPageState(controller);
            }, true);
            KillTimer(nullptr, timer);
            probe.Check(result == 0 && probe.readyCount == 1 && probe.stage == 3,
                "real manager reaches ready, minimizes, closes successfully");
            probe.Check(!FindOwnWindow(L"LumeOriginalPageWindow"), "manager HWND released");
            probe.ClockAlive();
            probe.Phase("clock_only_after_close");
            // Give WebView child processes time to exit while the native clock keeps pumping messages.
            PumpFor(dwellMs + 2000);
            probe.ClockAlive();
            probe.Snapshot("clock_only_after_close");
        }
        if (probe.clockEnabled) {
            probe.Check(controller.Handle({lume::clock::ActionType::Enable, 0}), "disable clock");
            probe.Check(!controller.Running() && !IsWindow(probe.clockWindow) &&
                !FindOwnWindow(L"LumeNativeDesktopClock"), "disable releases native HWND");
        }
        probe.Phase("component_disabled");
        PumpFor(dwellMs);
        probe.Snapshot("component_disabled");
    }
    controller.Shutdown();
    probe.Check(!controller.Running() && !FindOwnWindow(L"LumeNativeDesktopClock"), "shutdown releases clock");
    probe.Check(saves == (probe.clockEnabled ? rounds * 2 : 0), "only test enable and disable update in-memory settings");
    probe.Check(unexpectedActions == 0, "unexpected native input invalidates controlled resource run");
    if (cooldownMs) {
        probe.Phase("cooldown");
        PumpFor(cooldownMs);
        probe.Snapshot("cooldown");
    }
    probe.Phase(probe.passed ? "complete" : "failed");
    PumpFor(dwellMs);
    active = nullptr;
    std::cout << "RESULT " << (probe.passed ? "PASS" : "FAIL") << " rounds=" << rounds << " saves=" << saves
              << " unexpectedActions=" << unexpectedActions << " mode=" << mode
              << "; bounded sampling does not establish absence of long-term leaks" << std::endl;
    return probe.passed ? 0 : 1;
}
}

int main(int argc, char** argv) {
    if (argc < 4 || argc > 6) {
        std::cerr << "Usage: lume-clock-resource-probe <phase.json> <rounds 1..100> <dwell-ms 2000..30000> [combined|clock-only|manager-only] [cooldown-seconds 0..600]\n";
        return 2;
    }
    int rounds=0, dwell=0, cooldown=0;
    const std::string mode=argc>=5 ? argv[4] : "combined";
    try {
        rounds=std::stoi(argv[2]); dwell=std::stoi(argv[3]);
        if (argc==6) cooldown=std::stoi(argv[5]);
    } catch (const std::exception&) { return 2; }
    if (rounds < 1 || rounds > 100 || dwell < 2000 || dwell > 30000 || cooldown<0 || cooldown>600 ||
        (mode!="combined" && mode!="clock-only" && mode!="manager-only")) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    const int result = Run(std::filesystem::u8path(argv[1]), rounds, static_cast<DWORD>(dwell), mode, static_cast<DWORD>(cooldown)*1000);
    CoUninitialize();
    return result;
}
