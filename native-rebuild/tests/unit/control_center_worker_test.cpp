#include "platform/windows/control_center/panel_worker.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
using lume::platform::windows::control_center::PanelWorker;
using namespace std::chrono_literals;
namespace {
int failures=0;
void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);++failures;}}
template<class F> bool Until(F condition){for(int i=0;i<2000;++i){if(condition())return true;std::this_thread::sleep_for(1ms);}return condition();}
}
int main(){
    std::mutex mutex;std::condition_variable wake;bool release=false;
    std::atomic<int> active=0,maximum=0,started=0,oldNotifications=0,newNotifications=0,staleActions=0,apartments=0;
    auto worker=std::make_shared<PanelWorker>([&](auto,const auto& job){
        const int concurrent=++active;maximum.store(std::max(maximum.load(),concurrent));++started;
        if(job.key=="blocked") {std::unique_lock lock(mutex);wake.wait(lock,[&]{return release;});}
        if(job.key=="stale")++staleActions;
        --active;return job.key;
    },[&]{++apartments;},[&]{--apartments;});
    auto generation=worker->Bind([&](auto){++oldNotifications;return true;});
    Check(worker->Push(generation,{"blocked",0}),"first action admitted");
    Check(Until([&]{return started==1;}),"old system call is blocked");
    worker->Push(generation,{"stale",0});
    const auto before=std::chrono::steady_clock::now();
    for(int i=0;i<100;++i){worker->Unbind(generation);generation=worker->Bind([&](auto){++newNotifications;return true;});}
    Check(std::chrono::steady_clock::now()-before<500ms,"rapid close/reopen never waits for old call");
    Check(started==1&&apartments==1,"reopening does not create another worker or apartment");
    for(int i=0;i<500;++i)worker->Push(generation,{"volume",i});
    Check(worker->Queued()==1,"slider traffic is coalesced");
    for(int i=0;i<500;++i)worker->Push(generation,{"action",i});
    Check(worker->Queued()==PanelWorker::Capacity,"queued system actions are bounded");
    Check(!worker->Push(generation-1,{"stale",0}),"retired generation cannot enqueue");
    {std::lock_guard lock(mutex);release=true;}wake.notify_all();
    Check(Until([&]{return worker->Queued()==0&&active==0&&newNotifications>0;}),"new generation runs after blocked call completes");
    Check(maximum==1&&staleActions==0&&oldNotifications==0,"system calls stay serial and retired actions/results are discarded");
    Check(newNotifications==1,"unconsumed state messages coalesce");
    Check(!worker->Take(generation).empty(),"current panel consumes newest state");
    worker->Push(generation,{"refresh",true});
    Check(Until([&]{return newNotifications==2;}),"consuming a state permits the next notification");
    worker->Unbind(generation);
    Check(Until([&]{return !worker->Running()&&apartments==0;}),"unbound idle worker releases its apartment");
    if(!failures)std::puts("PASS control center generation isolation, bounded queues and single worker");
    return failures?1:0;
}
