#include "platform/windows/widget_ui/desktop_level.h"
#include <cstdio>
#include <atomic>
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
DWORD peekAttribute=0,peekBytes=0;BOOL peekValue=FALSE;bool failPeekAttribute=false;
HRESULT WINAPI PeekAttribute(HWND,DWORD attribute,LPCVOID value,DWORD bytes){
    peekAttribute=attribute;peekBytes=bytes;
    if(value&&bytes==sizeof(BOOL))peekValue=*static_cast<const BOOL*>(value);
    return failPeekAttribute?E_FAIL:S_OK;
}
void DesktopPeekScenario(){
    using namespace window_layer;
    HWND desktop=Make(L"LumeNativeDesktopPeekRoot",{30,30,230,200});
    // Exercise the DWM contract through an injected setter: do not depend on
    // DwmGetWindowAttribute supporting an attribute documented for setting.
    Check(desktop_peek::SetExcluded(desktop,true,PeekAttribute)&&desktop_peek::Excluded(desktop),
          "successful desktop peek exclusion publishes its companion inheritance state");
    Check(peekAttribute==DWMWA_EXCLUDED_FROM_PEEK&&peekBytes==sizeof(BOOL)&&peekValue==TRUE,
          "desktop exclusion uses EXCLUDED_FROM_PEEK rather than disabling application previews");
    failPeekAttribute=true;
    Check(!desktop_peek::SetExcluded(desktop,false,PeekAttribute)&&desktop_peek::Excluded(desktop),
          "failed DWM changes never publish a false inherited state");
    failPeekAttribute=false;
    Check(desktop_peek::SetExcluded(desktop,false,PeekAttribute)&&!desktop_peek::Excluded(desktop)&&peekValue==FALSE,
          "releasing peek exclusion restores default behavior");
    HWND bar=Make(L"LumeNativeDesktopPeekBar",{30,200,230,240});
    HWND island=Make(L"LumePeekIsland",{250,30,450,90});
    HWND popup=Make(L"LumePeekTransient",{40,40,180,160},WS_EX_TOOLWINDOW,desktop);
    HWND editor=Make(L"LumePeekEditor",{50,50,190,170},WS_EX_TOOLWINDOW,desktop);
    HWND tool=Make(L"LumePeekTool",{40,40,180,160});
    HWND manager=Make(lume::window_ids::ManagerClass,{0,0,600,500},0);
    HWND glass=Make(L"LumePeekGlass",{30,30,230,200});
    HWND managerGlass=Make(L"LumePeekManagerGlass",{0,0,600,500});
    Check(Register(desktop,Kind::Desktop)&&Register(bar,Kind::Desktop,desktop)&&
          Register(island,Kind::Island,nullptr,true)&&Register(popup,Kind::Popup,desktop)&&
          Register(editor,Kind::Editor,desktop)&&Register(tool,Kind::ExplicitTool),"register desktop and transient peek fixtures");
    Check(desktop_peek::Excluded(desktop)&&desktop_peek::Excluded(bar)&&desktop_peek::Excluded(island),
          "resident Dock layers and islands remain visible during another application's peek");
    Check(!desktop_peek::Excluded(popup)&&!desktop_peek::Excluded(editor)&&!desktop_peek::Excluded(tool)&&
          !desktop_peek::Excluded(manager),"menus editors tools and manager retain normal Aero Peek behavior");
    Check(desktop_peek::SyncFromOwner(glass,bar)&&desktop_peek::Excluded(glass),
          "worker-owned desktop material inherits the desktop exclusion");
    Check(desktop_peek::SyncFromOwner(managerGlass,manager)&&!desktop_peek::Excluded(managerGlass),
          "management window material retains ordinary application preview behavior");
    Check(desktop_peek::SyncFromOwner(glass,manager)&&!desktop_peek::Excluded(glass),
          "material exclusion follows its current owner without retaining a stale desktop role");
    Context next;next.foreignForeground=true;next.fullscreen={{0,0,800,600}};
    contextReader=[&]{return next;};Start();Pump();
    Check(!IsWindowVisible(desktop)&&!IsWindowVisible(bar)&&!IsWindowVisible(island)&&desktop_peek::Excluded(desktop),
          "peek exclusion never bypasses genuine fullscreen suppression");
    Stop();contextReader={};
    for(HWND window:{managerGlass,glass,manager,tool,editor,popup,island,bar,desktop})DestroyWindow(window);
}
struct BlockedGlassFixture { HWND owner=nullptr; std::atomic<unsigned> requests{0}; };
LRESULT CALLBACK BlockedGlassProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_NCCREATE)
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
    if(message==lume::window_ids::GlassRestackMessage){
        auto* fixture=reinterpret_cast<BlockedGlassFixture*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        RemovePropW(window,lume::window_ids::GlassRestackPendingProp);
        ++fixture->requests;
        HWND anchor=GetWindow(fixture->owner,GW_HWNDNEXT);
        if(GetPropW(anchor,lume::window_ids::DesktopCompanionOwnerProp)!=fixture->owner)anchor=fixture->owner;
        SetWindowPos(window,anchor,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
        return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
void BlockedMaterialRestackScenario(){
    HWND owner=Make(L"LumeNativeDesktopBlockedMaterialOwner",{40,40,240,200});
    HWND display=Make(L"LumeBlockedMaterialDisplay",{40,40,240,200});
    SetPropW(display,lume::window_ids::DesktopCompanionOwnerProp,owner);
    WNDCLASSW type{};type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=BlockedGlassProc;
    type.lpszClassName=L"LumeBlockedMaterialWorker";RegisterClassW(&type);
    const HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr),
        stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Check(ready&&release&&stop,"create blocked material test signals");
    if(!ready||!release||!stop){
        if(ready)CloseHandle(ready);if(release)CloseHandle(release);if(stop)CloseHandle(stop);
        DestroyWindow(display);DestroyWindow(owner);return;
    }
    const HDESK desktop=GetThreadDesktop(GetCurrentThreadId());
    BlockedGlassFixture fixture;fixture.owner=owner;
    std::atomic<HWND> glass{nullptr};
    std::thread worker([&]{
        if(!SetThreadDesktop(desktop)){SetEvent(ready);return;}
        const HWND surface=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Blocked material fixture",WS_POPUP|WS_VISIBLE,
            40,40,200,160,nullptr,nullptr,type.hInstance,&fixture);
        if(surface)SetPropW(surface,lume::window_ids::GlassSurfaceOwnerProp,owner);
        glass=surface;SetEvent(ready);
        // Model a renderer busy inside a WGC RPC: it owns a visible HWND but
        // cannot dispatch window messages until its independent work returns.
        WaitForSingleObject(release,INFINITE);
        while(WaitForSingleObject(stop,0)!=WAIT_OBJECT_0){
            MsgWaitForMultipleObjectsEx(1,&stop,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);Pump();
        }
        if(surface)DestroyWindow(surface);
    });
    WaitForSingleObject(ready,2000);Check(glass.load()!=nullptr,"worker owns an isolated material surface");
    // The watchdog makes the unfixed synchronous implementation fail a bounded
    // timing assertion instead of hanging the whole suite indefinitely.
    std::thread watchdog([&]{WaitForSingleObject(release,700);SetEvent(release);});
    const auto started=GetTickCount64();
    for(int n=0;n<20;++n)desktop_level::KeepOwnedGlassBelow(owner);
    const auto elapsed=GetTickCount64()-started;
    Check(elapsed<250,"desktop restacks never wait for a blocked material worker");
    Check(fixture.requests==0&&GetPropW(glass.load(),lume::window_ids::GlassRestackPendingProp),
          "repeated restacks coalesce while the material worker cannot pump messages");
    SetEvent(release);
    const auto deadline=GetTickCount64()+2000;
    while(GetTickCount64()<deadline&&
          (GetWindow(owner,GW_HWNDNEXT)!=display||GetWindow(display,GW_HWNDNEXT)!=glass.load()||fixture.requests==0)){
        Pump();Sleep(2);
    }
    Check(fixture.requests==1,"one queued material request handles a burst of desktop events");
    Check(GetWindow(owner,GW_HWNDNEXT)==display&&GetWindow(display,GW_HWNDNEXT)==glass.load(),
          "resumed material restores owner display glass family order");
    SetEvent(stop);
    while(WaitForSingleObject(worker.native_handle(),0)!=WAIT_OBJECT_0){Pump();Sleep(2);}
    worker.join();watchdog.join();
    CloseHandle(stop);CloseHandle(release);CloseHandle(ready);
    DestroyWindow(display);DestroyWindow(owner);
}
void ShellTransitionScenario(){
    using namespace window_layer;
    Context next;
    contextReader=[&]{return next;};
    HWND primary=Make(L"LumeNativeDesktopShellPrimary",{30,30,230,200});
    HWND secondary=Make(L"LumeNativeDesktopShellSecondary",{1030,30,1230,200});
    Check(Register(primary,Kind::Desktop)&&Register(secondary,Kind::Desktop),"register shell transition desktop fixtures");
    unsigned hides=0;
    observer=[&](HWND,Kind,bool blocked){if(blocked)++hides;};
    Start();
    // Model the foreground facts gathered by Capture, then run its production
    // resolver and the actual HWND suppression path. Shell foregrounds can
    // have a full-monitor rectangle or no rectangle while QUNS remains stale.
    for(const auto notification:{QUNS_ACCEPTS_NOTIFICATIONS,QUNS_RUNNING_D3D_FULL_SCREEN,QUNS_PRESENTATION_MODE}){
        for(const bool covering:{false,true}){
            next={};next.foreignForeground=true;
            if(covering)next.fullscreen={{0,0,800,600},{800,0,1600,600}};
            ApplyForegroundProtection(next,true,notification);Update(next);Pump();
            Check(IsWindowVisible(primary)&&IsWindowVisible(secondary)&&!Suppressed(primary)&&!Suppressed(secondary),
                  "taskbar thumbnails and shell overlays never suppress either monitor for geometry or stale QUNS");
            Check(!next.exclusive&&!next.allDisplays&&next.fullscreen.empty(),"shell context contains no fullscreen protection");
        }
    }
    Check(hides==0,"opening and closing shell thumbnails never causes transient hide and restore");
    Check(ExplorerSurfaceClass(L"TaskListThumbnailWnd")&&ExplorerSurfaceClass(L"Shell_SecondaryTrayWnd")&&
          ExplorerSurfaceClass(L"Progman")&&!ExplorerSurfaceClass(L"CabinetWClass")&&
          !ExplorerSurfaceClass(L"ExploreWClass")&&!ExplorerSurfaceClass(L""),
          "Explorer shell UI is distinct from File Explorer application windows");
    next={};next.foreignForeground=true;next.fullscreen={{0,0,800,600}};
    ApplyForegroundProtection(next,false,QUNS_RUNNING_D3D_FULL_SCREEN);Update(next);Pump();
    Check(!IsWindowVisible(primary)&&IsWindowVisible(secondary)&&next.exclusive&&!next.allDisplays,
          "genuine fullscreen game still suppresses only its located monitor");
    // Returning to taskbar UI must remove protection even when the API still
    // reports the game; restoring widgets must not depend on that API settling.
    ApplyForegroundProtection(next,true,QUNS_RUNNING_D3D_FULL_SCREEN);Update(next);Pump();
    Check(IsWindowVisible(primary)&&IsWindowVisible(secondary),"shell foreground immediately releases stale game suppression");
    next={};next.foreignForeground=true;next.fullscreen={{0,0,800,600}};
    ApplyForegroundProtection(next,ExplorerSurfaceClass(L"CabinetWClass"),QUNS_ACCEPTS_NOTIFICATIONS);Update(next);Pump();
    Check(!IsWindowVisible(primary)&&IsWindowVisible(secondary),"fullscreen File Explorer retains monitor-local application protection");
    next={};next.foreignForeground=true;
    ApplyForegroundProtection(next,false,QUNS_ACCEPTS_NOTIFICATIONS);Update(next);Pump();
    Check(IsWindowVisible(primary)&&IsWindowVisible(secondary),"ordinary windowed application leaves the desktop available");
    ApplyForegroundProtection(next,false,QUNS_RUNNING_D3D_FULL_SCREEN);Update(next);Pump();
    Check(!IsWindowVisible(primary)&&!IsWindowVisible(secondary)&&next.allDisplays,
          "unlocated exclusive game still protects all displays");
    Stop();contextReader={};DestroyWindow(primary);DestroyWindow(secondary);
}
void MaximizedWorkAreaTransitionScenario(){
    using namespace window_layer;
    // Observed Chromium custom frame: maximized, THICKFRAME, no CAPTION/POPUP.
    constexpr DWORD observedStyle=0x15070000;
    const RECT monitor{0,0,2560,1440},workArea{0,0,2560,1380};
    Context next;contextReader=[&]{return next;};
    HWND primary=Make(L"LumeNativeDesktopMaximizedPrimary",{30,30,230,200});
    HWND bar=Make(L"LumeNativeDesktopMaximizedBar",{30,220,230,260},WS_EX_TOOLWINDOW,primary);
    HWND secondary=Make(L"LumeNativeDesktopMaximizedSecondary",{2630,30,2830,200});
    HWND island=Make(L"LumeMaximizedIsland",{30,30,230,90});
    Check(Register(primary,Kind::Desktop)&&Register(bar,Kind::Desktop,primary)&&
          Register(secondary,Kind::Desktop)&&Register(island,Kind::Island,nullptr,true),
          "register ordinary-maximize transition fixture families");
    unsigned hides=0;observer=[&](HWND,Kind,bool blocked){if(blocked)++hides;};Start();
    const auto apply=[&](const RECT& bounds,bool maximized,DWORD style,QUERY_USER_NOTIFICATION_STATE notification){
        next={};next.foreignForeground=true;
        if(ForegroundCoversMonitor(bounds,monitor,maximized,style,notification))next.fullscreen.push_back(monitor);
        ApplyForegroundProtection(next,false,notification);Update(next);Pump();
    };
    for(const auto notification:{QUNS_BUSY,QUNS_ACCEPTS_NOTIFICATIONS}){
        for(const auto& bounds:{monitor,workArea,monitor,workArea}){
            apply(bounds,true,observedStyle,notification);
            Check(next.fullscreen.empty()&&!next.exclusive&&!next.allDisplays,
                  "ordinary maximized full-monitor and work-area frames never create fullscreen protection");
            Check(IsWindowVisible(primary)&&IsWindowVisible(bar)&&IsWindowVisible(secondary)&&IsWindowVisible(island)&&
                  !Suppressed(primary)&&!Suppressed(bar)&&!Suppressed(island),
                  "temporary work-area changes keep desktop Dock-family and island windows visible");
        }
    }
    Check(hides==0,"1440-to-1380 maximized work-area transitions never transiently suppress and restore visuals");
    Check(!ForegroundCoversMonitor(monitor,monitor,true,WS_OVERLAPPEDWINDOW|WS_MAXIMIZE,QUNS_BUSY),
          "ordinary captioned maximized windows also ignore temporary monitor-sized geometry");
    Check(ForegroundCoversMonitor(monitor,monitor,false,observedStyle,QUNS_BUSY),
          "nonmaximized monitor-sized application retains geometric fullscreen protection");
    for(const auto borderlessStyle:{static_cast<DWORD>(WS_POPUP|WS_VISIBLE|WS_MAXIMIZE),observedStyle&~WS_THICKFRAME}){
        apply(monitor,true,borderlessStyle,QUNS_ACCEPTS_NOTIFICATIONS);
        Check(!IsWindowVisible(primary)&&!IsWindowVisible(bar)&&!IsWindowVisible(island)&&IsWindowVisible(secondary)&&
              next.fullscreen.size()==1&&!next.exclusive&&!next.allDisplays,
              "zoomed borderless or popup fullscreen still suppresses only its monitor");
        apply(workArea,true,borderlessStyle,QUNS_ACCEPTS_NOTIFICATIONS);
        Check(IsWindowVisible(primary)&&IsWindowVisible(bar)&&IsWindowVisible(island),
              "leaving genuine borderless fullscreen restores the desktop family");
    }
    for(const auto notification:{QUNS_RUNNING_D3D_FULL_SCREEN,QUNS_PRESENTATION_MODE}){
        apply(monitor,true,observedStyle,notification);
        Check(!IsWindowVisible(primary)&&!IsWindowVisible(bar)&&!IsWindowVisible(island)&&IsWindowVisible(secondary)&&
              next.fullscreen.size()==1&&next.exclusive&&!next.allDisplays,
              "exclusive and presentation protection retains monitor location even with maximized frame styles");
        apply(workArea,true,observedStyle,QUNS_ACCEPTS_NOTIFICATIONS);
    }
    Stop();contextReader={};for(const HWND window:{island,bar,primary,secondary})DestroyWindow(window);
}
void FullscreenInputReleaseScenario(){
    using namespace window_layer;
    Context next;
    contextReader=[&]{return next;};
    HWND desktop=Make(L"LumeInputReleaseDesktop",{30,30,230,200});
    HWND popup=Make(L"LumeInputReleasePopup",{40,40,180,160},WS_EX_TOOLWINDOW,desktop);
    HWND companion=Make(L"LumeInputReleaseCompanion",{30,30,230,200});
    HWND application=Make(L"LumeInputReleaseApplication",{0,0,800,600},0);
    Check(desktop&&popup&&companion&&application,"create isolated fullscreen input fixtures");
    SetPropW(companion,lume::window_ids::DesktopCompanionOwnerProp,desktop);
    Check(Register(desktop,Kind::Desktop)&&Register(popup,Kind::Popup,desktop)&&
          Register(companion,Kind::Desktop,desktop),"register input fixture family");
    Start();
    SetActiveWindow(application);SetFocus(application);
    Check(GetActiveWindow()==application&&GetFocus()==application,"isolated application owns focus before suppression");
    for(const HWND captured:{desktop,popup}){
        ShowWindow(desktop,SW_SHOWNOACTIVATE);ShowWindow(popup,SW_SHOWNOACTIVATE);
        ShowWindow(companion,SW_SHOWNOACTIVATE);
        SetCapture(captured);
        Check(GetCapture()==captured,"desktop or popup starts with a real thread capture");
        next.foreignForeground=true;next.fullscreen={{0,0,800,600}};next.exclusive=true;
        Update(next);Pump();
        Check(GetCapture()==nullptr,"fullscreen suppression releases an active desktop-family mouse capture");
        Check(!IsWindowVisible(desktop)&&!IsWindowVisible(popup)&&!IsWindowVisible(companion),
              "capturing owner popup and companion all leave the protected display");
        Check(GetActiveWindow()==application&&GetFocus()==application,
              "fullscreen suppression preserves application activation and focus");
        SetCapture(application);
        Check(GetCapture()==application,"isolated application can hold capture during fullscreen");
        next={};Update(next);Pump();
        Check(GetCapture()==application&&GetActiveWindow()==application&&GetFocus()==application,
              "desktop restoration never steals the application's capture focus or activation");
        Check(IsWindowVisible(desktop)&&IsWindowVisible(companion)&&!IsWindowVisible(popup),
              "restoration shows resident surfaces without reviving the dismissed popup");
        if(GetCapture()==application)ReleaseCapture();
    }
    Stop();contextReader={};
    for(HWND window:{application,companion,popup,desktop})DestroyWindow(window);
}
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
    Check(ShellSurfaceImage(L"C:/Windows/SystemApps/ShellExperienceHost_cw5n1h2txyewy/ShellExperienceHost.exe")&&
          ShellSurfaceImage(L"C:/Windows/SystemApps/MicrosoftWindows.Client.CBS_cw5n1h2txyewy/searchhost.exe")&&
          !ShellSurfaceImage(L"C:/Games/Game.exe")&&!ShellSurfaceImage(nullptr),"shell overlays never count as fullscreen applications");
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
void DesktopRestoreScenario(){
    using namespace window_layer;
    constexpr UINT flags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER;
    desktop_level::desktopPlacementAnchor.Reset();
    Context next;bool atDesktop=false;
    contextReader=[&]{return next;};desktopForeground=[&]{return atDesktop;};
    HWND host=Make(L"LumePolicyRestoreDesktop",{0,0,800,600});
    HWND view=Make(L"SHELLDLL_DefView",{0,0,1,1},0,host,WS_CHILD|WS_VISIBLE);
    HWND app=Make(L"LumePolicyRestoreBrowser",{0,0,800,560},0);
    HWND widget=Make(L"LumeNativeDesktopRestoreWidget",{30,30,230,200});
    HWND dock=Make(L"LumeNativeDesktopRestoreDock",{250,500,600,570});
    Check(Register(widget,Kind::Desktop)&&Register(dock,Kind::Desktop),"register resident restore fixtures");
    Start();SetWindowPos(host,HWND_BOTTOM,0,0,0,0,flags);
    for(HWND window:{widget,dock}){
        ShowWindow(window,SW_MINIMIZE);
        Check(IsIconic(window)&&WantsDesktop(window),"Show Desktop minimization preserves resident visibility intent");
        SendMessageW(window,RestackMessage(),0,0);
        Check(IsIconic(window),"application foreground does not restore a minimized desktop surface");
    }
    atDesktop=true;desktop_level::foregroundDesktop=host;
    SetWindowPos(host,HWND_TOP,0,0,0,0,flags);
    desktop_level::RequestAll();Pump();
    for(HWND window:{widget,dock}){
        desktop_level::Place(window,false);
        Check(IsWindowVisible(window)&&!IsIconic(window),"return to desktop restores minimized widget and Dock without reenabling");
        Check(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST),"desktop restoration never promotes a surface to topmost");
        Check(desktop_level::Above(window,host),"restored desktop surfaces sit above their host");
    }
    // With no normal sibling above the raised desktop, the common placement
    // path must use its own hidden normal sibling rather than HWND_TOP.
    SetWindowPos(host,HWND_TOP,0,0,0,0,flags);
    SetWindowPos(widget,host,0,0,0,0,flags);
    Check(desktop_level::RequestPlacement(widget,HWND_TOP,flags,host),"desktop placement finds a concrete normal sibling");
    HWND anchor=desktop_level::desktopPlacementAnchor.Get();
    Check(anchor&&IsWindow(anchor)&&!IsWindowVisible(anchor)&&!GetWindow(anchor,GW_OWNER)&&
          !(GetWindowLongPtrW(anchor,GWL_EXSTYLE)&WS_EX_TOPMOST),
          "placement anchor remains hidden unowned and non-topmost");
    Check(desktop_level::Above(anchor,widget)&&desktop_level::Above(widget,host),
          "concrete sibling places the widget above the raised desktop");
    Check(desktop_level::RequestPlacement(widget,HWND_TOP,flags,host)&&
          desktop_level::desktopPlacementAnchor.Get()==anchor,"desktop placements reuse the anchor without create destroy loops");
    // HWND_TOP is not a reliable way to raise a fixture on an inactive
    // desktop. Move this test's hidden anchor below the host to reproduce
    // the same stale ordering without depending on foreground permission.
    Check(SetWindowPos(anchor,host,0,0,0,0,flags)!=FALSE,
          "fixture moves the cached anchor below the desktop host");
    Check(desktop_level::Above(host,anchor),"fixture raises the desktop above the cached anchor");
    Check(desktop_level::RequestPlacement(widget,HWND_TOP,flags,host)&&
          desktop_level::Above(desktop_level::desktopPlacementAnchor.Get(),host)&&desktop_level::Above(widget,host),
          "a newly raised desktop replaces the stale placement anchor");
    SetWindowPos(app,HWND_TOP,0,0,0,0,flags);
    Check(desktop_level::RequestPlacement(widget,HWND_TOP,flags,host)&&!desktop_level::Above(widget,app),
          "desktop sibling fallback still honors an application ceiling");
    // Explorer can also hide a layered top-level window during the desktop
    // transition. Explicit component suspension must still remain hidden.
    ShowWindow(widget,SW_HIDE);Check(WantsDesktop(widget),"desktop transition hiding preserves its enabled intent");
    desktop_level::RequestAll();Pump();
    Check(IsWindowVisible(widget),"restack includes and restores a hidden resident root");
    WantHidden(widget);ShowWindow(widget,SW_HIDE);desktop_level::RequestAll();Pump();
    Check(!IsWindowVisible(widget)&&!WantsDesktop(widget),"explicit component suspension is never undone by desktop recovery");
    ShowWindow(widget,SW_SHOWNOACTIVATE);
    atDesktop=false;desktop_level::foregroundDesktop=nullptr;
    SetWindowPos(app,HWND_TOP,0,0,0,0,flags);
    for(HWND window:{widget,dock}){
        desktop_level::Place(window,false);desktop_level::Select(window);
        Check(!desktop_level::Above(window,app),"browser foreground and widget clicks preserve the application ceiling after restoration");
    }
    next.foreignForeground=true;next.fullscreen={{0,0,800,600}};Update(next);Pump();
    atDesktop=true;desktop_level::RequestAll();Pump();
    Check(!IsWindowVisible(widget)&&!IsWindowVisible(dock),"desktop recovery cannot bypass fullscreen suppression");
    atDesktop=false;Stop();contextReader={};desktop_level::foregroundDesktop=nullptr;
    desktop_level::desktopPlacementAnchor.Reset();
    Check(!desktop_level::desktopPlacementAnchor.Get(),"placement anchor cleanup clears its native handle");
    for(HWND window:{widget,dock,app,view,host})DestroyWindow(window);
}
struct ImmediateRestackFixture { unsigned messages=0; bool nested=false; };
HWND immediateForeground=nullptr;
HWND WINAPI ImmediateForeground(){return immediateForeground;}
LRESULT CALLBACK ImmediateRestackProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_NCCREATE)
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
    if(message==desktop_level::Message()){
        auto* fixture=reinterpret_cast<ImmediateRestackFixture*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        ++fixture->messages;
        if(fixture->nested){fixture->nested=false;desktop_level::RequestAll(true);}
        desktop_level::Place(window,false);
        return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
void ImmediateRestackScenario(){
    constexpr UINT flags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER;
    HWND host=Make(L"LumePolicyImmediateDesktop",{0,0,800,600});
    HWND view=Make(L"SHELLDLL_DefView",{0,0,1,1},0,host,WS_CHILD|WS_VISIBLE);
    WNDCLASSW type{};type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=ImmediateRestackProc;
    type.lpszClassName=L"LumeNativeDesktopImmediateFixture";RegisterClassW(&type);
    ImmediateRestackFixture fixture;
    HWND widget=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Immediate restack",WS_POPUP|WS_VISIBLE,
        30,30,200,160,nullptr,nullptr,type.hInstance,&fixture);
    window_layer::Register(widget,window_layer::Kind::Desktop);
    // The isolated desktop must never steal the user's input foreground. Feed
    // its known host through the internal delivery seam; production always
    // supplies GetForegroundWindow and rechecks it before each direct send.
    SetWindowPos(widget,host,0,0,0,0,flags);
    Check(desktop_level::Above(host,widget),"desktop initially covers its resident fixture");
    immediateForeground=nullptr;
    desktop_level::detail::RequestAllImpl(true,ImmediateForeground);
    Check(fixture.messages==0,"an absent real desktop foreground retains queued delivery");
    Pump();
    fixture.messages=0;
    SetWindowPos(widget,host,0,0,0,0,flags);
    immediateForeground=host;
    desktop_level::detail::RequestAllImpl(true,ImmediateForeground);
    Check(fixture.messages>0&&desktop_level::Above(widget,host),
          "validated desktop restack repairs its own widgets before the posted message queue runs");
    fixture.messages=0;fixture.nested=true;
    desktop_level::detail::RequestAllImpl(true,ImmediateForeground);
    Check(fixture.messages==1,"nested desktop restacks are queued instead of recursively sent");
    Pump();immediateForeground=nullptr;
    desktop_level::foregroundDesktop=nullptr;desktop_level::desktopPlacementAnchor.Reset();
    DestroyWindow(widget);DestroyWindow(view);DestroyWindow(host);
}
}
int main(){
    wchar_t name[80]{};swprintf_s(name,L"LumeLayerPolicy-%lu",GetCurrentProcessId());
    HDESK desktop=CreateDesktopW(name,nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop)return 2;
    std::thread test([&]{if(!SetThreadDesktop(desktop)){++failures;return;}DesktopPeekScenario();BlockedMaterialRestackScenario();ShellTransitionScenario();MaximizedWorkAreaTransitionScenario();FullscreenInputReleaseScenario();Scenario();DesktopRestoreScenario();ImmediateRestackScenario();});test.join();CloseDesktop(desktop);
    if(!failures)std::puts("PASS isolated display policy: fullscreen, multimonitor, families, late callbacks and application ceilings");
    return failures?1:0;
}
