#include "apps/shell/desktop_application.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include <filesystem>
#include <iostream>

namespace {
using namespace lume;
bool passed=true;
void Check(bool ok,const char* reason){if(!ok){passed=false;std::cerr<<"FAIL: "<<reason<<'\n';}}
HWND AlmanacWindow(){
    HWND result=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL{
        wchar_t title[80]{};GetWindowTextW(hwnd,title,80);
        if(std::wstring(title)==L"Lume 黄历")*reinterpret_cast<HWND*>(data)=hwnd;
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
struct Probe {
    shell::DesktopApplication* app=nullptr;
    int stage=0,ready=0;
    bool sawState=false,sawEnabled=false,sawMaterial=false;
    ULONGLONG deadline=0,closedAt=0;
    HWND widget=nullptr;
    void Message(const std::wstring& message){
        if(message.find(L"\"type\":\"almanac-state\"")==std::wstring::npos)return;
        sawState=true;
        sawEnabled=message.find(L"\"enabled\":true")!=std::wstring::npos;
        sawMaterial=message.find(L"\"material\":\"liquid-glass\"")!=std::wstring::npos;
    }
    void Tick(){
        if(GetTickCount64()>deadline){
            std::cerr<<"almanac timeout stage="<<stage<<" ready="<<ready<<" manager="<<app->ManagerHandle()
                <<" widget="<<AlmanacWindow()<<" enabled="<<sawEnabled<<" material="<<sawMaterial<<'\n';
            Check(false,"almanac lifecycle deadline");app->Exit();return;
        }
        if(stage==0&&app->ManagerReady()){
            if(!sawEnabled||!sawMaterial)return;
            ++ready;Check(sawState,"ready publishes almanac state");
            if(ready==1){
                widget=AlmanacWindow();
                Check(sawEnabled&&sawMaterial&&widget,"real page toggle and material controls configure native window");
                if(!widget){app->Exit();stage=3;return;}
            }else Check(sawEnabled&&AlmanacWindow()==widget,"reopened page restores native session state");
            Check(!app->ClockRunning()&&!app->CalendarRunning(),"almanac does not start other widgets");
            stage=1;app->CloseManager();closedAt=GetTickCount64();
        }else if(stage==1&&!app->ManagerHandle()&&GetTickCount64()-closedAt>1500){
            Check(IsWindow(widget),"almanac alone retains background message loop");
            if(ready==1){sawState=false;sawEnabled=false;sawMaterial=false;stage=0;app->Open(shell::ManagerPage::Almanac);}
            else {stage=2;SendMessageW(widget,WM_CLOSE,0,0);Check(!IsWindow(widget),"native close releases window");}
        }
    }
};
Probe* probe=nullptr;
void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD){if(probe)probe->Tick();}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto root=std::filesystem::path(executable).parent_path()/L"desktop-almanac-artifacts"/std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    shell::DesktopApplicationOptions options;
    options.clockPreferencesPath=(root/L"clock.json").u8string();
    options.calendarPreferencesPath=(root/L"calendar.json").u8string();
    options.almanacPreferencesPath=(root/L"almanac.json").u8string();
    const auto almanacPath=options.almanacPreferencesPath;
    Probe state;options.verification=true;options.onMessage=[&](const auto& message){state.Message(message);};
    options.readyScript=LR"JS((()=>{
      let enabling=false,material=false,opened=false;
      const act=()=>{
        if(!opened){
          const toggle=document.querySelector('.Card [data-action="almanac-toggle"]');
          if(!toggle){document.querySelector('[data-action="back"]')?.click();return;}
          if(toggle.disabled)return;
          if(toggle.getAttribute('aria-checked')!=='true'){
            if(!enabling){enabling=true;toggle.click();}return;
          }
          opened=true;document.querySelector('[data-open="almanac"]').click();return;
        }
        const select=document.querySelector('#almanac-material');
        if(!select||select.disabled||material||select.textContent.includes('液态玻璃'))return;
        material=true;select.click();
        document.querySelector('[data-select-owner="almanac-material"][data-select-value="liquid-glass"]').click();
      };
      const observer=new MutationObserver(act);
      observer.observe(document.querySelector('#content'),{subtree:true,childList:true,attributes:true});
      act();
      window.addEventListener('beforeunload',()=>observer.disconnect(),{once:true});
    })();)JS";
    shell::DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));
    app.Open(shell::ManagerPage::Almanac);
    state.app=&app;state.deadline=GetTickCount64()+40000;probe=&state;
    const auto timer=SetTimer(nullptr,0,40,Tick);Check(timer!=0,"create lifecycle timer");
    const int result=app.Run();KillTimer(nullptr,timer);probe=nullptr;
    Check(result==0,"application clean exit");Check(state.ready==2&&state.stage==2,"complete two manager lifecycles");
    Check(!AlmanacWindow()&&!app.ManagerHandle(),"all windows released");
    platform::windows::LocalSettingsFileStore files;
    widget::PreferencesStore store(files,{320,152,320,152,720,720});
    const auto saved=store.Load(almanacPath);
    Check(saved.state==widget::LoadState::Loaded&&!saved.value.enabled&&!saved.value.frosted,"native close persisted disabled and page selected material");
    return passed?0:1;
}
