#include "apps/shell/desktop_application.h"
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
namespace {
using namespace lume;
bool passed=true;
void Check(bool ok,const char* why){if(!ok){passed=false;std::cerr<<"FAIL: "<<why<<'\n';}}
HWND Island(){HWND found=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p)->BOOL{
    wchar_t name[80]{};GetClassNameW(h,name,80);if(std::wstring(name)==L"LumeWindowsDynamicIsland")*reinterpret_cast<HWND*>(p)=h;return TRUE;
},reinterpret_cast<LPARAM>(&found));return found;}
struct Probe {
    shell::DesktopApplication* app=nullptr;
    bool enabled=false,locked=false;
    bool customized=false;
    std::string material;
    HWND island=nullptr;
    unsigned stage=0,ready=0;
    unsigned snapshots=0,snapshotAtOpen=0;
    ULONGLONG deadline=0,closed=0;
    void Message(const std::wstring& text){
        if(text.find(L"\"type\":\"dynamicIsland-state\"")==std::wstring::npos)return;
        const auto state=nlohmann::json::parse(text);enabled=state.value("enabled",false);
        locked=state.value("locked",false);material=state.value("material",std::string{});
        ++snapshots;
        if(const auto it=state.find("customization");it!=state.end()&&it->is_object()){
            const auto& c=*it;
            customized=state.value("error",std::string{}).empty()&&c["customIcons"]==true&&c["overview"]==true&&
                c["left"]["widget"]=="clock"&&c["right"]["widget"]=="cpu"&&c["right"]["style"]=="capsule"&&
                c["enabled"][6]==true&&c["order"][0]=="focus";
        }
    }
    void Tick(){
        if(GetTickCount64()>deadline){
            std::cerr<<"state: enabled="<<enabled<<" material="<<material<<'\n';
            Check(false,"real manager lifecycle deadline");app->Exit();return;
        }
        if(stage==0&&app->ManagerReady()&&enabled&&locked&&material=="frosted-glass"&&customized&&snapshots>snapshotAtOpen){
            ++ready;
            Check(Island()!=nullptr,"real settings page creates island window");
            if(ready==1)island=Island();else Check(Island()==island,"reopen retains same native session");
            Check(!app->ClockRunning()&&!app->CalendarRunning(),"island does not enable unrelated components");
            stage=1;app->CloseManager();closed=GetTickCount64();
        }else if(stage==1&&!app->ManagerHandle()&&GetTickCount64()-closed>1100){
            Check(IsWindow(island),"island alone retains background message loop");
            if(ready==1){stage=0;snapshotAtOpen=snapshots;app->Open(shell::ManagerPage::DynamicIsland);}
            else{stage=2;SendMessageW(island,WM_CLOSE,0,0);Check(!IsWindow(island),"native close releases island");app->Exit();}
        }
    }
};
Probe* current=nullptr;
void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD){if(current)current->Tick();}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto root=std::filesystem::path(exe).parent_path()/L"verification"/L"desktop-dynamic-island"/std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    shell::DesktopApplicationOptions options;options.verification=true;
    options.clockPreferencesPath=(root/L"clock.json").u8string();options.calendarPreferencesPath=(root/L"calendar.json").u8string();
    options.dynamicIslandPreferencesPath=(root/L"island.json").u8string();
    {
        std::ofstream seed(options.dynamicIslandPreferencesPath,std::ios::binary);
        seed<<R"JSON({"version":1,"enabled":true,"locked":true,"material":"frosted-glass","placed":false,"x":0,"y":0,"width":320,"height":152,"panelColor":2894892})JSON";
    }
    Probe probe;options.onMessage=[&](const auto& text){probe.Message(text);};
    options.readyScript=LR"JS((()=>{
      let material=false,dragged=false;
      const act=()=>{
        const section=document.querySelector('[data-settings-scope="dynamicIsland"]');
        if(!section)return;
        if(!material){
          const select=section.querySelector('#dynamicIsland-material');
          if(!select||select.disabled||select.getAttribute('aria-disabled')==='true')return;
          const option=select.nextElementSibling?.querySelector('[data-select-value="frosted-glass"]');
          if(!option)return;
          material=true;select.click();option.click();return;
        }
        const ready=button=>button&&!button.disabled&&button.getAttribute('aria-disabled')!=='true';
        const switchOn=selector=>{
          const button=section.querySelector(selector);
          if(!ready(button))return false;
          if(button.getAttribute('aria-checked')!=='true'){button.click();return false;}
          return true;
        };
        if(!switchOn('[data-action="dynamicIsland-customIcons"]'))return;
        for(const selector of [
          '[data-action="dynamicIsland-leftWidget"][data-value="4"]',
          '[data-action="dynamicIsland-rightWidget"][data-value="10"]',
          '[data-action="dynamicIsland-rightStyle"][data-value="capsule"]'
        ]){
          const button=section.querySelector(selector);if(!ready(button))return;
          if(button.getAttribute('aria-pressed')!=='true'){button.click();return;}
        }
        if(!switchOn('[data-island-card="focus"] [role="switch"]'))return;
        const focus=section.querySelector('[data-island-card="focus"]');
        const first=focus?.parentElement.firstElementChild;
        if(first!==focus){
          if(dragged||!focus?.draggable)return;
          dragged=true;const transfer=new DataTransfer();
          focus.dispatchEvent(new DragEvent('dragstart',{bubbles:true,dataTransfer:transfer}));
          first.dispatchEvent(new DragEvent('drop',{bubbles:true,dataTransfer:transfer}));
          focus.dispatchEvent(new DragEvent('dragend',{bubbles:true,dataTransfer:transfer}));return;
        }
        if(!switchOn('[data-action="dynamicIsland-overview"]'))return;
      };
      const observer=new MutationObserver(act);observer.observe(document.querySelector('#content'),{subtree:true,attributes:true,childList:true});act();
      window.addEventListener('beforeunload',()=>observer.disconnect(),{once:true});
    })();)JS";
    shell::DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));
    probe.app=&app;probe.deadline=GetTickCount64()+35000;current=&probe;
    app.Open(shell::ManagerPage::DynamicIsland);
    const auto timer=SetTimer(nullptr,0,40,Tick);Check(timer!=0,"create lifecycle driver");
    const auto result=app.Run();KillTimer(nullptr,timer);current=nullptr;
    Check(result==0&&probe.ready==2&&probe.stage==2,"complete real manager reopen and native close");
    Check(!Island()&&!app.ManagerHandle(),"application releases all island windows");
    std::cout<<(passed?"PASS":"FAIL")<<" island manager protocol, real UI controls, close/reopen and message-loop lifetime\n";
    return passed?0:1;
}
