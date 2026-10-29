#include "platform/windows/material/material_api.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <objbase.h>
template<class T> T Resolve(HMODULE module,const char* name){const FARPROC address=GetProcAddress(module,name);T result=nullptr;static_assert(sizeof(result)==sizeof(address));std::memcpy(&result,&address,sizeof(result));return result;}
void Expect(bool b,const char* text){if(!b){std::cerr<<"FAIL: "<<text<<" error="<<GetLastError()<<'\n';std::exit(1);}}
unsigned statusEvents=0;
void Pump(DWORD ms){const auto end=GetTickCount64()+ms;while(GetTickCount64()<end){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==LumeMaterialStatusChangedMessage)++statusEvents;TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}}
int main(int argc,char**argv){
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
 SetLayeredWindowAttributes(owner,0,1,LWA_ALPHA);SetLayeredWindowAttributes(owner2,0,1,LWA_ALPHA);ShowWindow(owner,SW_SHOWNOACTIVATE);ShowWindow(owner2,SW_SHOWNOACTIVATE);
 p.owner=owner;p.opticalProfile=1;p.lightForeground=1;
 p.cornerRadius=std::numeric_limits<float>::quiet_NaN();Expect(attach(&p)==0,"invalid corner radius rejected");
 p.cornerRadius=8;
 p.panelCount=2;p.panels[0]={18,18,30,30,-1};p.panels[1]={58,18,100,60,8};
 const auto a=attach(&p);Expect(a!=0,"rounded material with circle and panel attaches");
 p.owner=owner2;p.frosted=1;const auto b=attach(&p);Expect(b!=0,"second material attaches");
 LumeMaterialStatus sa{},sb{};sa.size=sizeof(sa);sb.size=sizeof(sb);
 for(int n=0;n<100;++n){Pump(50);Expect(inspect(a,&sa)&&inspect(b,&sb),"inspect active handles");if(sa.failed||sb.failed||(sa.frameReady&&sb.frameReady))break;}
 std::cout<<"liquid frames="<<sa.frames<<" ready="<<sa.frameReady<<" hr="<<std::hex<<sa.lastError<<std::dec<<"; frosted frames="<<sb.frames<<" ready="<<sb.frameReady<<" hr="<<std::hex<<sb.lastError<<std::dec<<'\n';
 LumeMaterialStatisticsData s{};s.size=sizeof(s);Expect(statistics(&s)&&s.users==2&&s.devices==1,"two windows share one GPU device");
 Expect(sa.frameReady&&sb.frameReady&&sa.frames&&sb.frames,"liquid and frosted present actual captured frames");
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
 detach(b);Expect(statistics(&s)&&s.users==0&&s.devices==0&&s.captures==0,"last detach releases GPU and captures");
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
   detach(restarted);
   Expect(statistics(&s)&&s.users==0&&s.devices==0&&s.captures==0,"restarted material releases all resources");
 }
 DestroyWindow(owner);DestroyWindow(owner2);DestroyWindow(background);FreeLibrary(module);
 module=LoadLibraryA(argv[1]);Expect(module!=nullptr,"DLL reload after final detach");FreeLibrary(module);
 Expect(statusEvents>0,"material state changes notify the owner without polling");std::cout<<"PASS shared material lifecycle and native presentation\n";
}
