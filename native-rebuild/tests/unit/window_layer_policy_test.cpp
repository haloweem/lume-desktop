#include "platform/windows/widget_ui/desktop_level.h"
#include <cstdio>
#include <thread>
using namespace lume::platform::windows;
namespace {
int failures=0;
void Check(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL: %s\n",why);++failures;}}
LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
HWND Make(const wchar_t* name,RECT r,DWORD ex=WS_EX_TOOLWINDOW,HWND owner=nullptr,DWORD style=WS_POPUP|WS_VISIBLE){
    WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=Proc;c.lpszClassName=name;RegisterClassW(&c);
    return CreateWindowExW(ex,name,L"Layer policy fixture",style,r.left,r.top,r.right-r.left,r.bottom-r.top,owner,nullptr,c.hInstance,nullptr);
}
void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
void Scenario(){
    using namespace window_layer;
    Context next;
    contextReader=[&]{return next;};
    HWND a=Make(L"LumeNativeDesktopPolicyA",{30,30,230,200});
    HWND b=Make(L"LumeNativeDesktopPolicyB",{1030,30,1230,200});
    HWND cross=Make(L"LumeNativeDesktopPolicyCross",{700,30,1100,200});
    HWND popup=Make(L"LumePolicyPopup",{40,40,180,160},WS_EX_TOOLWINDOW|WS_EX_TOPMOST,a);
    HWND editor=Make(L"LumePolicyEditor",{50,50,190,170},WS_EX_TOOLWINDOW|WS_EX_TOPMOST,a);
    HWND companion=Make(L"LumePolicyDisplay",{30,30,230,200});
    SetPropW(companion,lume::window_ids::DesktopCompanionOwnerProp,a);
    Check(a&&b&&cross&&popup&&editor&&companion,"create isolated window families");
    Check(Register(a,Kind::Desktop)&&Register(b,Kind::Desktop)&&Register(cross,Kind::Desktop)&&
          Register(popup,Kind::Popup,a)&&Register(editor,Kind::Editor,a)&&Register(companion,Kind::Desktop,a),"register all family members");
    Check(!(GetWindowLongPtrW(popup,GWL_EXSTYLE)&WS_EX_TOPMOST)&&!(GetWindowLongPtrW(editor,GWL_EXSTYLE)&WS_EX_TOPMOST),
          "transient windows do not retain global topmost");
    Check(Covers({0,0,800,600},{0,0,800,600})&&!Covers({0,0,800,560},{0,0,800,600}),"borderless fullscreen differs from work-area maximize");
    Start();next.fullscreen={{0,0,800,600}};next.foreignForeground=true;Update(next);Pump();
    Check(!IsWindowVisible(a)&&!IsWindowVisible(cross)&&IsWindowVisible(b),"fullscreen protects its display and crossing windows only");
    SetWindowPos(b,nullptr,60,50,200,170,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);Pump();
    Check(!IsWindowVisible(b),"moving into a protected display is checked against the new bounds");
    SetWindowPos(b,nullptr,1030,30,200,170,SWP_NOZORDER|SWP_NOACTIVATE);Refresh();Pump();
    Check(!IsWindowVisible(popup)&&!IsWindowVisible(editor)&&!IsWindowVisible(companion),"all attached visuals and dialogs leave together");
    ShowWindow(a,SW_SHOWNOACTIVATE);ShowWindow(popup,SW_SHOWNOACTIVATE);Pump();
    Check(!IsWindowVisible(a)&&!IsWindowVisible(popup),"late show cannot bypass active suppression");
    HWND cold=Make(L"LumeNativeDesktopCold",{30,40,240,230},WS_EX_TOOLWINDOW,nullptr,WS_POPUP);
    Register(cold,Kind::Desktop);desktop_level::Place(cold,true);Pump();
    Check(!IsWindowVisible(cold)&&Suppressed(cold),"component enabled during fullscreen remains hidden");
    next.fullscreen.clear();next.foreignForeground=false;Update(next);Pump();
    Check(IsWindowVisible(a)&&IsWindowVisible(b)&&IsWindowVisible(cross)&&IsWindowVisible(editor)&&IsWindowVisible(companion),"ordinary windows and editor resume their prior display intent");
    Check(!IsWindowVisible(popup),"expired popup does not reopen after fullscreen");
    Check(IsWindowVisible(cold),"component enabled during fullscreen appears when the display is available");
    DestroyWindow(cold);
    ShowWindow(a,SW_HIDE);next.allDisplays=true;Update(next);Pump();next.allDisplays=false;Update(next);Pump();
    Check(!IsWindowVisible(a)&&IsWindowVisible(b),"user-hidden component stays hidden after a global exclusive interval");
    const HWND doomed=Make(L"LumePolicyDisposable",{20,20,80,80});Register(doomed,Kind::Desktop);
    DestroyWindow(doomed);Check(!windows.count(doomed),"destroy unregisters window generation");
    HWND island=Make(L"LumePolicyIsland",{30,30,230,90},WS_EX_TOOLWINDOW|WS_EX_TOPMOST);
    HWND ordinary=Make(L"LumePolicyIslandOrdinaryApp",{0,0,800,560},0);
    HWND islandPopup=Make(L"LumePolicyIslandMenu",{40,45,180,150},WS_EX_TOOLWINDOW|WS_EX_TOPMOST,island);
    Register(island,Kind::Island,nullptr,true,true);Register(islandPopup,Kind::Popup,island);
    SetWindowPos(island,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    Check((GetWindowLongPtrW(island,GWL_EXSTYLE)&WS_EX_TOPMOST)&&
          (GetWindowLongPtrW(islandPopup,GWL_EXSTYLE)&WS_EX_TOPMOST),"explicit island and its menu may overlay ordinary applications");
    Check(desktop_level::Above(island,ordinary),"explicit island is actually above the ordinary application in Z order");
    next.fullscreen={{0,0,800,600}};next.foreignForeground=true;Update(next);Pump();
    Check(!IsWindowVisible(island)&&!IsWindowVisible(islandPopup),"island defaults to hiding its whole family in fullscreen");
    ConfigureIsland(island,true,false);Pump();
    Check(IsWindowVisible(island)&&!Suppressed(island)&&(GetWindowLongPtrW(island,GWL_EXSTYLE)&WS_EX_TOPMOST),
          "island fullscreen option can permit an ordinary fullscreen application");
    Check(!IsWindowVisible(cross)&&Suppressed(cross),"island choice never disables other components' fullscreen protection");
    ShowWindow(islandPopup,SW_SHOWNOACTIVATE);Pump();
    Check(IsWindowVisible(islandPopup),"new island popup inherits the explicit fullscreen choice");
    next.exclusive=true;Update(next);Pump();
    ShowWindow(island,SW_SHOWNOACTIVATE);Pump();
    Check(!IsWindowVisible(island)&&!IsWindowVisible(islandPopup),"exclusive game protection overrides island settings and late shows");
    next.exclusive=false;next.fullscreen.clear();next.foreignForeground=false;Update(next);Pump();
    Check(IsWindowVisible(island)&&!IsWindowVisible(islandPopup),"island restores without reviving an expired menu");
    next.allDisplays=true;Update(next);Pump();
    Check(!IsWindowVisible(island),"unlocated exclusive or presentation protection still hides the island");
    next.allDisplays=false;Update(next);Pump();
    DestroyWindow(islandPopup);DestroyWindow(island);DestroyWindow(ordinary);
    Stop();contextReader={};
    for(HWND h:{popup,editor,companion,a,b,cross})DestroyWindow(h);

    HWND host=Make(L"LumePolicyDesktop",{0,0,800,600});
    HWND view=Make(L"SHELLDLL_DefView",{0,0,1,1},0,host,WS_CHILD|WS_VISIBLE);
    HWND app=Make(L"LumePolicyOrdinaryApp",{0,0,600,500},0);
    HWND widget=Make(L"LumeNativeDesktopPolicyWidget",{30,30,230,200});
    constexpr UINT flags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER;
    SetWindowPos(host,HWND_BOTTOM,0,0,0,0,flags);
    desktop_level::foregroundDesktop=nullptr;
    desktop_level::Place(widget,true);
    desktop_level::Select(widget);
    Check(!desktop_level::Above(widget,app),"click cannot lift a desktop widget above an application");
    SetWindowPos(app,HWND_BOTTOM,0,0,0,0,flags);
    desktop_level::Watcher::ForegroundChanged(app);desktop_level::Place(widget,false);
    Check(!desktop_level::Above(widget,app),"application below a raised desktop remains a ceiling");
    for(HWND h:{widget,app,view,host})DestroyWindow(h);
}
}
int main(){
    wchar_t name[80]{};swprintf_s(name,L"LumeLayerPolicy-%lu",GetCurrentProcessId());
    HDESK desktop=CreateDesktopW(name,nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop)return 2;
    std::thread test([&]{if(!SetThreadDesktop(desktop)){++failures;return;}Scenario();});test.join();CloseDesktop(desktop);
    if(!failures)std::puts("PASS isolated display policy: fullscreen, multimonitor, families, late callbacks and application ceilings");
    return failures?1:0;
}
