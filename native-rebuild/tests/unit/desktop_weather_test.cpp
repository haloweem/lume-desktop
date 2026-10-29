#include "apps/shell/desktop_application.h"
#include "core/weather/weather_preferences.h"
#include "platform/windows/local_settings_file_store.h"
#include <filesystem>
#include <iostream>

namespace {
using namespace lume;
bool passed=true;
void Check(bool ok,const char* reason){if(!ok){passed=false;std::cerr<<"FAIL: "<<reason<<'\n';}}
HWND WeatherWindow(){
    HWND result=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL{
        wchar_t title[80]{};GetWindowTextW(hwnd,title,80);
        if(std::wstring(title)==L"Lume 天气")*reinterpret_cast<HWND*>(data)=hwnd;
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
struct Probe {
    shell::DesktopApplication* app=nullptr;
    int stage=0,ready=0;
    bool sawState=false,sawEnabled=false,sawMaterial=false,sawCity=false;
    ULONGLONG deadline=0,closedAt=0;
    HWND widget=nullptr;
    void Message(const std::wstring& message){
        if(message.find(L"\"type\":\"weather-state\"")==std::wstring::npos)return;
        sawState=true;
        sawEnabled=message.find(L"\"enabled\":true")!=std::wstring::npos;
        sawMaterial=message.find(L"\"material\":\"liquid-glass\"")!=std::wstring::npos;
        sawCity=message.find(L"\"city\":\"shanghai\"")!=std::wstring::npos;
    }
    void Tick(){
        if(GetTickCount64()>deadline){Check(false,"weather lifecycle deadline");app->Exit();return;}
        if(stage==0&&app->ManagerReady()){
            if(!sawEnabled||!sawMaterial||!sawCity)return;
            ++ready;Check(sawState,"ready publishes weather state");
            if(ready==1){
                widget=WeatherWindow();
                Check(widget!=nullptr,"real page toggle, material and city configure native weather window");
                if(!widget){app->Exit();stage=3;return;}
            }else Check(WeatherWindow()==widget,"reopened page retains same weather session");
            Check(!app->ClockRunning()&&!app->CalendarRunning(),"weather does not start other widgets");
            stage=1;app->CloseManager();closedAt=GetTickCount64();
        }else if(stage==1&&!app->ManagerHandle()&&GetTickCount64()-closedAt>1500){
            Check(IsWindow(widget),"weather alone retains background message loop");
            if(ready==1){
                sawState=sawEnabled=sawMaterial=sawCity=false;stage=0;app->Open(shell::ManagerPage::Weather);
            }else{
                stage=2;SendMessageW(widget,WM_CLOSE,0,0);Check(!IsWindow(widget),"native close releases weather window");
            }
        }
    }
};
Probe* probe=nullptr;
void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD){if(probe)probe->Tick();}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto root=std::filesystem::path(executable).parent_path()/L"desktop-weather-artifacts"/std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    shell::DesktopApplicationOptions options;
    options.clockPreferencesPath=(root/L"clock.json").u8string();
    options.calendarPreferencesPath=(root/L"calendar.json").u8string();
    options.weatherPreferencesPath=(root/L"weather.json").u8string();
    const auto weatherPath=options.weatherPreferencesPath;
    // Fix city before enabling so this UI regression never requests IP geolocation.
    platform::windows::LocalSettingsFileStore files;weather::PreferencesStore store(files);
    weather::Preferences initial;initial.city="beijing";
    Check(store.Save(weatherPath,initial),"isolated weather settings");
    Probe state;options.verification=true;options.onMessage=[&](const auto& message){state.Message(message);};
    options.readyScript=LR"JS((()=>{
      let enabling=false,material=false,cityRequested=false,cityReady=false,opened=false;
      const act=()=>{
        if(!cityReady){
          const city=document.querySelector('#weather-city');
          if(!city||city.disabled)return;
          if(!city.textContent.includes('上海')){
            if(!cityRequested){
              cityRequested=true;city.click();
              document.querySelector('[data-select-owner="weather-city"][data-select-value="shanghai"]').click();
            }
            return;
          }
          cityReady=true;document.querySelector('[data-action="back"]').click();return;
        }
        if(!opened){
          const toggle=document.querySelector('.Card [data-action="weather-toggle"]');
          if(!toggle||toggle.disabled)return;
          if(toggle.getAttribute('aria-checked')!=='true'){
            if(!enabling){enabling=true;toggle.click();}return;
          }
          opened=true;document.querySelector('[data-open="weather"]').click();return;
        }
        const select=document.querySelector('#weather-material');
        if(!select||select.disabled||material||select.textContent.includes('液态玻璃'))return;
        material=true;select.click();
        document.querySelector('[data-select-owner="weather-material"][data-select-value="liquid-glass"]').click();
      };
      const observer=new MutationObserver(act);
      observer.observe(document.querySelector('#content'),{subtree:true,childList:true,attributes:true});
      act();
      window.addEventListener('beforeunload',()=>observer.disconnect(),{once:true});
    })();)JS";
    shell::DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));
    app.Open(shell::ManagerPage::Weather);
    state.app=&app;state.deadline=GetTickCount64()+40000;probe=&state;
    const auto timer=SetTimer(nullptr,0,40,Tick);Check(timer!=0,"create lifecycle timer");
    const int result=app.Run();KillTimer(nullptr,timer);probe=nullptr;
    Check(result==0,"application clean exit");Check(state.ready==2&&state.stage==2,"complete two manager lifecycles");
    Check(!WeatherWindow()&&!app.ManagerHandle(),"all windows released");
    const auto saved=store.Load(weatherPath);
    Check(saved.state==widget::LoadState::Loaded&&!saved.value.window.enabled&&!saved.value.window.frosted&&saved.value.city=="shanghai",
        "native close persisted disabled, material and city");
    return passed?0:1;
}
