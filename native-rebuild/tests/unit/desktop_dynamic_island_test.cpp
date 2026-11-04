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
std::size_t IslandCount(){std::size_t count=0;EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL{
    wchar_t name[80]{};GetClassNameW(hwnd,name,80);if(std::wstring(name)==L"LumeWindowsDynamicIsland")++*reinterpret_cast<std::size_t*>(data);return TRUE;
},reinterpret_cast<LPARAM>(&count));return count;}
struct Probe {
    shell::DesktopApplication* app=nullptr;
    bool enabled=false,locked=false,previewDecoded=false;
    std::string material;
    std::string shape;
    HWND island=nullptr;
    unsigned stage=0,ready=0;
    unsigned snapshots=0,snapshotAtOpen=0;
    unsigned previews=0;
    unsigned allScreenChecks=0,singleScreenChecks=0;
    unsigned expansionRound=0;
    bool clickModeConfirmed=false;
    bool previewSettled=false;
    bool animationConfirmed=false;
    ULONGLONG deadline=0,closed=0;
    void Message(const std::wstring& text){
        if(text.find(L"\"type\":\"dynamicIsland-preview\"")!=std::wstring::npos){
            const auto image=nlohmann::json::parse(text);
            Check(image.value("image",std::string{}).rfind("data:image/jpeg;base64,",0)==0,"real manager receives native preview pixels");
            previewSettled=!image.value("animating",false)&&image.value("shape",std::string{})=="standard"&&
                image.value("material",std::string{})=="frosted-glass"&&image.value("animationSpeed",std::string{})=="fast"&&image.value("bounce",std::string{})=="bouncy";
            ++previews;return;
        }
        if(text.find(L"\"type\":\"dynamicIsland-state\"")==std::wstring::npos)return;
        const auto state=nlohmann::json::parse(text);enabled=state.value("enabled",false);
        locked=state.value("locked",false);material=state.value("material",std::string{});
        shape=state.value("shape",std::string{});
        animationConfirmed=state.value("hoverDelayMs",0)==1000&&state.value("animationSpeed",std::string{})=="fast"&&state.value("bounce",std::string{})=="bouncy";
        previewDecoded=state.value("hover",false)&&state.value("topmost",false)&&state.value("hideFullscreen",false);
        ++snapshots;
        if(state.value("saved",false)&&state.contains("screens")){
            const int selected=state.value("screen",1);
            Check(IslandCount()==(selected==0?state["screens"].size():1),"real display policy applies the requested native window count");
            if(selected==0)++allScreenChecks;
            else{
                ++singleScreenChecks;
                const auto monitors=platform::windows::ConnectedIslandMonitors();RECT rect{};
                Check(selected<=static_cast<int>(monitors.size())&&GetWindowRect(Island(),&rect)&&
                    std::abs((rect.left+rect.right)-(monitors[static_cast<std::size_t>(selected-1)].work.left+monitors[static_cast<std::size_t>(selected-1)].work.right))<=2,
                    "real screen selection centers the native island on that monitor");
                if(allScreenChecks>ready){
                    if(!state.value("hover",false))clickModeConfirmed=true;
                    else if(clickModeConfirmed){expansionRound=ready+1;clickModeConfirmed=false;}
                }
            }
        }
        Check(!state.contains("customization"),"retired customization absent from native state");
    }
    void Tick(){
        if(GetTickCount64()>deadline){
            std::cerr<<"state: enabled="<<enabled<<" material="<<material<<'\n';
            Check(false,"real manager lifecycle deadline");app->Exit();return;
        }
        if(stage==0&&app->ManagerReady()&&enabled&&locked&&material=="frosted-glass"&&shape=="standard"&&snapshots>snapshotAtOpen&&previews>ready&&previewDecoded&&previewSettled&&animationConfirmed&&expansionRound>ready){
            ++ready;
            Check(Island()!=nullptr,"real settings page creates island window");
            Check((GetWindowLongPtrW(Island(),GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,"real display-layer dropdown applies native overlay state");
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
      let material=false,shape=false,layer=false,hideOff=false,hideOn=false,allScreens=false,singleScreen=false,clickMode=false,hoverMode=false;
      const act=()=>{
        const section=document.querySelector('[data-settings-scope="dynamicIsland"]');
        if(!section)return;
        if(section.querySelector('[data-action="dynamicIsland-customIcons"],[data-action="dynamicIsland-overview"],[data-island-card]'))return;
        if(!shape){
          const choice=section.querySelector('button[data-island-shape="standard"]');
          if(!choice||choice.disabled||choice.getAttribute('aria-disabled')==='true')return;
          shape=true;choice.click();return;
        }
        if(!material){
          const choice=section.querySelector('[data-island-preference="material"][data-value="frosted-glass"]');
          if(!choice||choice.disabled||choice.getAttribute('aria-disabled')==='true')return;
          material=true;choice.click();return;
        }
        const all=section.querySelector('[data-island-screen="0"]');
        if(all&&!allScreens){
          if(all.disabled||all.getAttribute('aria-disabled')==='true')return;
          if(all.getAttribute('aria-pressed')!=='true'){all.click();return;}
          allScreens=true;
        }
        if(allScreens&&!singleScreen){
          const screens=section.querySelectorAll('[data-island-screen]:not([data-island-screen="0"])');
          const selected=screens[screens.length-1];
          if(!selected||selected.disabled||selected.getAttribute('aria-disabled')==='true')return;
          if(selected.getAttribute('aria-pressed')!=='true'){selected.click();return;}
          singleScreen=true;
        }
        const click=section.querySelector('[data-island-expansion="click"]');
        if(click&&!clickMode){
          if(click.disabled||click.getAttribute('aria-disabled')==='true')return;
          if(click.getAttribute('aria-pressed')!=='true'){click.click();return;}
          clickMode=true;
        }
        if(clickMode&&!hoverMode){
          const hover=section.querySelector('[data-island-expansion="hover"]');
          if(!hover||hover.disabled||hover.getAttribute('aria-disabled')==='true')return;
          if(hover.getAttribute('aria-pressed')!=='true'){hover.click();return;}
          hoverMode=true;
        }
        const delay=section.querySelector('[data-island-hover-delay]');
        if(delay){
          if(delay.disabled||delay.getAttribute('aria-disabled')==='true')return;
          if(delay.value!=='1000'){
            delay.value='1000';delay.dispatchEvent(new Event('input',{bubbles:true}));delay.dispatchEvent(new Event('change',{bubbles:true}));return;
          }
        }
        for(const [key,value] of [['animationSpeed','fast'],['bounce','bouncy']]){
          const choice=section.querySelector(`[data-island-preference="${key}"][data-value="${value}"]`);
          if(choice){
            if(choice.disabled||choice.getAttribute('aria-disabled')==='true')return;
            if(choice.getAttribute('aria-pressed')!=='true'){choice.click();return;}
          }
        }
        const preview=section.querySelector('[data-island-native-preview]');
        if(material&&shape&&preview&&!preview.hidden&&preview.complete&&preview.naturalWidth===960&&!layer){
          section.querySelector('[data-tab="settings"]').click();return;
        }
        const displayLayer=section.querySelector('#dynamicIsland-layer');
        if(displayLayer&&!layer){
          if(displayLayer.disabled||displayLayer.getAttribute('aria-disabled')==='true')return;
          if(!displayLayer.textContent.includes('应用上方')){
            displayLayer.click();section.querySelector('[data-select-owner="dynamicIsland-layer"][data-select-value="above"]').click();return;
          }
          layer=true;
        }
        const fullscreen=section.querySelector('[data-action="dynamicIsland-hideFullscreen"]');
        if(layer&&fullscreen){
          if(fullscreen.disabled||fullscreen.getAttribute('aria-disabled')==='true')return;
          if(!hideOff){hideOff=true;fullscreen.click();return;}
          if(fullscreen.getAttribute('aria-checked')==='false'&&!hideOn){hideOn=true;fullscreen.click();return;}
          if(hideOn&&fullscreen.getAttribute('aria-checked')==='true'){section.querySelector('[data-tab="home"]').click();return;}
        }
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
    Check(probe.allScreenChecks>=2&&probe.singleScreenChecks>=2,"real display policy buttons work before and after reopening the manager");
    Check(probe.expansionRound==2,"real click and hover mode buttons finish before each manager close");
    Check(probe.animationConfirmed,"real animation controls persist through the manager close and reopen");
    Check(!Island()&&!app.ManagerHandle(),"application releases all island windows");
    std::cout<<(passed?"PASS":"FAIL")<<" island manager protocol, real UI controls, close/reopen and message-loop lifetime\n";
    return passed?0:1;
}
