#include "apps/shell/desktop_application.h"
#include "core/taskbar/taskbar_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "vendor/nlohmann/json.hpp"
#include <atomic>
#include <filesystem>
#include <iostream>

namespace {
using namespace lume;
using namespace platform::windows;
bool passed=true;
void Check(bool value,const char* reason) {
    if(!value){passed=false;std::cerr<<"FAIL: "<<reason<<'\n';}
}
// The real WebView, controller, store and worker run. This explicit private
// system port prevents a page-protocol test from modifying Explorer.
struct Fixture {std::atomic_bool hidden{false},core{false},fullyHidden{false},observedFullHide{false};std::atomic_uint acquired{0},released{0};};
class Port final:public taskbar::ITaskbarPort {
public:
    explicit Port(Fixture& fixture):fixture(fixture){}
    taskbar::Error Acquire(bool,taskbar::Mode& mode)override {
        ++fixture.acquired;mode=taskbar::Mode::Windows11X64;return taskbar::Error::None;
    }
    void Release()override{++fixture.released;}
    taskbar::Error ReadAutoHide(bool& value)override{value=fixture.hidden;return taskbar::Error::None;}
    taskbar::Error SetAutoHide(bool value)override{fixture.hidden=value;return taskbar::Error::None;}
    taskbar::Error ReadVisibility(std::vector<taskbar::Visibility>& out)override{
        taskbar::Visibility value;value.window=10;value.process=20;value.processCreated=30;
        out={value};return taskbar::Error::None;
    }
    taskbar::Error Hide(const taskbar::Visibility&)override{fixture.fullyHidden=true;fixture.observedFullHide=true;return taskbar::Error::None;}
    taskbar::RestoreResult RestoreVisibility(const taskbar::Visibility&)override{fixture.fullyHidden=false;return taskbar::RestoreResult::Restored;}
    taskbar::Error ReadClassic(std::vector<taskbar::Appearance>&)override{return taskbar::Error::Unsupported;}
    taskbar::Error MakeTransparent(const taskbar::Appearance&)override{return taskbar::Error::Unsupported;}
    taskbar::RestoreResult RestoreAppearance(const taskbar::Appearance&)override{return taskbar::RestoreResult::Failed;}
    taskbar::Error StartCore()override{fixture.core=true;return taskbar::Error::None;}
    bool CoreAlive()const override{return fixture.core;}
    taskbar::Error StopCore(bool)override{fixture.core=false;return taskbar::Error::None;}
private:
    Fixture& fixture;
};
struct Probe {
    shell::DesktopApplication* app=nullptr;
    Fixture* fixture=nullptr;
    nlohmann::json latest;
    int stage=0;
    ULONGLONG deadline=0,closedAt=0;
    bool observedApplying=false,observedActive=false;
    void Message(const std::wstring& text) {
        const auto message=nlohmann::json::parse(text.begin(),text.end(),nullptr,false);
        if(message.is_discarded()||message.value("type","")!="taskbar-state")return;
        latest=message;
        observedApplying=observedApplying||message.value("applying",false);
        observedActive=observedActive||(message.value("running",false)&&message.value("transparentTaskbar",false));
    }
    void Tick() {
        if(GetTickCount64()>deadline) {
            std::cerr<<"stage="<<stage<<" state="<<latest.dump()<<'\n';
            Check(false,"taskbar WebView integration deadline");app->Exit();return;
        }
        if(!latest.is_object())return;
        const bool settled=!latest.value("applying",true)&&latest.value("error","unknown").empty();
        if(stage==0&&app->ManagerReady()&&settled&&latest.value("enabled",false)&&
           latest.value("running",false)&&!latest.value("transparentTaskbar",true)&&!latest.value("autoHideTaskbar",true)) {
            Check(observedApplying&&observedActive,"page sees asynchronous application and running acknowledgement");
            Check(!fixture->core&&!fixture->hidden&&fixture->observedFullHide&&!fixture->fullyHidden,
                  "dropdown full hiding, return to auto-hide and switch off reached native port");
            Check(!app->ClockRunning()&&!app->CalendarRunning(),"system feature has no unrelated widget side effects");
            stage=1;closedAt=GetTickCount64();app->CloseManager();
        } else if(stage==1&&!app->ManagerHandle()&&GetTickCount64()-closedAt>800) {
            Check(fixture->acquired>fixture->released,"taskbar ownership survives manager destruction");
            stage=2;app->Open(shell::ManagerPage::Taskbar);
        } else if(stage==2&&app->ManagerReady()&&settled&&!latest.value("enabled",true)&&
                  !latest.value("running",true)&&!latest.value("pendingRecovery",true)) {
            Check(!fixture->core&&!fixture->hidden&&fixture->acquired==fixture->released,
                  "reopened page disable releases independent system ownership");
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
    const auto root=std::filesystem::path(executable).parent_path()/L"desktop-taskbar-artifacts"/
        (std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root);
    Fixture fixture;Probe probe;probe.fixture=&fixture;
    shell::DesktopApplicationOptions options;
    options.clockPreferencesPath=(root/"clock.json").u8string();
    options.calendarPreferencesPath=(root/"calendar.json").u8string();
    options.taskbarPaths.stateFile=(root/"taskbar.json").u8string();
    options.verification=true;
    options.verificationTaskbar=[&](const auto&,const auto&){return std::make_unique<Port>(fixture);};
    options.onMessage=[&](const auto& message){probe.Message(message);};
    options.readyScript=LR"JS((()=>{
      let mode=null,step=0;
      const act=()=>{
        if(step===0){
          const enabled=document.querySelector('.Card [data-action="taskbar-toggle"]');
          if(!enabled){document.querySelector('[data-action="back"]')?.click();return;}
          if(enabled.disabled||enabled.getAttribute('aria-disabled')==='true')return;
          mode=enabled.getAttribute('aria-checked')==='true'?'disable':'enable';
          step=1;enabled.click();return;
        }
        if(step===1){
          const enabled=document.querySelector('.Card [data-action="taskbar-toggle"]');
          if(!enabled||enabled.disabled||enabled.getAttribute('aria-disabled')==='true'||
             enabled.getAttribute('aria-checked')!==(mode==='enable'?'true':'false'))return;
          step=2;document.querySelector('[data-open="taskbar"]').click();return;
        }
        if(mode==='disable')return;
        const transparency=document.querySelector('[data-action="taskbar-transparent"]');
        const autoHide=document.querySelector('[data-action="taskbar-autoHide"]');
        if(step===2&&autoHide&&!autoHide.disabled&&autoHide.getAttribute('aria-disabled')!=='true'){step++;autoHide.click();return;}
        if(step===3&&transparency&&!transparency.disabled){step++;transparency.click();return;}
        const behavior=document.querySelector('#taskbar-hide-behavior');
        if((step===4||step===5)&&behavior&&!behavior.disabled&&behavior.getAttribute('aria-disabled')!=='true'){
          const value=step===4?'fully-hidden':'auto-hide';step++;
          behavior.click();
          document.querySelector(`[data-select-owner="taskbar-hide-behavior"][data-select-value="${value}"]`).click();return;
        }
        if(step===6&&autoHide&&!autoHide.disabled&&autoHide.getAttribute('aria-disabled')!=='true'){step++;autoHide.click();}
      };
      const observer=new MutationObserver(act);
      observer.observe(document.querySelector('#content'),{subtree:true,childList:true,attributes:true});
      act();window.addEventListener('beforeunload',()=>observer.disconnect(),{once:true});
    })();)JS";
    shell::DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));
    probe.app=&app;probe.deadline=GetTickCount64()+40000;active=&probe;
    app.Open(shell::ManagerPage::Taskbar);
    const auto timer=SetTimer(nullptr,0,40,Observe);Check(timer!=0,"taskbar integration deadline timer");
    const int result=app.Run();KillTimer(nullptr,timer);active=nullptr;
    Check(result==0&&probe.stage==3&&!app.ManagerHandle(),"taskbar actual manager close and reopen cycle");
    LocalSettingsFileStore files;taskbar::RecordStore store(files);
    const auto saved=store.Load((root/"taskbar.json").u8string());
    Check(saved.state==taskbar::LoadState::Loaded&&!saved.record.preferences.enabled&&
          !saved.record.preferences.transparentTaskbar&&!saved.record.preferences.autoHideTaskbar&&
          !taskbar::HasRecovery(saved.record),"acknowledged settings and completed recovery persisted");
    Check(!fixture.hidden&&!fixture.core&&fixture.acquired==fixture.released,"private system port restored");
    return passed?0:1;
}
