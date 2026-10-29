#pragma once
#include "core/minimize_animation/minimize_animation.h"
#include <windows.h>
#include <cstring>
#include <string>

namespace lume::platform::windows::minimize_detail {
constexpr LONG ProtocolVersion = 1;
constexpr DWORD HostDrainTimeoutMs = 2000;
template<class Function> Function Procedure(HMODULE module,const char* name) noexcept {
    const FARPROC address=GetProcAddress(module,name);
    Function function=nullptr;
    static_assert(sizeof(function)==sizeof(address));
    std::memcpy(&function,&address,sizeof(function));
    return function;
}
#ifdef _WIN64
constexpr unsigned NativeBits = 64;
#else
constexpr unsigned NativeBits = 32;
#endif
inline std::wstring ObjectName(unsigned bits, const wchar_t* suffix) {
    return L"Local\\Lume.MinimizeAnimation.v1."+std::to_wstring(bits)+L"."+suffix;
}
// Cross-bitness wire format. No pointers, size_t, bool, or compiler STL types.
// Single settings writer (Shell); readers use the Interlocked sequence.
struct ProcessLease {
    volatile LONG pid;
    LONG createdLow;
    LONG createdHigh;
    volatile LONG entries;
    volatile LONG workers;
};
constexpr unsigned ProcessLeaseCount=128;
struct SharedControl {
    LONG version;
    LONG size;
    LONG ownerPid;
    LONG ownerTimeLow;
    LONG ownerTimeHigh;
    LONG targetThread;
    LONG targetProcess;
    volatile LONG sequence;
    volatile LONG enabled;
    volatile LONG durationMs;
    volatile LONG restore;
    volatile LONG launch;
    volatile LONG multiMonitor;
    volatile LONG unhideTaskbar;
    volatile LONG unhideDurationMs;
    volatile LONG classic;
    volatile LONG hostPid;
    volatile LONG startupError;
    volatile LONG installedHooks;
    volatile LONG minimized;
    volatile LONG restored;
    volatile LONG launched;
    volatile LONG completed;
    volatile LONG cancelled;
    volatile LONG failed;
    volatile LONG frames;
    volatile LONG captures;
    volatile LONG lastTargetPid;
    ProcessLease processes[ProcessLeaseCount];
};
inline LONG ActiveEntries(SharedControl& s) noexcept {
    LONG result=0;
    for (auto& lease:s.processes) result+=InterlockedCompareExchange(&lease.entries,0,0);
    return result;
}
inline LONG ActiveWorkers(SharedControl& s) noexcept {
    LONG result=0;
    for (auto& lease:s.processes) result+=InterlockedCompareExchange(&lease.workers,0,0);
    return result;
}
static_assert(sizeof(LONG) == 4);
inline LONG Read(volatile LONG& value) noexcept { return InterlockedCompareExchange(&value,0,0); }
inline void ReapExitedTargets(SharedControl& state) {
    // Query only processes already registered by hook callbacks. A crashed
    // target cannot decrement its own counters; PID reuse must not keep a lease.
    for (auto& lease:state.processes) {
        const LONG pid=Read(lease.pid);
        if (pid<=0 || (!Read(lease.entries) && !Read(lease.workers))) continue;
        const LONG low=lease.createdLow,high=lease.createdHigh;
        HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(pid));
        bool exited=!process && GetLastError()==ERROR_INVALID_PARAMETER;
        if (process) {
            FILETIME created{},ended{},kernel{},user{};
            exited=WaitForSingleObject(process,0)==WAIT_OBJECT_0 ||
                (GetProcessTimes(process,&created,&ended,&kernel,&user) &&
                 (static_cast<LONG>(created.dwLowDateTime)!=low ||
                  static_cast<LONG>(created.dwHighDateTime)!=high));
            CloseHandle(process);
        }
        if (!exited || InterlockedCompareExchange(&lease.pid,-1,pid)!=pid) continue;
        if (lease.createdLow!=low || lease.createdHigh!=high) {
            InterlockedExchange(&lease.pid,pid);continue;
        }
        InterlockedExchange(&lease.entries,0);
        InterlockedExchange(&lease.workers,0);
        InterlockedExchange(&lease.pid,0);
    }
}
inline void WriteSettings(SharedControl& s, const minimize_animation::Preferences& p) noexcept {
    InterlockedIncrement(&s.sequence);
    InterlockedExchange(&s.durationMs,p.durationMs);
    InterlockedExchange(&s.restore,p.restore);
    InterlockedExchange(&s.launch,p.launch);
    InterlockedExchange(&s.multiMonitor,p.multiMonitor);
    InterlockedExchange(&s.unhideTaskbar,p.unhideTaskbar);
    InterlockedExchange(&s.unhideDurationMs,p.unhideDurationMs);
    InterlockedExchange(&s.classic,p.engine == minimize_animation::Engine::Classic);
    InterlockedExchange(&s.enabled,p.enabled);
    InterlockedIncrement(&s.sequence);
}
inline bool ReadSettings(SharedControl& s, minimize_animation::Preferences& p) noexcept {
    for (int retry=0; retry<8; ++retry) {
        const LONG before=Read(s.sequence); if (before&1) continue;
        p.enabled=Read(s.enabled)!=0; p.durationMs=Read(s.durationMs);
        p.restore=Read(s.restore)!=0; p.launch=Read(s.launch)!=0;
        p.multiMonitor=Read(s.multiMonitor)!=0; p.unhideTaskbar=Read(s.unhideTaskbar)!=0;
        p.unhideDurationMs=Read(s.unhideDurationMs);
        p.engine=Read(s.classic) ? minimize_animation::Engine::Classic : minimize_animation::Engine::Modern;
        if (before == Read(s.sequence)) return minimize_animation::Valid(p);
    }
    return false;
}
}
