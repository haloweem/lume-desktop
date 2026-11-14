#include "platform/windows/material/capture_retirement.h"
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using Queue=lume_glass::CaptureRetirementQueue;
void Check(bool ok,const char* reason){if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}}
template<class Predicate> bool Wait(Predicate predicate,DWORD timeout=3000){
    const auto end=GetTickCount64()+timeout;
    do{if(predicate())return true;Sleep(1);}while(GetTickCount64()<end);
    return predicate();
}
struct Graphics {
    explicit Graphics(std::atomic_uint& destroyed):destroyed(destroyed){}
    ~Graphics(){++destroyed;}
    std::atomic_uint& destroyed;
};
struct Capture {
    std::shared_ptr<Graphics> graphics;
    HANDLE began=nullptr,release=nullptr;
    std::atomic_uint* destroyed=nullptr;
    std::atomic_uint* running=nullptr;
    std::atomic_uint* maximum=nullptr;
    std::atomic<DWORD>* cleanupThread=nullptr;
    bool block=false;
    ~Capture(){
        const auto count=++*running;
        auto high=maximum->load();while(high<count&&!maximum->compare_exchange_weak(high,count)){}
        DWORD unset=0;cleanupThread->compare_exchange_strong(unset,GetCurrentThreadId());
        Check(cleanupThread->load()==GetCurrentThreadId(),"all cleanup uses the same worker");
        if(block){SetEvent(began);WaitForSingleObject(release,INFINITE);}
        ++*destroyed;--*running;
    }
};
void Destroy(void* object) noexcept {delete static_cast<Capture*>(object);}
std::shared_ptr<Capture> Make(Queue& queue,std::shared_ptr<Graphics> graphics,
    std::atomic_uint& destroyed,std::atomic_uint& running,std::atomic_uint& maximum,
    std::atomic<DWORD>& thread,HANDLE began,HANDLE release,bool block=false){
    const auto slot=queue.Reserve(graphics.get());
    if(slot==Queue::Invalid)return {};
    auto* capture=new Capture{std::move(graphics),began,release,&destroyed,&running,&maximum,&thread,block};
    return std::shared_ptr<Capture>(capture,[&queue,slot](Capture* object){queue.Retire(slot,object,&Destroy);});
}
bool FailPrepare() noexcept {return false;}
HANDLE initializationRelease=nullptr;
bool DelayedPrepare() noexcept {return WaitForSingleObject(initializationRelease,3000)==WAIT_OBJECT_0;}
int BlockedExit(){
    auto& queue=*new Queue(nullptr,nullptr);
    std::atomic_uint graphicsDestroyed{0},destroyed{0},running{0},maximum{0};
    std::atomic<DWORD> cleanupThread{0};
    const HANDLE began=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    const HANDLE release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Check(began&&release,"blocked-exit barriers created");
    auto graphics=std::make_shared<Graphics>(graphicsDestroyed);
    auto blocked=Make(queue,graphics,destroyed,running,maximum,cleanupThread,began,release,true);
    auto pending=Make(queue,graphics,destroyed,running,maximum,cleanupThread,began,release);
    Check(blocked&&pending,"blocked-exit capture fixtures admitted");
    blocked.reset();
    Check(WaitForSingleObject(began,2000)==WAIT_OBJECT_0,"blocked-exit cleanup entered its permanent wait");
    pending.reset();graphics.reset();
    const auto state=queue.Inspect();
    Check(state.total==2&&state.cleaning==1&&state.pending==1&&destroyed==0&&graphicsDestroyed==0,
        "normal exit starts with a permanently blocked Close and pending resources");
    // Leave the barrier unsignaled. Normal CRT/process teardown must not join
    // the process-lifetime queue or try to destroy its pending captures.
    return 0;
}
void CheckBlockedExit(){
    wchar_t executable[32768]{};
    const auto length=GetModuleFileNameW(nullptr,executable,32768);
    Check(length&&length<32768,"blocked-exit test locates its own executable");
    std::wstring command=L"\""+std::wstring(executable,length)+L"\" --blocked-exit";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION child{};
    Check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
        nullptr,nullptr,&startup,&child)!=FALSE,"blocked-exit child starts");
    CloseHandle(child.hThread);
    const auto result=WaitForSingleObject(child.hProcess,3000);
    if(result!=WAIT_OBJECT_0){
        // Only the test child created above may be terminated on failure.
        TerminateProcess(child.hProcess,1);WaitForSingleObject(child.hProcess,1000);
        CloseHandle(child.hProcess);
        Check(false,"normal process exit must finish within three seconds while cleanup is blocked");
    }
    DWORD exitCode=1;const bool read=GetExitCodeProcess(child.hProcess,&exitCode)!=FALSE;
    CloseHandle(child.hProcess);
    Check(read&&exitCode==0,"blocked cleanup and pending captures do not prevent normal exit");
}
}
int main(int argc,char** argv){
    if(argc==2&&std::strcmp(argv[1],"--blocked-exit")==0)return BlockedExit();
    // These fixtures explicitly omit the production DLL pin callback. They do
    // not attempt to pin an EXE or pretend to verify material-module identity.
    auto& queue=*new Queue(nullptr,nullptr);
    std::atomic_uint graphicsDestroyed{0},destroyed{0},running{0},maximum{0};
    std::atomic<DWORD> cleanupThread{0};
    HANDLE began=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Check(began&&release,"cleanup barriers created");
    auto firstDevice=std::make_shared<Graphics>(graphicsDestroyed);
    auto secondDevice=std::make_shared<Graphics>(graphicsDestroyed);
    const auto firstIdentity=firstDevice.get();
    std::vector<std::shared_ptr<Capture>> captures;
    for(unsigned i=0;i<Queue::Capacity;++i){
        auto capture=Make(queue,i<Queue::Capacity/2?firstDevice:secondDevice,
            destroyed,running,maximum,cleanupThread,began,release,i==0);
        Check(capture!=nullptr,"live capture admission fills the fixed budget");
        captures.push_back(std::move(capture));
    }
    const auto initial=queue.Inspect();
    Check(initial.total==Queue::Capacity&&initial.active==Queue::Capacity&&initial.pending==0&&initial.cleaning==0,
        "the budget includes every live capture before retirement");
    Check(queue.DeviceCount(nullptr)==2&&queue.DeviceCount(firstIdentity)==2,"live device identities are deduplicated");
    firstDevice.reset();secondDevice.reset();
    captures.front().reset();
    Check(WaitForSingleObject(began,3000)==WAIT_OBJECT_0,"cleanup reaches the deliberately blocked Close");
    Check(cleanupThread.load()!=GetCurrentThreadId(),"final cleanup never runs on the releasing render/UI caller");
    captures.clear(); // Must return while the first cleanup remains blocked.
    const auto stalled=queue.Inspect();
    Check(stalled.total==Queue::Capacity&&stalled.active==0&&stalled.cleaning==1&&stalled.pending==Queue::Capacity-1,
        "a stalled Close retains all real pending resources inside the original bound");
    Check(queue.Reserve(nullptr)==Queue::Invalid,"a stalled cleanup cannot admit unbounded replacement captures");
    Check(queue.DeviceCount(nullptr)==2&&graphicsDestroyed==0,"queued and closing captures retain both devices and their statistics");
    Check(destroyed==0&&running==1&&maximum==1,"there is one cleanup worker and the render caller remains free");
    SetEvent(release);
    Check(Wait([&]{return queue.Inspect().total==0;}),"unblocking Close eventually releases every occupied slot");
    Check(destroyed==Queue::Capacity&&graphicsDestroyed==2&&queue.DeviceCount(nullptr)==0,
        "statistics become zero only after capture and graphics destruction actually finish");
    Check(maximum==1,"cleanup tasks never overlap");
    const auto reused=queue.Reserve(nullptr);
    Check(reused!=Queue::Invalid,"finished cleanup returns admission capacity");
    queue.Abandon(reused);
    Check(queue.Inspect().total==0,"failed construction can return its unused reserved slot");
    auto& refused=*new Queue(&FailPrepare,nullptr);
    Check(refused.Reserve(nullptr)==Queue::Invalid&&refused.Inspect().total==0,
        "a failed module lifetime prerequisite never admits capture objects");
    auto& failedWorker=*new Queue(nullptr,&FailPrepare);
    Check(failedWorker.Reserve(nullptr)==Queue::Invalid&&failedWorker.Inspect().total==0,
        "a failed worker apartment never admits capture objects");
    initializationRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Check(initializationRelease!=nullptr,"initialization barrier created");
    auto& delayed=*new Queue(nullptr,&DelayedPrepare);
    const auto start=GetTickCount64();
    Check(delayed.Reserve(nullptr)==Queue::Invalid,"an unresponsive initializer does not block admission indefinitely");
    Check(GetTickCount64()-start<2000&&delayed.Inspect().total==0,"initialization wait has a finite caller bound");
    const auto repeated=GetTickCount64();
    for(unsigned i=0;i<64;++i)Check(delayed.Reserve(nullptr)==Queue::Invalid,"pending initialization keeps capture admission closed");
    Check(GetTickCount64()-repeated<1000,"later targets poll initialization without repeating the first wait");
    SetEvent(initializationRelease);
    unsigned readySlot=Queue::Invalid;
    Check(Wait([&]{readySlot=delayed.Reserve(nullptr);return readySlot!=Queue::Invalid;}),
        "a delayed initializer can recover without creating another worker");
    delayed.Abandon(readySlot);
    CloseHandle(initializationRelease);CloseHandle(release);CloseHandle(began);
    CheckBlockedExit();
    std::cout<<"capture-retirement: blocked Close, bounded admission, single worker, graphics lifetime, real statistics and normal blocked-cleanup exit passed\n";
}
