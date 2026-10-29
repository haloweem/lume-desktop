#include "apps/shell/minimize_animation_session.h"
#include "platform/windows/minimize_animation/minimize_animation_ipc.h"
#include "vendor/nlohmann/json.hpp"
#include <map>
#include <cstdlib>
#include <iostream>
using namespace lume;
namespace {
void Check(bool value,const char* message) {if (!value) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
struct Files final : settings::IFileStore {
    std::map<std::string,std::string> values;bool fail=false;
    bool Exists(const std::string& p)const override{return values.count(p)!=0;}
    bool Read(const std::string& p,std::string& s)const override{
        const auto it=values.find(p);if(it==values.end())return false;s=it->second;return true;
    }
    bool Write(const std::string& p,const std::string& s)override{if(fail)return false;values[p]=s;return true;}
    bool Rename(const std::string& a,const std::string& b)override{
        if(fail || !Exists(a))return false;
        values[b]=values[a];values.erase(a);return true;
    }
    bool Remove(const std::string& p)override{values.erase(p);return true;}
};
nlohmann::json Json(const std::wstring& s){return nlohmann::json::parse(s.begin(),s.end());}
}
int main() {
    MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
    Files files;unsigned notifications=0;
    platform::windows::MinimizeAnimationScope scope;
    scope.threadId=GetCurrentThreadId();scope.processId=GetCurrentProcessId();scope.requireBothArchitectures=false;
    const auto notify=[&](const std::wstring& state,bool){
        Check(Json(state)["type"]=="minimize-state","typed notification");++notifications;
    };
    {
        shell::MinimizeAnimationSession session(GetModuleHandleW(nullptr),"animation.json",notify,&files,scope);
        Check(session.Restore() && !session.Running() && files.values.empty(),"fresh disabled creates no host or config");
        const auto state=Json(session.State());
        Check(state["durationMs"]==450 && state["restore"]==true && state["launch"]==true &&
              state["multiMonitor"]==false && state["unhideTaskbar"]==true &&
              state["unhideDurationMs"]==450 && state["engine"]=="modern","source settings in management state");
        Check(state["entries"]==0 && state["pendingCleanup"]==0,"fresh state has no hidden cleanup ownership");
        for (const auto* invalid:{L"v1|weather|enabled|true",L"v1|minimize|durationMs|049",
                L"v1|minimize|durationMs|450.5",L"v1|minimize|durationMs|2001",
                L"v1|minimize|unhideDurationMs|-1",L"v1|minimize|engine|unknown",
                L"v1|minimize|enabled|TRUE",L"v1|minimize|launch|true|extra"})
            Check(!session.HandleMessage(invalid),"reject noncanonical command");
        auto reply=session.HandleMessage(L"v1|minimize|durationMs|600");
        Check(reply && Json(*reply)["saved"]==true && !session.Running(),"disabled settings save without host");
        reply=session.HandleMessage(L"v1|minimize|enabled|true");
        Check(reply && Json(*reply)["saved"]==true && session.Running(),"session starts actual scoped hooks");
        const auto saved=files.values["animation.json"];files.fail=true;
        reply=session.HandleMessage(L"v1|minimize|engine|classic");
        Check(reply && Json(*reply)["error"]=="save" && session.Preferences().engine==minimize_animation::Engine::Modern &&
              session.Running() && files.values["animation.json"]==saved,"save failure restores running settings");
        reply=session.HandleMessage(L"v1|minimize|enabled|false");
        Check(reply && Json(*reply)["error"]=="save" && session.Running(),"failed disable persistence restarts prior runtime");
        files.fail=false;reply=session.HandleMessage(L"v1|minimize|engine|classic");
        Check(reply && Json(*reply)["saved"]==true && session.Preferences().engine==minimize_animation::Engine::Classic,
              "classic preference saved");
        using namespace platform::windows::minimize_detail;
        HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,ObjectName(NativeBits,L"Control").c_str());
        auto* wire=mapping ? static_cast<SharedControl*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControl))) : nullptr;
        Check(wire!=nullptr,"hold scoped session shutdown control");
        FILETIME created{},ended{},kernel{},user{};
        Check(GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user),"identify session fixture lease");
        auto& lease=wire->processes[0];
        lease.createdLow=static_cast<LONG>(created.dwLowDateTime);lease.createdHigh=static_cast<LONG>(created.dwHighDateTime);
        InterlockedExchange(&lease.pid,static_cast<LONG>(GetCurrentProcessId()));
        InterlockedExchange(&lease.entries,1);InterlockedExchange(&lease.workers,1);
        const bool stopped=session.Shutdown();
        const auto pending=Json(session.State());
        InterlockedExchange(&lease.workers,0);InterlockedExchange(&lease.entries,0);
        UnmapViewOfFile(wire);CloseHandle(mapping);
        Check(!stopped && !session.Running() && pending["error"]=="stop-timeout" &&
              pending["hosts"]==0 && pending["hooks"]==0 && pending["workers"]==1 &&
              pending["entries"]==1 && pending["pendingCleanup"]==1,
              "session reports unfinished target cleanup after bounded Host exit");
        Check(session.Shutdown() && !session.Running() && Json(session.State())["pendingCleanup"]==0,
              "session shutdown retries actual cleanup and releases retained ownership");
        Check(!session.HandleMessage(L"v1|minimize|enabled|true"),"stopped session cannot restart");
    }
    {
        shell::MinimizeAnimationSession session(GetModuleHandleW(nullptr),"animation.json",notify,&files,scope);
        Check(session.Restore() && session.Running() && session.Preferences().durationMs==600,
              "enabled setting restores on application startup");
        auto reply=session.HandleMessage(L"v1|minimize|reset");
        Check(reply && session.Running() && session.Preferences().durationMs==450 &&
              session.Preferences().engine==minimize_animation::Engine::Modern,"reset preserves enable and source defaults");
        reply=session.HandleMessage(L"v1|minimize|enabled|false");
        Check(reply && Json(*reply)["saved"]==true && !session.Running(),"disable is durable");
    }
    Check(notifications>=8,"session publishes actual results");
    std::cout<<"minimize animation session settings/rollback/restart passed\n";
}
