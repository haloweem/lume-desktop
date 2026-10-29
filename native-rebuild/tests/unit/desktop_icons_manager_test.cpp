#include "apps/shell/desktop_application.h"
#include "core/desktop_icons/desktop_icons_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "vendor/nlohmann/json.hpp"
#include <commctrl.h>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>

namespace {
using namespace lume;
using namespace platform::windows;
bool passed = true;
void Check(bool ok, const char* why) {
    if (!ok) { passed = false; std::cerr << "FAIL: " << why << '\n'; }
}
struct Fixture {
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread worker;
    DesktopIconsTarget target;
    Fixture() {
        std::promise<DesktopIconsTarget> ready;
        auto result = ready.get_future();
        worker = std::thread([this, ready = std::move(ready)]() mutable {
            INITCOMMONCONTROLSEX controls{}; controls.dwSize = sizeof(controls); controls.dwICC = ICC_LISTVIEW_CLASSES;
            InitCommonControlsEx(&controls);
            WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"SHELLDLL_DefView"; wc.lpfnWndProc = DefWindowProcW;
            RegisterClassExW(&wc);
            HWND shell = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Lume 管理页测试专用桌面",
                WS_POPUP | WS_VISIBLE, 70, 70, 220, 160, nullptr, nullptr, wc.hInstance, nullptr);
            HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 220, 160,
                shell, nullptr, wc.hInstance, nullptr);
            ready.set_value({shell,list,GetCurrentProcessId(),GetCurrentThreadId()});
            for (;;) {
                const auto event = MsgWaitForMultipleObjectsEx(1,&stop,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
                if(event == WAIT_OBJECT_0 || event == WAIT_FAILED)break;
                MSG message{};
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                    TranslateMessage(&message);DispatchMessageW(&message);
                }
            }
            DestroyWindow(shell);
        });
        target = result.get();
    }
    ~Fixture(){SetEvent(stop);worker.join();CloseHandle(stop);}
};
struct Probe {
    shell::DesktopApplication* app = nullptr;
    DesktopIconsTarget target;
    nlohmann::json latest;
    int stage = 0;
    ULONGLONG deadline = 0,closedAt = 0;
    bool disabledSaved = false;
    void Message(const std::wstring& text) {
        const auto value = nlohmann::json::parse(text.begin(),text.end(),nullptr,false);
        if(value.is_discarded() || value.value("type","")!="icons-state")return;
        latest=value;
        if(stage==2 && value.value("saved",false) && !value.value("enabled",true))disabledSaved=true;
    }
    void Tick() {
        if(GetTickCount64()>deadline){
            std::cerr<<"stage="<<stage<<" last="<<latest.dump()<<'\n';
            Check(false,"real icons manager lifecycle deadline");app->Exit();return;
        }
        if(stage==0 && app->ManagerReady() && latest.is_object() &&
           latest.value("enabled",false) && latest.value("running",false) &&
           latest.value("autoHideDelay",0)==9 && !latest.value("showIconsOnAnyInput",true)) {
            Check(!IsWindowVisible(target.list),"page enable restored manual hide on actual private desktop");
            Check(!app->ClockRunning()&&!app->CalendarRunning(),"system module did not start unrelated widgets");
            stage=1;app->CloseManager();closedAt=GetTickCount64();
        } else if(stage==1 && !app->ManagerHandle() && GetTickCount64()-closedAt>800) {
            Check(DesktopIconsRuntime::ActiveWorkers()==1&&!IsWindowVisible(target.list),
                  "worker and private system effect survive manager destruction");
            stage=2;app->Open(shell::ManagerPage::Icons);
        } else if(stage==2 && app->ManagerReady() && disabledSaved) {
            Check(IsWindowVisible(target.list)&&DesktopIconsRuntime::ActiveWorkers()==0,
                  "reopened page disable restores desktop and stops worker");
            stage=3;app->Exit();
        }
    }
};
Probe* probe = nullptr;
void CALLBACK Timer(HWND,UINT,UINT_PTR,DWORD){if(probe)probe->Tick();}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Fixture fixture;
    Check(ValidDesktopIconsTarget(fixture.target),"private desktop target ready");
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto root=std::filesystem::path(executable).parent_path()/L"desktop-icons-artifacts"/std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    LocalSettingsFileStore files;desktop_icons::PreferencesStore store(files);
    desktop_icons::Preferences initial;initial.enableAutoHide=false;initial.manuallyHidden=true;
    const auto path=(root/L"desktop-icons.json").u8string();
    Check(store.Save(path,initial),"isolated initial settings");
    shell::DesktopApplicationOptions options;
    options.clockPreferencesPath=(root/L"clock.json").u8string();
    options.calendarPreferencesPath=(root/L"calendar.json").u8string();
    options.desktopIconsPreferencesPath=path;
    options.verification=true;options.verificationDesktopIcons=[&]{return std::vector<DesktopIconsTarget>{fixture.target};};
    Probe state;state.target=fixture.target;
    options.onMessage=[&](const auto& message){state.Message(message);};
    options.readyScript=LR"JS((()=>{
      let mode=null,step=0;
      const act=()=>{
        if(step===0){
          const toggle=document.querySelector('.Card [data-action="icons-toggle"]');
          if(!toggle){document.querySelector('[data-action="back"]')?.click();return;}
          if(toggle.disabled)return;
          mode=toggle.getAttribute('aria-checked')==='true'?'disable':'enable';
          step=1;toggle.click();return;
        }
        if(step===1){
          const toggle=document.querySelector('.Card [data-action="icons-toggle"]');
          if(!toggle||toggle.disabled||toggle.getAttribute('aria-checked')!==(mode==='enable'?'true':'false'))return;
          step=2;document.querySelector('[data-open="icons"]').click();return;
        }
        if(mode==='disable')return;
        const delay=document.querySelector('#icons-delay');
        if(step===2&&delay&&!delay.disabled){
          step++;delay.value='9';delay.dispatchEvent(new Event('change',{bubbles:true}));return;
        }
        const restore=document.querySelector('[data-action="icons-restoreOnInput"]');
        if(step===3&&restore&&!restore.disabled){step++;restore.click();}
      };
      const observer=new MutationObserver(act);
      observer.observe(document.querySelector('#content'),{subtree:true,childList:true,attributes:true});
      act();window.addEventListener('beforeunload',()=>observer.disconnect(),{once:true});
    })();)JS";
    shell::DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));
    app.Open(shell::ManagerPage::Icons);
    state.app=&app;state.deadline=GetTickCount64()+40000;probe=&state;
    const auto timer=SetTimer(nullptr,0,40,Timer);Check(timer!=0,"manager probe timer");
    const int result=app.Run();KillTimer(nullptr,timer);probe=nullptr;
    Check(result==0&&state.stage==3&&!app.ManagerHandle(),"real manager two-window cycle completed");
    const auto saved=store.Load(path);
    Check(saved.state==desktop_icons::LoadState::Loaded&&!saved.value.enabled&&
          saved.value.autoHideDelay==9&&!saved.value.showIconsOnAnyInput&&saved.value.manuallyHidden,
          "native acknowledgements correspond to saved settings");
    Check(DesktopIconsRuntime::ActiveWorkers()==0&&IsWindowVisible(fixture.target.list),
          "all system workers stopped and private desktop restored");
    return passed?0:1;
}
