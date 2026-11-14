// Real settings DOM -> WebView bridge -> DesktopApplication -> DockSession ->
// isolated preferences, including manager destruction/reopening. No user file
// is modified and no pinned fixture application is launched.
#include "apps/shell/desktop_application.h"
#include "platform/windows/dock/dock_sources.h"
#include "platform/windows/lume_window_ids.h"
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
using namespace lume;
bool passed=true;
void Check(bool ok,const char* why){if(!ok){passed=false;std::cerr<<"FAIL: "<<why<<'\n';}}
HWND DockWindow(){
    HWND found=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL{
        wchar_t name[128]{};GetClassNameW(hwnd,name,128);
        if(std::wstring(name)==window_ids::DockIconsClass)*reinterpret_cast<HWND*>(data)=hwnd;
        return TRUE;
    },reinterpret_cast<LPARAM>(&found));
    return found;
}
nlohmann::json Read(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary);
    if(!input)return {};
    return nlohmann::json::parse(input,nullptr,false);
}
struct Probe {
    shell::DesktopApplication* app=nullptr;
    std::filesystem::path preferences;
    std::vector<std::string> pinned;
    unsigned phase=0,snapshots=0,savedOff=0,savedOn=0;
    bool initialDefault=false,reopenedDisabled=false;
    bool enabled=false,showRunning=true,controlCenter=false;
    std::size_t visibleItems=0,pinnedCount=0;
    ULONGLONG deadline=0,closedAt=0;
    HWND dock=nullptr;
    void Message(const std::wstring& message){
        if(message.find(L"\"type\":\"dock-state\"")==std::wstring::npos)return;
        const auto state=nlohmann::json::parse(message);
        Check(state.contains("showRunning")&&state["showRunning"].is_boolean(),"host publishes a typed showRunning state");
        enabled=state.value("enabled",false);showRunning=state.value("showRunning",true);
        controlCenter=state.value("systemTray",false);
        visibleItems=state.value("visibleItems",std::size_t{0});pinnedCount=state.value("pinned",std::size_t{0});
        ++snapshots;
        if(phase==0&&!state.value("saved",false)&&showRunning)initialDefault=true;
        if(phase==2&&!state.value("saved",false)&&!showRunning)reopenedDisabled=true;
        if(state.value("saved",false)){
            if(!showRunning)++savedOff;
            if(phase==2&&showRunning)++savedOn;
        }
    }
    void CheckStored(bool expected){
        const auto stored=Read(preferences);
        Check(stored.is_object()&&stored.contains("showRunning")&&stored.value("showRunning",!expected)==expected,
              "real WebView toggle writes the selected running-area value to disk");
        if(!stored.is_object())return;
        Check(stored.value("enabled",false)&&stored.value("controlCenter",false)&&stored.value("lumeVisible",false)&&
              stored.value("pinned",std::vector<std::string>{})==pinned,
              "running-area setting preserves enabled Dock, control center and fixed application order");
    }
    void Tick(){
        if(GetTickCount64()>deadline){
            std::cerr<<"dock settings timeout phase="<<phase<<" snapshots="<<snapshots<<" savedOff="<<savedOff
                <<" savedOn="<<savedOn<<" manager="<<app->ManagerHandle()<<" showRunning="<<showRunning<<'\n';
            Check(false,"real Dock settings lifecycle finishes before deadline");app->Exit();return;
        }
        if(phase==0&&app->ManagerReady()&&savedOff){
            Check(initialDefault,"legacy preferences reach WebView with running area enabled by default");
            Check(enabled&&!showRunning&&controlCenter&&pinnedCount==pinned.size(),
                  "real settings click updates native state while retaining fixed controls");
            // Two pins, fixed Lume, launchpad, control center and its separator;
            // running apps, duplicate running Lume and their separator disappear.
            Check(visibleItems==pinned.size()+4,"native Dock hides its running section and retains the control-center separator");
            dock=DockWindow();Check(dock&&IsWindow(dock),"Dock window remains alive with running area hidden");
            CheckStored(false);
            phase=1;app->CloseManager();closedAt=GetTickCount64();
        }else if(phase==1&&!app->ManagerHandle()&&GetTickCount64()-closedAt>1100){
            Check(dock&&IsWindow(dock),"closing settings preserves the running Dock session");
            phase=2;app->Open(shell::ManagerPage::Dock);
        }else if(phase==2&&app->ManagerReady()&&savedOn){
            Check(reopenedDisabled,"reopened real WebView receives the persisted disabled setting before the next click");
            Check(showRunning&&enabled&&controlCenter&&pinnedCount==pinned.size(),
                  "reopened settings switch restores the native running area");
            Check(visibleItems>pinned.size()+4,"manager's running Lume entry returns when running area is enabled");
            Check(DockWindow()==dock,"settings reopen and toggle reuse the same native Dock window");
            CheckStored(true);
            Check(!app->ClockRunning()&&!app->CalendarRunning(),"Dock settings do not enable unrelated widgets");
            phase=3;app->Exit();
        }
    }
};
Probe* current=nullptr;
void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD){if(current)current->Tick();}
}

int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto root=std::filesystem::path(executable).parent_path()/L"verification"/L"desktop-dock-settings"/
        std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    Probe probe;probe.preferences=root/L"dock.json";
    probe.pinned={"C:\\LumeDockSettingsFixture\\first.exe","C:\\LumeDockSettingsFixture\\second.exe"};
    dock::Preferences seed;seed.enabled=seed.transparent=true;seed.pinned=probe.pinned;
    seed.controlCenter=seed.lumeVisible=true;
    std::string encoded;
    Check(dock::Encode(seed,encoded),"encode isolated Dock preferences");
    auto legacy=nlohmann::json::parse(encoded);legacy.erase("showRunning");
    {std::ofstream output(probe.preferences,std::ios::binary);output<<legacy.dump();}
    shell::DesktopApplicationOptions options;options.verification=true;
    options.clockPreferencesPath=(root/L"clock.json").u8string();
    options.calendarPreferencesPath=(root/L"calendar.json").u8string();
    options.performancePreferencesPath=(root/L"performance.json").u8string();
    options.spectrumPreferencesPath=(root/L"spectrum.json").u8string();
    options.almanacPreferencesPath=(root/L"almanac.json").u8string();
    options.weatherPreferencesPath=(root/L"weather.json").u8string();
    options.todayTodoPreferencesPath=(root/L"today-todo.json").u8string();
    options.dynamicIslandPreferencesPath=(root/L"island.json").u8string();
    options.organizerPreferencesPath=(root/L"organizer.json").u8string();
    options.desktopIconsPreferencesPath=(root/L"desktop-icons.json").u8string();
    options.minimizePreferencesPath=(root/L"minimize.json").u8string();
    options.diagnosticsPath=(root/L"diagnostics.json").u8string();
    options.applicationSettings.preferencesPath=(root/L"application-settings.json").u8string();
    options.wallpaperOptions.root=(root/L"wallpaper").u8string();
    options.accountOptions.storageDirectory=(root/L"account").u8string();
    options.radial.preferencesPath=(root/L"radial.json").u8string();
    options.radialVisible=options.notesVisible=options.minimizeVisible=false;
    options.dock.preferencesPath=probe.preferences.u8string();
    options.onMessage=[&](const auto& text){probe.Message(text);};
    options.readyScript=LR"JS((()=>{
      let clicked=false;
      const act=()=>{
        if(clicked)return;
        const section=document.querySelector('[data-settings-scope="dock"]');
        if(!section)return;
        const settings=section.querySelector('[data-tab="settings"]');
        if(settings&&settings.getAttribute('aria-selected')!=='true'){settings.click();return;}
        const toggle=section.querySelector('[data-action="dock-show-running"]');
        if(!toggle||toggle.disabled||toggle.getAttribute('aria-disabled')==='true')return;
        // First visit changes the native default true to false; the recreated
        // WebView must read persisted false and changes it back to true.
        clicked=true;toggle.click();
      };
      const observer=new MutationObserver(act);
      observer.observe(document.querySelector('#content'),{subtree:true,attributes:true,childList:true});
      act();
      window.addEventListener('beforeunload',()=>observer.disconnect(),{once:true});
    })();)JS";
    shell::DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));
    probe.app=&app;probe.deadline=GetTickCount64()+35000;current=&probe;
    app.Open(shell::ManagerPage::Dock);
    const auto timer=SetTimer(nullptr,0,40,Tick);Check(timer!=0,"create Dock lifecycle driver");
    if(!timer)return 1;
    const auto result=app.Run();KillTimer(nullptr,timer);current=nullptr;
    Check(result==0&&probe.phase==3&&probe.savedOff==1&&probe.savedOn==1,
          "two real settings visits each send one successful running-area change");
    Check(!DockWindow()&&!app.ManagerHandle(),"application exit releases Dock and manager windows");
    std::cout<<(passed?"PASS":"FAIL")<<" real Dock settings bridge, persistence, fixed items and manager reopen\n";
    return passed?0:1;
}
