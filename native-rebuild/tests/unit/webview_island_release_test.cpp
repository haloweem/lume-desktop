#include "platform/windows/webview_page_window.h"
#include "vendor/nlohmann/json.hpp"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using Json=nlohmann::json;
using lume::platform::windows::WebViewPageWindow;
std::wstring Wide(const Json& value){const auto text=value.dump(-1,' ',true);return {text.begin(),text.end()};}
bool MovePointer(POINT point){
    const int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(width<=1||height<=1)return false;
    INPUT input{};input.type=INPUT_MOUSE;
    input.mi.dx=MulDiv(point.x-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,width-1);
    input.mi.dy=MulDiv(point.y-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,height-1);
    input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    return SendInput(1,&input,sizeof(input))==1;
}
bool Button(DWORD flag){INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=flag;return SendInput(1,&input,sizeof(input))==1;}
const wchar_t* kScript=LR"JS((()=>{
  let down=0,moves=0,up=0,nativeDrags=0,live=false;
  const send=data=>chrome.webview.postMessage({type:'island-release-probe',...data});
  const order=()=>[...document.querySelectorAll('[data-island-expanded-chip].is-enabled')].map(node=>Number(node.dataset.islandExpandedChip));
  document.addEventListener('pointerdown',event=>{if(event.isTrusted&&event.target.closest('[data-island-expanded-chip]'))++down;});
  document.addEventListener('dragstart',()=>++nativeDrags);
  document.addEventListener('pointermove',event=>{
    if(!event.isTrusted||!(event.buttons&1))return;++moves;
    if(!live&&document.querySelector('.is-dragging')){
      live=true;const box=document.querySelector('.is-dragging').getBoundingClientRect();
      send({phase:'live',order:order(),follows:event.clientX>=box.left&&event.clientX<=box.right&&event.clientY>=box.top&&event.clientY<=box.bottom,
        givingWay:[...document.querySelectorAll('[data-island-expanded-chip]:not(.is-dragging)')].some(node=>node.getAnimations().some(animation=>animation.playState==='running'))});
    }
  });
  document.addEventListener('pointerup',event=>{
    if(!event.isTrusted||!down)return;++up;
    requestAnimationFrame(()=>send({phase:'released',order:order(),down,moves,up,nativeDrags,dragging:!!document.querySelector('.is-dragging')}));
  });
  const ready=()=>{
    const source=document.querySelector('[data-island-expanded-chip="0"] img');
    const target=document.querySelector('[data-expanded-widgets]');
    if(!source||!target||order().join(',')!=='0'){requestAnimationFrame(ready);return;}
    source.scrollIntoView({block:'nearest'});
    document.fonts.ready.then(()=>requestAnimationFrame(()=>requestAnimationFrame(()=>{
      const a=source.getBoundingClientRect(),b=target.getBoundingClientRect();
        send({phase:'ready',source:[a.left+a.width/2,a.top+a.height/2],target:[b.right-6,a.top+a.height/2],dpr:devicePixelRatio,
          widgets:document.querySelectorAll('[data-island-expanded-chip]').length,
          compactWidgets:document.querySelectorAll('[data-island-compact-widget]').length,
          fileTray:!!document.querySelector('[data-island-compact-widget="18"]'),
          compactSides:document.querySelectorAll('[data-island-compact-side]').length,
          tabs:[...document.querySelectorAll('.settings-side-nav [data-tab]')].map(node=>node.dataset.tab)});
    })));
  };ready();
})();)JS";
struct Probe {
    WebViewPageWindow window;
    Json state=Json::parse(R"({"version":1,"type":"dynamicIsland-state","enabled":true,"running":true,"locked":true,"expanded":false,"focusing":false,"finished":false,"remaining":0,"hideFullscreen":true,"topmost":false,"hover":false,"autoHide":true,"size":"standard","material":"none","shape":"capsule","leftWidget":0,"rightWidget":0,"screen":1,"screens":[1],"hoverDelayMs":300,"animationSpeed":"normal","bounce":"normal","expandedLayout":1,"loadStatus":"loaded","error":"","result":"snapshot","saved":false})");
    Json evidence=Json::array();
    std::thread pointer;
    std::atomic<bool> motionDone{false},inputFailed{false};
    bool hasPoints=false,started=false,live=false,released=false,failed=false,complete=false;
    unsigned saves=0;
    POINT source{},target{},oldPointer{};HWND oldForeground=nullptr;
    ULONGLONG deadline=GetTickCount64()+25000;
    ~Probe(){if(pointer.joinable())pointer.join();if(started){SetCursorPos(oldPointer.x,oldPointer.y);if(IsWindow(oldForeground))SetForegroundWindow(oldForeground);}}
    void Fail(const char* message){failed=true;std::cerr<<"FAIL: "<<message<<'\n';window.RequestClose();}
    void Message(std::wstring_view text){
        const auto message=Json::parse(text);
        if(!message.is_object())return;
        if(message.value("type",std::string{})!="island-release-probe")return;
        evidence.push_back(message);const auto phase=message.value("phase",std::string{});
        if(phase=="ready"){
            if(message.at("widgets")!=5||message.at("compactWidgets")!=0||message.at("fileTray")!=false||message.at("compactSides")!=0||
               message.at("tabs")!=Json::array({"home","todo","notifications","settings","details"})){
                Fail("current island navigation and five expanded widgets must be present without retired compact controls");return;
            }
            const double dpr=message.at("dpr");
            const auto point=[&](const char* name){POINT value{static_cast<LONG>(std::lround(message.at(name)[0].get<double>()*dpr)),static_cast<LONG>(std::lround(message.at(name)[1].get<double>()*dpr))};ClientToScreen(window.Handle(),&value);return value;};
            source=point("source");target=point("target");hasPoints=true;
        }else if(phase=="live"){
            live=message.value("follows",false)&&saves==0;
        }else if(phase=="released"){
            released=message.value("down",0u)==1&&message.value("moves",0u)>2&&message.value("up",0u)==1&&
                message.value("nativeDrags",0u)==0&&!message.value("dragging",true)&&message.at("order")==Json::array({0});
            if(!released)Fail("real pointer release must retain all enabled widgets and clear the drag");
        }
    }
    void Tick(){
        if(failed)return;
        if(GetTickCount64()>deadline){Fail("native pointer reorder did not complete");return;}
        if(hasPoints&&!started){
            GetCursorPos(&oldPointer);oldForeground=GetForegroundWindow();started=true;
            window.Present();SetWindowPos(window.Handle(),HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
            const HWND hit=WindowFromPoint(source);
            if(hit!=window.Handle()&&!IsChild(window.Handle(),hit)){Fail("native reorder target is occluded");return;}
            pointer=std::thread([this]{
                Sleep(150);if(!MovePointer(source))inputFailed=true;Sleep(80);
                const bool pressed=Button(MOUSEEVENTF_LEFTDOWN);if(!pressed)inputFailed=true;Sleep(100);
                if(pressed)for(int step=1;step<=32;++step){
                    const POINT point{source.x+(target.x-source.x)*step/32,source.y+(target.y-source.y)*step/32};
                    if(!MovePointer(point))inputFailed=true;Sleep(25);
                }
                Sleep(80);if(pressed&&!Button(MOUSEEVENTF_LEFTUP))inputFailed=true;motionDone=true;
            });
        }
        if(motionDone&&released&&saves==0){complete=!inputFailed;if(!complete)Fail("SendInput failed");else window.RequestClose();}
    }
};
Probe* active=nullptr;
void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD){if(active)active->Tick();}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Probe probe;active=&probe;
    probe.window.ConfigureApplicationCommands([&](std::wstring_view text){probe.Message(text);});
    const auto timer=SetTimer(nullptr,0,25,Tick);
    const auto result=probe.window.Run(GetModuleHandleW(nullptr),[&](lume::ui::PageCommand command){
        const int number=static_cast<int>(command),first=static_cast<int>(lume::ui::PageCommand::IslandExpandedLayoutFirst);
        if(number>=first&&number<=static_cast<int>(lume::ui::PageCommand::IslandExpandedLayoutLast)){
            ++probe.saves;const int code=number-first;
            probe.Fail("dragging the single published music widget must not write preferences");
            probe.state["expandedLayout"]=code;probe.state["saved"]=true;probe.state["result"]="applied";
            probe.window.Post(Wide(probe.state));
        }
        if(command==lume::ui::PageCommand::Ready){
            probe.window.Post(LR"({"version":1,"type":"app-settings-state","revision":1,"language":"zh-CN","balancedMode":true,"autostart":false,"busy":false,"error":"","update":{"status":"idle","version":"","progress":0}})");
            probe.window.Post(Wide(probe.state));
            probe.window.Post(LR"({"version":1,"type":"navigate","page":"dynamicIsland"})");
        }
        return std::wstring{};
    },true,kScript);
    KillTimer(nullptr,timer);active=nullptr;
    const auto root=std::filesystem::current_path()/L"verification"/L"webview-island-widgets";
    std::filesystem::create_directories(root);std::ofstream(root/L"results.json")<<probe.evidence.dump(2);
    const bool ok=result==0&&probe.complete&&!probe.failed&&probe.saves==0&&probe.live&&probe.released;
    std::cout<<(ok?"PASS":"FAIL")<<" native WebView2 restored island catalogs and navigation, singleton drag stability; saves="<<probe.saves<<" live="<<probe.live<<" released="<<probe.released<<'\n';
    return ok?0:1;
}
