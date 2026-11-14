#include "platform/windows/webview_page_window.h"
#include "core/ui/original_window_layout.h"
#include "vendor/nlohmann/json.hpp"
#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>

namespace {
using Json=nlohmann::json;
lume::platform::windows::WebViewPageWindow* active=nullptr;
bool failed=false,finished=false;
void Check(bool pass,const char* reason) {
    if(!pass){failed=true;std::cerr<<"FAIL: "<<reason<<'\n';}
}
void CALLBACK Timeout(HWND,UINT,UINT_PTR,DWORD) {
    Check(false,"proportional scale verification deadline");active->RequestClose();
}
std::wstring Wide(const Json& value) {const auto s=value.dump(-1,' ',true);return {s.begin(),s.end()};}
}
int main(int argc,char** argv) {
    const unsigned dpi=argc>1?static_cast<unsigned>(std::strtoul(argv[1],nullptr,10)):96;
    lume::platform::windows::WebViewPageWindow window;active=&window;
    RECT reference{};
    std::map<std::string,Json> baselines;
    Json samples=Json::array();
    unsigned count=0;
    window.ConfigureApplicationCommands([&](std::wstring_view text) {
        const auto message=Json::parse(text,nullptr,false);
        if(!message.is_object()||message.value("scope","")!="scale-probe")return;
        const auto action=message.value("action","");
        if(action=="navigate") {
            window.Post(message.at("message").is_object()?Wide(message.at("message")):L"");
            window.Post(L"{\"type\":\"scale-probe-ack\"}");
        } else if(action=="scale") {
            if(!reference.right) {
                GetWindowRect(window.Handle(),&reference);
            }
            const int percent=message.at("value");
            Check(window.SetInterfaceScale(percent),"native window accepts a valid scale");
            RECT current{};GetWindowRect(window.Handle(),&current);
            const auto expected=lume::ui::ScaleManagerWindowBounds({reference.left,reference.top,
                reference.right-reference.left,reference.bottom-reference.top},percent);
            if(!message.value("fullscreen",false)) {
                Check(current.left==reference.left&&current.top==reference.top,"native top-left stays fixed");
                Check(current.right-current.left==expected.width&&current.bottom-current.top==expected.height,
                    "native width and height use the same scale without cumulative drift");
            }
            window.Post(L"{\"type\":\"scale-probe-ack\"}");
        } else if(action=="sample") {
            const auto page=message.at("page").get<std::string>();
            const auto& sample=message.at("sample");
            if(!baselines.count(page))baselines[page]=sample;
            const auto& base=baselines.at(page);
            const auto ratio=message.at("percent").get<double>()/75.0;
            Check(std::abs(sample.at("dpr").get<double>()/base.at("dpr").get<double>()-ratio)<0.004,
                "real WebView raster scale follows the native window ratio");
            Check(sample.at("nodes").size()==base.at("nodes").size(),"scaling preserves the visible control count");
            if(sample.at("nodes").size()==base.at("nodes").size()) {
                for(std::size_t index=0;index<base.at("nodes").size();++index) {
                    const auto& before=base.at("nodes")[index];const auto& after=sample.at("nodes")[index];
                    Check(before.at("tag")==after.at("tag"),"scale preserves control order");
                    Check(before.at("font")==after.at("font"),"font CSS size is unchanged at every scale");
                    const auto pixelTolerance=(std::max)(2.1,1.0/sample.at("dpr").get<double>()+
                        1.0/base.at("dpr").get<double>());
                    for(const auto* key:{"w","h"})
                        Check(std::abs(before.at(key).get<double>()-after.at(key).get<double>())<pixelTolerance,
                            "control dimensions retain their proportions within font raster rounding");
                    for(const auto* key:{"x","y"}) {
                        // Chromium rounds normal font line boxes at each raster
                        // scale. Long settings columns accumulate that rounding;
                        // bound it to 1% + 2.1 logical pixels, while dimensions
                        // above remain tight and the 75% restoration is exact.
                        const auto original=before.at(key).get<double>();
                        const auto tolerance=2.1+std::abs(original)*0.01;
                        Check(std::abs(original-after.at(key).get<double>())<tolerance,
                            "scale keeps page layout without responsive reflow");
                    }
                    if(message.at("percent")==75)
                        Check(before==after,"return to 75 restores exact original layout and text geometry");
                }
            }
            Check(sample.at("overflow").get<double>()<=1,"scale does not add document horizontal overflow");
            samples.push_back(message);++count;
            window.Post(L"{\"type\":\"scale-probe-ack\"}");
        } else if(action=="done") {finished=true;window.RequestClose();}
        else if(action=="error") {Check(false,message.value("detail","").c_str());window.RequestClose();}
    });
    const auto timeout=SetTimer(nullptr,0,90000,Timeout);
    const auto result=window.Run(GetModuleHandleW(nullptr),[](auto){return std::wstring{};},true,LR"JS(
      (async()=>{
        const style=document.createElement('style');style.textContent='*,*::before,*::after{animation:none!important;transition:none!important}';document.head.append(style);
        const send=(action,fields={})=>new Promise(resolve=>{
          const listener=event=>{if(event.data?.type==='scale-probe-ack'){chrome.webview.removeEventListener('message',listener);resolve();}};
          chrome.webview.addEventListener('message',listener);
          chrome.webview.postMessage({scope:'scale-probe',action,...fields});
        });
        const settle=()=>new Promise(resolve=>setTimeout(()=>requestAnimationFrame(()=>requestAnimationFrame(resolve)),90));
        let baselineDpr=0;
        const scaleTimeouts=[];
        const waitForScale=async percent=>{
          const started=performance.now(),deadline=started+2500;
          let matchingFrames=0;
          while(performance.now()<deadline){
            const frame=await new Promise(resolve=>{
              let complete=false;
              const timer=setTimeout(()=>{
                if(complete)return;complete=true;cancelAnimationFrame(request);resolve(false);
              },Math.max(0,deadline-performance.now()));
              const request=requestAnimationFrame(()=>{
                if(complete)return;complete=true;clearTimeout(timer);resolve(true);
              });
            });
            if(!frame)break;
            matchingFrames=Math.abs(devicePixelRatio/baselineDpr-percent/75)<0.004?matchingFrames+1:0;
            if(matchingFrames>=2)return {ready:true,elapsedMs:performance.now()-started};
          }
          return {ready:false,elapsedMs:performance.now()-started,expectedDpr:baselineDpr*percent/75,actualDpr:devicePixelRatio};
        };
        await document.fonts.ready;
        const routes=['home','wallpaperLocal','wallpaperOfficial','desktopBeauty','widgets',
          'clock','calendar','performance','spectrum','almanac','todayTodo','weather','organizer','icons','taskbar','minimize','music','dock','dynamicIsland',
          'applicationSettings','user','autostart','support','about','modal'];
        for(const page of routes){
          if(page==='modal'){
            const {createLumeModalController}=await import('https://lume.local/lume-modal.js');
            createLumeModalController(document).showQuota(false);
          }else if(['home','wallpaperLocal','wallpaperOfficial','desktopBeauty','widgets'].includes(page)){
            if(page!=='home'){
              const link=document.querySelector(`[data-view="${page}"],[data-open="${page}"]`);
              if(!link)throw Error('missing real navigation '+page);link.click();
            }
          }else{
            const section=['user','autostart','support','about'].includes(page)?page:'general';
            await send('navigate',{message:{version:1,type:'navigate',page:page==='applicationSettings'||section!=='general'?'applicationSettings':page,
              ...(page==='applicationSettings'||section!=='general'?{section}:{})}});
          }
          for(const percent of [75,50,62,75]){
            await send('scale',{value:percent});
            // The native ACK confirms window resizing, not the renderer's
            // asynchronous zoom update. Capture the first 75% baseline once,
            // then require the requested DPR for two real rendering frames.
            if(!baselineDpr){await settle();baselineDpr=devicePixelRatio;}
            const scaleWait=await waitForScale(percent);
            if(!scaleWait.ready)scaleTimeouts.push({page,percent,...scaleWait});
            const nodes=[...document.querySelectorAll('header,main,h1,h2,h3,label,button,input,select,.ModuleToolbar,.setting-row,.component-card')]
              .filter(node=>{const r=node.getBoundingClientRect();return r.width&&r.height&&getComputedStyle(node).visibility!=='hidden';})
              .slice(0,48).map(node=>{const r=node.getBoundingClientRect();return {tag:node.tagName,x:r.x,y:r.y,w:r.width,h:r.height,font:parseFloat(getComputedStyle(node).fontSize)||0};});
            await send('sample',{page,percent,scaleWait,sample:{dpr:devicePixelRatio,nodes,overflow:document.documentElement.scrollWidth-innerWidth}});
          }
        }
        const fullscreen=expanded=>new Promise(resolve=>{
          const listener=event=>{if(event.data?.type==='window'&&event.data.fullscreen===expanded){chrome.webview.removeEventListener('message',listener);resolve();}};
          chrome.webview.addEventListener('message',listener);chrome.webview.postMessage('v1|window|maximize');
        });
        await fullscreen(true);
        for(const value of [50,62,75])await send('scale',{value,fullscreen:true});
        // Fullscreen restoration separately fits changed/virtual work areas;
        // it may move a verification canvas that exceeds the physical screen.
        await fullscreen(false);await send('scale',{value:75,fullscreen:true});
        if(scaleTimeouts.length)throw Error('WebView scale render deadline: '+JSON.stringify(scaleTimeouts));
        chrome.webview.postMessage({scope:'scale-probe',action:'done'});
      })().catch(error=>chrome.webview.postMessage({scope:'scale-probe',action:'error',detail:String(error)}));
    )JS",dpi);
    if(timeout)KillTimer(nullptr,timeout);
    Check(result==0&&finished&&count==100,"all 24 manager routes and the top-layer modal finish four native scale transitions");
    std::ofstream("interface-scale-"+std::to_string(dpi)+"-evidence.json")<<samples.dump(2);
    if(!failed)std::cout<<"PASS: "<<count<<" real WebView page samples at DPI "<<dpi<<", anchor and proportional text/control geometry\n";
    return failed?1:0;
}
