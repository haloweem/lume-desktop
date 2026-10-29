#include "apps/shell/desktop_application.h"
#include "core/organizer/organizer_store.h"
#include "platform/windows/local_settings_file_store.h"
#include <filesystem>
#include <iostream>
namespace {
using namespace lume;
bool passed=true;
void Check(bool ok,const char* reason){if(!ok){passed=false;std::cerr<<"FAIL: "<<reason<<'\n';}}
HWND OrganizerWindow(){
    HWND result=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL{
        wchar_t title[80]{};GetWindowTextW(hwnd,title,80);
        if (std::wstring(title) == L"Lume 图标收纳") {
            *reinterpret_cast<HWND*>(data) = hwnd;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));return result;
}
struct Probe {
    shell::DesktopApplication* app=nullptr;int stage=0,ready=0,expectedReady=2;bool enabled=false,material=false,scaled=false;HWND widget=nullptr;ULONGLONG deadline=0,closedAt=0;
    void Message(const std::wstring& text){
        if(text.find(L"\"type\":\"organizer-state\"")==std::wstring::npos)return;
        enabled=text.find(L"\"enabled\":true")!=std::wstring::npos;
        material=text.find(L"\"material\":\"liquid-glass\"")!=std::wstring::npos;
        scaled=text.find(L"\"iconScale\":73")!=std::wstring::npos;
    }
    void Tick(){
        if(GetTickCount64()>deadline){Check(false,"organizer lifecycle deadline");app->Exit();return;}
        if(stage==0&&app->ManagerReady()){
            if(!enabled||!material||!scaled)return;
            ++ready;widget=widget?widget:OrganizerWindow();Check(widget!=nullptr,"organizer native window created");
            stage=1;app->CloseManager();closedAt=GetTickCount64();
        }else if(stage==1&&!app->ManagerHandle()&&GetTickCount64()-closedAt>800){
            Check(IsWindow(widget),"organizer remains alive after manager close");
            if(ready<expectedReady){enabled=material=false;stage=0;app->Open(shell::ManagerPage::Organizer);}
            else {stage=2;SendMessageW(widget,WM_CLOSE,0,0);Check(!IsWindow(widget),"organizer native close releases window");}
        }
    }
};
Probe* probe=nullptr;void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD){if(probe)probe->Tick();}
}
int main(int argc,char** argv){
    const bool narrow=argc==2&&std::string(argv[1])=="--narrow";
    if(argc!=1&&!narrow){std::cerr<<"Usage: lume-desktop-organizer-test [--narrow]\n";return 2;}
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto root=std::filesystem::path(executable).parent_path()/L"desktop-organizer-artifacts"/std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    shell::DesktopApplicationOptions options;
    options.clockPreferencesPath=(root/L"clock.json").u8string();
    options.calendarPreferencesPath=(root/L"calendar.json").u8string();
    options.organizerPreferencesPath=(root/L"organizer.json").u8string();
    if(narrow){
        platform::windows::LocalSettingsFileStore fixtureFiles;
        organizer::Store fixtureStore(fixtureFiles,[](std::string_view text){return std::string(text);});
        auto fixture=organizer::Defaults();fixture.collections[0].enabled=true;
        fixture.collections[0].bounds=organizer::Bounds{80,80,120,569};
        fixture.collections[0].mode=organizer::Mode::Collapsed;
        Check(fixtureStore.Save(options.organizerPreferencesPath,fixture),"narrow enabled organizer fixture saved");
    }
    Probe state;state.expectedReady=narrow?1:2;
    options.verification=true;options.onMessage=[&](const auto& message){state.Message(message);};
    options.readyScript=LR"JS((()=>{
      let enabling=false,material=false,opened=false,cycle=window.organizerNarrowVerification?0:3;
      const act=()=>{
        if(!opened){
          const toggle=document.querySelector('.Card [data-action="organizer-toggle"]');
          if(!toggle){document.querySelector('[data-action="back"]')?.click();return;}
          if(toggle.disabled)return;
          if(cycle===0){if(toggle.getAttribute('aria-checked')!=='true')return;cycle=1;toggle.click();return;}
          if(cycle===1){if(toggle.getAttribute('aria-checked')!=='false')return;cycle=2;toggle.click();return;}
          if(cycle===2){if(toggle.getAttribute('aria-checked')!=='true')return;cycle=3;}
          if(toggle.getAttribute('aria-checked')!=='true'){if(!enabling){enabling=true;toggle.click();}return;}
          opened=true;document.querySelector('[data-open="organizer"]').click();return;
        }
        const slider=document.querySelector('#organizer-icon-scale'),number=document.querySelector('#organizer-icon-scale-number');
        if(!slider||!number||slider.disabled||number.readOnly)return;
        if(slider.value!=='73'){number.value='73';number.dispatchEvent(new Event('input',{bubbles:true}));number.dispatchEvent(new Event('change',{bubbles:true}));return;}
        const select=document.querySelector('#organizer-material');
        if(!select||select.disabled||material||select.textContent.includes('液态玻璃'))return;
        material=true;select.click();document.querySelector('[data-select-owner="organizer-material"][data-select-value="liquid-glass"]').click();
      };
      const observer=new MutationObserver(act);observer.observe(document.querySelector('#content'),{subtree:true,childList:true,attributes:true});act();
      window.addEventListener('beforeunload',()=>observer.disconnect(),{once:true});
    })();)JS";
    if(narrow)options.readyScript=L"window.organizerNarrowVerification=true;\n"+options.readyScript;
    shell::DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));app.Open(shell::ManagerPage::Organizer);
    state.app=&app;state.deadline=GetTickCount64()+30000;probe=&state;
    const auto timer=SetTimer(nullptr,0,40,Tick);Check(timer!=0,"organizer timer");
    const int result=app.Run();KillTimer(nullptr,timer);probe=nullptr;
    Check(result==0&&state.ready==state.expectedReady&&state.stage==2,"organizer manager and widget lifecycles");
    Check(!OrganizerWindow()&&!app.ManagerHandle(),"organizer all windows released");
    platform::windows::LocalSettingsFileStore files;organizer::Store store(files,[](std::string_view text){return std::string(text);});
    const auto saved=store.Load((root/L"organizer.json").u8string());
    Check(saved.state==organizer::LoadState::Loaded&&saved.value.collections[0].enabled==false&&!saved.value.collections[0].frosted,
        "organizer native close persisted disabled and material");
    Check(saved.value.collections[0].iconScale==73&&saved.value.collections[0].mode==organizer::Mode::Expanded,"real slider saves icon size and legacy compact mode is retired");
    if(narrow)Check(saved.value.collections[0].bounds&&saved.value.collections[0].bounds->width==120,
        "narrow state roundtrip preserves the user's one-column width");
    return passed?0:1;
}
