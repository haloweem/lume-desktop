#include "platform/windows/material/material_api.h"
#include "platform/windows/material/widget_material_runtime.h"
#include "platform/windows/lume_window_ids.h"
#include "material_release_wait.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#include <objbase.h>
template<class T> T Resolve(HMODULE module,const char* name){const FARPROC address=GetProcAddress(module,name);T result=nullptr;static_assert(sizeof(result)==sizeof(address));std::memcpy(&result,&address,sizeof(result));return result;}
void Expect(bool b,const char* text){if(!b){std::cerr<<"FAIL: "<<text<<" error="<<GetLastError()<<'\n';std::exit(1);}}
unsigned statusEvents=0;
void Pump(DWORD ms){const auto end=GetTickCount64()+ms;while(GetTickCount64()<end){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==LumeMaterialStatusChangedMessage)++statusEvents;TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}}
HWND SurfaceFor(HWND owner){
 struct Search{HWND owner,surface;};Search search{owner,nullptr};
 EnumWindows([](HWND window,LPARAM context)->BOOL{
   auto& search=*reinterpret_cast<Search*>(context);
   DWORD pid=0;GetWindowThreadProcessId(window,&pid);
   if(pid==GetCurrentProcessId()&&GetPropW(window,lume::window_ids::GlassSurfaceOwnerProp)==search.owner){search.surface=window;return FALSE;}
   return TRUE;
 },reinterpret_cast<LPARAM>(&search));
 return search.surface;
}
RECT VirtualDesktopBounds(){
 const LONG left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN);
 return {left,top,left+GetSystemMetrics(SM_CXVIRTUALSCREEN),top+GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}
bool Contains(const RECT& outer,const RECT& inner){
 return outer.left<=inner.left&&outer.top<=inner.top&&outer.right>=inner.right&&outer.bottom>=inner.bottom;
}
void ExpectShellSafeSurface(HWND owner,unsigned bottomInset=0){
 const HWND surface=SurfaceFor(owner);Expect(surface!=nullptr,"material owns a native composition surface");
 Expect(GetPropW(surface,L"NonRudeHWND")!=nullptr,"composition surface opts out of shell fullscreen detection");
 RECT bounds{};Expect(GetWindowRect(surface,&bounds)!=FALSE,"composition surface bounds available");
 MONITORINFO monitor{sizeof(monitor)};
 Expect(GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor)!=FALSE,"read material owner's physical monitor");
 RECT body{};POINT origin{};
 Expect(GetClientRect(owner,&body)&&ClientToScreen(owner,&origin),"read owner client rectangle in screen pixels");
 body.bottom=(std::max)(body.top+1,body.bottom-MulDiv(static_cast<int>(bottomInset),GetDpiForWindow(owner),96));
 OffsetRect(&body,origin.x,origin.y);
 RECT visible{};
 const RECT desktop=VirtualDesktopBounds();
 if(IntersectRect(&visible,&body,&desktop))
   Expect(Contains(bounds,visible),
          "bounded composition canvas preserves every visible owner edge");
 Expect(!(bounds.left<=monitor.rcMonitor.left&&bounds.top<=monitor.rcMonitor.top&&
          bounds.right>=monitor.rcMonitor.right&&bounds.bottom>=monitor.rcMonitor.bottom),
        "small material HWND never covers a complete physical monitor");
 Expect(bounds.right-bounds.left<=body.right-body.left+128&&bounds.bottom-bounds.top<=body.bottom-body.top+128,
        "native canvas bounds follow the lens instead of spanning the virtual desktop");
}
void ExplicitCanvasScenario(LumeMaterialAttachFn attach,LumeMaterialUpdateFn update,
                           LumeMaterialDetachFn detach,LumeMaterialInspectFn inspect,const std::vector<RECT>& screens){
 Expect(!screens.empty(),"explicit material has a display fixture");
 const RECT screen=screens.front(),desktop=VirtualDesktopBounds();
 const int left=screen.left+(screen.right-screen.left-220)/2;
 const int top=screen.top+(screen.bottom-screen.top-160)/2;
 HWND owner=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,L"STATIC",L"V7 material canvas fixture",
     WS_POPUP,left,top,220,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 Expect(owner!=nullptr,"explicit material owner created");
 SetLayeredWindowAttributes(owner,0,1,LWA_ALPHA);ShowWindow(owner,SW_SHOWNOACTIVATE);
 const uint32_t pixels[4]={0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffffffu};
 LumeMaterialParameters p{};p.size=sizeof(p);p.owner=owner;p.opticalProfile=1;p.lightForeground=1;p.cornerRadius=8;
 p.lensOverride=1;p.lensX=-40;p.lensY=10;p.lensWidth=260;p.lensHeight=110;
 p.contentEnabled=1;p.contentX=20;p.contentY=0;p.contentWidth=260;p.contentHeight=110;
 p.contentPixels=pixels;p.contentPixelWidth=p.contentPixelHeight=2;p.contentGeneration=1;
 const auto handle=attach(&p);Expect(handle!=0,"V7 lens and foreground attach");
 LumeMaterialStatus status{};status.size=sizeof(status);
 const auto waitCanvas=[&](unsigned after,auto ready){
   RECT canvas{};bool complete=false;
   for(int n=0;n<100;++n){
     Pump(20);Expect(inspect(handle,&status)!=0,"inspect V7 material canvas");
     if(status.failed)break;
     if(status.frameReady&&status.frames>after&&GetWindowRect(SurfaceFor(owner),&canvas)&&ready(canvas)){complete=true;break;}
   }
   Expect(complete&&!status.failed,"V7 material presents with its complete visible envelope");
   Expect(Contains(desktop,canvas),"V7 composition canvas never extends beyond the virtual desktop");
   return canvas;
 };
 const auto screenRect=[&](float x,float y,float width,float height){
   POINT origin{};Expect(ClientToScreen(owner,&origin)!=FALSE,"read V7 owner origin");
   const float scale=GetDpiForWindow(owner)/96.f;
   return RECT{origin.x+static_cast<LONG>(std::floor(x*scale)),origin.y+static_cast<LONG>(std::floor(y*scale)),
       origin.x+static_cast<LONG>(std::ceil((x+width)*scale)),origin.y+static_cast<LONG>(std::ceil((y+height)*scale))};
 };
 const auto visiblePart=[&](const RECT& bounds){RECT visible{};IntersectRect(&visible,&bounds,&desktop);return visible;};
 const RECT explicitLens=visiblePart(screenRect(-40,10,260,110));
 const RECT explicitContent=visiblePart(screenRect(-20,10,260,110));
 waitCanvas(0,[&](const RECT& canvas){return Contains(canvas,explicitLens)&&Contains(canvas,explicitContent);});

 // The material worker must keep a click-through foreground between the
 // widget and its glass, including its normal post-frame restack path.
 HWND display=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,L"STATIC",L"V7 foreground companion",WS_POPUP,
     left,top,220,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 Expect(display!=nullptr,"V7 foreground companion created");
 SetLayeredWindowAttributes(display,0,1,LWA_ALPHA);
 SetPropW(display,lume::window_ids::DesktopCompanionOwnerProp,owner);
 Expect(SetWindowPos(display,owner,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW)!=FALSE,
        "foreground companion placed directly below its owner");
 PostMessageW(SurfaceFor(owner),lume::window_ids::GlassRestackMessage,0,0);
 waitCanvas(status.frames,[&](const RECT&){return GetWindow(owner,GW_HWNDNEXT)==display&&GetWindow(display,GW_HWNDNEXT)==SurfaceFor(owner);});
 Pump(100);
 Expect(GetWindow(display,GW_HWNDNEXT)==SurfaceFor(owner),"normal material frames preserve foreground companion order");
 DestroyWindow(display);
 PostMessageW(SurfaceFor(owner),lume::window_ids::GlassRestackMessage,0,0);
 waitCanvas(status.frames,[&](const RECT&){return GetWindow(owner,GW_HWNDNEXT)==SurfaceFor(owner);});

 // The future endpoint lies outside the owner. It must be reserved before
 // playback begins, and the same canvas must survive the complete trajectory.
 LumeMaterialKeyframe frames[9]{};
 for(int n=0;n<9;++n){
   const float mix=n/8.f;auto& frame=frames[n];
   frame.lensX=-60+260*mix;frame.lensY=10;frame.lensWidth=160+20*mix;frame.lensHeight=110;
   frame.contentX=-20;frame.contentY=0;frame.contentWidth=frame.lensWidth+40;frame.contentHeight=110;
   frame.opacity=frame.contentOpacity=1;frame.cornerRadius=8;
 }
 LARGE_INTEGER frequency{},now{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&now);
 p.timeline=frames;p.timelineCount=9;p.timelineStepMs=100;p.timelineStart=now.QuadPart+frequency.QuadPart/5;
 const RECT first=visiblePart(screenRect(-80,10,200,110));
 const RECT last=visiblePart(screenRect(180,10,220,110));
 const auto beforeTimeline=status.frames;
 Expect(update(handle,&p)!=0,"V7 foreground timeline accepted");
 const RECT reserved=waitCanvas(beforeTimeline,[&](const RECT& canvas){return Contains(canvas,first)&&Contains(canvas,last);});
 const auto until=GetTickCount64()+1100;
 while(GetTickCount64()<until){
   Pump(40);RECT current{};
   Expect(inspect(handle,&status)&&!status.failed&&GetWindowRect(SurfaceFor(owner),&current),"timeline remains healthy");
   Expect(EqualRect(&current,&reserved)!=FALSE,"timeline retains its complete presentation canvas through motion");
 }
 Expect(status.frames>beforeTimeline+1,"V7 timeline presents multiple actual frames");

 // Island-style timelines stay at their handed-over screen origin while the
 // owner's ordinary foreground HWND moves independently.
 p.lensOverride=2;QueryPerformanceCounter(&now);p.timelineStart=now.QuadPart+frequency.QuadPart/5;
 const auto beforeAnchor=status.frames;
 Expect(update(handle,&p)!=0,"anchored V7 timeline accepted");
 const RECT anchored=waitCanvas(beforeAnchor,[&](const RECT& canvas){return Contains(canvas,first)&&Contains(canvas,last);});
 Expect(SetWindowPos(owner,nullptr,left+70,top+30,260,190,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
        "move owner independently of anchored material timeline");
 waitCanvas(status.frames,[&](const RECT& canvas){return EqualRect(&canvas,&anchored)!=FALSE;});

 // Use a real shared monitor edge when the machine has one. The owner sits
 // mostly on one display, while the explicit lens and its foreground cross
 // into the other; clipping to MonitorFromWindow(owner) would fail this check.
 bool straddled=false;
 for(const auto& a:screens){
   for(const auto& b:screens){
     if(EqualRect(&a,&b))continue;
     const LONG overlapTop=(std::max)(a.top,b.top),overlapBottom=(std::min)(a.bottom,b.bottom);
     const LONG overlapLeft=(std::max)(a.left,b.left),overlapRight=(std::min)(a.right,b.right);
     const bool vertical=a.right==b.left&&overlapBottom-overlapTop>=280;
     const bool horizontal=a.bottom==b.top&&overlapRight-overlapLeft>=340;
     if(!vertical&&!horizontal)continue;
     const LONG x=vertical?a.right-160:overlapLeft+30;
     const LONG y=vertical?overlapTop+30:a.bottom-120;
     Expect(SetWindowPos(owner,nullptr,x,y,220,160,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"place V7 owner across a physical display edge");
     const float scale=GetDpiForWindow(owner)/96.f;
     p.lensOverride=1;p.timeline=nullptr;p.timelineCount=0;
     p.lensX=p.lensY=p.contentX=p.contentY=0;
     p.lensWidth=p.contentWidth=(vertical?280.f:220.f)/scale;
     p.lensHeight=p.contentHeight=(vertical?160.f:260.f)/scale;
     const RECT across=visiblePart(screenRect(0,0,p.lensWidth,p.lensHeight));
     const auto beforeCross=status.frames;
     Expect(update(handle,&p)!=0,"cross-monitor explicit lens and foreground accepted");
     waitCanvas(beforeCross,[&](const RECT& canvas){return Contains(canvas,across);});
     Expect(!Contains(a,across)&&!Contains(b,across),"cross-monitor fixture actually spans both displays");
     straddled=true;break;
   }
   if(straddled)break;
 }
 if(!straddled)std::cout<<"SKIP V7 cross-monitor edge: no adjacent physical displays\n";
 else std::cout<<"PASS V7 foreground spans adjacent physical displays\n";
 detach(handle);DestroyWindow(owner);
 std::cout<<"PASS V7 explicit lens, foreground, reserved timeline and anchored-owner motion\n";
}
int main(int argc,char**argv){
 wchar_t temp[32768]{};GetTempPathW(32768,temp);
 const auto diagnosticRoot=std::filesystem::path(temp)/(L"lume-material-诊断-"+std::to_wstring(GetCurrentProcessId()));
 std::filesystem::create_directories(diagnosticRoot);
 const auto diagnosticLog=diagnosticRoot/L"material.log";
 wchar_t priorLog[32768]{};const auto priorLength=GetEnvironmentVariableW(L"LUME_MATERIAL_DIAGNOSTIC_LOG",priorLog,32768);
 SetEnvironmentVariableW(L"LUME_MATERIAL_DIAGNOSTIC_LOG",diagnosticLog.c_str());
 Expect(argc==2,"DLL path supplied");HMODULE module=LoadLibraryA(argv[1]);Expect(module!=nullptr,"shared native material module loads");
 const auto attach=Resolve<LumeMaterialAttachFn>(module,"LumeMaterialAttach");
 const auto update=Resolve<LumeMaterialUpdateFn>(module,"LumeMaterialUpdate");
 const auto detach=Resolve<LumeMaterialDetachFn>(module,"LumeMaterialDetach");
 const auto inspect=Resolve<LumeMaterialInspectFn>(module,"LumeMaterialInspect");
 const auto statistics=Resolve<LumeMaterialStatisticsFn>(module,"LumeMaterialStatistics");
 Expect(attach&&update&&detach&&inspect&&statistics,"all ABI symbols present");
 LumeMaterialParameters p{};p.size=sizeof(p);Expect(attach(&p)==0,"invalid HWND rejected");
 HWND background=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Native material isolated verification",WS_POPUP|WS_VISIBLE|SS_CENTER,50,50,800,520,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 HWND owner=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,120,120,380,320,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 HWND owner2=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,510,130,320,320,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 Expect(background&&owner&&owner2,"isolated test windows created");
 SetLayeredWindowAttributes(owner,0,1,LWA_ALPHA);SetLayeredWindowAttributes(owner2,0,1,LWA_ALPHA);ShowWindow(owner,SW_SHOWNOACTIVATE);
 p.owner=owner;p.opticalProfile=1;p.lightForeground=1;
 p.cornerRadius=std::numeric_limits<float>::quiet_NaN();Expect(attach(&p)==0,"invalid corner radius rejected");
 p.cornerRadius=8;
 p.panelCount=2;p.panels[0]={18,18,30,30,-1};p.panels[1]={58,18,100,60,8};
 const auto a=attach(&p);Expect(a!=0,"rounded material with circle and panel attaches");
 p.owner=owner2;p.frosted=1;const auto b=attach(&p);Expect(b!=0,"second material attaches");
 // Hidden prewarming must establish the shell contract before the first show.
 // A full-desktop HWND can otherwise alter secondary taskbar behavior even
 // though the material's actual lens region occupies only a small widget.
 for(int n=0;n<100&&!SurfaceFor(owner2);++n)Pump(20);
 ExpectShellSafeSurface(owner2);
 Expect(!IsWindowVisible(SurfaceFor(owner2)),"prewarmed composition surface remains hidden");
 LumeMaterialStatus hidden{};hidden.size=sizeof(hidden);
 for(int n=0;n<100;++n){
   Pump(20);Expect(inspect(b,&hidden)&&!hidden.failed,"hidden prewarm remains healthy");
   if(hidden.frameReady&&hidden.frames>=2)break;
 }
 Expect(hidden.frameReady&&hidden.frames>=2,"hidden prewarm completes its warm frames");
 const auto hiddenFrames=hidden.frames;
 for(int n=0;n<20;++n){
   SetWindowTextW(background,n%2?L"Hidden material background A":L"Hidden material background B");
   RedrawWindow(background,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
   Pump(20);
 }
 Expect(inspect(b,&hidden)&&!hidden.failed&&hidden.frames==hiddenFrames,
        "hidden prewarm stops presenting while its background changes");
 p.cornerRadius=10;
 Expect(update(b,&p)!=0,"hidden material accepts an explicit prewarm update");
 for(int n=0;n<100;++n){
   Pump(20);Expect(inspect(b,&hidden)&&!hidden.failed,"inspect updated hidden prewarm");
   if(hidden.frameReady&&hidden.frames>hiddenFrames)break;
 }
 Expect(hidden.frameReady&&hidden.frames>hiddenFrames&&!IsWindowVisible(SurfaceFor(owner2)),
        "explicit hidden updates prepare a fresh frame without showing the surface");
 Pump(100);Expect(inspect(b,&hidden)&&!hidden.failed,"hidden update settles");
 const auto updatedHiddenFrames=hidden.frames;
 Pump(300);
 Expect(inspect(b,&hidden)&&!hidden.failed&&hidden.frames==updatedHiddenFrames,
        "updated hidden prewarm resumes idling after its explicit submission");
 ShowWindow(owner2,SW_SHOWNOACTIVATE);
 LumeMaterialStatus sa{},sb{};sa.size=sizeof(sa);sb.size=sizeof(sb);
 for(int n=0;n<100;++n){Pump(50);Expect(inspect(a,&sa)&&inspect(b,&sb),"inspect active handles");if(sa.failed||sb.failed||(sa.frameReady&&sb.frameReady&&sb.frames>updatedHiddenFrames))break;}
 Expect(sb.frameReady&&!sb.failed&&sb.frames>updatedHiddenFrames&&IsWindowVisible(SurfaceFor(owner2)),
        "showing a prewarmed owner resumes visible material presentation");
 std::cout<<"liquid frames="<<sa.frames<<" ready="<<sa.frameReady<<" hr="<<std::hex<<sa.lastError<<std::dec<<"; frosted frames="<<sb.frames<<" ready="<<sb.frameReady<<" hr="<<std::hex<<sb.lastError<<std::dec<<'\n';
 LumeMaterialStatisticsData s{};s.size=sizeof(s);Expect(statistics(&s)&&s.users==2&&s.devices==1,"two windows share one GPU device");
 Expect(sa.frameReady&&sb.frameReady&&sa.frames&&sb.frames,"liquid and frosted present actual captured frames");
 ExpectShellSafeSurface(owner);ExpectShellSafeSurface(owner2);
 // Ordinary foreground coverage pauses work without rebuilding the surface
 // or capture leases, so minimizing the covering application cannot flash.
 const HWND coveredSurface=SurfaceFor(owner2);
 Expect(SetPropW(owner2,lume::window_ids::ContentOccludedProp,reinterpret_cast<HANDLE>(1))!=FALSE,
        "publish complete desktop coverage");
 PostMessageW(coveredSurface,WM_NULL,0,0);Pump(100);
 Expect(inspect(b,&sb)&&!sb.failed,"covered material remains healthy");
 const auto coveredFrames=sb.frames;
 for(int n=0;n<20;++n){
   SetWindowTextW(background,n%2?L"Covered background A":L"Covered background B");
   RedrawWindow(background,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);Pump(20);
 }
 Expect(inspect(b,&sb)&&sb.frames==coveredFrames&&SurfaceFor(owner2)==coveredSurface&&IsWindowVisible(coveredSurface),
        "covered background changes do not present or replace committed material");
 RemovePropW(owner2,lume::window_ids::ContentOccludedProp);PostMessageW(coveredSurface,WM_NULL,0,0);
 p.owner=owner2;Expect(update(b,&p)!=0,"uncovered material accepts fresh content");
 for(int n=0;n<100;++n){Pump(20);Expect(inspect(b,&sb)&&!sb.failed,"uncovered material stays healthy");if(sb.frames>coveredFrames)break;}
 Expect(sb.frames>coveredFrames&&SurfaceFor(owner2)==coveredSurface,"uncovered material resumes on the same surface");
 const auto timingReads=[&]{
   std::ifstream file(diagnosticLog,std::ios::binary);
   const std::string text{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
   unsigned count=0;std::size_t at=0;
   while((at=text.find("DisplayTimingReady",at))!=std::string::npos){++count;at+=18;}
   return count;
 };
 // Deliver only to our own material HWND: exercise the real worker's
 // same-monitor invalidation without changing the user's display mode.
 for(const auto notification:{WM_DISPLAYCHANGE,WM_SETTINGCHANGE,WM_POWERBROADCAST}){
   const auto before=timingReads();const auto beforeFrames=sb.frames;
   const HWND surface=SurfaceFor(owner2);
   // WM_SETTINGCHANGE is a system pointer message and cannot use PostMessage.
   DWORD_PTR response=0;
   Expect(SendMessageTimeoutW(surface,notification,notification==WM_POWERBROADCAST?PBT_APMRESUMEAUTOMATIC:0,0,
          SMTO_ABORTIFHUNG|SMTO_ERRORONEXIT,1000,&response)!=0,"deliver display notification to the material worker");
   for(int n=0;n<100;++n){
     Pump(20);Expect(inspect(b,&sb)&&!sb.failed,"display notification retains healthy material");
     if(timingReads()>before&&sb.frames>beforeFrames)break;
   }
   Expect(timingReads()>before,"same-monitor display or resume notification refreshes the real output timing");
   Expect(SurfaceFor(owner2)==surface&&sb.frameReady&&sb.frames>beforeFrames,
          "timing refresh presents again without recreating a healthy composition surface");
   Expect(statistics(&s)&&s.users==2&&s.devices==1,"display timing refresh preserves the shared graphics device");
 }
 std::cout<<"PASS same-monitor display, settings and resume timing invalidation\n";
 // Layer policy can suppress a still-visible owner before its UI hides it.
 // Retire once, then await restoration instead of repeatedly rebuilding.
 Expect(SetPropW(owner2,lume::window_ids::LayerSuppressedProp,reinterpret_cast<HANDLE>(1))!=FALSE,
        "suppress a visible owner without changing its HWND visibility");
 for(int n=0;n<100&&SurfaceFor(owner2);++n)Pump(20);
 Expect(IsWindowVisible(owner2)&&!SurfaceFor(owner2),"suppression retires the material even before owner hide");
 Expect(inspect(b,&sb)&&!sb.failed,"suppression is not reported as a renderer failure");
 const auto suppressedFrames=sb.frames;
 for(int n=0;n<40;++n){
   Pump(20);
   Expect(!SurfaceFor(owner2)&&inspect(b,&sb)&&!sb.failed&&sb.frames==suppressedFrames,
          "suppressed visible owner does not rebuild or submit hidden frames");
 }
 RemovePropW(owner2,lume::window_ids::LayerSuppressedProp);
 for(int n=0;n<100;++n){
   Pump(20);Expect(inspect(b,&sb),"inspect material after suppression clears");
   if(sb.failed||(sb.frameReady&&sb.frames>suppressedFrames&&IsWindowVisible(SurfaceFor(owner2))))break;
 }
 Expect(!sb.failed&&sb.frameReady&&sb.frames>suppressedFrames&&IsWindowVisible(SurfaceFor(owner2)),
        "clearing suppression resumes the same material handle");
 std::cout<<"PASS visible-owner suppression idles and restores\n";
 for(int cycle=0;cycle<2;++cycle){
   ShowWindow(owner2,SW_HIDE);
   for(int n=0;n<100&&SurfaceFor(owner2);++n)Pump(20);
   Expect(!SurfaceFor(owner2),"hiding a shown owner retires its composition surface");
   Expect(inspect(b,&sb)&&!sb.failed,"hidden material handle remains healthy");
   const auto beforeReopen=sb.frames;
   ShowWindow(owner2,SW_SHOWNOACTIVATE);
   for(int n=0;n<100;++n){
     Pump(20);Expect(inspect(b,&sb),"inspect reopened material");
     if(sb.failed||(sb.frameReady&&sb.frames>beforeReopen&&IsWindowVisible(SurfaceFor(owner2))))break;
   }
   Expect(!sb.failed&&sb.frameReady&&sb.frames>beforeReopen&&IsWindowVisible(SurfaceFor(owner2)),
          "closing and reopening presents a fresh visible material surface");
 }
 // Exercise physical screen coordinates, including a negative-origin monitor
 // when available, without assuming where the primary display is arranged.
 RECT original{};Expect(GetWindowRect(owner2,&original)!=FALSE,"save material owner bounds");
 std::vector<RECT> monitorBounds;
 EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR,HDC,LPRECT bounds,LPARAM data)->BOOL{
   reinterpret_cast<std::vector<RECT>*>(data)->push_back(*bounds);return TRUE;
 },reinterpret_cast<LPARAM>(&monitorBounds));
 for(const auto& screen:monitorBounds){
   const int width=(std::min)(320L,screen.right-screen.left-40);
   const int height=(std::min)(280L,screen.bottom-screen.top-40);
   if(width<=0||height<=0)continue;
   const auto before=sb.frames;
   Expect(SetWindowPos(owner2,nullptr,screen.left+20,screen.top+20,width,height,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
          "move and resize material owner on each physical screen");
   bool placed=false;
   for(int n=0;n<100;++n){
     Pump(20);Expect(inspect(b,&sb),"inspect material during canvas move");
     RECT canvas{};
     if(GetWindowRect(SurfaceFor(owner2),&canvas)&&canvas.left<=screen.left+20&&canvas.top<=screen.top+20&&
        canvas.right>=screen.left+20+width&&canvas.bottom>=screen.top+20+height&&sb.frames>before){placed=true;break;}
   }
   Expect(placed&&!sb.failed&&sb.frameReady,"bounded canvas moves without replacing or losing its material");
   ExpectShellSafeSurface(owner2,p.bottomInset);
 }
 Expect(SetWindowPos(owner2,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,
        SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"restore material owner after monitor moves");
 Pump(200);ExpectShellSafeSurface(owner2);
 ExplicitCanvasScenario(attach,update,detach,inspect,monitorBounds);
 const auto beforePanel=sb.frames;p.frosted=2;p.panelCount=0;
 Expect(update(b,&p)!=0,"detached original frosted panel update");
 for(int n=0;n<100;++n){Pump(20);Expect(inspect(b,&sb),"inspect detached panel");if(sb.failed||sb.frames>beforePanel)break;}
 Expect(!sb.failed&&sb.frameReady&&sb.frames>beforePanel,"detached original panel presents a new captured frame");
 p.frosted=0;Expect(update(b,&p)!=0,"live material update");Pump(80);
 p.size=LumeMaterialParametersV1Size;Expect(update(b,&p)!=0,"previous ABI remains supported");
 p.size=LumeMaterialParametersV2Size;Expect(update(b,&p)!=0,"corner-radius ABI remains supported");
 p.size=LumeMaterialParametersV3Size;Expect(update(b,&p)!=0,"scene-anchor ABI remains supported");
 p.size=LumeMaterialParametersV4Size;Expect(update(b,&p)!=0,"body-inset ABI remains supported");
 p.size=LumeMaterialParametersV5Size;p.innerRadius=76;Expect(update(b,&p)!=0,"annulus ABI remains supported");p.innerRadius=0;
 p.size=sizeof(p);p.innerRadius=std::numeric_limits<double>::quiet_NaN();
 Expect(update(b,&p)==0,"invalid annulus radius rejected");
 p.innerRadius=-1;Expect(update(b,&p)==0,"negative annulus radius rejected");
 p.innerRadius=76;Expect(update(b,&p)!=0,"annulus material update accepted");p.innerRadius=0;
 p.opacityEnabled=1;p.opacity=std::numeric_limits<float>::quiet_NaN();Expect(update(b,&p)==0,"nonfinite material opacity rejected");
 p.opacity=-.1f;Expect(update(b,&p)==0,"negative material opacity rejected");
 p.opacity=1.1f;Expect(update(b,&p)==0,"out of range material opacity rejected");
 for(float opacity:{0.f,.5f,1.f}){p.opacity=opacity;Expect(update(b,&p)!=0,"animated material opacity accepted");}
 p.opacityEnabled=0;p.opacity=0;Expect(update(b,&p)!=0,"existing widgets keep default opaque presentation");
 p.size=sizeof(p);p.sceneAnchor=owner;Expect(update(b,&p)!=0,"panel can share the calendar source stack");p.sceneAnchor=nullptr;
 p.size=sizeof(p);p.cornerRadius=-1;Expect(update(b,&p)==0,"negative corner radius rejected");p.cornerRadius=8;
 p.bottomInset=4097;Expect(update(b,&p)==0,"unbounded material inset rejected");
 p.bottomInset=0;Expect(update(b,&p)!=0,"full material body restored");Pump(100);
 Expect(inspect(b,&sb)&&sb.surfacePixels>0,"full body surface size available");
 const auto fullSurface=sb.surfacePixels;
 p.bottomInset=144;Expect(update(b,&p)!=0,"reserve transparent foreground below body");
 for(int n=0;n<100;++n){Pump(20);Expect(inspect(b,&sb),"inspect constrained body");if(sb.failed||sb.surfacePixels<fullSurface)break;}
 Expect(!sb.failed&&sb.frameReady&&sb.surfacePixels<fullSurface,
        "glass surface and capture shrink independently of the foreground HWND");
 RECT ownerBounds{};GetClientRect(owner2,&ownerBounds);Expect(ownerBounds.bottom==320,"body inset does not resize foreground window");
 detach(a);Expect(statistics(&s)&&s.users==1&&s.devices==1,"first detach preserves second user");
 detach(b);Expect(material_test::Released(statistics,s,Pump),"last detach releases GPU and captures");
 // Keep the DLL loaded across complete worker shutdowns. A temporary COM
 // client (such as text rasterization) must not leave stale WinRT factories
 // for the next material window after its apartment is torn down.
 for(int cycle=0;cycle<3;++cycle){
   Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"temporary rendering apartment initializes");
   CoUninitialize();
   const auto restarted=attach(&p);Expect(restarted!=0,"material restarts with DLL still loaded");
   LumeMaterialStatus status{};status.size=sizeof(status);
   for(int n=0;n<100;++n){Pump(50);Expect(inspect(restarted,&status),"inspect restarted material");if(status.failed||status.frameReady)break;}
   Expect(!status.failed&&status.frameReady&&status.frames>0,"restarted material presents after apartment shutdown");
   ExpectShellSafeSurface(owner2,p.bottomInset);
   detach(restarted);
   Expect(material_test::Released(statistics,s,Pump),"restarted material releases all resources");
 }
 DestroyWindow(owner);DestroyWindow(owner2);DestroyWindow(background);FreeLibrary(module);
 // The cleanup worker pins its code for process lifetime, including if an OS
 // Close never returns. Reacquiring a lease must still work after all users stop.
 module=LoadLibraryA(argv[1]);Expect(module!=nullptr,"material DLL remains loadable after final detach");FreeLibrary(module);
 {
   lume::platform::windows::WidgetMaterialRuntime rejected;
   LumeMaterialParameters invalid{};invalid.size=sizeof(invalid);
   Expect(!rejected.Attach(invalid)&&rejected.State()==lume::platform::windows::WidgetMaterialState::TransparentFallback,
          "invalid owner remains a visible fallback failure rather than a fake ready material");
   const auto failure=rejected.Status();Expect(failure.failed&&failure.lastError<0,"material failure preserves its diagnostic HRESULT");
   std::ifstream input(diagnosticLog,std::ios::binary);
   const std::string log{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
   Expect(log.find("D3D11CreateDevice hr=0x00000000")!=std::string::npos,
          "real DLL initialization stages reach the Unicode diagnostic path without debug environment setup");
   Expect(log.find("MaterialLoadFailure hr=0x")!=std::string::npos&&log.find("loadError=")!=std::string::npos,
          "runtime loader or rejected attachment error reaches the material diagnostic log");
   const auto bytes=std::filesystem::file_size(diagnosticLog);
   for(int n=0;n<100;++n)rejected.Status();
   Expect(std::filesystem::file_size(diagnosticLog)==bytes,"unchanged fallback state does not log every polling frame");
   Expect(log.find("T ")==std::string::npos,"persistent diagnostics do not enable per-frame timing");
 }
 SetEnvironmentVariableW(L"LUME_MATERIAL_DIAGNOSTIC_LOG",priorLength&&priorLength<32768?priorLog:nullptr);
 std::filesystem::remove_all(diagnosticRoot);
 Expect(statusEvents>0,"material state changes notify the owner without polling");std::cout<<"PASS shared material lifecycle and native presentation\n";
}
