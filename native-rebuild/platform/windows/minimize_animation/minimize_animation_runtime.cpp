#include "platform/windows/minimize_animation/minimize_animation_runtime.h"
#include "platform/windows/minimize_animation/minimize_animation_ipc.h"
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <vector>

namespace lume::platform::windows {
using namespace minimize_detail;
namespace {
struct Instance {
    unsigned bits = 0;
    HANDLE mapping = nullptr, stop = nullptr, ready = nullptr, process = nullptr, job = nullptr;
    SharedControl* state = nullptr;
    MinimizeAnimationProcessApi api;
    bool stopping = false, terminationRequested = false;
    ULONGLONG stoppingAt = 0, terminationAt = 0;
    DWORD cleanupError = ERROR_TIMEOUT;
    void RequestStop() {
        if (!stopping) {stopping=true;stoppingAt=GetTickCount64();}
        if (state) InterlockedExchange(&state->enabled,FALSE);
        if (stop) SetEvent(stop);
    }
    bool HasActivity() {
        if (!state) return false;
        ReapExitedTargets(*state);
        return ActiveWorkers(*state) || ActiveEntries(*state);
    }
    bool Finished() {
        return (!process || api.wait(process,0)==WAIT_OBJECT_0) && !HasActivity();
    }
    bool Accumulate(MinimizeAnimationSnapshot& result) {
        const DWORD status=process ? api.wait(process,0) : WAIT_OBJECT_0;
        // WAIT_FAILED is not proof of exit. Keep the owned process count until
        // a successful wait observes that exact process handle signalled.
        if (status!=WAIT_OBJECT_0) {
            ++result.hostCount;result.x64 |= bits==64;result.x86 |= bits==32;
        }
        if (stopping) ReapExitedTargets(*state);
        const auto workers=ActiveWorkers(*state),entries=ActiveEntries(*state);
        const auto hooks=status==WAIT_TIMEOUT ? Read(state->installedHooks) : 0;
        result.hooks+=hooks;result.workers+=workers;result.entries+=entries;
        if (stopping && (status!=WAIT_OBJECT_0 || workers || entries)) ++result.pendingCleanup;
        if (status!=WAIT_TIMEOUT && Read(state->enabled) && result.error==MinimizeAnimationError::None)
            result.error=MinimizeAnimationError::HostExited;
        result.minimized+=Read(state->minimized);result.restored+=Read(state->restored);
        result.launched+=Read(state->launched);result.completed+=Read(state->completed);
        result.cancelled+=Read(state->cancelled);result.failed+=Read(state->failed);
        result.frames+=Read(state->frames);result.captures+=Read(state->captures);
        const auto pid=Read(state->lastTargetPid);
        if (pid) result.lastTargetPid=static_cast<DWORD>(pid);
        return status==WAIT_TIMEOUT && hooks==2 && Read(state->enabled) && !stopping;
    }
    ~Instance() {
        RequestStop();
        // Only this owned Host belongs to the job. Injected target processes
        // never do; their pinned workers observe stop/owner/Host independently.
        if (job) CloseHandle(job);
        if (state) UnmapViewOfFile(state);
        for (HANDLE handle : {mapping,stop,ready,process}) if (handle) CloseHandle(handle);
    }
};
struct PendingInstances {
    std::mutex mutex;
    std::vector<std::unique_ptr<Instance>> hosts;
    void ReapLocked() {
        hosts.erase(std::remove_if(hosts.begin(),hosts.end(),
            [](const auto& host){return host->Finished();}),hosts.end());
    }
    unsigned Reap() {
        std::lock_guard<std::mutex> lock(mutex);ReapLocked();
        return static_cast<unsigned>(hosts.size());
    }
    void Keep(std::vector<std::unique_ptr<Instance>>& values) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& host:values) hosts.push_back(std::move(host));
        values.clear();
    }
    void Accumulate(MinimizeAnimationSnapshot& result) {
        std::lock_guard<std::mutex> lock(mutex);ReapLocked();
        for (auto& host:hosts) host->Accumulate(result);
        if (!hosts.empty() && result.error==MinimizeAnimationError::None) {
            result.error=MinimizeAnimationError::StopTimeout;result.systemError=hosts.front()->cleanupError;
        }
    }
};
PendingInstances& Pending() {static PendingInstances value;return value;}
std::wstring ModuleDirectory() {
    std::wstring path(32768,L'\0');
    DWORD count=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if (!count || count>=path.size()) return {};
    path.resize(count); return std::filesystem::path(path).parent_path().wstring();
}
bool IsX64System() {
    SYSTEM_INFO info{}; GetNativeSystemInfo(&info);
    return info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
}
}
struct MinimizeAnimationRuntime::Impl {
    Impl(MinimizeAnimationScope value,MinimizeAnimationTimings limits,MinimizeAnimationProcessApi calls)
        : scope(std::move(value)),timings(limits),api(calls) {
        if (scope.directory.empty()) scope.directory=ModuleDirectory();
    }
    ~Impl() {
        if (!Stop()) Pending().Keep(hosts);
    }
    MinimizeAnimationScope scope;
    MinimizeAnimationTimings timings;
    MinimizeAnimationProcessApi api;
    std::vector<std::unique_ptr<Instance>> hosts;
    MinimizeAnimationSnapshot last;
    bool Fail(MinimizeAnimationError error, DWORD system=0) {
        last.error=error; last.systemError=system; return false;
    }
    bool StartOne(unsigned bits, const minimize_animation::Preferences& p) {
        auto host=std::make_unique<Instance>(); host->bits=bits;host->api=api;
        const auto mapName=ObjectName(bits,L"Control");
        host->mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,
                                        sizeof(SharedControl),mapName.c_str());
        const DWORD mapError=GetLastError();
        if (!host->mapping) return Fail(MinimizeAnimationError::Mapping,mapError);
        if (mapError == ERROR_ALREADY_EXISTS) return Fail(MinimizeAnimationError::AlreadyRunning,mapError);
        host->state=static_cast<SharedControl*>(MapViewOfFile(host->mapping,FILE_MAP_ALL_ACCESS,0,0,
                                                           sizeof(SharedControl)));
        if (!host->state) return Fail(MinimizeAnimationError::Mapping,GetLastError());
        auto& s=*host->state; s.version=ProtocolVersion; s.size=sizeof(s);
        s.ownerPid=static_cast<LONG>(GetCurrentProcessId());
        FILETIME created{},exited{},kernel{},user{};
        if (!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))
            return Fail(MinimizeAnimationError::Mapping,GetLastError());
        s.ownerTimeLow=static_cast<LONG>(created.dwLowDateTime);
        s.ownerTimeHigh=static_cast<LONG>(created.dwHighDateTime);
        s.targetThread=static_cast<LONG>(scope.threadId);
        s.targetProcess=static_cast<LONG>(scope.processId);
        WriteSettings(s,p);
        host->stop=CreateEventW(nullptr,TRUE,FALSE,ObjectName(bits,L"Stop").c_str());
        if (!host->stop || GetLastError()==ERROR_ALREADY_EXISTS)
            return Fail(MinimizeAnimationError::Mapping,GetLastError());
        host->ready=CreateEventW(nullptr,TRUE,FALSE,ObjectName(bits,L"Ready").c_str());
        if (!host->ready || GetLastError()==ERROR_ALREADY_EXISTS)
            return Fail(MinimizeAnimationError::Mapping,GetLastError());
        const auto exe=(std::filesystem::path(scope.directory)/
                         (L"LumeMinimizeHost"+std::to_wstring(bits)+L".exe")).wstring();
        std::wstring command=L"\""+exe+L"\"";
        host->job=api.createJob(nullptr,nullptr);
        if (!host->job) return Fail(MinimizeAnimationError::StartProcess,GetLastError());
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(host->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
            return Fail(MinimizeAnimationError::StartProcess,GetLastError());
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        if (!bytes) return Fail(MinimizeAnimationError::StartProcess,GetLastError());
        std::vector<unsigned char> storage(bytes);
        auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes,1,0,&bytes))
            return Fail(MinimizeAnimationError::StartProcess,GetLastError());
        if (!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                       &host->job,sizeof(host->job),nullptr,nullptr)) {
            const DWORD error=GetLastError();DeleteProcThreadAttributeList(attributes);
            return Fail(MinimizeAnimationError::StartProcess,error);
        }
        STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.lpAttributeList=attributes;
        si.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;si.StartupInfo.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION pi{};
        // JOB_LIST assigns ownership inside CreateProcess, including before it
        // returns. There is no caller-side create/assign crash window.
        const bool started=api.createProcess(exe.c_str(),command.data(),nullptr,nullptr,FALSE,
            CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,
            nullptr,scope.directory.c_str(),&si.StartupInfo,&pi)!=FALSE;
        const DWORD startError=GetLastError();DeleteProcThreadAttributeList(attributes);
        if (!started) return Fail(MinimizeAnimationError::StartProcess,startError);
        host->process=pi.hProcess;
        HANDLE waits[]={host->ready,host->process};
        hosts.push_back(std::move(host));
        const bool resumed=ResumeThread(pi.hThread)!=static_cast<DWORD>(-1);
        const DWORD resumeError=GetLastError();CloseHandle(pi.hThread);
        if (!resumed) return Fail(MinimizeAnimationError::StartProcess,resumeError);
        const DWORD waited=WaitForMultipleObjects(2,waits,FALSE,timings.readyTimeoutMs);
        const LONG error=Read(s.startupError), hooks=Read(s.installedHooks);
        if (waited != WAIT_OBJECT_0 || error || hooks!=2)
            return Fail(MinimizeAnimationError::HookInstall,error ? static_cast<DWORD>(error) : ERROR_TIMEOUT);
        return true;
    }
    MinimizeAnimationSnapshot Snapshot() const {
        auto result=last;
        result.running=!hosts.empty();
        for (const auto& host:hosts) result.running &= host->Accumulate(result);
        Pending().Accumulate(result);
        result.running &= result.pendingCleanup==0;
        return result;
    }
    bool Stop() {
        if (hosts.empty()) {
            if (Pending().Reap()) return Fail(MinimizeAnimationError::StopTimeout,ERROR_TIMEOUT);
            last={};return true;
        }
        for (auto& host:hosts) host->RequestStop();
        DWORD cleanupError=ERROR_TIMEOUT;
        for (;;) {
            bool complete=true,waiting=false;
            for (auto& host:hosts) {
                const bool activity=host->HasActivity();
                const DWORD status=api.wait(host->process,0);
                if (status==WAIT_FAILED) host->cleanupError=GetLastError();
                if (status==WAIT_OBJECT_0 && !activity) continue;
                complete=false;
                const auto now=GetTickCount64();
                if (!host->terminationRequested && now-host->stoppingAt>=timings.stopTimeoutMs &&
                    !activity && status!=WAIT_OBJECT_0) {
                    // A blocked initialization has no remaining target work.
                    // Close only its own job, then still require observed exit.
                    if (TerminateJobObject(host->job,ERROR_TIMEOUT)) {
                        host->terminationRequested=true;host->terminationAt=now;
                    } else host->cleanupError=GetLastError();
                }
                cleanupError=host->cleanupError;
                waiting |= host->terminationRequested ?
                    now-host->terminationAt<timings.finalExitTimeoutMs :
                    now-host->stoppingAt<timings.stopTimeoutMs;
            }
            if (complete) break;
            if (!waiting) return Fail(MinimizeAnimationError::StopTimeout,cleanupError);
            Sleep(std::max<DWORD>(1,timings.waitSliceMs));
        }
        hosts.clear(); last={}; return true;
    }
};
MinimizeAnimationRuntime::MinimizeAnimationRuntime(MinimizeAnimationScope scope,
    MinimizeAnimationTimings timings,MinimizeAnimationProcessApi api)
    : impl_(std::make_unique<Impl>(std::move(scope),timings,api)) {}
MinimizeAnimationRuntime::~MinimizeAnimationRuntime() = default;
bool MinimizeAnimationRuntime::Start(const minimize_animation::Preferences& p) {
    if (!minimize_animation::Valid(p) || !p.enabled)
        return impl_->Fail(MinimizeAnimationError::InvalidSettings);
    if (!impl_->hosts.empty()) {
        if (impl_->Snapshot().running) return Configure(p);
        if (!impl_->Stop()) return false;
    }
    if (Pending().Reap()) return impl_->Fail(MinimizeAnimationError::StopTimeout,ERROR_TIMEOUT);
    impl_->last={};
    SYSTEM_INFO info{}; GetNativeSystemInfo(&info);
    if (info.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64 &&
        info.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_INTEL)
        return impl_->Fail(MinimizeAnimationError::UnsupportedArchitecture);
    if ((impl_->scope.threadId==0)!=(impl_->scope.processId==0))
        return impl_->Fail(MinimizeAnimationError::InvalidSettings);
    if (impl_->scope.threadId) {
        HANDLE thread=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,impl_->scope.threadId);
        const bool valid=thread && GetProcessIdOfThread(thread)==impl_->scope.processId;
        if (thread) CloseHandle(thread);
        if (!valid) return impl_->Fail(MinimizeAnimationError::InvalidSettings,ERROR_INVALID_PARAMETER);
    }
    std::vector<unsigned> architectures{NativeBits};
    if (!impl_->scope.threadId && impl_->scope.requireBothArchitectures && IsX64System())
        architectures={64,32};
    for (const auto bits:architectures) {
        for (const auto* extension:{L".exe",L".dll"}) {
            const std::wstring stem=extension==std::wstring(L".exe")?L"LumeMinimizeHost":L"LumeMinimizeHook";
            const auto path=std::filesystem::path(impl_->scope.directory)/
                (stem+std::to_wstring(bits)+extension);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path,ec))
                return impl_->Fail(MinimizeAnimationError::MissingHost,ERROR_FILE_NOT_FOUND);
        }
    }
    for (const auto bits:architectures) if (!impl_->StartOne(bits,p)) {
        const auto error=impl_->last;
        if (impl_->Stop()) impl_->last=error;
        return false;
    }
    return true;
}
bool MinimizeAnimationRuntime::Configure(const minimize_animation::Preferences& p) {
    if (!minimize_animation::Valid(p)) return impl_->Fail(MinimizeAnimationError::InvalidSettings);
    if (!p.enabled) return Stop();
    if (impl_->hosts.empty()) return Start(p);
    if (!impl_->Snapshot().running) return Start(p);
    for (auto& h:impl_->hosts) WriteSettings(*h->state,p);
    impl_->last.error=MinimizeAnimationError::None;impl_->last.systemError=0;
    return true;
}
bool MinimizeAnimationRuntime::Stop() { return impl_->Stop(); }
MinimizeAnimationSnapshot MinimizeAnimationRuntime::Snapshot() const { return impl_->Snapshot(); }
unsigned MinimizeAnimationRuntime::PendingCleanup() {return Pending().Reap();}
const char* MinimizeAnimationErrorName(MinimizeAnimationError error) noexcept {
    switch(error) {
        case MinimizeAnimationError::None:return "";
        case MinimizeAnimationError::InvalidSettings:return "invalid-settings";
        case MinimizeAnimationError::MissingHost:return "missing-host";
        case MinimizeAnimationError::AlreadyRunning:return "already-running";
        case MinimizeAnimationError::Mapping:return "ipc";
        case MinimizeAnimationError::StartProcess:return "host-start";
        case MinimizeAnimationError::HookInstall:return "hook-install";
        case MinimizeAnimationError::StopTimeout:return "stop-timeout";
        case MinimizeAnimationError::HostExited:return "host-exited";
        case MinimizeAnimationError::UnsupportedArchitecture:return "unsupported-architecture";
    }
    return "runtime";
}
}
