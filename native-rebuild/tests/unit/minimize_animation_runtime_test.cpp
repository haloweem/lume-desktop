#include "platform/windows/minimize_animation/minimize_animation_runtime.h"
#include "platform/windows/minimize_animation/minimize_animation_ipc.h"
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <functional>
#include <filesystem>
#include <iomanip>

using namespace lume::platform::windows;
using namespace lume::platform::windows::minimize_detail;
namespace {
constexpr UINT Action=WM_APP+180;
struct AtomicCreationState {volatile LONG pid=0;DWORD low=0,high=0;BOOL member=FALSE;};
HANDLE observedJob=nullptr,observedProcess=nullptr;
bool memberAtCreation=false,failExitWait=false;
AtomicCreationState* crashAfterCreation=nullptr;
HANDLE WINAPI ObserveCreateJob(LPSECURITY_ATTRIBUTES attributes,LPCWSTR name) {
    observedJob=CreateJobObjectW(attributes,name);return observedJob;
}
BOOL WINAPI ObserveCreateProcess(LPCWSTR application,LPWSTR command,
    LPSECURITY_ATTRIBUTES processAttributes,LPSECURITY_ATTRIBUTES threadAttributes,
    BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,
    LPSTARTUPINFOW startup,LPPROCESS_INFORMATION created) {
    const BOOL result=CreateProcessW(application,command,processAttributes,threadAttributes,inherit,
                                     flags,environment,directory,startup,created);
    if (result) {
        BOOL member=FALSE;
        memberAtCreation=observedJob && IsProcessInJob(created->hProcess,observedJob,&member) && member;
        observedProcess=created->hProcess;
        if (crashAfterCreation) {
            FILETIME birth{},end{},kernel{},user{};
            GetProcessTimes(created->hProcess,&birth,&end,&kernel,&user);
            crashAfterCreation->low=birth.dwLowDateTime;crashAfterCreation->high=birth.dwHighDateTime;
            crashAfterCreation->member=memberAtCreation;
            InterlockedExchange(&crashAfterCreation->pid,static_cast<LONG>(created->dwProcessId));
            // The kernel has returned the child, but no caller-side assignment,
            // resume, or ownership bookkeeping instruction has run.
            TerminateProcess(GetCurrentProcess(),94);ExitProcess(95);
        }
    }
    return result;
}
DWORD WINAPI ObserveWait(HANDLE handle,DWORD timeout) {
    if (failExitWait && handle==observedProcess) {
        SetLastError(ERROR_INVALID_HANDLE);return WAIT_FAILED;
    }
    return WaitForSingleObject(handle,timeout);
}
MinimizeAnimationProcessApi ObservedApi() {return {ObserveCreateJob,ObserveCreateProcess,ObserveWait};}
HANDLE fixtureJob=nullptr;
void CloseFixtureJob() {if (fixtureJob) {CloseHandle(fixtureJob);fixtureJob=nullptr;}}
void Check(bool value,const char* text) {
    if (!value) {std::cerr<<"FAIL: "<<text<<" win32="<<GetLastError()<<'\n';std::exit(1);}
}
std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring result=L"\"";
    unsigned slashes=0;
    for (wchar_t character:value) {
        if (character==L'\\') {++slashes;continue;}
        result.append(character==L'"' ? slashes*2+1 : slashes,L'\\');
        result.push_back(character);slashes=0;
    }
    result.append(slashes*2,L'\\');result.push_back(L'"');return result;
}
std::wstring PayloadDirectory(int argc,wchar_t** argv,int first) {
    if (argc==first) return {};
    Check(argc==first+2 && std::wstring(argv[first])==L"--payload-directory",
          "usage: runtime-test [--payload-directory <unpacked product directory>]");
    return std::filesystem::absolute(argv[first+1]).wstring();
}
bool Until(const std::function<bool()>& predicate,DWORD timeout=4000) {
    const auto until=GetTickCount64()+timeout;
    do {if (predicate()) return true;Sleep(10);} while (GetTickCount64()<until);
    return predicate();
}
std::filesystem::path Executable() {
    wchar_t value[32768]{};
    Check(GetModuleFileNameW(nullptr,value,32768)!=0,"locate test executable");
    return value;
}
// This copy of the test executable replaces only the owned Host in a temporary
// payload. It simulates loading a DLL that never returns; no hook is installed
// and no target window is touched.
int StalledInitializationHost() {
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,ObjectName(NativeBits,L"Control").c_str());
    if (!mapping) return 11;
    auto* state=static_cast<SharedControl*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControl)));
    if (!state) {CloseHandle(mapping);return 12;}
    InterlockedExchange(&state->hostPid,static_cast<LONG>(GetCurrentProcessId()));
    Sleep(INFINITE);
    return 13;
}
void AtomicCreationCrashRegression(const std::filesystem::path& executable,const std::filesystem::path& directory) {
    const auto name=L"Local\\Lume.Minimize.AtomicFixture."+std::to_wstring(GetCurrentProcessId())+
        L"."+std::to_wstring(GetTickCount64());
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(AtomicCreationState),name.c_str());
    auto* state=mapping ? static_cast<AtomicCreationState*>(
        MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(AtomicCreationState))) : nullptr;
    Check(state!=nullptr,"create atomic crash fixture result");*state={};
    fixtureJob=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    Check(fixtureJob && SetInformationJobObject(fixtureJob,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),
          "atomic crash fixture cleanup job");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION owner{};
    std::wstring command=QuoteArgument(executable.wstring())+L" --atomic-owner "+
        QuoteArgument(directory.wstring())+L" "+QuoteArgument(name);
    Check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,
                         nullptr,nullptr,&startup,&owner),"start exact-creation crash owner");
    Check(AssignProcessToJobObject(fixtureJob,owner.hProcess),"contain only atomic crash fixture descendants");
    ResumeThread(owner.hThread);CloseHandle(owner.hThread);
    Check(WaitForSingleObject(owner.hProcess,5000)==WAIT_OBJECT_0,"owner exits immediately after kernel child creation");
    const bool created=Read(state->pid)!=0,member=state->member!=FALSE;
    HANDLE child=created ? OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,
                                      static_cast<DWORD>(Read(state->pid))) : nullptr;
    bool exited=!child && GetLastError()==ERROR_INVALID_PARAMETER;
    if (child) {
        FILETIME birth{},end{},kernel{},user{};
        Check(GetProcessTimes(child,&birth,&end,&kernel,&user),"validate exact crashed owner's child identity");
        const bool same=birth.dwLowDateTime==state->low && birth.dwHighDateTime==state->high;
        exited=!same || WaitForSingleObject(child,3000)==WAIT_OBJECT_0;
        CloseHandle(child);
    }
    CloseFixtureJob();CloseHandle(owner.hProcess);UnmapViewOfFile(state);CloseHandle(mapping);
    Check(created && member,"Host is already in its own job when CreateProcessW returns");
    Check(exited,"crash before caller-side bookkeeping leaves no suspended owned Host");
}
void InitializationOwnerCrashRegression(const std::filesystem::path& executable) {
    const auto directory=std::filesystem::temp_directory_path()/
        (L"LumeMinimizeInitialization-"+std::to_wstring(GetCurrentProcessId())+L"-"+
         std::to_wstring(GetTickCount64()));
    std::error_code error;
    Check(std::filesystem::create_directory(directory,error),"create isolated initialization fixture");
    const auto fixture=directory/(L"LumeMinimizeHost"+std::to_wstring(NativeBits)+L".exe");
    const auto library=directory/(L"LumeMinimizeHook"+std::to_wstring(NativeBits)+L".dll");
    Check(CopyFileW(executable.c_str(),fixture.c_str(),TRUE),"copy non-graphical owned Host fixture");
    HANDLE placeholder=CreateFileW(library.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(placeholder!=INVALID_HANDLE_VALUE,"create fixture-only payload sentinel");
    CloseHandle(placeholder);
    // The outer job contains only this owner and its descendants. Even a red
    // regression cannot leave the deliberately stalled Host behind.
    HANDLE job=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    Check(job && SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),
          "initialization fixture cleanup job");
    fixtureJob=job;std::atexit(CloseFixtureJob);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION owner{};
    std::wstring command=QuoteArgument(executable.wstring())+L" --initialization-owner "+
        QuoteArgument(directory.wstring());
    Check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,
                         nullptr,nullptr,&startup,&owner),"start initialization-only owner");
    Check(AssignProcessToJobObject(job,owner.hProcess),"contain only initialization fixture descendants");
    Check(ResumeThread(owner.hThread)!=static_cast<DWORD>(-1),"resume initialization fixture owner");
    CloseHandle(owner.hThread);
    HANDLE mapping=nullptr;
    Check(Until([&]{
        mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,ObjectName(NativeBits,L"Control").c_str());
        return mapping!=nullptr;
    }),"initialization owner creates control mapping");
    auto* state=static_cast<SharedControl*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControl)));
    Check(state && Until([&]{return Read(state->hostPid)!=0;}),"owned Host enters stalled initialization");
    HANDLE host=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,
                            static_cast<DWORD>(Read(state->hostPid)));
    Check(host!=nullptr && Read(state->installedHooks)==0,"hold exact non-graphical fixture Host");
    Check(TerminateProcess(owner.hProcess,93),"crash test owner before hook initialization completes");
    Check(WaitForSingleObject(owner.hProcess,3000)==WAIT_OBJECT_0,"initialization fixture owner exits");
    const bool hostExited=WaitForSingleObject(host,3000)==WAIT_OBJECT_0;
    // Complete fixture cleanup before reporting a failing assertion.
    CloseFixtureJob();
    Check(WaitForSingleObject(host,3000)==WAIT_OBJECT_0,"outer fixture job reaps stalled child");
    CloseHandle(host);CloseHandle(owner.hProcess);UnmapViewOfFile(state);CloseHandle(mapping);
    AtomicCreationCrashRegression(executable,directory);
    Check(std::filesystem::remove(library,error) && !error,"remove fixture sentinel");
    Check(std::filesystem::remove(fixture,error) && !error,"remove fixture executable");
    Check(std::filesystem::remove(directory,error) && !error,"remove empty fixture directory");
    Check(hostExited,"parent death during initialization leaves no owned Host alive");
}
void ShutdownLeaseRegression(const std::wstring& payloadDirectory) {
    MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
    MinimizeAnimationScope scope;
    scope.threadId=GetCurrentThreadId();scope.processId=GetCurrentProcessId();
    scope.requireBothArchitectures=false;scope.directory=payloadDirectory;
    MinimizeAnimationTimings timings;timings.stopTimeoutMs=HostDrainTimeoutMs+250;
    MinimizeAnimationRuntime runtime(scope,timings);
    lume::minimize_animation::Preferences settings;settings.enabled=true;
    Check(runtime.Start(settings),"start thread-scoped shutdown fixture without creating windows");
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,ObjectName(NativeBits,L"Control").c_str());
    auto* state=mapping ? static_cast<SharedControl*>(
        MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControl))) : nullptr;
    Check(state!=nullptr,"inspect shutdown fixture lease");
    HANDLE host=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(Read(state->hostPid)));
    Check(host!=nullptr,"retain exact shutdown fixture Host");
    FILETIME created{},ended{},kernel{},user{};
    Check(GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user),"identify fixture-owned lease");
    auto& lease=state->processes[0];
    lease.createdLow=static_cast<LONG>(created.dwLowDateTime);
    lease.createdHigh=static_cast<LONG>(created.dwHighDateTime);
    InterlockedExchange(&lease.pid,static_cast<LONG>(GetCurrentProcessId()));
    // A deliberately outstanding lease models a target thread that has not
    // returned yet. It never represents or modifies a real user application.
    InterlockedExchange(&lease.workers,1);
    const bool stopped=runtime.Stop();
    const auto pending=runtime.Snapshot();
    const bool hostBounded=WaitForSingleObject(host,0)==WAIT_OBJECT_0;
    InterlockedExchange(&lease.workers,0);
    Check(WaitForSingleObject(host,3000)==WAIT_OBJECT_0,"fixture lease release lets owned Host exit");
    CloseHandle(host);UnmapViewOfFile(state);CloseHandle(mapping);
    const bool restarted=runtime.Start(settings);
    const bool recoveredStop=runtime.Stop();
    // Always release the artificial lease and the hooks before failing.
    Check(!stopped && pending.error==MinimizeAnimationError::StopTimeout && pending.workers==1,
          "Host exit cannot acknowledge shutdown while a target lease is outstanding");
    Check(hostBounded,"Host shutdown has a bounded wait for outstanding target leases");
    Check(restarted && recoveredStop,"start recovers after a timed-out lease actually finishes");
}
void RetainedOwnershipRegression(const std::wstring& payloadDirectory) {
    MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
    MinimizeAnimationScope scope;
    scope.threadId=GetCurrentThreadId();scope.processId=GetCurrentProcessId();
    scope.requireBothArchitectures=false;scope.directory=payloadDirectory;
    MinimizeAnimationTimings timings;timings.stopTimeoutMs=30;timings.finalExitTimeoutMs=50;timings.waitSliceMs=1;
    lume::minimize_animation::Preferences settings;settings.enabled=true;
    HANDLE mapping=nullptr,host=nullptr;SharedControl* state=nullptr;
    {
        MinimizeAnimationRuntime runtime(scope,timings);
        Check(runtime.Start(settings),"start pending-destruction fixture");
        mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,ObjectName(NativeBits,L"Control").c_str());
        state=mapping ? static_cast<SharedControl*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControl))) : nullptr;
        Check(state!=nullptr,"retain pending-destruction control");
        host=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(Read(state->hostPid)));
        Check(host!=nullptr,"retain pending-destruction exact Host");
        FILETIME created{},ended{},kernel{},user{};
        Check(GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user),"identify pending-destruction lease");
        auto& lease=state->processes[0];
        lease.createdLow=static_cast<LONG>(created.dwLowDateTime);
        lease.createdHigh=static_cast<LONG>(created.dwHighDateTime);
        InterlockedExchange(&lease.pid,static_cast<LONG>(GetCurrentProcessId()));
        InterlockedExchange(&lease.entries,1);InterlockedExchange(&lease.workers,1);
        Check(!runtime.Stop() && runtime.Snapshot().pendingCleanup==1,"failed stop retains its pending record");
    }
    MinimizeAnimationRuntime next(scope);
    const auto pending=next.Snapshot();
    const bool retained=MinimizeAnimationRuntime::PendingCleanup()==1 &&
        pending.pendingCleanup==1 && pending.entries==1 && pending.workers==1;
    const bool blocked=!next.Start(settings) && next.Snapshot().error==MinimizeAnimationError::StopTimeout;
    InterlockedExchange(&state->processes[0].workers,0);InterlockedExchange(&state->processes[0].entries,0);
    Check(WaitForSingleObject(host,4000)==WAIT_OBJECT_0,"released target leases finish Host shutdown");
    CloseHandle(host);UnmapViewOfFile(state);CloseHandle(mapping);
    Check(Until([]{return MinimizeAnimationRuntime::PendingCleanup()==0;}),"pending destruction observes actual final cleanup");
    Check(retained && blocked,"destruction preserves outstanding ownership and prevents replacement");
    Check(next.Start(settings) && next.Stop(),"new runtime recovers after pending cleanup is confirmed");
    HANDLE retainedProcess=nullptr;
    {
        MinimizeAnimationRuntime runtime(scope,timings,ObservedApi());
        Check(runtime.Start(settings) && memberAtCreation,"kernel atomically assigns the live Host to its exact job");
        Check(DuplicateHandle(GetCurrentProcess(),observedProcess,GetCurrentProcess(),&retainedProcess,
                              SYNCHRONIZE,FALSE,0),"retain exact failed-wait Host");
        failExitWait=true;
        Check(!runtime.Stop() && runtime.Snapshot().hostCount==1 && runtime.Snapshot().pendingCleanup==1 &&
              runtime.Snapshot().systemError==ERROR_INVALID_HANDLE,
              "WAIT_FAILED cannot clear the owned Host count or acknowledge shutdown");
    }
    const auto failed=next.Snapshot();
    const bool stillOwned=MinimizeAnimationRuntime::PendingCleanup()==1 &&
        failed.hostCount==1 && failed.pendingCleanup==1;
    failExitWait=false;
    Check(WaitForSingleObject(retainedProcess,3000)==WAIT_OBJECT_0,"failed-wait fixture Host actually exits");
    CloseHandle(retainedProcess);
    Check(Until([]{return MinimizeAnimationRuntime::PendingCleanup()==0;}),"later successful wait releases retained ownership");
    Check(stillOwned,"runtime destruction cannot discard a process whose exit was not observed");
}
struct ProcessResources {
    HANDLE process=nullptr;
    const char* role=nullptr;
    ULONGLONG startCpu=0,endCpu=0;
    SIZE_T startPrivate=0,endPrivate=0,peakPrivate=0,startWorking=0,endWorking=0,peakWorking=0;
    void Sample(bool first=false) {
        FILETIME created{},ended{},kernel{},user{};
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
        Check(GetProcessTimes(process,&created,&ended,&kernel,&user) &&
              K32GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)),
              "sample owned animation process CPU and memory");
        const auto ticks=[](FILETIME time) {
            return (static_cast<ULONGLONG>(time.dwHighDateTime)<<32)|time.dwLowDateTime;
        };
        endCpu=ticks(kernel)+ticks(user);endPrivate=memory.PrivateUsage;endWorking=memory.WorkingSetSize;
        if (first) {startCpu=endCpu;startPrivate=endPrivate;startWorking=endWorking;}
        peakPrivate=std::max(peakPrivate,endPrivate);peakWorking=std::max(peakWorking,endWorking);
    }
    void Print(const char* engine,ULONGLONG wall) const {
        constexpr double MiB=1024.0*1024;
        const double cpu=static_cast<double>(endCpu-startCpu)/10000.0;
        std::cout<<std::fixed<<std::setprecision(3)<<"resource-process architecture="<<NativeBits
            <<" engine="<<engine<<" role="<<role<<" cpu_ms="<<cpu
            <<" one_core_cpu_percent="<<(wall ? cpu*100/wall : 0)
            <<" private_start_mib="<<startPrivate/MiB<<" private_end_mib="<<endPrivate/MiB
            <<" sampled_private_peak_mib="<<peakPrivate/MiB
            <<" working_start_mib="<<startWorking/MiB<<" working_end_mib="<<endWorking/MiB
            <<" sampled_working_peak_mib="<<peakWorking/MiB<<'\n';
    }
};
void MeasureEngine(MinimizeAnimationRuntime& runtime,lume::minimize_animation::Preferences settings,
                   lume::minimize_animation::Engine engine,HWND target,HANDLE fixture) {
    const char* label=engine==lume::minimize_animation::Engine::Modern ? "modern" : "classic";
    settings.engine=engine;settings.durationMs=180;settings.launch=false;
    Check(runtime.Configure(settings),"apply engine for real cross-process resource sample");
    RECT bounds{};Check(GetWindowRect(target,&bounds),"measure actual resource fixture dimensions");
    const UINT dpi=GetDpiForWindow(target);
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,ObjectName(NativeBits,L"Control").c_str());
    auto* state=mapping ? static_cast<SharedControl*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControl))) : nullptr;
    Check(state!=nullptr,"locate measured owned Host");
    HANDLE host=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,
                            static_cast<DWORD>(Read(state->hostPid)));
    UnmapViewOfFile(state);CloseHandle(mapping);Check(host!=nullptr,"open only measured owned Host");
    ProcessResources processes[]={{GetCurrentProcess(),"test-parent"},{fixture,"fixture-target"},{host,"owned-host"}};
    for (auto& process:processes) process.Sample(true);
    const auto before=runtime.Snapshot();
    const auto begin=GetTickCount64();
    const auto observed=[&]{
        for (auto& process:processes) process.Sample();
        return runtime.Snapshot();
    };
    constexpr unsigned cycles=4;
    for (unsigned cycle=0;cycle<cycles;++cycle) {
        const auto prior=runtime.Snapshot();
        Check(PostMessageW(target,Action,0,0),"measured fixture minimizes");
        Check(Until([&]{
            const auto current=observed();
            return IsIconic(target) && current.minimized>prior.minimized &&
                current.completed>prior.completed && !current.workers && !current.entries;
        }),"measured minimize completes with real frames");
        const auto minimized=runtime.Snapshot();
        Check(PostMessageW(target,Action,5,0),"measured fixture restores");
        Check(Until([&]{
            const auto current=observed();
            return !IsIconic(target) && current.restored>minimized.restored &&
                current.completed>minimized.completed && !current.workers && !current.entries;
        }),"measured restore completes with real frames");
    }
    const auto after=observed();const auto wall=GetTickCount64()-begin;
    std::cout<<"resource-animation architecture="<<NativeBits<<" engine="<<label
        <<" cycles="<<cycles<<" wall_ms="<<wall<<" frames="<<after.frames-before.frames
        <<" captures="<<after.captures-before.captures<<" completed="<<after.completed-before.completed
        <<" failed="<<after.failed-before.failed<<" cancelled="<<after.cancelled-before.cancelled
        <<" target_width_px="<<bounds.right-bounds.left<<" target_height_px="<<bounds.bottom-bounds.top
        <<" target_dpi="<<dpi<<'\n';
    for (const auto& process:processes) process.Print(label,wall);
    CloseHandle(host);
    Check(after.captures-before.captures==cycles*2 && after.completed-before.completed==cycles*2 &&
          after.frames>before.frames && after.failed==before.failed,
          "resource sample contains eight completed captured cross-process animations");
}
LRESULT CALLBACK Fixture(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if (msg==WM_PAINT || msg==WM_PRINT) {
        PAINTSTRUCT paint{};
        HDC dc=msg==WM_PRINT ? reinterpret_cast<HDC>(w) : BeginPaint(hwnd,&paint);
        RECT r{};GetClientRect(hwnd,&r);
        HBRUSH brush=CreateSolidBrush(RGB(38,73,116));FillRect(dc,&r,brush);DeleteObject(brush);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
        DrawTextW(dc,L"Lume 跨进程动画测试窗口",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if (msg==WM_PAINT) EndPaint(hwnd,&paint);
        return 0;
    }
    if (msg==Action) {
        switch (w) {
            case 0: ShowWindow(hwnd,SW_MINIMIZE);break;
            case 1: DefWindowProcW(hwnd,WM_SYSCOMMAND,SC_MINIMIZE,0);break;
            case 2: CloseWindow(hwnd);break;
            case 3: {WINDOWPLACEMENT p{};p.length=sizeof(p);GetWindowPlacement(hwnd,&p);
                     p.showCmd=SW_SHOWMINIMIZED;SetWindowPlacement(hwnd,&p);break;}
            case 4: ShowWindowAsync(hwnd,SW_MINIMIZE);break;
            case 5: ShowWindow(hwnd,SW_RESTORE);break;
            case 6: ShowWindow(hwnd,SW_HIDE);break;
            case 7: SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_SHOWWINDOW);break;
            case 8: DestroyWindow(hwnd);break;
            case 9: {
                wchar_t title[256]{};GetWindowTextW(hwnd,title,256);
                const auto nextTitle=std::wstring(title)+L" launch";
                HWND next=CreateWindowExW(0,L"LumeMinimizeFixture",nextTitle.c_str(),WS_OVERLAPPEDWINDOW,
                    210,170,600,420,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                SetWindowPos(next,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_SHOWWINDOW);break;
            }
        }
        return 0;
    }
    if (msg==WM_DESTROY) {PostQuitMessage(0);return 0;}
    return DefWindowProcW(hwnd,msg,w,l);
}
int Child(const wchar_t* title) {
    WNDCLASSW wc{};wc.lpfnWndProc=Fixture;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"LumeMinimizeFixture";RegisterClassW(&wc);
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,title,WS_OVERLAPPEDWINDOW,
        180,140,600,420,nullptr,nullptr,wc.hInstance,nullptr);
    if (!hwnd) return 2;
    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);
    MSG msg{};while (GetMessageW(&msg,nullptr,0,0)>0) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    return 0;
}
}
int wmain(int argc,wchar_t** argv) {
    const auto executable=Executable();
    if (argc==1 && executable.filename()==L"LumeMinimizeHost"+std::to_wstring(NativeBits)+L".exe")
        return StalledInitializationHost();
    const bool atomicOwner=argc==4 && std::wstring(argv[1])==L"--atomic-owner";
    if (atomicOwner || (argc==3 && std::wstring(argv[1])==L"--initialization-owner")) {
        MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
        MinimizeAnimationScope scope;
        scope.threadId=GetCurrentThreadId();scope.processId=GetCurrentProcessId();
        scope.requireBothArchitectures=false;scope.directory=argv[2];
        HANDLE atomicMapping=nullptr;
        if (atomicOwner) {
            atomicMapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,argv[3]);
            crashAfterCreation=atomicMapping ? static_cast<AtomicCreationState*>(
                MapViewOfFile(atomicMapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(AtomicCreationState))) : nullptr;
            if (!crashAfterCreation) return 15;
        }
        MinimizeAnimationRuntime runtime(scope,{},ObservedApi());
        lume::minimize_animation::Preferences settings;settings.enabled=true;
        runtime.Start(settings);
        Sleep(INFINITE);
        return 14;
    }
    if (argc==2 && std::wstring(argv[1])==L"--initialization-regression") {
        InitializationOwnerCrashRegression(executable);
        std::cout<<"non-graphical initialization owner-crash regression passed\n";
        return 0;
    }
    if (argc>=2 && std::wstring(argv[1])==L"--shutdown-regression") {
        const auto payload=PayloadDirectory(argc,argv,2);
        ShutdownLeaseRegression(payload);RetainedOwnershipRegression(payload);
        std::cout<<"non-graphical shutdown lease regression passed\n";
        return 0;
    }
    if (argc==2 && std::wstring(argv[1])==L"--help") {
        std::cout<<"Usage: lume-minimize-animation-runtime-test [--payload-directory <directory>]\n"
                 <<"Tests its own "<<NativeBits<<"-bit Host/DLL using only a dedicated child GUI thread.\n"
                 <<"The test executable is also its own fixture; no extra fixture executable is needed.\n"
                 <<"--initialization-regression runs only the non-graphical owner-crash regression.\n"
                 <<"--shutdown-regression runs only the thread-scoped shutdown lease regression.\n";
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc==3 && std::wstring(argv[1])==L"--child") return Child(argv[2]);
    const bool ownerMode=argc>=2 && std::wstring(argv[1])==L"--owner";
    Check(!ownerMode || argc>=4,"owner requires fixture thread and process IDs");
    const auto payloadDirectory=PayloadDirectory(argc,argv,ownerMode ? 4 : 1);
    if (ownerMode) {
        MinimizeAnimationScope scope;
        scope.threadId=std::wcstoul(argv[2],nullptr,10);scope.processId=std::wcstoul(argv[3],nullptr,10);
        scope.requireBothArchitectures=false;scope.directory=payloadDirectory;
        MinimizeAnimationRuntime runtime(scope);
        lume::minimize_animation::Preferences settings;settings.enabled=true;settings.durationMs=2000;
        if (!runtime.Start(settings)) return 3;
        Sleep(INFINITE);
        return 0;
    }
    InitializationOwnerCrashRegression(executable);
    ShutdownLeaseRegression(payloadDirectory);
    RetainedOwnershipRegression(payloadDirectory);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto title=L"Lume minimize fixture "+std::to_wstring(GetCurrentProcessId());
    std::wstring command=L"\""+std::wstring(exe)+L"\" --child \""+title+L"\"";
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    fixtureJob=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    Check(fixtureJob && SetInformationJobObject(fixtureJob,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),
          "dedicated test child cleanup job");
    std::atexit(CloseFixtureJob);
    Check(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&si,&pi),
          "start dedicated cross-process fixture");
    Check(AssignProcessToJobObject(fixtureJob,pi.hProcess),"scope cleanup only to fixture");
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    HWND target=nullptr;
    Check(Until([&]{target=FindWindowW(L"LumeMinimizeFixture",title.c_str());return target!=nullptr;}),
          "fixture window appears");
    DWORD targetPid=0;const DWORD tid=GetWindowThreadProcessId(target,&targetPid);
    Check(targetPid==pi.dwProcessId && targetPid!=GetCurrentProcessId(),"fixture is another process");
    BOOL initialTransitions=FALSE;
    const HRESULT transitionQuery=DwmGetWindowAttribute(target,DWMWA_TRANSITIONS_FORCEDISABLED,
                                                        &initialTransitions,sizeof(initialTransitions));
    const bool canReadTransitions=SUCCEEDED(transitionQuery);
    // This is documented as a set attribute. The Hook deliberately leaves it
    // unchanged if querying its previous value is unsupported; do not invent a
    // successful readback or a restoration guarantee for an unreadable value.
    std::cout<<"DWM transition_readback_supported="<<canReadTransitions
             <<" query_hresult=0x"<<std::hex<<static_cast<unsigned long>(transitionQuery)<<std::dec<<'\n';
    const auto transitionsRestored=[&] {
        if (!canReadTransitions) return true;
        BOOL current=!initialTransitions;
        return SUCCEEDED(DwmGetWindowAttribute(target,DWMWA_TRANSITIONS_FORCEDISABLED,&current,sizeof(current))) &&
            current==initialTransitions;
    };
    MinimizeAnimationScope scope;scope.threadId=tid;scope.processId=targetPid;scope.requireBothArchitectures=false;
    scope.directory=payloadDirectory;
    MinimizeAnimationRuntime runtime(scope);
    lume::minimize_animation::Preferences settings;settings.enabled=true;settings.durationMs=180;
    Check(!runtime.Snapshot().running && runtime.Snapshot().hostCount==0,"disabled has no host");
    Check(runtime.Start(settings),"install hooks for fixture thread only");
    Check(runtime.Snapshot().running && runtime.Snapshot().hooks==2,"host confirms actual hook installation");
    Check(runtime.Snapshot().hostCount==1 && runtime.Snapshot().x64==(NativeBits==64) &&
          runtime.Snapshot().x86==(NativeBits==32),"test actually starts the matching architecture host");
    std::cout<<"runtime test architecture="<<NativeBits<<" host started; hooks="<<runtime.Snapshot().hooks<<'\n';
    for (unsigned route=0;route<5;++route) {
        const auto previous=runtime.Snapshot();
        Check(PostMessageW(target,Action,route,0),"request minimize through real API");
        Check(Until([&]{return IsIconic(target) &&
            runtime.Snapshot().minimized>previous.minimized && runtime.Snapshot().workers==0;}),
            "each original minimize entrypoint reaches cross-process hook");
        const auto minimized=runtime.Snapshot();
        std::cout<<"route="<<route<<" min="<<minimized.minimized<<" frames="<<minimized.frames
                 <<" done="<<minimized.completed<<" failed="<<minimized.failed
                 <<" cancelled="<<minimized.cancelled<<" pid="<<minimized.lastTargetPid<<std::endl;
        Check(minimized.lastTargetPid==targetPid && minimized.captures>previous.captures &&
              minimized.frames>previous.frames && minimized.completed>previous.completed,
              "target process captures window and renders actual warped frames");
        Check(PostMessageW(target,Action,5,0),"request restore");
        Check(Until([&]{return !IsIconic(target) && runtime.Snapshot().restored>minimized.restored &&
            runtime.Snapshot().workers==0;}),"restore runs in child and drains worker");
        DWORD cloaked=99;DwmGetWindowAttribute(target,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        Check(!cloaked,"restore leaves child window visible at compositor");
    }
    MeasureEngine(runtime,settings,lume::minimize_animation::Engine::Modern,target,pi.hProcess);
    MeasureEngine(runtime,settings,lume::minimize_animation::Engine::Classic,target,pi.hProcess);
    settings.engine=lume::minimize_animation::Engine::Classic;
    Check(runtime.Configure(settings),"configure classic without restarting host");
    const auto classic=runtime.Snapshot();
    PostMessageW(target,Action,0,0);
    Check(Until([&]{const auto now=runtime.Snapshot();return IsIconic(target) &&
        now.workers==0 && now.completed>classic.completed && now.frames>classic.frames;}),
        "classic scanline engine renders real external window");
    PostMessageW(target,Action,5,0);
    Check(Until([&]{return !IsIconic(target) && runtime.Snapshot().workers==0;}),"classic restore completes");
    settings.launch=true;Check(runtime.Configure(settings),"enable experimental launch");
    const auto launching=runtime.Snapshot();PostMessageW(target,Action,9,0);
    Check(Until([&]{const auto now=runtime.Snapshot();return now.launched>launching.launched &&
        now.completed>launching.completed && !now.workers;}),"first SetWindowPos show renders launch genie");
    settings.launch=false;Check(runtime.Configure(settings),"disable launch without restarting host");
    Check(runtime.Stop(),"warmup stop");
    // User32 creates a process window-station connection on its first cross-
    // process HWND use. Measure repeated lifecycle growth after that warmup.
    DWORD handlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    const DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    const DWORD userBefore=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    DWORD childHandlesBefore=0;GetProcessHandleCount(pi.hProcess,&childHandlesBefore);
    const DWORD childGdiBefore=GetGuiResources(pi.hProcess,GR_GDIOBJECTS);
    const DWORD childUserBefore=GetGuiResources(pi.hProcess,GR_USEROBJECTS);
    Check(runtime.Start(settings),"restart after warmup");
    for (unsigned cycle=0;cycle<8;++cycle) {
        settings.durationMs=2000;Check(runtime.Configure(settings),"set long animation for stop race");
        const auto prior=runtime.Snapshot();PostMessageW(target,Action,0,0);
        Check(Until([&]{return runtime.Snapshot().frames>prior.frames;}),"animation enters render phase");
        Check(runtime.Stop(),"stop waits for external worker restoration");
        Check(!runtime.Snapshot().running && !runtime.Snapshot().hostCount,"stop releases host and hooks");
        DWORD cloaked=99;
        DwmGetWindowAttribute(target,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        Check(!cloaked && transitionsRestored(),"stop restores target cloak and any readable original transitions");
        PostMessageW(target,Action,5,0);
        Check(Until([&]{return !IsIconic(target);}),"target still responds after hook unload");
        Check(runtime.Start(settings),"restart clean hook lifecycle");
    }
    Check(runtime.Stop(),"final stop");
    // UnhookWindowsHookEx removes the registrations synchronously; USER32 can
    // defer unloading the target DLL until that GUI thread next enters USER32.
    Check(Until([&]{
        SendMessageTimeoutW(target,WM_NULL,0,0,SMTO_ABORTIFHUNG,50,nullptr);
        DWORD count=0;GetProcessHandleCount(pi.hProcess,&count);
        return count<=childHandlesBefore+2;
    },2000),"target hook DLL resources drain after USER32 dispatch");
    DWORD handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);
    DWORD childHandlesAfter=0;GetProcessHandleCount(pi.hProcess,&childHandlesAfter);
    std::cout<<"parent handles "<<handlesBefore<<" -> "<<handlesAfter
             <<", child handles "<<childHandlesBefore<<" -> "<<childHandlesAfter
             <<", child GDI "<<childGdiBefore<<" -> "<<GetGuiResources(pi.hProcess,GR_GDIOBJECTS)
             <<", child USER "<<childUserBefore<<" -> "<<GetGuiResources(pi.hProcess,GR_USEROBJECTS)<<'\n';
    Check(handlesAfter<=handlesBefore,"runtime handle count returns to warm baseline");
    Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdiBefore &&
          GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=userBefore,"no parent GDI or USER leak");
    Check(childHandlesAfter<=childHandlesBefore+2 &&
          GetGuiResources(pi.hProcess,GR_GDIOBJECTS)<=childGdiBefore &&
          GetGuiResources(pi.hProcess,GR_USEROBJECTS)<=childUserBefore+1,
          "child resources return to warm baseline within one lazy USER object");
    // Kill only a separate fixture owner. Exercise both a live minimize and a
    // genuinely cloaked restore; the target must survive the Host's job exit.
    for (unsigned restoring=0;restoring<2;++restoring) {
        PROCESS_INFORMATION owner{};
        command=L"\""+std::wstring(exe)+L"\" --owner "+std::to_wstring(tid)+L" "+std::to_wstring(targetPid);
        if (!payloadDirectory.empty()) command+=L" --payload-directory "+QuoteArgument(payloadDirectory);
        Check(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&si,&owner),
              "start separate test owner");
        Check(AssignProcessToJobObject(fixtureJob,owner.hProcess),"owner belongs to test cleanup scope");
        ResumeThread(owner.hThread);CloseHandle(owner.hThread);
        HANDLE map=nullptr;
        Check(Until([&]{map=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,ObjectName(NativeBits,L"Control").c_str());
            return map!=nullptr;}),"owner starts independent control mapping");
        auto* wire=static_cast<SharedControl*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControl)));
        Check(wire && Until([&]{return Read(wire->installedHooks)==2;}),"new owner installs actual hooks");
        HANDLE host=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(Read(wire->hostPid)));
        Check(host!=nullptr,"hold exact test host process");
        PostMessageW(target,Action,0,0);
        Check(Until([&]{return Read(wire->frames)>0;}),"owner test reaches an active frame");
        if (restoring) {
            Check(Until([&]{return IsIconic(target) && Read(wire->completed)>0 && !ActiveWorkers(*wire);}),
                  "prepare actual restore before second owner crash");
            const auto frames=Read(wire->frames);
            PostMessageW(target,Action,5,0);
            Check(Until([&]{
                DWORD cloaked=0;DwmGetWindowAttribute(target,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
                return cloaked && Read(wire->restored)>0 && Read(wire->frames)>frames && ActiveWorkers(*wire)>0;
            }),"second owner crash occurs while real restore cloak and worker are active");
        }
        Check(TerminateProcess(owner.hProcess,91),"simulate crash of dedicated test owner");
        Check(WaitForSingleObject(host,5000)==WAIT_OBJECT_0,"Host exits after parent crash");
        Check(Until([&]{
            DWORD cloaked=99;
            DwmGetWindowAttribute(target,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
            return !cloaked && transitionsRestored() && ActiveWorkers(*wire)==0 && ActiveEntries(*wire)==0;
        }),"parent crash independently drains target workers and restores compositor visibility");
        std::cout<<"owner-crash architecture="<<NativeBits<<" kind="<<(restoring ? "restore" : "minimize")
                 <<" frames="<<Read(wire->frames)<<" captures="<<Read(wire->captures)
                 <<" completed="<<Read(wire->completed)<<" workers="<<ActiveWorkers(*wire)
                 <<" entries="<<ActiveEntries(*wire)<<" cloak=0 transitions_readback_verified="<<canReadTransitions<<'\n';
        CloseHandle(host);CloseHandle(owner.hProcess);UnmapViewOfFile(wire);CloseHandle(map);
        PostMessageW(target,Action,5,0);
        Check(Until([&]{return !IsIconic(target);}),"target remains usable after owner death");
    }
    Check(runtime.Start(settings),"start final target-crash test");
    const auto dying=runtime.Snapshot();PostMessageW(target,Action,0,0);
    Check(Until([&]{return runtime.Snapshot().frames>dying.frames;}),"target-crash test reaches active frame");
    Check(TerminateProcess(pi.hProcess,92),"simulate crash of dedicated animation target");
    Check(WaitForSingleObject(pi.hProcess,4000)==WAIT_OBJECT_0,"test target exits");
    Check(runtime.Stop(),"host reaps only the exited event-owned process lease");
    CloseHandle(pi.hProcess);
    std::cout<<"cross-process minimize/restore 5 API routes, modern/classic, 8 stop races passed\n";
    return 0;
}
