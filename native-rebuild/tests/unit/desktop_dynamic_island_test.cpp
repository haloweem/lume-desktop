#include "apps/shell/desktop_application.h"
#include "core/dynamic_island/expanded_widgets.h"
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
namespace {
using namespace lume;
const unsigned ClockLayout=dynamic_island::EncodeExpandedLayout({dynamic_island::ExpandedWidget::Music,
    dynamic_island::ExpandedWidget::CalendarWeather,dynamic_island::ExpandedWidget::Todos,dynamic_island::ExpandedWidget::Clock});
const unsigned PerformanceLayout=dynamic_island::EncodeExpandedLayout({dynamic_island::ExpandedWidget::Music,
    dynamic_island::ExpandedWidget::CalendarWeather,dynamic_island::ExpandedWidget::Todos,dynamic_island::ExpandedWidget::Clock,
    dynamic_island::ExpandedWidget::System});
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
    bool enabled=false,locked=false,visibilityConfirmed=false;
    std::string material;
    std::string shape;
    HWND island=nullptr;
    unsigned stage=0,ready=0;
    unsigned snapshots=0,snapshotAtOpen=0;
    unsigned previews=0;
    unsigned allScreenChecks=0,singleScreenChecks=0;
    unsigned expansionRound=0;
    unsigned fullscreenRound=0;
    bool clickModeConfirmed=false;
    bool fullscreenOffConfirmed=false;
    bool animationConfirmed=false;
    bool layoutConfirmed=false,legacyLayoutObserved=false,clockSaveConfirmed=false,performanceSaveConfirmed=false;
    bool clockStyleConfirmed=false,progressStyleSaved=false,digitalStyleSaved=false,dateStyleSaved=false,flipStyleSaved=false,pixelStyleSaved=false,clockStyleReopened=false;
    ULONGLONG deadline=0,closed=0;
    void Message(const std::wstring& text){
        if(text.find(L"\"type\":\"dynamicIsland-preview\"")!=std::wstring::npos){
            ++previews;return;
        }
        if(text.find(L"\"type\":\"dynamicIsland-state\"")==std::wstring::npos)return;
        const auto state=nlohmann::json::parse(text);enabled=state.value("enabled",false);
        locked=state.value("locked",false);material=state.value("material",std::string{});
        shape=state.value("shape",std::string{});
        const auto expandedLayout=state.value("expandedLayout",0u);
        if(ready==0&&expandedLayout==53)legacyLayoutObserved=true;
        if(state.value("saved",false)&&expandedLayout==ClockLayout)clockSaveConfirmed=true;
        if(state.value("saved",false)&&expandedLayout==PerformanceLayout)performanceSaveConfirmed=true;
        layoutConfirmed=expandedLayout==PerformanceLayout&&state.value("leftWidget",-1)==20&&state.value("rightWidget",-1)==1;
        const auto clockStyle=state.value("clockStyle",std::string{});
        clockStyleConfirmed=clockStyle=="pixel";
        if(state.value("saved",false)){
            if(clockStyle=="progress")progressStyleSaved=true;
            if(clockStyle=="digital")digitalStyleSaved=true;
            if(clockStyle=="date")dateStyleSaved=true;
            if(clockStyle=="flip")flipStyleSaved=true;
            if(clockStyle=="pixel")pixelStyleSaved=true;
        }
        if(ready==1&&clockStyleConfirmed)clockStyleReopened=true;
        animationConfirmed=state.value("hoverDelayMs",0)==1000&&state.value("animationSpeed",std::string{})=="fast"&&state.value("bounce",std::string{})=="bouncy";
        visibilityConfirmed=state.value("hover",false)&&state.value("topmost",false)&&state.value("hideFullscreen",false);
        ++snapshots;
        if(state.value("saved",false)&&state.contains("screens")){
            if(!state.value("hideFullscreen",false))fullscreenOffConfirmed=true;
            else if(fullscreenOffConfirmed){fullscreenRound=ready+1;fullscreenOffConfirmed=false;}
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
            std::cerr<<"state: enabled="<<enabled<<" material="<<material<<" legacyLayout="<<legacyLayoutObserved<<
                " clockSaved="<<clockSaveConfirmed<<" performanceSaved="<<performanceSaveConfirmed<<
                " fiveLayout="<<layoutConfirmed<<" clockStyle="<<clockStyleConfirmed<<" progressSaved="<<progressStyleSaved<<
                " digitalSaved="<<digitalStyleSaved<<" dateSaved="<<dateStyleSaved<<" previews="<<previews<<
                " ready="<<ready<<" stage="<<stage<<" allScreens="<<allScreenChecks<<" singleScreen="<<singleScreenChecks<<
                " expansionRound="<<expansionRound<<" fullscreenRound="<<fullscreenRound<<'\n';
            Check(false,"real manager lifecycle deadline");app->Exit();return;
        }
        if(stage==0&&app->ManagerReady()&&enabled&&locked&&material=="frosted-glass"&&shape=="standard"&&snapshots>snapshotAtOpen&&visibilityConfirmed&&animationConfirmed&&layoutConfirmed&&clockStyleConfirmed&&progressStyleSaved&&digitalStyleSaved&&dateStyleSaved&&flipStyleSaved&&pixelStyleSaved&&legacyLayoutObserved&&clockSaveConfirmed&&performanceSaveConfirmed&&expansionRound>ready&&fullscreenRound>ready){
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
    {
        std::ofstream seed(root/L"island-display.json",std::ios::binary);
        seed<<R"JSON({"version":1,"expandedLayout":311,"leftWidget":"temperatureRange","rightWidget":"lunar"})JSON";
    }
    Probe probe;options.onMessage=[&](const auto& text){probe.Message(text);};
    options.readyScript=LR"JS((()=>{
      let material=false,shape=false,layer=false,hideOff=false,hideOn=false,allScreens=false,singleScreen=false,clickMode=false,hoverMode=false;
      let clockStyleStage=-1,clockStylesDone=false;
      const act=()=>{
        const section=document.querySelector('[data-settings-scope="dynamicIsland"]');
        if(!section)return;
        if(section.querySelector('[data-action="dynamicIsland-customIcons"],[data-action="dynamicIsland-overview"],[data-island-card],[data-island-preview-mode],[data-island-native-preview],[data-island-compact-widget]'))return;
        if(!shape){
          const choice=section.querySelector('button[data-island-shape="standard"]');
          if(!choice||choice.disabled||choice.getAttribute('aria-disabled')==='true')return;
          // A selected control is a no-op. On reopen there is no preview
          // traffic to trigger this observer again, so advance immediately.
          if(choice.getAttribute('aria-pressed')!=='true'){choice.click();return;}
          shape=true;
        }
        if(!material){
          const choice=section.querySelector('[data-island-preference="material"][data-value="frosted-glass"]');
          if(!choice||choice.disabled||choice.getAttribute('aria-disabled')==='true')return;
          if(choice.getAttribute('aria-pressed')!=='true'){choice.click();return;}
          material=true;
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
        const picker=section.querySelector('[data-island-expanded-picker]');
        if(picker){
          if(!section.querySelector('[data-tab="music"],[data-tab="todo"],[data-tab="quick-launch"],[data-tab="clock"],[data-tab="notifications"]')||
             picker.querySelectorAll('[data-island-expanded-chip]').length!==5)return;
          for(const id of [0,1,4,5,3]){
            const choice=picker.querySelector(`[data-island-expanded-toggle="${id}"]`);
            if(!choice||choice.disabled||choice.getAttribute('aria-disabled')==='true')return;
            if(choice.getAttribute('aria-checked')!=='true'){choice.click();return;}
          }
          if([...picker.querySelectorAll('img')].some(image=>!image.complete||!image.naturalWidth))return;
          if(!clockStylesDone){
          const clockCategory=section.querySelector('[data-island-style-widget="clock"]');
          if(!clockCategory||clockCategory.disabled||clockCategory.getAttribute('aria-disabled')==='true')return;
          if(clockCategory.getAttribute('aria-pressed')!=='true'){clockCategory.click();return;}
          const finalStyle=section.querySelector('[data-island-clock-style="pixel"]');
          if(!finalStyle||finalStyle.disabled||finalStyle.getAttribute('aria-disabled')==='true')return;
          // Reopening retains Pixel. Only the first visit walks all five
          // styles; every transition waits for the host-confirmed selection.
          if(clockStyleStage<0)clockStyleStage=finalStyle.getAttribute('aria-pressed')==='true'?5:0;
          const styles=['progress','digital','date','flip','pixel'];
          while(clockStyleStage<styles.length){
            const choice=section.querySelector(`[data-island-clock-style="${styles[clockStyleStage]}"]`);
            if(!choice||choice.disabled||choice.getAttribute('aria-disabled')==='true')return;
            if(choice.getAttribute('aria-pressed')!=='true'){choice.click();return;}
            ++clockStyleStage;
          }
          if(finalStyle.getAttribute('aria-pressed')!=='true')return;
          clockStylesDone=true;
          }
        }
        if(material&&shape&&picker&&!layer){
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
    Check(probe.fullscreenRound==2,"both manager visits finish style-card confirmation before the settings fullscreen toggle round-trip");
    Check(probe.animationConfirmed,"real animation controls persist through the manager close and reopen");
    Check(probe.layoutConfirmed,"real widget selection reaches the native session through WebView");
    Check(probe.progressStyleSaved&&probe.digitalStyleSaved&&probe.dateStyleSaved&&probe.flipStyleSaved&&probe.pixelStyleSaved&&probe.clockStyleReopened,
          "all five alternate clock styles save through WebView and the final pixel style is restored when the manager reopens");
    Check(probe.previews==0,"the settings page no longer requests whole-island native previews");
    Check(probe.legacyLayoutObserved&&probe.clockSaveConfirmed,"real WebView restores the legacy layout, saves clock as a fourth widget and retains it after reopening");
    Check(probe.performanceSaveConfirmed,"real WebView adds performance after clock and retains all five widgets after reopening");
    {
        std::ifstream persisted(root/L"island-display.json",std::ios::binary);
        const auto display=nlohmann::json::parse(persisted,nullptr,false);
        Check(display.is_object()&&display.value("expandedLayout",0u)==PerformanceLayout&&display.value("clockStyle",std::string{})=="pixel"&&
              display.value("leftWidget",std::string{})=="temperatureRange"&&display.value("rightWidget",std::string{})=="lunar",
              "real manager five-widget selection and clock style persist without changing compact widget selections");
    }
    Check(!Island()&&!app.ManagerHandle(),"application releases all island windows");
    std::cout<<(passed?"PASS":"FAIL")<<" island manager protocol, real UI controls, close/reopen and message-loop lifetime\n";
    return passed?0:1;
}
