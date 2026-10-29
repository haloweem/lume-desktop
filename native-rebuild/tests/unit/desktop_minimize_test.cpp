#include "apps/shell/desktop_application.h"
#include "core/minimize_animation/minimize_animation_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <iostream>

namespace {
using namespace lume;
bool passed=true;
void Check(bool value,const char* reason) {
    if(!value){passed=false;std::cerr<<"FAIL: "<<reason<<'\n';}
}
struct Probe {
    shell::DesktopApplication* app=nullptr;
    nlohmann::json latest;
    int stage=0;
    ULONGLONG deadline=0,closedAt=0;
    bool observedActive=false,observedSaved=false;
    void Message(const std::wstring& text) {
        const auto message=nlohmann::json::parse(text.begin(),text.end(),nullptr,false);
        if(message.is_discarded()||message.value("type","")!="minimize-state")return;
        latest=message;
        observedActive=observedActive||(message.value("running",false)&&message.value("hosts",0)>0);
        observedSaved=observedSaved||(message.value("saved",false)&&message.value("result","")=="applied");
    }
    void Tick() {
        if(GetTickCount64()>deadline) {
            std::cerr<<"stage="<<stage<<" state="<<latest.dump()<<'\n';
            Check(false,"animation WebView integration deadline");app->Exit();return;
        }
        if(!latest.is_object())return;
        const bool settled=latest.value("error","unknown").empty();
        if(stage==0&&app->ManagerReady()&&settled&&latest.value("running",false)&&
           latest.value("durationMs",0)==600&&latest.value("engine","")=="classic"&&
           !latest.value("restore",true)) {
            Check(observedActive&&observedSaved,"page sees actual host and durable setting acknowledgement");
            Check(!app->ClockRunning()&&!app->CalendarRunning(),"animation has no unrelated widget side effects");
            stage=1;closedAt=GetTickCount64();app->CloseManager();
        } else if(stage==1&&!app->ManagerHandle()&&GetTickCount64()-closedAt>800) {
            stage=2;app->Open(shell::ManagerPage::MinimizeAnimation);
        } else if(stage==2&&app->ManagerReady()&&settled&&!latest.value("enabled",true)&&
                  !latest.value("running",true)&&latest.value("hosts",1)==0) {
            Check(latest.value("durationMs",0)==600&&latest.value("engine","")=="classic",
                  "reopened manager retains independent animation preferences");
            stage=3;app->Exit();
        }
    }
};
Probe* active=nullptr;
void CALLBACK Observe(HWND,UINT,UINT_PTR,DWORD){if(active)active->Tick();}
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto root=std::filesystem::path(executable).parent_path()/L"desktop-minimize-artifacts"/
        (std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root);
    Probe probe;shell::DesktopApplicationOptions options;
    options.clockPreferencesPath=(root/"clock.json").u8string();
    options.calendarPreferencesPath=(root/"calendar.json").u8string();
    options.minimizePreferencesPath=(root/"minimize.json").u8string();
    options.verification=true;
    // Exercise the real Host and hook DLL on this test's GUI thread only.
    // Cross-process and x86 window animation are covered by the runtime probe.
    options.verificationMinimizeScope.threadId=GetCurrentThreadId();
    options.verificationMinimizeScope.processId=GetCurrentProcessId();
    options.verificationMinimizeScope.requireBothArchitectures=false;
    options.onMessage=[&](const auto& message){probe.Message(message);};
    options.readyScript=LR"JS((()=>{
      let mode=null,step=0;
      const act=()=>{
        if(step===0){
          const enabled=document.querySelector('.Card [data-action="minimize-toggle"]');
          if(!enabled){document.querySelector('[data-action="back"]')?.click();return;}
          if(enabled.disabled||enabled.getAttribute('aria-disabled')==='true')return;
          mode=enabled.getAttribute('aria-checked')==='true'?'disable':'enable';
          step=1;enabled.click();return;
        }
        if(step===1){
          const enabled=document.querySelector('.Card [data-action="minimize-toggle"]');
          if(!enabled||enabled.disabled||enabled.getAttribute('aria-disabled')==='true'||
             enabled.getAttribute('aria-checked')!==(mode==='enable'?'true':'false'))return;
          step=2;document.querySelector('[data-open="minimize"]').click();return;
        }
        if(mode==='disable')return;
        const duration=document.querySelector('#minimize-durationMs');
        if(step===2&&duration&&!duration.disabled){
          step++;duration.value='600';duration.dispatchEvent(new Event('change',{bubbles:true}));return;
        }
        const engine=document.querySelector('#minimize-engine');
        if(step===3&&engine&&!engine.disabled){
          step++;engine.click();
          document.querySelector('[data-select-owner="minimize-engine"][data-select-value="classic"]').click();return;
        }
        const restore=document.querySelector('[data-action="minimize-restore"]');
        if(step===4&&restore&&!restore.disabled){step++;restore.click();}
      };
      const observer=new MutationObserver(act);
      observer.observe(document.querySelector('#content'),{subtree:true,childList:true,attributes:true});
      act();window.addEventListener('beforeunload',()=>observer.disconnect(),{once:true});
    })();)JS";
    shell::DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));
    probe.app=&app;probe.deadline=GetTickCount64()+45000;active=&probe;
    app.Open(shell::ManagerPage::MinimizeAnimation);
    const auto timer=SetTimer(nullptr,0,40,Observe);Check(timer!=0,"animation integration deadline timer");
    const int result=app.Run();KillTimer(nullptr,timer);active=nullptr;
    Check(result==0&&probe.stage==3&&!app.ManagerHandle(),"animation actual manager close and reopen cycle");
    platform::windows::LocalSettingsFileStore files;minimize_animation::PreferencesStore store(files);
    const auto saved=store.Load((root/"minimize.json").u8string());
    Check(saved.state==minimize_animation::LoadState::Loaded&&!saved.value.enabled&&
          saved.value.durationMs==600&&saved.value.engine==minimize_animation::Engine::Classic&&
          !saved.value.restore,"acknowledged animation preferences persisted");
    return passed?0:1;
}
