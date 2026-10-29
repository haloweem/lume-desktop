// Explicit developer probe: not part of the default build, CTest or package.
#include "core/calendar/calendar_controller.h"
#include "platform/windows/calendar_runtime.h"
#include "process_memory_snapshot.h"
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace lume;
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Pump(DWORD milliseconds) {
    const auto deadline = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) throw std::runtime_error("Unexpected quit in isolated calendar probe");
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto now = GetTickCount64();
        if (now >= deadline) break;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(deadline - now), QS_ALLINPUT);
    } while (true);
}
unsigned ChildCount() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    Check(snapshot != INVALID_HANDLE_VALUE, "process snapshot failed");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    unsigned children = 0;
    if (Process32FirstW(snapshot, &entry)) do {
        if (entry.th32ParentProcessID == GetCurrentProcessId()) ++children;
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return children;
}
HWND OwnPicker(HWND owner) {
    struct Search { HWND owner; HWND found = nullptr; } search{owner};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t name[80]{};
        GetClassNameW(window, name, 80);
        if (GetWindow(window, GW_OWNER) == search.owner &&
            std::wstring(name) == L"LumeNativeCalendarMonthPicker") {
            search.found = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
void Sample(std::ofstream& samples, const char* phase, int round) {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    Check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
        sizeof(memory)) != FALSE, "memory sampling failed");
    DWORD handles = 0;
    Check(GetProcessHandleCount(GetCurrentProcess(), &handles) != FALSE, "handle sampling failed");
    FILETIME creation{}, exit{}, kernel{}, user{};
    Check(GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) != FALSE, "CPU sampling failed");
    const auto ticks = [](FILETIME time) { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    const auto children = ChildCount();
    samples << "{\"phase\":\"" << phase << "\",\"round\":" << round
        << ",\"pid\":" << GetCurrentProcessId() << ",\"tickMs\":" << GetTickCount64()
        << ",\"privateBytes\":" << memory.PrivateUsage << ",\"workingSetBytes\":" << memory.WorkingSetSize
        << ",\"handles\":" << handles << ",\"gdi\":" << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)
        << ",\"user\":" << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)
        << ",\"cpu100ns\":" << ticks(kernel) + ticks(user) << ",\"children\":" << children << "}\n";
    samples.flush();
    Check(samples.good(), "sample output failed");
    Check(children == 0, "native calendar created a child process");
}
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) return 1;
    int result = 1;
    try {
        wchar_t module[32768]{};
        const DWORD count = GetModuleFileNameW(nullptr, module, 32768);
        Check(count > 0 && count < 32768, "module path failed");
        const auto directory = std::filesystem::path(module).parent_path() / L"verification" /
            (L"calendar-resource-" + std::to_wstring(GetCurrentProcessId()));
        Check(!std::filesystem::exists(directory), "fresh probe directory required");
        std::filesystem::create_directories(directory);
        std::ofstream samples(directory / L"samples.jsonl");
        Check(samples.good(), "open probe samples failed");
        const auto diagnostics = directory / L"diagnostics.jsonl";
        Sample(samples, "baseline", 0);
        {
            calendar::CalendarController* active = nullptr;
            bool nativeActionsOk = true;
            platform::windows::NativeCalendarRuntime runtime(GetModuleHandleW(nullptr), [&](calendar::Action action) {
                // Never throw across a Win32 callback; report on the outer test stack.
                if (active && !active->Handle(action)) nativeActionsOk = false;
            }, [] {});
            const auto pump = [&](DWORD milliseconds) {
                Pump(milliseconds);
                Check(nativeActionsOk, "native bounds action failed");
            };
            calendar::Preferences persisted;
            calendar::CalendarController controller(persisted, runtime, [&](const calendar::Preferences& value) {
                persisted = value;
                return true; // Isolated in-memory preferences, never APPDATA.
            });
            active = &controller;
            Check(controller.Restore(), "restore disabled calendar failed");
            for (int round = 1; round <= 10; ++round) {
                Check(controller.Handle({calendar::ActionType::Enable, 1}), "enable calendar failed");
                for (int theme = 0; theme <= 1; ++theme) {
                    Check(controller.Handle({calendar::ActionType::SelectTheme, theme}), "theme update failed");
                    pump(1200);
                    Check(controller.Running(), "calendar not running after open");
                    Sample(samples, theme ? "split" : "spacious", round);
                }
                const auto window = runtime.Handle();
                const auto dpi = GetDpiForWindow(window);
                // The 820 x 500 reference puts both themes' title at this point.
                SendMessageW(window, WM_LBUTTONDOWN, 0,
                    MAKELPARAM(MulDiv(80, dpi, 96), MulDiv(50, dpi, 96)));
                pump(100);
                const auto picker = OwnPicker(window);
                Check(picker != nullptr, "own month picker did not open");
                Sample(samples, "picker", round);
                SendMessageW(picker, WM_KEYDOWN, VK_ESCAPE, 0);
                pump(100);
                Check(!IsWindow(picker), "month picker survived close");
                const auto profile = *controller.Snapshot().Active();
                Check(controller.Handle({calendar::ActionType::Bounds, profile.x, profile.y, 480, 390}), "small resize failed");
                pump(200);
                Check(controller.Handle({calendar::ActionType::Bounds, profile.x, profile.y, 820, 500}), "restore size failed");
                pump(200);
                Check(controller.Handle({calendar::ActionType::Enable, 0}), "disable calendar failed");
                pump(300);
                Check(!controller.Running(), "calendar running after disable");
                Sample(samples, "closed", round);
                Check(WriteProcessMemorySnapshot(diagnostics, "closed", round), "diagnostic output failed");
            }
            Check(controller.Handle({calendar::ActionType::Enable, 1}), "idle reopen failed");
            pump(2000);
            Sample(samples, "idle", 0);
            for (int second = 1; second <= 10; ++second) {
                pump(1000);
                Sample(samples, "idle", second);
            }
            controller.Shutdown();
            active = nullptr;
            Check(!runtime.IsRunning(), "shutdown kept calendar alive");
        }
        Pump(500);
        Sample(samples, "destroyed", 0);
        Check(WriteProcessMemorySnapshot(diagnostics, "destroyed", 0), "final diagnostic output failed");
        std::cout << "CALENDAR_RESOURCE_PROBE_PASS: " << directory.u8string() << '\n';
        result = 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
    }
    CoUninitialize();
    return result;
}
