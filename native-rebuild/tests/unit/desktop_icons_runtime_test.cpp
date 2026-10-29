#include "platform/windows/desktop_icons/desktop_icons_runtime.h"
#include <commctrl.h>
#include <objbase.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>

using namespace lume::platform::windows;
namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class F> bool WithPump(F&& operation) {
    // The real target is owned by this thread. Keep pumping while a background
    // caller waits for its ShowWindowAsync command to be applied.
    auto pending = std::async(std::launch::async, std::forward<F>(operation));
    const auto deadline = GetTickCount64() + 10000;
    while (pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Check(GetTickCount64() < deadline, "runtime operation deadline");
        MsgWaitForMultipleObjectsEx(0, nullptr, 2, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    return pending.get();
}
template<class F> bool PumpUntil(F&& condition, DWORD timeout = 4000) {
    const auto deadline = GetTickCount64() + timeout;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (condition()) return true;
        MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (GetTickCount64() < deadline);
    return condition();
}
void RequestRescan() {
    HWND observer = nullptr;
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        DWORD process = 0;
        GetWindowThreadProcessId(hwnd, &process);
        wchar_t name[64]{};
        GetClassNameW(hwnd, name, 64);
        if (process == GetCurrentProcessId() && wcscmp(name, L"LumeDesktopIconsObserver") == 0) {
            *reinterpret_cast<HWND*>(data) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&observer));
    Check(observer && PostMessageW(observer, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0),
          "notify only private runtime observer of desktop recreation");
}
unsigned RawDevicesFor(HWND target = nullptr) {
    UINT count = 0;
    Check(GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) != UINT(-1), "input inventory");
    std::vector<RAWINPUTDEVICE> devices(count);
    if (count) Check(GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) != UINT(-1), "input inventory data");
    unsigned result = 0;
    for (const auto& device : devices)
        if (device.usUsagePage == 1 && (device.usUsage == 2 || device.usUsage == 6) &&
            (!target || device.hwndTarget == target)) ++result;
    return result;
}
struct ResourceCounts {
    DWORD handles = 0;
    DWORD gdi = 0;
    DWORD user = 0;
    unsigned threads = 0;
    SIZE_T privateCommit = 0;
    SIZE_T workingSet = 0;
    std::vector<DWORD> threadIds;
};
void ReportThreads(const char* phase) {
    using QueryThread = LONG (NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    QueryThread query = nullptr;
    const auto queryAddress =
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread");
    static_assert(sizeof(query) == sizeof(queryAddress));
    std::memcpy(&query, &queryAddress, sizeof(query));
    struct PoolProbe { HANDLE done; QueryThread query; void* start = nullptr; };
    PoolProbe probe{CreateEventW(nullptr, TRUE, FALSE, nullptr), query};
    Check(probe.done != nullptr, "diagnostic pool event");
    Check(TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, void* context) {
        auto& sample = *static_cast<PoolProbe*>(context);
        if (sample.query) sample.query(GetCurrentThread(), 9, &sample.start,
                                       sizeof(sample.start), nullptr);
        SetEvent(sample.done);
    }, &probe, nullptr) != FALSE, "diagnostic pool callback");
    Check(WaitForSingleObject(probe.done, 2000) == WAIT_OBJECT_0, "diagnostic pool completion");
    const auto poolEntry = probe.start;
    CloseHandle(probe.done);
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    Check(snapshot != INVALID_HANDLE_VALUE, "diagnostic thread snapshot");
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) do {
        if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
        const HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        void* start = nullptr;
        FILETIME created{}, exited{}, kernel{}, user{};
        ULONG64 cycles = 0;
        if (query) query(thread, 9, &start, sizeof(start), nullptr);
        GetThreadTimes(thread, &created, &exited, &kernel, &user);
        QueryThreadCycleTime(thread, &cycles);
        HMODULE module = nullptr;
        wchar_t path[MAX_PATH]{};
        if (start && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      reinterpret_cast<LPCWSTR>(start), &module))
            GetModuleFileNameW(module, path, MAX_PATH);
        const auto* basename = wcsrchr(path, L'\\');
        std::wcout << L"thread phase=" << phase << L" tid=" << entry.th32ThreadID
            << L" start=" << start << L" module=" << (basename ? basename + 1 : path)
            << L" pool=" << (start && start == poolEntry) << L" cycles=" << cycles
            << L" created=" << ((static_cast<ULONGLONG>(created.dwHighDateTime) << 32) |
                               created.dwLowDateTime) << L'\n';
        CloseHandle(thread);
    } while (Thread32Next(snapshot, &entry));
    CloseHandle(snapshot);
}
ResourceCounts Resources() {
    ResourceCounts result{};
    Check(GetProcessHandleCount(GetCurrentProcess(), &result.handles) != FALSE, "handle count");
    SetLastError(ERROR_SUCCESS);
    result.gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    Check(result.gdi || GetLastError()==ERROR_SUCCESS,"GDI counter available");
    SetLastError(ERROR_SUCCESS);
    result.user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    Check(result.user || GetLastError()==ERROR_SUCCESS,"USER counter available");
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
    Check(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
        sizeof(memory))!=FALSE,"process memory counters");
    result.privateCommit=memory.PrivateUsage;result.workingSet=memory.WorkingSetSize;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    Check(snapshot != INVALID_HANDLE_VALUE, "thread snapshot");
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == GetCurrentProcessId()) {
                ++result.threads;
                result.threadIds.push_back(entry.th32ThreadID);
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    Check(result.threads>0,"current process threads were enumerated");
    return result;
}
void Report(const char* phase,const ResourceCounts& counts){
    std::cout<<"{\"phase\":\""<<phase<<"\",\"pid\":"<<GetCurrentProcessId()
        <<",\"handles\":"<<counts.handles<<",\"gdi\":"<<counts.gdi<<",\"user\":"<<counts.user
        <<",\"threads\":"<<counts.threads<<",\"privateCommitBytes\":"<<counts.privateCommit
        <<",\"workingSetBytes\":"<<counts.workingSet<<",\"threadIds\":[";
    for (std::size_t index=0;index<counts.threadIds.size();++index) {
        if(index)std::cout<<',';
        std::cout<<counts.threadIds[index];
    }
    std::cout<<"]}\n";
}
bool WithinBaseline(const ResourceCounts& after, const ResourceCounts& baseline) {
    return after.handles <= baseline.handles + 2 &&
           after.gdi <= baseline.gdi + 1 &&
           after.user <= baseline.user + 1 &&
           after.threads <= baseline.threads + 1;
}
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
    INITCOMMONCONTROLSEX controls{}; controls.dwSize = sizeof(controls); controls.dwICC = ICC_LISTVIEW_CLASSES;
    Check(InitCommonControlsEx(&controls), "native list");
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = DefWindowProcW; wc.lpszClassName = L"SHELLDLL_DefView";
    Check(RegisterClassExW(&wc) != 0, "private class");
    HWND shell = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Lume 桌面图标运行时验证",
                                 WS_POPUP | WS_VISIBLE, 60, 60, 220, 160, nullptr, nullptr, wc.hInstance, nullptr);
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 220, 160,
                                shell, nullptr, wc.hInstance, nullptr);
    Check(shell && list, "private native desktop");
    DesktopIconsTarget target{shell, list, GetCurrentProcessId(), GetCurrentThreadId()};
    std::mutex targetsMutex;
    unsigned discoveredCopies = 1;
    const auto originalRawCount = RawDevicesFor();
    ResourceCounts baselineResources{};
    Check(originalRawCount == 0, "isolated test process has no input owner");
    std::atomic_bool saveFails{false};
    std::atomic_uint saves{0}, notices{0};
    DesktopIconsRuntime runtime([&] {
        std::lock_guard<std::mutex> lock(targetsMutex);
        return std::vector<DesktopIconsTarget>(discoveredCopies, target);
    });
    DesktopIconsCallbacks callbacks{
        [&](const auto&) { ++saves; return !saveFails; },
        [&] { ++notices; }
    };
    lume::desktop_icons::Preferences preferences;
    Check(!WithPump([&] { return runtime.Start(preferences, callbacks); }) &&
          DesktopIconsRuntime::ActiveWorkers() == 0, "disabled cannot acquire runtime");
    preferences.enabled = true;
    preferences.enableAutoHide = false;
    preferences.manuallyHidden = true;
    for (unsigned cycle = 0; cycle < 16; ++cycle) {
        Check(WithPump([&] { return runtime.Start(preferences, callbacks); }), "start actual worker and input registration");
        const auto state = runtime.Snapshot();
        Check(state.running && state.desktops == 1 && state.inputDevices == 2 &&
              state.eventHooks == 1 && !state.scheduled && !IsWindowVisible(list),
              "manual preference hides private desktop without polling");
        Check(DesktopIconsRuntime::ActiveWorkers() == 1 && RawDevicesFor() == 2,
              "one worker and two real input registrations");
        if(cycle==15)Report("enabled_after_4_warmup_and_11_measured_cycles",Resources());
        auto changed = preferences; changed.autoHideDelay = 9; changed.manuallyHidden = false;
        saveFails = true;
        Check(!WithPump([&] { return runtime.Configure(changed); }) &&
              runtime.Snapshot().preferences.autoHideDelay == preferences.autoHideDelay &&
              runtime.Snapshot().error == DesktopIconsError::Save, "save failure preserves runtime settings");
        saveFails = false;
        Check(WithPump([&] { return runtime.Configure(changed); }) &&
              runtime.Snapshot().preferences.autoHideDelay == 9 &&
              runtime.Snapshot().preferences.manuallyHidden, "configuration preserves the worker's manual state");
        Check(WithPump([&] { return runtime.Stop(); }) && IsWindowVisible(list),
              "stop restores owned private desktop");
        const auto stopped = runtime.Snapshot();
        Check(!stopped.running && !stopped.scheduled && stopped.inputDevices == 0 &&
              stopped.eventHooks == 0 && DesktopIconsRuntime::ActiveWorkers() == 0 &&
              RawDevicesFor() == originalRawCount, "stop releases registrations, hooks, worker and schedule");
        if(cycle==3){
            baselineResources=Resources();Report("disabled_after_4_warmup_cycles",baselineResources);
        }
    }
    const auto stoppedResources=Resources();
    Report("disabled_after_12_measured_cycles",stoppedResources);
    Check(WithinBaseline(stoppedResources,baselineResources),
          "repeated normal start and stop return process resources to baseline");
    Check(saves == 32 && notices > 0, "only actual user updates persist, startup restore does not write");
    saveFails = true;
    Check(!WithPump([&] { return runtime.Start(preferences, callbacks, true); }) &&
          IsWindowVisible(list) && RawDevicesFor() == 0 && DesktopIconsRuntime::ActiveWorkers() == 0,
          "failed enable persistence unwinds acquired runtime and visibility");
    saveFails = false;
    Report("disabled_after_save_failure",Resources());
    RAWINPUTDEVICE prior{1, 2, RIDEV_INPUTSINK, shell};
    Check(RegisterRawInputDevices(&prior, 1, sizeof(prior)), "other process-local input consumer");
    Check(!WithPump([&] { return runtime.Start(preferences, callbacks); }) &&
          runtime.Snapshot().error == DesktopIconsError::InputBusy &&
          RawDevicesFor(shell) == 1 && DesktopIconsRuntime::ActiveWorkers() == 0,
          "existing input owner is not overwritten or removed");
    prior.dwFlags = RIDEV_REMOVE; prior.hwndTarget = nullptr;
    Check(RegisterRawInputDevices(&prior, 1, sizeof(prior)), "release fixture input ownership");
    Report("disabled_after_input_conflict",Resources());
    Check(WithPump([&] { return runtime.Start(preferences, callbacks); }), "start before stalled desktop shutdown");
    auto stalledStop = std::async(std::launch::async, [&] { return runtime.Stop(); });
    // Hold only this test window's owning thread. A queued ShowWindowAsync must
    // not be reported as a successful restoration until this thread pumps again.
    Sleep(650);
    Check(stalledStop.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready &&
          !stalledStop.get(), "unresponsive target reports incomplete restoration");
    Check(DesktopIconsRuntime::ActiveWorkers() == 0 && RawDevicesFor() == 0,
          "failed restoration does not keep input hooks or a worker alive");
    Check(WithPump([&] { return runtime.Stop(); }) && IsWindowVisible(list),
          "later shutdown retries retained recovery target");
    Report("disabled_after_stalled_restore",Resources());
    const auto recreateAndRecover = [&] {
        Check(WithPump([&] { return runtime.Start(preferences, callbacks); }), "start before desktop recreation");
        {
        std::lock_guard<std::mutex> lock(targetsMutex);
        DestroyWindow(shell);
        discoveredCopies = 0;
        }
        RequestRescan();
        Check(PumpUntil([&] { return runtime.Snapshot().desktops == 0; }),
          "desktop disappearance detaches old target");
        {
        std::lock_guard<std::mutex> lock(targetsMutex);
        shell = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Lume 重建桌面验证",
            WS_POPUP | WS_VISIBLE, 60, 60, 220, 160, nullptr, nullptr, wc.hInstance, nullptr);
        list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 220, 160,
            shell, nullptr, wc.hInstance, nullptr);
        Check(shell && list, "replacement private desktop");
        target = {shell, list, GetCurrentProcessId(), GetCurrentThreadId()};
        discoveredCopies = 1;
        }
        RequestRescan();
        Check(PumpUntil([&] {
        const auto state = runtime.Snapshot();
        return state.desktops == 1 && state.eventHooks == 1 && !IsWindowVisible(list);
    }), "replacement desktop inherits manual state and receives a new event subscription");
        {
        std::lock_guard<std::mutex> lock(targetsMutex);
        discoveredCopies = 9;
        }
        RequestRescan();
        Check(PumpUntil([&] { return runtime.Snapshot().error == DesktopIconsError::Startup; }),
          "temporary discovery failure is disclosed");
        {
        std::lock_guard<std::mutex> lock(targetsMutex);
        discoveredCopies = 1;
        }
        Check(PumpUntil([&] {
        const auto state = runtime.Snapshot();
        return state.error == DesktopIconsError::None && !state.scheduled;
    }),
          "discovery retries without another desktop event and clears recovered error");
        Check(WithPump([&] { return runtime.Stop(); }) && IsWindowVisible(list),
          "recreated desktop restored on disable");
        const auto state=runtime.Snapshot();
        Check(!state.running&&!state.scheduled&&state.inputDevices==0&&state.eventHooks==0&&
              state.pendingRestores==0&&DesktopIconsRuntime::ActiveWorkers()==0&&RawDevicesFor()==0,
              "each recreation cycle releases all owned runtime resources");
    };
    // Recovery exercises lazy Windows thread-pool work that short normal
    // start/stop cycles never reach. Warm the same workload before comparing;
    // module-owned workers/hooks/input still have to reach zero on every cycle.
    ResourceCounts recoveryBaseline{};
    for(unsigned cycle=0;cycle<16;++cycle) {
        recreateAndRecover();
        if(cycle==3) {
            recoveryBaseline=Resources();
            Report("disabled_after_4_recovery_warmup_cycles",recoveryBaseline);
        }
    }
    const auto finalResources = Resources();
    Report("disabled_after_12_measured_recovery_cycles",finalResources);
    if(!WithinBaseline(finalResources,recoveryBaseline)) {
        ReportThreads("failure");
    }
    Check(WithinBaseline(finalResources,recoveryBaseline),
          "repeated start, rescan and stop return process resources to the matched baseline");
    preferences.manuallyHidden = false;
    Check(WithPump([&] { return runtime.Start(preferences, callbacks); }), "restart after conflict disappears");
    DestroyWindow(shell);
    Check(WithPump([&] { return runtime.Stop(); }) && RawDevicesFor() == 0,
          "destroyed desktop does not block shutdown");
    CoUninitialize();
    std::cout << "desktop icon native worker lifecycle passed; user's desktop was not targeted\n";
}
