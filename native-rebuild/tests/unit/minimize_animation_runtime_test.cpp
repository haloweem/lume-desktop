#include "platform/windows/minimize_animation/minimize_animation_runtime.h"
#include "platform/windows/minimize_animation/minimize_animation_ipc.h"
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include "platform/windows/dock/dock_animation_targets.h"
#include "platform/windows/minimize_animation/dock_genie_packet.h"
#include <chrono>
#include <vector>
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
constexpr UINT_PTR FirstPaintTimer=1;
constexpr wchar_t FirstShowCloaked[]=L"Lume.Fixture.FirstShowCloaked";
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
// Read the composited outline in one blit so the shoulder and neck come
// from the same frame. Only the fixture's solid client color contributes.
struct Outline {
    POINT interior{};
    double neckRatio=1, curvature=0;
    bool visible=false;
};
Outline FixtureOutline(RECT bounds) {
    const int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
    if (w<1 || h<1 || !lume::minimize_animation::SnapshotBytes(w,h)) return {};
    BITMAPINFO info{};
    info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    void* pixels=nullptr;
    HBITMAP frame=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    auto old=frame && dc ? SelectObject(dc,frame) : nullptr;
    const bool copied=old && BitBlt(dc,0,0,w,h,screen,bounds.left,bounds.top,SRCCOPY|CAPTUREBLT);
    GdiFlush();
    struct Span {int left=0,width=0;};
    std::vector<Span> rows(h);
    int begin=0,end=0,run=0;
    if (copied) for (int y=0;y<h;++y) {
        const auto* row=static_cast<const DWORD*>(pixels)+static_cast<std::size_t>(y)*w;
        int left=w,right=-1,count=0;
        for (int x=0;x<w;++x) {
            if ((row[x]&0xffffff)!=0x264974) continue;
            left=std::min(left,x);right=x;++count;
        }
        if (count>=8 && count>(right-left)/2) {
            rows[y]={left,right-left+1};
            if (++run>end-begin) {end=y+1;begin=end-run;}
        } else run=0;
    }
    if (old) SelectObject(dc,old);
    if (frame) DeleteObject(frame);
    if (dc) DeleteDC(dc);
    ReleaseDC(nullptr,screen);
    if (end-begin<32) return {};
    const auto at=[&](double depth){return rows[begin+int((end-begin-1)*depth)];};
    const double shoulder=at(.10).width,neck=at(.90).width;
    double deviation=0;
    for (const double fraction : {.25,.5,.75}) {
        const double straight=shoulder+(neck-shoulder)*fraction;
        deviation=std::max(deviation,std::abs(at(.10+.80*fraction).width-straight));
    }
    const auto solid=at(.28);
    return {{bounds.left+solid.left+solid.width/4,bounds.top+begin+LONG((end-begin-1)*.28)},
            neck/shoulder,deviation/shoulder,true};
}
LRESULT CALLBACK Fixture(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if (msg==WM_WINDOWPOSCHANGED && (reinterpret_cast<WINDOWPOS*>(l)->flags&SWP_SHOWWINDOW) &&
        !GetPropW(hwnd,FirstShowCloaked)) {
        DWORD cloak=0;
        DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloak,sizeof(cloak));
        SetPropW(hwnd,FirstShowCloaked,LongToHandle(cloak ? 1 : 2));
    }
    if (msg==WM_TIMER && w==FirstPaintTimer) {
        KillTimer(hwnd,FirstPaintTimer);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
        InvalidateRect(hwnd,nullptr,FALSE);
        UpdateWindow(hwnd);
        return 0;
    }
    if (msg==WM_PAINT || msg==WM_PRINT) {
        PAINTSTRUCT paint{};
        HDC dc=msg==WM_PRINT ? reinterpret_cast<HDC>(w) : BeginPaint(hwnd,&paint);
        RECT r{};GetClientRect(hwnd,&r);
        HBRUSH brush=CreateSolidBrush(RGB(38,73,116));FillRect(dc,&r,brush);DeleteObject(brush);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
        if (!GetWindowLongPtrW(hwnd,GWLP_USERDATA))
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
            case 10: ShowWindow(hwnd,SW_MAXIMIZE);break;
            case 11: SetActiveWindow(nullptr);SetActiveWindow(hwnd);break;
            case 15:
                ShowWindow(hwnd,SW_SHOWMINIMIZED);
                ShowWindow(hwnd,SW_RESTORE);
                break;
            case 12: {
                // Like QQ Music's shadow: an owned layered popup with a border.
                HWND shadow=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT,L"STATIC",L"shadow",
                    WS_POPUP|WS_BORDER,160,120,640,460,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
                SetLayeredWindowAttributes(shadow,0,60,LWA_ALPHA);
                ShowWindow(shadow,SW_SHOWNOACTIVATE);
                DestroyWindow(shadow);
                break;
            }
            case 9: {
                wchar_t title[256]{};GetWindowTextW(hwnd,title,256);
                const auto nextTitle=std::wstring(title)+(l<0 ? L" blank" : L" launch");
                HWND next=CreateWindowExW(0,L"LumeMinimizeFixture",nextTitle.c_str(),l ? WS_POPUP : WS_OVERLAPPEDWINDOW,
                    210,170,600,420,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                SetWindowLongPtrW(next,GWLP_USERDATA,l);
                if (l>0) SetTimer(next,FirstPaintTimer,static_cast<UINT>(l),nullptr);
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
    const bool dockMode=argc>=2 && std::wstring(argv[1])==L"--dock";
    const auto payloadDirectory=PayloadDirectory(argc,argv,ownerMode ? 4 : dockMode ? 2 : 1);
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
    if (!dockMode) {
        InitializationOwnerCrashRegression(executable);
        ShutdownLeaseRegression(payloadDirectory);
        RetainedOwnershipRegression(payloadDirectory);
    }
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
    if (dockMode) {
        settings.dockOnly=true;settings.close=true;settings.durationMs=400;
        Check(runtime.Start(settings),"start Dock renderer and fixture-only window events");
        const auto monitor=MonitorFromWindow(target,MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);
        const RECT anchor{info.rcWork.right-100,info.rcWork.bottom-100,info.rcWork.right-44,info.rcWork.bottom-44};
        WNDCLASSW tileClass{};tileClass.lpfnWndProc=DefWindowProcW;tileClass.hInstance=GetModuleHandleW(nullptr);
        tileClass.lpszClassName=L"LumeGenieTestTile";RegisterClassW(&tileClass);
        HWND icon=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,tileClass.lpszClassName,L"Dock test slot",
            WS_POPUP|WS_VISIBLE,anchor.left,anchor.top,56,56,nullptr,nullptr,nullptr,nullptr);
        Check(icon!=nullptr,"create a visible fixture Dock icon");
        dock_animation::Targets table{};
        table.icons=table.bar=HandleToLong(icon);
        table.count=1;table.slots[0]={dock_animation::CollectionProgram,0,dock_slots::ProgramKey(executable.wstring()),
            anchor.left,anchor.top,anchor.right,anchor.bottom};
        dock_animation::Publisher publisher;
        publisher.Publish(monitor,table);
        HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,ObjectName(NativeBits,L"Control").c_str());
        auto* shared=static_cast<SharedControl*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControl)));
        Check(shared!=nullptr,"read presented frame counters");
        auto play=[&](unsigned action,const char* label) {
            const auto completed=Read(shared->dockCompleted),first=Read(shared->dockFrames);
            auto previous=first;
            std::vector<double> intervals;
            using Timer=std::chrono::steady_clock;
            auto last=Timer::now(),firstFrameTime=last;
            RECT sourceRect{};
            DwmGetWindowAttribute(target,DWMWA_EXTENDED_FRAME_BOUNDS,&sourceRect,sizeof(sourceRect));
            bool pixelSeen=false, testedHit=false;
            const bool sample=action==1;
            RECT region{};
            UnionRect(&region,&sourceRect,&anchor);
            IntersectRect(&region,&region,&info.rcMonitor);
            if (action == 13 || action == 14 || action == 15) {
                Check(SendMessageTimeoutW(target,dock_animation::ActivatedMessage(),0,0,
                    SMTO_ABORTIFHUNG,250,nullptr)!=0,"prepare the minimized Dock window before system restore");
                if (action==15)
                    Check(SendMessageTimeoutW(target,Action,15,0,SMTO_ABORTIFHUNG,250,nullptr)!=0,
                        "repeat QQ minimized-state notification on the window thread before restore");
                else if (action==13) ShowWindow(target,SW_RESTORE);
                else Check(SendMessageTimeoutW(target,WM_SYSCOMMAND,SC_RESTORE,0,
                    SMTO_ABORTIFHUNG,250,nullptr)!=0,"restore on the window thread after Dock preparation");
            } else if (action == 11)
                Check(SendNotifyMessageW(target,dock_animation::ActivatedMessage(),0,0),"notify the activated Dock window");
            else Check(PostMessageW(target,Action,action,action==9 ? 450 : 0),"issue real fixture window command");
            const auto deadline=GetTickCount64()+4000;
            while (GetTickCount64()<deadline) {
                MSG msg{};
                while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&msg);
                const auto current=Read(shared->dockFrames);
                if (current!=previous) {
                    if (action==9 && previous==first) {
                        const auto next=FindWindowW(L"LumeMinimizeFixture",(title+L" launch").c_str());
                        Check(next && !GetWindowLongPtrW(next,GWLP_USERDATA),"wait for actual first content");
                    }
                    const auto now=Timer::now();
                    if (previous==first) firstFrameTime=now;
                    if (sample && !pixelSeen) {
                        const float t=std::chrono::duration<float,std::milli>(now-firstFrameTime).count()/settings.durationMs;
                        if (t>.44f && t<.55f) {
                            const auto shape=FixtureOutline(region);
                            if (shape.visible) {
                                std::cout<<"mid-frame neck/shoulder="<<shape.neckRatio
                                         <<" deviation from straight sides="<<shape.curvature<<std::endl;
                                pixelSeen=shape.neckRatio<.36 && shape.curvature>.065;
                                HWND overlay=FindWindowW(dock_genie::OverlayClass,L"LumeDockGenie");
                                testedHit=overlay && WindowFromPhysicalPoint(shape.interior)!=overlay;
                            }
                        }
                    }
                    if (previous!=first) intervals.push_back(std::chrono::duration<double,std::milli>(now-last).count()/(current-previous));
                    previous=current;last=now;
                }
                if (Read(shared->dockCompleted)>completed && !ActiveWorkers(*shared)) break;
                Sleep(1);
            }
            std::sort(intervals.begin(),intervals.end());
            std::cout<<"dock "<<label<<" frames="<<Read(shared->dockFrames)-first<<" completed="<<Read(shared->dockCompleted)-completed
                <<" median_ms="<<(intervals.empty()?0:intervals[intervals.size()/2])
                <<" p95_ms="<<(intervals.empty()?0:intervals[(intervals.size()-1)*95/100])
                <<" restored="<<Read(shared->restored)<<" failed="<<Read(shared->failed)
                <<" captures="<<Read(shared->captures)<<" workers="<<ActiveWorkers(*shared)<<std::endl;
            Check(Read(shared->dockCompleted)>completed && Read(shared->dockFrames)>first+2,label);
            if (sample) {
                Check(pixelSeen,"composited mid-frame has a curved shoulder and a narrow neck");
                Check(testedHit,"composition overlay passes mouse input through its painted content");
            }
        };
        const auto clickProgram = dock_slots::ProgramKey(executable.wstring());
        const auto beforeFocus = Read(shared->dockCompleted);
        Check(publisher.RecordClick(clickProgram, 0, anchor),"record visible Dock click for inactive window");
        SendMessageW(target,Action,12,0);
        Check(Read(shared->launched)==0 && Read(shared->captures)==0,"owned shadow leaves the click for the main window");
        play(11,"Dock click activates an existing window");
        PostMessageW(target,Action,11,0);
        Sleep(500);
        Check(Read(shared->dockCompleted)==beforeFocus+1,"ordinary activation does not replay the Dock click");
        PostMessageW(target,Action,6,0);
        Check(Until([&]{return !IsWindowVisible(target);}),"hide existing window like a tray application");
        Check(publisher.RecordClick(clickProgram, 0, anchor),"record visible Dock click for hidden window");
        play(7,"Dock click reopens the same hidden window");
        play(1,"title-minimize");Check(IsIconic(target),"window minimized");
        play(5,"restore");Check(!IsIconic(target),"window restored");
        play(10,"maximize");Check(IsZoomed(target),"window maximized");
        play(1,"minimize the maximized window");
        Check(publisher.RecordClick(clickProgram,0,anchor),"record Dock click for maximized restore");
        play(13,"Dock restores the maximized window through the system fallback");
        Check(!IsIconic(target) && IsZoomed(target),"Dock restore preserves the maximized state");
        play(1,"minimize the maximized window before a native Dock restore");
        Check(publisher.RecordClick(clickProgram,0,anchor),"record Dock click before native restore notifications");
        const auto restoredBeforeDock=Read(shared->restored),failedBeforeDock=Read(shared->failed);
        play(14,"Dock restore survives the native restore notification");
        Check(!IsIconic(target) && IsZoomed(target) && Read(shared->restored)==restoredBeforeDock+1 &&
              Read(shared->failed)==failedBeforeDock,"duplicate restore notification preserves one completed animation");
        play(1,"minimize maximized window before QQ restore sequence");
        Check(publisher.RecordClick(clickProgram,0,anchor),"record Dock click before repeated minimize notification");
        const auto restoredBeforeQQ=Read(shared->restored),failedBeforeQQ=Read(shared->failed);
        play(15,"QQ repeated minimized-state notification preserves Dock restore");
        Check(!IsIconic(target) && IsZoomed(target) && Read(shared->restored)==restoredBeforeQQ+1 &&
              Read(shared->failed)==failedBeforeQQ,"redundant minimize keeps the prepared restore alive");
        play(5,"unmaximize");
        RECT bounds{};GetWindowRect(target,&bounds);
        const RECT covered{bounds.left+56,bounds.top+56,bounds.left+112,bounds.top+112};
        SetWindowPos(icon,HWND_NOTOPMOST,covered.left,covered.top,56,56,SWP_NOACTIVATE);
        SetWindowPos(icon,HWND_BOTTOM,0,0,0,0,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);
        SetForegroundWindow(target);
        table.slots[0]={dock_animation::CollectionProgram,0,clickProgram,covered.left,covered.top,covered.right,covered.bottom};
        publisher.Publish(monitor,table);
        Check(GetAncestor(WindowFromPhysicalPoint({covered.left+28,covered.top+28}),GA_ROOT)==target,
              "source window covers the desktop-layer collection tile");
        play(0,"minimize into covered collection without a click");
        SetWindowPos(icon,HWND_TOPMOST,anchor.left,anchor.top,56,56,SWP_NOACTIVATE);
        table.slots[0]={dock_animation::CollectionProgram,0,clickProgram,anchor.left,anchor.top,anchor.right,anchor.bottom};
        publisher.Publish(monitor,table);
        play(5,"restore after minimizing into the covered collection");
        ShowWindow(icon,SW_HIDE);
        const LONG before=Read(shared->dockFrames);
        PostMessageW(target,Action,1,0);
        Check(Until([&]{return IsIconic(target)!=FALSE;}),"native minimize with hidden icon");
        PostMessageW(target,Action,5,0);
        Check(Until([&]{return !IsIconic(target);}),"native restore with hidden icon");
        Check(before==Read(shared->dockFrames),"hidden icon produces zero Dock frames");
        ShowWindow(icon,SW_SHOWNOACTIVATE);
        settings.durationMs=180; Check(runtime.Configure(settings),"match legacy performance fixture duration");
        const auto baseline=Read(shared->dockFrames);
        for (int cycle=0;cycle<4;++cycle) {
            play(0,"benchmark-minimize");
            play(5,"benchmark-restore");
        }
        std::cout<<"dock benchmark 4 cycles 180ms frames="<<Read(shared->dockFrames)-baseline<<std::endl;
        settings.durationMs=400; Check(runtime.Configure(settings),"restore default duration");
        const auto launches=Read(shared->launched);
        Check(publisher.RecordClick(clickProgram,0,anchor),"record Dock click for delayed first paint");
        play(9,"first-show after delayed first paint");
        const auto launch=FindWindowW(L"LumeMinimizeFixture",(title+L" launch").c_str());
        DWORD cloak=99;
        Check(GetPropW(launch,FirstShowCloaked)==LongToHandle(1) &&
              SUCCEEDED(DwmGetWindowAttribute(launch,DWMWA_CLOAKED,&cloak,sizeof(cloak))) &&
              !cloak && Read(shared->launched)==launches+1,"first show is covered before native presentation and revealed once");
        const auto failed=Read(shared->failed),frames=Read(shared->dockFrames);
        Check(publisher.RecordClick(clickProgram,0,anchor),"record Dock click for blank window");
        Check(PostMessageW(target,Action,9,-1),"show window that supplies only blank captures");
        Check(Until([&]{return Read(shared->failed)>failed && !ActiveWorkers(*shared);}),
              "blank launch expires");
        const auto blank=FindWindowW(L"LumeMinimizeFixture",(title+L" blank").c_str());
        Check(IsWindowVisible(blank) && GetPropW(blank,FirstShowCloaked)==LongToHandle(1) &&
              SUCCEEDED(DwmGetWindowAttribute(blank,DWMWA_CLOAKED,&cloak,sizeof(cloak))) &&
              !cloak && Read(shared->dockFrames)==frames,"blank launch hands off to the visible native window without replay");
        play(8,"close");
        Check(Read(shared->dockClosed)==1 && Read(shared->dockZoomed)>=2,"separate close and zoom paths");
        UnmapViewOfFile(shared);CloseHandle(mapping);
        Check(runtime.Stop(),"stop Dock animation host");
        DestroyWindow(icon); CloseHandle(pi.hProcess);
        return 0;
    }
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
