#include "platform/windows/widget_ui/native_widget_window.h"
#include <objbase.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>
#include "widget_presentation_capture.h"
#include "platform/windows/widget_ui/widget_snap_protocol.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "widget_snap_message_probe.h"
using namespace lume::platform::windows;
void Expect(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
namespace {
WNDPROC originalProcedure=nullptr;
unsigned dragCount=0;
LPARAM dragPoint=0;
LRESULT CALLBACK ObserveDrag(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_NCLBUTTONDOWN&&w==HTCAPTION){++dragCount;dragPoint=l;return 0;}
    return CallWindowProcW(originalProcedure,window,message,w,l);
}
void PumpSnapMessages(){
    MSG message{};
    while(PeekMessageW(&message,nullptr,widget_snap::Message,widget_snap::Message+16,PM_REMOVE))
        DispatchMessageW(&message);
}
struct CursorRestore {
    POINT saved{};
    CursorRestore(){Expect(GetCursorPos(&saved)!=FALSE,"save cursor for isolated drag regression");}
    ~CursorRestore(){SetCursorPos(saved.x,saved.y);}
};
// Own only the button press injected into this test's verified topmost window.
// A timer-queue callback can release it even while the UI thread is modal.
struct OwnedDragInput {
    explicit OwnedDragInput(HWND owner):window(owner){}
    ~OwnedDragInput(){Restore();}
    bool Press(POINT point){
        for(const int button:{VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2})
            if(GetAsyncKeyState(button)&0x8000)return false;
        DWORD process=0;
        GetWindowThreadProcessId(window,&process);
        if(process!=GetCurrentProcessId()||
           !SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE))return false;
        SetForegroundWindow(window);
        if(!SetCursorPos(point.x,point.y)||WindowFromPoint(point)!=window)return false;
        if(!CreateTimerQueueTimer(&watchdog,nullptr,Timeout,this,5000,0,WT_EXECUTEONLYONCE))return false;
        std::lock_guard<std::mutex> lock(buttonMutex);
        POINT actual{};
        if(timedOut||!GetCursorPos(&actual)||actual.x!=point.x||actual.y!=point.y||
           WindowFromPoint(actual)!=window||(GetAsyncKeyState(VK_LBUTTON)&0x8000))return false;
        INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
        held=SendInput(1,&input,sizeof(input))==1;
        return held;
    }
    bool Release(){
        std::lock_guard<std::mutex> lock(buttonMutex);
        if(!held)return true;
        INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_LEFTUP;
        if(SendInput(1,&input,sizeof(input))!=1){failed=true;return false;}
        held=false;return true;
    }
    void Cancel(){
        Release();
        PostMessageW(window,WM_CANCELMODE,0,0);
        PostMessageW(window,WM_LBUTTONUP,0,0);
    }
    void Restore(){
        if(restored)return;
        if(watchdog){DeleteTimerQueueTimer(nullptr,watchdog,INVALID_HANDLE_VALUE);watchdog=nullptr;}
        Release();
        if(GetCapture()==window)ReleaseCapture();
        if(IsWindow(foreground)&&GetForegroundWindow()==window)SetForegroundWindow(foreground);
        restored=true;
    }
    static VOID CALLBACK Timeout(PVOID context,BOOLEAN){
        auto& input=*static_cast<OwnedDragInput*>(context);
        input.timedOut=true;input.Cancel();
    }
    HWND window=nullptr,foreground=GetForegroundWindow();
    HANDLE watchdog=nullptr;
    std::mutex buttonMutex;
    std::atomic<bool> timedOut=false,failed=false;
    bool held=false,restored=false;
};
unsigned systemMoveMessages=0,systemMoveEntries=0,systemMoveExits=0,systemCaptureChanges=0;
POINT systemMoveStart{};
RECT systemMoveOrigin{};
bool systemMoveOriginValid=false;
bool systemMoveHorizontal=true,systemMoveGeometryValid=true;
bool systemMoveTracing=false;
unsigned systemMoveTick=0,systemMoveLastTraceTick=~0u;
struct SystemMoveTrace {
    UINT message=0;unsigned sequence=0,tick=0,dpi=0;
    POINT cursor{},physical{};
    RECT actual{},before{},after{};
    HWND capture=nullptr,nextCapture=nullptr;
};
std::vector<SystemMoveTrace> systemMoveTrace;
WNDPROC systemMoveProcedure=nullptr;
LRESULT CALLBACK ObserveSystemMove(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_ENTERSIZEMOVE){
        ++systemMoveEntries;
        systemMoveOriginValid=GetCursorPos(&systemMoveStart)&&GetWindowRect(window,&systemMoveOrigin);
    }
    if(message==WM_MOVING)++systemMoveMessages;
    if(message==WM_EXITSIZEMOVE)++systemMoveExits;
    if(message==WM_CAPTURECHANGED)++systemCaptureChanges;
    const bool trace=systemMoveTracing&&(message==WM_ENTERSIZEMOVE||message==WM_EXITSIZEMOVE||
        message==WM_CAPTURECHANGED||message==WM_DPICHANGED||
        (message==WM_MOVING&&(systemMoveMessages<=4||systemMoveTick!=systemMoveLastTraceTick)));
    const auto index=systemMoveTrace.size();
    if(trace){
        SystemMoveTrace sample;sample.message=message;sample.sequence=systemMoveMessages;
        sample.tick=systemMoveTick;sample.dpi=GetDpiForWindow(window);
        GetCursorPos(&sample.cursor);GetPhysicalCursorPos(&sample.physical);GetWindowRect(window,&sample.actual);
        sample.capture=GetCapture();
        if(message==WM_CAPTURECHANGED)sample.nextCapture=reinterpret_cast<HWND>(l);
        if(message==WM_MOVING||message==WM_DPICHANGED)sample.before=*reinterpret_cast<RECT*>(l);
        systemMoveTrace.push_back(sample);
        if(message==WM_MOVING)systemMoveLastTraceTick=systemMoveTick;
    }
    POINT moveCursor{};
    if(message==WM_MOVING)GetCursorPos(&moveCursor);
    const auto result=CallWindowProcW(systemMoveProcedure,window,message,w,l);
    if(message==WM_MOVING&&systemMoveOriginValid){
        const auto& bounds=*reinterpret_cast<RECT*>(l);
        const auto delta=systemMoveHorizontal?moveCursor.x-systemMoveStart.x:moveCursor.y-systemMoveStart.y;
        const auto expected=delta < -12 ? delta : 0;
        const auto edges=reinterpret_cast<UINT_PTR>(GetPropW(window,widget_snap::ActiveEdges));
        const auto axis=systemMoveHorizontal?3u:12u;
        systemMoveGeometryValid=systemMoveGeometryValid&&delta<=0&&delta>=-20&&
            bounds.left==systemMoveOrigin.left+(systemMoveHorizontal?expected:0)&&
            bounds.top==systemMoveOrigin.top+(systemMoveHorizontal?0:expected)&&
            ((edges&axis)!=0)==(delta>=-12);
    }
    if(trace&&(message==WM_MOVING||message==WM_DPICHANGED))
        systemMoveTrace[index].after=*reinterpret_cast<RECT*>(l);
    return result;
}
int SystemMoveReleaseRegression(bool horizontal){
    CursorRestore cursor;
    constexpr UINT_PTR timer=0x7519;
    const lume::widget::Limits limits{340,216,320,204,720,720};
    unsigned ticks=0,boundsEvents=0,movesAtLastTick=0,preludeSteps=0;POINT start{};
    // Exercise Windows' actual modal move loop with input messages addressed
    // only to our HWND. Raw injected clicks are an optional desktop diagnostic,
    // since another application's/user's input can intercept their drag start.
    const bool systemCommand=std::getenv("LUME_WIDGET_DRAG_MOUSE_INPUT")==nullptr;
    OwnedDragInput* input=nullptr;
    bool cursorFailed=false;
    WidgetWindowPorts ports;
    ports.draw=[](WidgetCanvas& canvas,float,float){canvas.Text(L"system move",{20,20,140,20},12);};
    ports.action=[&](WidgetAction action,const auto&){if(action==WidgetAction::Bounds)++boundsEvents;};
    ports.event=[&](HWND hwnd,UINT message,WPARAM w,LPARAM)->std::optional<LRESULT>{
        if(message!=WM_TIMER||w!=timer)return {};
        if(!input||systemMoveExits)return 0;
        if(!systemMoveEntries){
            // Some system paths wait for drag detection before entering.
            if(preludeSteps<8){
                const auto step=static_cast<int>(++preludeSteps);
                if(!SetCursorPos(start.x-(horizontal?step:0),start.y-(horizontal?0:step))){
                    cursorFailed=true;KillTimer(hwnd,timer);input->Cancel();
                }
            }
            return 0;
        }
        if(systemMoveEntries!=1||!systemMoveOriginValid)return 0;
        if(!ticks)start=systemMoveStart;
        if(systemCommand&&!systemMoveMessages&&!ticks){
            PostMessageW(hwnd,WM_KEYDOWN,horizontal?VK_LEFT:VK_UP,0);
            return 0;
        }
        // Allow the system to consume each move before issuing the next one.
        if(ticks&&systemMoveMessages==movesAtLastTick)return 0;
        if(ticks<20){
            movesAtLastTick=systemMoveMessages;
            systemMoveTick=ticks+1;
            ++ticks;
            if(!SetCursorPos(start.x-(horizontal?static_cast<int>(ticks):0),
                start.y-(horizontal?0:static_cast<int>(ticks)))){
                cursorFailed=true;KillTimer(hwnd,timer);input->Cancel();
            }else if(systemCommand){
                POINT point{start.x-(horizontal?static_cast<LONG>(ticks):0),
                    start.y-(horizontal?0:static_cast<LONG>(ticks))};
                ScreenToClient(hwnd,&point);
                PostMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM(point.x,point.y));
            }
        }else {
            KillTimer(hwnd,timer);input->Release();
            if(systemCommand)PostMessageW(hwnd,WM_LBUTTONUP,0,0);
        }
        return 0;
    };
    NativeWidgetWindow source(GetModuleHandleW(nullptr),L"System drag source",limits,ports);
    NativeWidgetWindow target(GetModuleHandleW(nullptr),L"System drag target",limits,{});
    auto preferences=lume::widget::Defaults(limits);preferences.placed=true;preferences.x=700;preferences.y=240;
    Expect(target.Configure(preferences)&&target.Start(),"system drag target starts");
    RECT neighbor{};GetWindowRect(target.Handle(),&neighbor);
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    Expect(GetMonitorInfoW(MonitorFromWindow(target.Handle(),MONITOR_DEFAULTTOPRIMARY),&monitor)!=FALSE,
        "system drag work area");
    const auto width=neighbor.right-neighbor.left;
    SetWindowPos(target.Handle(),nullptr,monitor.rcWork.left+128+width+8,neighbor.top,0,0,
        SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    GetWindowRect(target.Handle(),&neighbor);
    preferences.x=neighbor.left-MulDiv(preferences.width,static_cast<int>(GetDpiForWindow(target.Handle())),96)-8;
    Expect(source.Configure(preferences)&&source.Start(),"system drag source starts");
    RECT origin{};GetWindowRect(source.Handle(),&origin);start={origin.left+60,origin.top+70};
    systemMoveEntries=0;systemMoveMessages=0;systemMoveExits=0;systemCaptureChanges=0;systemMoveOriginValid=false;
    systemMoveHorizontal=horizontal;systemMoveGeometryValid=true;
    systemMoveTracing=std::getenv("LUME_WIDGET_DRAG_TRACE")!=nullptr;
    systemMoveTick=0;systemMoveLastTraceTick=~0u;systemMoveTrace.clear();
    systemMoveProcedure=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(source.Handle(),GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ObserveSystemMove)));
    OwnedDragInput dragInput(source.Handle());input=&dragInput;
    const bool timerReady=SetTimer(source.Handle(),timer,35,nullptr)!=0;
    bool pressed=false;
    if(timerReady&&systemCommand){
        SetWindowPos(source.Handle(),HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetCursorPos(start.x,start.y);
        pressed=CreateTimerQueueTimer(&dragInput.watchdog,nullptr,OwnedDragInput::Timeout,&dragInput,
            5000,0,WT_EXECUTEONLYONCE)!=FALSE;
        if(pressed)SendMessageW(source.Handle(),WM_SYSCOMMAND,SC_MOVE,0);
    }else pressed=timerReady&&dragInput.Press(start);
    if(pressed){
        const auto deadline=GetTickCount64()+6000;
        while(!systemMoveExits&&!dragInput.timedOut&&GetTickCount64()<deadline){
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            if(!systemMoveExits&&!dragInput.timedOut)
                MsgWaitForMultipleObjectsEx(0,nullptr,35,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        if(!systemMoveExits)dragInput.Cancel();
    }
    KillTimer(source.Handle(),timer);
    dragInput.Restore();input=nullptr;
    RECT result{};GetWindowRect(source.Handle(),&result);
    SetWindowLongPtrW(source.Handle(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(systemMoveProcedure));
    const bool released=pressed&&!cursorFailed&&!dragInput.timedOut&&!dragInput.failed&&
        ticks==20&&systemMoveEntries==1&&systemMoveExits==1&&boundsEvents==1&&systemMoveOriginValid&&
        systemMoveMessages>=13&&systemMoveGeometryValid&&
        result.left==systemMoveOrigin.left-(horizontal?20:0)&&result.top==systemMoveOrigin.top-(horizontal?0:20)&&
        result.right-result.left==origin.right-origin.left&&result.bottom-result.top==origin.bottom-origin.top;
    std::cout<<"system drag axis="<<(horizontal?"x":"y")<<" pressed="<<pressed<<" timedOut="<<dragInput.timedOut<<" ticks="<<ticks
        <<" prelude="<<preludeSteps<<" entries="<<systemMoveEntries<<" moves="<<systemMoveMessages
        <<" exits="<<systemMoveExits<<" captureChanges="<<systemCaptureChanges
        <<" boundsEvents="<<boundsEvents<<" geometryValid="<<systemMoveGeometryValid
        <<" pointerDelta=-20 windowDelta="<<(horizontal?result.left-systemMoveOrigin.left:result.top-systemMoveOrigin.top)<<'\n';
    for(const auto& sample:systemMoveTrace){
        const char* name=sample.message==WM_ENTERSIZEMOVE?"enter":sample.message==WM_EXITSIZEMOVE?"exit":
            sample.message==WM_CAPTURECHANGED?"capture":sample.message==WM_DPICHANGED?"dpi":"moving";
        std::cout<<"drag trace "<<name<<" tick="<<sample.tick<<" move="<<sample.sequence<<" dpi="<<sample.dpi
            <<" cursor="<<sample.cursor.x<<','<<sample.cursor.y<<" physical="<<sample.physical.x<<','<<sample.physical.y
            <<" actual="<<sample.actual.left<<','<<sample.actual.top
            <<" in="<<sample.before.left<<','<<sample.before.top<<" out="<<sample.after.left<<','<<sample.after.top
            <<" capture="<<sample.capture<<" nextCapture="<<sample.nextCapture<<'\n';
    }
    source.Stop();target.Stop();PumpSnapMessages();
    if(!released)std::cerr<<"FAIL: actual Windows move loop releases after accumulated small cursor movement\n";
    return released?0:1;
}
int DpiDragAnchorRegression(){
    CursorRestore cursor;
    const lume::widget::Limits limits{340,216,320,204,720,720};
    NativeWidgetWindow source(GetModuleHandleW(nullptr),L"DPI drag anchor",limits,{});
    auto preferences=lume::widget::Defaults(limits);preferences.placed=true;preferences.x=160;preferences.y=240;
    Expect(source.Configure(preferences)&&source.Start(),"DPI drag source starts");
    RECT origin{};GetWindowRect(source.Handle(),&origin);
    POINT start{origin.left+60,origin.top+70};
    Expect(SetCursorPos(start.x,start.y)!=FALSE,"DPI drag cursor origin");
    SendMessageW(source.Handle(),WM_ENTERSIZEMOVE,0,0);
    Expect(GetCursorPos(&start)!=FALSE,"read the accepted DPI drag cursor origin");
    Expect(SetCursorPos(start.x+20,start.y)!=FALSE,"move before the DPI transition");
    RECT moving=origin;OffsetRect(&moving,20,0);
    SendMessageW(source.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));
    SetWindowPos(source.Handle(),nullptr,moving.left,moving.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    const auto nextDpi=GetDpiForWindow(source.Handle())==144?120:144;
    RECT suggested{moving.left+48,moving.top+32,
        moving.left+48+MulDiv(preferences.width,nextDpi,96),
        moving.top+32+MulDiv(preferences.height,nextDpi,96)};
    SendMessageW(source.Handle(),WM_DPICHANGED,MAKELONG(nextDpi,nextDpi),reinterpret_cast<LPARAM>(&suggested));
    RECT applied{};GetWindowRect(source.Handle(),&applied);
    Expect(SetCursorPos(start.x+21,start.y)!=FALSE,"move one pixel after the DPI transition");
    moving=suggested;OffsetRect(&moving,1,0);
    SendMessageW(source.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));
    const bool rebased=EqualRect(&applied,&suggested)&&moving.left==suggested.left+1&&
        moving.top==suggested.top&&moving.right-moving.left==suggested.right-suggested.left&&
        moving.bottom-moving.top==suggested.bottom-suggested.top;
    std::cout<<"DPI drag suggested="<<suggested.left<<','<<suggested.top
        <<" next="<<moving.left<<','<<moving.top<<'\n';
    SendMessageW(source.Handle(),WM_EXITSIZEMOVE,0,0);source.Stop();PumpSnapMessages();
    if(!rebased)std::cerr<<"FAIL: moving after a DPI transition uses the suggested window origin\n";
    return rebased?0:1;
}
// Supply the same accumulated cursor movement as a real system move loop.
// The RECT deliberately feeds back the previous snapped position: slow moves
// must still escape once total pointer movement exceeds the release distance.
int SnapReleaseRegression(){
    CursorRestore cursor;
    const lume::widget::Limits limits{340,216,320,204,720,720};
    WidgetWindowPorts ports;
    ports.draw=[](WidgetCanvas& canvas,float,float){canvas.Text(L"drag release",{20,20,140,20},12);};
    NativeWidgetWindow source(GetModuleHandleW(nullptr),L"Snap release source",limits,ports);
    NativeWidgetWindow target(GetModuleHandleW(nullptr),L"Snap release target",limits,ports);
    auto preferences=lume::widget::Defaults(limits);preferences.placed=true;preferences.x=700;preferences.y=240;
    Expect(target.Configure(preferences)&&target.Start(),"snap release target starts");
    RECT neighbor{};GetWindowRect(target.Handle(),&neighbor);
    const auto dpi=static_cast<int>(GetDpiForWindow(target.Handle()));
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    Expect(GetMonitorInfoW(MonitorFromWindow(target.Handle(),MONITOR_DEFAULTTOPRIMARY),&monitor)!=FALSE,
        "slow drag work area");
    SetWindowPos(target.Handle(),nullptr,monitor.rcWork.left+128+neighbor.right-neighbor.left+8,neighbor.top,0,0,
        SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    GetWindowRect(target.Handle(),&neighbor);
    preferences.x=neighbor.left-MulDiv(preferences.width,dpi,96)-8;
    Expect(source.Configure(preferences)&&source.Start(),"snap release source starts");
    unsigned failures=0;
    const auto verify=[&](bool valid,const char* text){
        if(!valid){++failures;std::cerr<<"FAIL: "<<text<<'\n';}
    };
    for(const bool horizontal:{true,false}){
        auto reset=source.Bounds();reset.x=preferences.x;reset.y=preferences.y;
        Expect(source.Configure(reset),"restore starting geometry between axes");
        RECT origin{};GetWindowRect(source.Handle(),&origin);
        POINT start{origin.left+60,origin.top+70};
        Expect(SetCursorPos(start.x,start.y)!=FALSE,"place test cursor at drag origin");
        SendMessageW(source.Handle(),WM_ENTERSIZEMOVE,0,0);
        Expect(GetCursorPos(&start)!=FALSE,"read the accepted slow drag cursor origin");
        RECT moving=origin;
        SendMessageW(source.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));PumpSnapMessages();
        Expect(GetPropW(source.Handle(),widget_snap::ActiveEdges)&&GetPropW(target.Handle(),widget_snap::ActiveEdges),
            "slow drag regression begins with both guides");
        for(int distance=1;distance<=20;++distance){
            Expect(SetCursorPos(start.x-(horizontal?distance:0),start.y-(horizontal?0:distance))!=FALSE,
                "advance cursor by one physical pixel");
            OffsetRect(&moving,horizontal?-1:0,horizontal?0:-1);
            SendMessageW(source.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));
            SetWindowPos(source.Handle(),nullptr,moving.left,moving.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            PumpSnapMessages();
            const auto value=horizontal?moving.left:moving.top;
            const auto initial=horizontal?origin.left:origin.top;
            verify(value==initial-(distance>12?distance:0),
                "slow accumulated drag releases after twelve pixels instead of remaining stuck");
            if(distance==13){
                const auto own=reinterpret_cast<UINT_PTR>(GetPropW(source.Handle(),widget_snap::ActiveEdges));
                const auto peer=reinterpret_cast<UINT_PTR>(GetPropW(target.Handle(),widget_snap::ActiveEdges));
                verify(!(own&(horizontal?3u:12u))&&!(peer&(horizontal?3u:12u)),
                    "released axis clears both source and peer guides");
            }
        }
        verify(moving.right-moving.left==origin.right-origin.left&&moving.bottom-moving.top==origin.bottom-origin.top,
            "drag release preserves the original size");
        // Return within the original acquisition distance in the same drag.
        Expect(SetCursorPos(start.x-(horizontal?6:0),start.y-(horizontal?0:6))!=FALSE,"return toward snap target");
        moving=origin;OffsetRect(&moving,horizontal?-6:0,horizontal?0:-6);
        SendMessageW(source.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));PumpSnapMessages();
        verify(moving.left==origin.left&&moving.top==origin.top,"drag can reacquire the original snap target");
        SetWindowPos(source.Handle(),nullptr,moving.left,moving.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(source.Handle(),WM_EXITSIZEMOVE,0,0);
        SendMessageW(source.Handle(),WM_ENTERSIZEMOVE,0,0);
        SendMessageW(source.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));PumpSnapMessages();
        verify(GetPropW(source.Handle(),widget_snap::ActiveEdges)&&GetPropW(target.Handle(),widget_snap::ActiveEdges),
            "a second drag starts from its new anchor and reacquires guides");
        SendMessageW(source.Handle(),WM_CANCELMODE,0,0);PumpSnapMessages();
        verify(!GetPropW(source.Handle(),widget_snap::ActiveEdges)&&!GetPropW(target.Handle(),widget_snap::ActiveEdges),
            "cancelled drag clears both guide owners");
    }
    source.Stop();target.Stop();PumpSnapMessages();
    return failures?1:0;
}
int SnapRegression(bool handoff){
    const lume::widget::Limits limits{340,216,320,204,720,720};
    unsigned sourcePaints=0,targetPaints=0,failures=0;
    const auto verify=[&](bool valid,const char* text){
        if(!valid){++failures;std::cerr<<"FAIL: "<<text<<'\n';}
    };
    WidgetWindowPorts sourcePorts,targetPorts;
    sourcePorts.draw=[&](WidgetCanvas& canvas,float,float){++sourcePaints;canvas.Text(L"source",{20,20,100,20},12);};
    targetPorts.draw=[&](WidgetCanvas& canvas,float,float){++targetPaints;canvas.Text(L"target",{20,20,100,20},12);};
    NativeWidgetWindow source(GetModuleHandleW(nullptr),L"Snap source regression",limits,sourcePorts);
    NativeWidgetWindow target(GetModuleHandleW(nullptr),L"Snap target regression",limits,targetPorts);
    auto preferences=lume::widget::Defaults(limits);preferences.placed=true;preferences.x=60;preferences.y=80;
    Expect(source.Configure(preferences)&&source.Start(),"snap regression source starts");
    preferences.x=560;
    Expect(target.Configure(preferences)&&target.Start(),"snap regression target starts");
    lume::test::WidgetSnapMessageProbe messages(target.Handle());
    Expect(messages.Attached(),"observe real peer snap message delivery");
    RECT sourceBounds{},targetBounds{};GetWindowRect(source.Handle(),&sourceBounds);GetWindowRect(target.Handle(),&targetBounds);
    const auto width=sourceBounds.right-sourceBounds.left,height=sourceBounds.bottom-sourceBounds.top;
    RECT moving{targetBounds.left-width-7,targetBounds.top,targetBounds.left-7,targetBounds.top+height};
    // These are message/highlight tests with explicit geometry. Do not enter a
    // cursor-driven system drag: the real input regression covers that path.
    widget_snap::BeginInteraction(source.Handle());
    SendMessageW(source.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));
    PumpSnapMessages();
    if(!GetPropW(source.Handle(),widget_snap::ActiveEdges)||!GetPropW(target.Handle(),widget_snap::ActiveEdges)){
        POINT current{};GetCursorPos(&current);
        std::cerr<<"FAIL: snap regression establishes both guide owners; source="
            <<sourceBounds.left<<','<<sourceBounds.top<<" target="<<targetBounds.left<<','<<targetBounds.top
            <<" size="<<width<<','<<height
            <<" cursor="<<current.x<<','<<current.y
            <<" result="<<moving.left<<','<<moving.top<<" dpi="<<GetDpiForWindow(source.Handle())<<'\n';
        source.Stop();target.Stop();PumpSnapMessages();return 1;
    }
    if(handoff){
        SendMessageW(source.Handle(),WM_EXITSIZEMOVE,0,0);PumpSnapMessages();
        SendMessageW(target.Handle(),WM_ENTERSIZEMOVE,0,0);PumpSnapMessages();
        verify(!GetPropW(source.Handle(),widget_snap::ActiveEdges),
            "starting a drag on the former target clears the previous source guide");
        verify(!GetPropW(target.Handle(),widget_snap::ActiveEdges),
            "starting a drag on the former target clears its received guide immediately");
        SendMessageW(target.Handle(),WM_EXITSIZEMOVE,0,0);
    }else{
        const auto initialSourcePaints=sourcePaints,initialTargetPaints=targetPaints;
        const auto initialMessages=messages.Count();
        for(unsigned repeat=0;repeat<3;++repeat){
            SendMessageW(source.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));
            PumpSnapMessages();
        }
        verify(sourcePaints==initialSourcePaints,"unchanged local snap guides do not repaint the source");
        verify(messages.Count()==initialMessages,"unchanged target and edge signature does not enqueue peer messages");
        verify(targetPaints==initialTargetPaints,"unchanged snap target and edges do not repaint the peer");
        const auto edges=reinterpret_cast<UINT_PTR>(GetPropW(target.Handle(),widget_snap::ActiveEdges));
        const auto beforeDuplicate=targetPaints;
        SendMessageW(target.Handle(),widget_snap::Message,edges,reinterpret_cast<LPARAM>(source.Handle()));
        verify(targetPaints==beforeDuplicate,"a duplicate received guide does not repaint the peer");
        SendMessageW(source.Handle(),WM_EXITSIZEMOVE,0,0);
    }
    source.Stop();target.Stop();PumpSnapMessages();
    return failures?1:0;
}
}
bool Below(HWND window,HWND other){
    for(HWND w=GetWindow(window,GW_HWNDPREV);w;w=GetWindow(w,GW_HWNDPREV))if(w==other)return true;
    return false;
}
// Explorer can replace the desktop host after a widget is shown (Win10 creates
// WorkerW hosts above the old one on a wallpaper's 0x052C). A widget left
// behind the host must return above it when the shell asks for a restack.
// The host is a hidden in-process stand-in; Explorer is never touched.
int DesktopLevelRegression(){
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=DefWindowProcW;
    type.lpszClassName=L"LumeTestDesktopHost";Expect(RegisterClassExW(&type)!=0,"register stand-in host");
    type.lpszClassName=L"SHELLDLL_DefView";Expect(RegisterClassExW(&type)!=0,"register stand-in icon view");
    const HWND host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"LumeTestDesktopHost",L"",WS_POPUP,
        0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
    Expect(host&&CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD,0,0,1,1,host,nullptr,type.hInstance,nullptr),
        "create stand-in desktop host");
    Expect(desktop_level::Host()==host,"stand-in is the topmost desktop host");
    const lume::widget::Limits limits{340,216,320,204,720,720};
    NativeWidgetWindow widget(GetModuleHandleW(nullptr),L"Desktop level",limits,{});
    auto preferences=lume::widget::Defaults(limits);preferences.placed=true;preferences.x=120;preferences.y=160;
    Expect(widget.Configure(preferences)&&widget.Start(),"desktop level widget starts");
    Expect(Below(host,widget.Handle()),"started widget sits above the desktop host");
    // A glass surface right below its owner and above the host: restacking must
    // not insert the widget below its own surface (that showed as flicker).
    type.lpszClassName=L"LumeSharedMaterialSurfaceTest";Expect(RegisterClassExW(&type)!=0,"register stand-in glass");
    const HWND glass=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"",WS_POPUP,
        0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
    Expect(glass&&SetWindowPos(glass,widget.Handle(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),
        "stand-in glass sits below its widget");
    Expect(Below(glass,widget.Handle())&&Below(host,glass),"glass is between widget and host");
    desktop_level::Place(widget.Handle(),false);
    const bool keptAboveGlass=Below(glass,widget.Handle());
    DestroyWindow(glass);
    if(!keptAboveGlass){std::cerr<<"FAIL: restack keeps a widget above its own glass surface\n";widget.Stop();DestroyWindow(host);return 1;}
    SetWindowPos(host,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    Expect(Below(widget.Handle(),host),"fixture stacks a new host above the widget");
    desktop_level::Watcher watcher;
    Expect(watcher.Start(GetModuleHandleW(nullptr)),"desktop level watcher starts");
    MSG message{};
    for(const auto deadline=GetTickCount64()+1000;GetTickCount64()<deadline&&!Below(host,widget.Handle());){
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);
        Sleep(10);
    }
    const bool restored=Below(host,widget.Handle());
    watcher.Stop();
    bool keptLevel=true;
    if(restored){
        // Activation (a click, its menu or search box) asks to lift the widget
        // to the top; it must stay right above the host, under other windows.
        const HWND cover=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"LumeTestDesktopHost",L"",WS_POPUP|WS_VISIBLE,
            0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
        Expect(cover&&Below(widget.Handle(),cover),"stand-in application window covers the widget");
        SetWindowPos(widget.Handle(),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
        keptLevel=Below(widget.Handle(),cover)&&Below(host,widget.Handle());
        DestroyWindow(cover);
    }
    ShowWindow(widget.Handle(),SW_HIDE);
    desktop_level::RequestAll();
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);
    const bool stayedHidden=!IsWindowVisible(widget.Handle());
    widget.Stop();DestroyWindow(host);
    if(!restored)std::cerr<<"FAIL: watcher restacks a widget left behind the desktop host\n";
    if(!stayedHidden)std::cerr<<"FAIL: restack never shows a hidden widget\n";
    if(!keptLevel)std::cerr<<"FAIL: activating a desktop widget lifts it above other windows\n";
    return restored&&stayedHidden&&keptLevel?0:1;
}
int GlassPairRegression(){
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=DefWindowProcW;
    type.lpszClassName=L"LumeGlassPairOwnerTest";Expect(RegisterClassExW(&type)!=0,"register pair owner");
    const HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"",WS_POPUP|WS_VISIBLE,
        0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
    Expect(owner&&SetPropW(owner,L"LumeDesktopWidget",reinterpret_cast<HANDLE>(1)),"create pair owner");
    type.lpszClassName=L"LumeSharedGlassPairTest";Expect(RegisterClassExW(&type)!=0,"register pair glass");
    const HWND glass=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"",WS_POPUP|WS_VISIBLE,
        0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
    Expect(glass&&SetPropW(glass,L"LumeGlassSurfaceOwner",owner),"create owned glass stand-in");
    desktop_level::Place(owner,false);
    const bool initial=Below(glass,owner);
    SetWindowPos(glass,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    const bool displaced=Below(owner,glass);
    desktop_level::Place(owner,false);
    const bool repaired=Below(glass,owner);
    RemovePropW(glass,L"LumeGlassSurfaceOwner");DestroyWindow(glass);
    RemovePropW(owner,L"LumeDesktopWidget");DestroyWindow(owner);
    if(!initial||!displaced||!repaired)std::cerr<<"FAIL: desktop restack repairs an inverted foreground/glass pair\n";
    return initial&&displaced&&repaired?0:1;
}
int ManagerCeilingRegression(){
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=DefWindowProcW;
    type.lpszClassName=L"LumeOriginalPageWindow";Expect(RegisterClassExW(&type)!=0,"register stand-in manager");
    const HWND manager=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"",WS_POPUP|WS_VISIBLE,
        0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
    Expect(manager!=nullptr,"create stand-in manager");
    type.lpszClassName=L"LumeTestDesktopHost";
    const HWND host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"",WS_POPUP,
        0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
    Expect(host&&CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD,0,0,1,1,host,nullptr,type.hInstance,nullptr),
        "create replacement host above manager");
    const lume::widget::Limits limits{340,216,320,204,720,720};
    NativeWidgetWindow widget(type.hInstance,L"Manager ceiling",limits,{});
    auto preferences=lume::widget::Defaults(limits);preferences.placed=true;preferences.x=120;preferences.y=160;
    Expect(widget.Configure(preferences)&&widget.Start(),"ceiling widget starts");
    Expect(Below(widget.Handle(),manager),"showing widget never crosses the manager even with a raised desktop host");
    for(int i=0;i<12;++i){
        SetWindowPos(host,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        desktop_level::RequestAll();
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);
        Expect(Below(widget.Handle(),manager),"automatic restacks stay below manager without clicking any widget");
    }
    type.lpszClassName=L"LumeSharedManagerSurfaceTest";Expect(RegisterClassExW(&type)!=0,"register manager glass");
    const HWND glass=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"",WS_POPUP|WS_VISIBLE,
        0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
    Expect(glass&&SetPropW(glass,L"LumeGlassSurfaceOwner",manager),"create manager glass");
    Expect(!desktop_level::InDesktopLayer(glass),"manager glass is not a desktop widget surface");
    SetWindowPos(glass,manager,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    desktop_level::Place(widget.Handle(),false);
    Expect(Below(widget.Handle(),glass),"widget stays below manager glass as well as its owner");
    widget.Stop();DestroyWindow(glass);DestroyWindow(host);DestroyWindow(manager);
    return 0;
}
// A clicked widget is lifted above other windows; once another window takes
// the foreground it must sink back, so closing or minimizing that window does
// not reveal the widget over the user's work.
int DeselectRegression(){
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=DefWindowProcW;
    type.lpszClassName=L"LumeTestWorkApplication";Expect(RegisterClassExW(&type)!=0,"register stand-in application");
    type.lpszClassName=L"LumeTestDesktopHost";
    const HWND host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
    Expect(host&&CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD,0,0,1,1,host,nullptr,type.hInstance,nullptr),"create stand-in host");
    SetWindowPos(host,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    const HWND work=CreateWindowExW(0,L"LumeTestWorkApplication",L"Work",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,300,200,nullptr,nullptr,type.hInstance,nullptr);
    const HWND other=CreateWindowExW(0,L"LumeTestWorkApplication",L"Other",WS_OVERLAPPEDWINDOW,60,60,300,200,nullptr,nullptr,type.hInstance,nullptr);
    Expect(work&&other,"create stand-in applications");
    const lume::widget::Limits limits{340,216,320,204,720,720};
    NativeWidgetWindow widget(type.hInstance,L"Deselect",limits,{});
    auto preferences=lume::widget::Defaults(limits);preferences.placed=true;preferences.x=120;preferences.y=160;
    Expect(widget.Configure(preferences)&&widget.Start(),"deselect widget starts");
    desktop_level::Watcher watcher;Expect(watcher.Start(type.hInstance),"watcher starts");
    MSG message{};
    auto pump=[&](unsigned ms){for(const auto end=GetTickCount64()+ms;GetTickCount64()<end;){while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);Sleep(10);}};
    pump(200);
    desktop_level::Select(widget.Handle());pump(100);
    Expect(Below(work,widget.Handle()),"a clicked widget is lifted above the application");
    ShowWindow(other,SW_SHOW);SetForegroundWindow(other);pump(600);
    // Without foreground rights in a test session, deliver the change the hook would see.
    if(GetForegroundWindow()!=other){desktop_level::Watcher::ForegroundChanged(other);pump(300);}
    ShowWindow(other,SW_MINIMIZE);pump(300);
    const bool sank=Below(widget.Handle(),work);
    watcher.Stop();widget.Stop();DestroyWindow(other);DestroyWindow(work);DestroyWindow(host);
    if(!sank)std::cerr<<"FAIL: a widget clicked earlier reappears over the user's application\n";
    return sank?0:1;
}
// The desktop layer keeps fixed bands: ordinary widgets below the icon
// organizer, both below the Dock. Clicking, showing or an outside lift of a
// widget must never put it over the organizer or the Dock it overlaps.
int BandRegression(){
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=DefWindowProcW;
    type.lpszClassName=lume::window_ids::DockIconsClass;Expect(RegisterClassExW(&type)!=0,"register stand-in Dock");
    // The previous regression minimizes a window; let Windows finish
    // activating the next one first, or it lands inside this fixture's layer.
    MSG message{};
    for(const auto end=GetTickCount64()+400;GetTickCount64()<end;Sleep(10))
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);
    const HWND host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"LumeTestDesktopHost",L"",WS_POPUP,
        0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
    Expect(host&&CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD,0,0,1,1,host,nullptr,type.hInstance,nullptr),
        "create band stand-in host");
    Expect(desktop_level::Host()==host,"band stand-in is the topmost desktop host");
    const lume::widget::Limits limits{340,216,320,204,720,720};
    auto at=[&](int x,int y){auto p=lume::widget::Defaults(limits);p.placed=true;p.x=x;p.y=y;return p;};
    NativeWidgetWindow widget(type.hInstance,L"Band widget",limits,{});
    WidgetWindowPorts shelfPorts;shelfPorts.shelf=true;
    NativeWidgetWindow shelf(type.hInstance,L"Band shelf",limits,shelfPorts);
    NativeWidgetWindow later(type.hInstance,L"Band later widget",limits,{});
    Expect(widget.Configure(at(120,160))&&widget.Start(),"band widget starts");
    Expect(shelf.Configure(at(200,200))&&shelf.Start(),"band shelf starts");
    const HWND dock=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,lume::window_ids::DockIconsClass,L"",WS_POPUP|WS_VISIBLE,
        100,300,400,80,nullptr,nullptr,type.hInstance,nullptr);
    Expect(dock&&SetWindowPos(dock,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),"stand-in Dock above the layer");
    const auto ordered=[&](HWND w){return Below(host,w)&&Below(w,shelf.Handle())&&Below(w,dock)&&Below(shelf.Handle(),dock);};
    const bool started=ordered(widget.Handle());
    desktop_level::Select(widget.Handle());
    const bool clicked=ordered(widget.Handle());
    {desktop_level::PlacementScope lift(widget.Handle());
     SetWindowPos(widget.Handle(),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
    Expect(Below(dock,widget.Handle()),"fixture lifts the widget over the Dock");
    desktop_level::Place(widget.Handle(),false);
    const bool restored=ordered(widget.Handle());
    Expect(later.Configure(at(140,180))&&later.Start(),"later widget starts");
    const bool joined=ordered(later.Handle())&&ordered(widget.Handle());
    desktop_level::Select(widget.Handle());
    const bool reordered=Below(later.Handle(),widget.Handle())&&ordered(widget.Handle())&&ordered(later.Handle());
    later.Stop();shelf.Stop();widget.Stop();DestroyWindow(dock);DestroyWindow(host);
    if(!started)std::cerr<<"FAIL: a started widget sits below the organizer and the Dock\n";
    if(!clicked)std::cerr<<"FAIL: clicking a widget never lifts it over the organizer or the Dock\n";
    if(!restored)std::cerr<<"FAIL: a widget lifted over the Dock returns below it on restack\n";
    if(!joined)std::cerr<<"FAIL: a newly shown widget joins below the organizer\n";
    if(!reordered)std::cerr<<"FAIL: a click still brings a widget to the top of its own band\n";
    return started&&clicked&&restored&&joined&&reordered?0:1;
}
// A widget drawn anew every frame (the spectrum) takes the mouse only on its
// fixed hit shape; its frames show through a click-through display window
// that stays directly below it and follows it.
int SplitHitRegression(){
    const HWND host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"LumeTestDesktopHost",L"",WS_POPUP,
        0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Expect(host&&CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD,0,0,1,1,host,nullptr,GetModuleHandleW(nullptr),nullptr),
        "create split stand-in host");
    const lume::widget::Limits limits{400,200,200,60,800,400};
    WidgetWindowPorts ports;ports.glass=false;ports.hoverPane=false;ports.bottom=true;ports.showCloseButton=false;
    ports.draw=[](WidgetCanvas& canvas,float w,float h){canvas.Rounded({0,0,w,h},0,0xff3366,1);};
    ports.hitShape=[](float w,float h){return std::vector<WidgetRect>{{20,h-30,w-40,20}};};
    NativeWidgetWindow widget(GetModuleHandleW(nullptr),L"Split hit",limits,ports);
    auto preferences=lume::widget::Defaults(limits);preferences.placed=true;preferences.x=160;preferences.y=180;
    Expect(widget.Configure(preferences)&&widget.Start(),"split widget starts");
    MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);
    HWND display=nullptr;
    for(HWND w=FindWindowExW(nullptr,nullptr,L"LumeWidgetDisplaySurface",nullptr);w;w=FindWindowExW(nullptr,w,L"LumeWidgetDisplaySurface",nullptr))
        if(GetPropW(w,lume::window_ids::DesktopCompanionOwnerProp)==widget.Handle())display=w;
    Expect(display!=nullptr,"split widget creates its display window");
    const auto matches=[&]{RECT a{},b{};GetWindowRect(widget.Handle(),&a);GetWindowRect(display,&b);return EqualRect(&a,&b)!=FALSE;};
    RECT r{};GetWindowRect(widget.Handle(),&r);
    const int scale=static_cast<int>(GetDpiForWindow(widget.Handle()));
    const POINT drawnOnly{(r.left+r.right)/2,r.top+MulDiv(40,scale,96)};
    const POINT grip{(r.left+r.right)/2,r.bottom-MulDiv(20,scale,96)};
    const bool shown=IsWindowVisible(display)&&GetWindow(widget.Handle(),GW_HWNDNEXT)==display&&matches();
    const HWND overDrawing=WindowFromPoint(drawnOnly);
    const bool through=overDrawing!=widget.Handle()&&overDrawing!=display;
    const bool grabbed=WindowFromPoint(grip)==widget.Handle();
    SetWindowPos(widget.Handle(),nullptr,r.left+60,r.top+40,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    const bool followed=matches();
    desktop_level::Place(widget.Handle(),false);
    const bool paired=GetWindow(widget.Handle(),GW_HWNDNEXT)==display;
    ShowWindow(widget.Handle(),SW_HIDE);
    const bool hidden=!IsWindowVisible(display);
    widget.Stop();DestroyWindow(host);
    if(!shown)std::cerr<<"FAIL: the display window shows directly below its widget\n";
    if(!through)std::cerr<<"FAIL: drawn content outside the hit shape lets clicks through\n";
    if(!grabbed)std::cerr<<"FAIL: the fixed hit shape takes the mouse\n";
    if(!followed)std::cerr<<"FAIL: the display window follows a moved widget\n";
    if(!paired)std::cerr<<"FAIL: restacking keeps the display window below its widget\n";
    if(!hidden)std::cerr<<"FAIL: hiding the widget hides its display window\n";
    return shown&&through&&grabbed&&followed&&paired&&hidden?0:1;
}
// Windows refuses a z-order request against a window owned by a higher-
// integrity or protected process, and that refusal also cancels the requested
// show. A refused anchor must not leave a desktop widget invisible.
int PlacementRefusalRegression(){
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=DefWindowProcW;
    type.lpszClassName=L"LumeTestRefusedAnchor";Expect(RegisterClassExW(&type)!=0,"register refused-anchor class");
    const HWND widget=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"",WS_POPUP,
        200,200,120,80,nullptr,nullptr,type.hInstance,nullptr);
    const HWND host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"",WS_POPUP,
        0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
    Expect(widget&&host&&!IsWindowVisible(widget),"create a hidden widget and its host");
    const UINT flags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW;
    const HWND unusable=reinterpret_cast<HWND>(static_cast<INT_PTR>(0x7FFE1234));
    // An unusable anchor is skipped: the host anchor still places and shows it.
    const bool placed=desktop_level::RequestPlacement(widget,unusable,flags,host);
    const bool fellBack=placed&&IsWindowVisible(widget);
    ShowWindow(widget,SW_HIDE);
    // With no usable anchor at all it must still appear, not stay hidden.
    const bool ordered=desktop_level::RequestPlacement(widget,unusable,flags,nullptr);
    const bool shownAnyway=!ordered&&IsWindowVisible(widget);
    DestroyWindow(widget);DestroyWindow(host);
    if(!fellBack)std::cerr<<"FAIL: a refused anchor keeps a desktop widget hidden\n";
    if(!shownAnyway)std::cerr<<"FAIL: placement never shows a widget when no anchor is usable\n";
    return fellBack&&shownAnyway?0:1;
}
int main(int argc,char** argv){
    const auto initialAwareness=GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Expect(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
        "shared widget tests use the shell's per-monitor DPI context");
    if(std::getenv("LUME_WIDGET_DRAG_TRACE"))
        std::cout<<"widget DPI awareness before="<<initialAwareness
            <<" after="<<GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext())<<'\n';
    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    if(argc==2&&std::string(argv[1])=="--desktop-level"){
        const int result=DesktopLevelRegression()|PlacementRefusalRegression()|GlassPairRegression()|ManagerCeilingRegression()|DeselectRegression()|
            BandRegression()|SplitHitRegression();CoUninitialize();
        if(!result)std::cout<<"desktop level regression passed\n";
        return result;
    }
    if(argc==3&&std::string(argv[1])=="--capture-presentation"){
        const int result=widget_presentation_capture::Run(argv[2]);CoUninitialize();return result;
    }
    if(argc==2&&(std::string(argv[1])=="--snap-dedup"||std::string(argv[1])=="--snap-handoff")){
        const int result=SnapRegression(std::string(argv[1])=="--snap-handoff");CoUninitialize();return result;
    }
    if(argc==2&&std::string(argv[1])=="--snap-release"){
        const int result=SnapReleaseRegression();CoUninitialize();return result;
    }
    if(argc==2&&std::string(argv[1])=="--snap-system-release"){
        const int horizontal=SystemMoveReleaseRegression(true);
        const int vertical=SystemMoveReleaseRegression(false);
        CoUninitialize();return horizontal||vertical;
    }
    if(argc==2&&std::string(argv[1])=="--snap-dpi-anchor"){
        const int result=DpiDragAnchorRegression();CoUninitialize();return result;
    }
    Expect(argc==1,"Usage: lume-native-widget-window-test [--snap-dedup|--snap-handoff|--snap-release|--snap-system-release|--snap-dpi-anchor|--capture-presentation <directory>]");
    Expect(SnapRegression(false)==0&&SnapRegression(true)==0,"snap redraw and interaction handoff regressions");
    HMODULE glass=LoadLibraryW(L"LumeGlass.dll");Expect(glass!=nullptr,"shared glass library");
    auto address=GetProcAddress(glass,"LumeMaterialStatistics");LumeMaterialStatisticsFn statistics=nullptr;
    std::memcpy(&statistics,&address,sizeof(statistics));Expect(statistics!=nullptr,"statistics export");
    const auto stats=[&]{LumeMaterialStatisticsData s{};s.size=sizeof(s);Expect(statistics(&s)!=0,"material statistics");return s;};
    {
        const lume::widget::Limits limits{340,216,320,204,720,720};
        unsigned actions=0,events=0,panelRequests=0,inputEvents=0,geometryEvents=0,paints=0;
        WidgetWindowPorts ports;
        ports.draw=[&](WidgetCanvas& c,float,float){++paints;c.Text(L"CPU",{20,20,100,20},12);};
        ports.action=[&](WidgetAction action,const auto&){if(action==WidgetAction::Close)++actions;};
        ports.event=[&](HWND,UINT message,WPARAM,LPARAM)->std::optional<LRESULT>{
            if(message==WM_TIMER){++events;return 71;}
            if(message==WM_LBUTTONDBLCLK||message==WM_CONTEXTMENU||message==WM_COMMAND) {
                ++inputEvents;return 72;
            }
            if(message==WM_SIZE||message==WM_MOVE||message==WM_DPICHANGED)++geometryEvents;
            return {};
        };
        ports.panels=[&](float,float){
            ++panelRequests;return std::vector<LumeMaterialPanel>{{20,40,100,40,8},{20,90,100,40,8}};
        };
        NativeWidgetWindow first(GetModuleHandleW(nullptr),L"Performance verification",limits,ports);
        ports.showCloseButton=false;
        ports.materialBottomInset=[](float,float){return 36u;};
        NativeWidgetWindow second(GetModuleHandleW(nullptr),L"Shared material verification",limits,ports);
        Expect(!first.Handle()&&stats().users==0,"no resources before enable");
        auto p=lume::widget::Defaults(limits);p.placed=true;p.x=60;p.y=80;
        Expect(first.Configure(p)&&first.Start(),"first native window starts");
        p.x=460;
        Expect(second.Configure(p)&&second.Start(),"second native window starts");
        const auto beforeMaterialReady=paints;
        SendMessageW(second.Handle(),LumeMaterialStatusChangedMessage,0,0);
        Expect(paints==beforeMaterialReady+1,"material readiness refreshes the layered foreground");
        auto shared=stats();
        Expect(shared.users==2&&shared.devices==1,"two material leases share one GPU device");
        Expect(panelRequests>=2,"business panels are passed through the common material lease");
        Expect(SendMessageW(first.Handle(),WM_TIMER,123,0)==71&&events==1,"business events remain feature owned");
        Expect(SendMessageW(first.Handle(),WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(50,80))==72&&inputEvents==1,
               "double click reaches feature instead of being lost in shared window");
        Expect((GetClassLongPtrW(first.Handle(),GCL_STYLE)&CS_DBLCLKS)!=0,
               "native window class requests actual double click messages");
        Expect(SendMessageW(first.Handle(),WM_CONTEXTMENU,0,MAKELPARAM(50,80))==72&&
               SendMessageW(first.Handle(),WM_COMMAND,123,0)==72&&inputEvents==3,
               "feature context menu and editor commands are routed");
        Expect(geometryEvents>0,"feature observes actual geometry changes");
        auto withoutClose=second.Bounds();withoutClose.locked=true;
        Expect(second.Configure(withoutClose),"configure feature with its own close interaction");
        const int closeX=MulDiv(withoutClose.width-16,static_cast<int>(GetDpiForWindow(second.Handle())),96);
        const int closeY=MulDiv(12,static_cast<int>(GetDpiForWindow(second.Handle())),96);
        SendMessageW(second.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(closeX,closeY));
        SendMessageW(second.Handle(),WM_LBUTTONUP,0,MAKELPARAM(closeX,closeY));
        Expect(actions==0,"hidden shared close button has no leftover active hitbox");
        Expect(first.Bounds().width==340&&first.Bounds().height==216,"original DIP dimensions");
        Expect(widget_snap::Compatible(first.Handle())&&widget_snap::Compatible(second.Handle()),
               "all shared widgets participate in the shared snap protocol");
        RECT neighbor{},moving{};GetWindowRect(second.Handle(),&neighbor);GetWindowRect(first.Handle(),&moving);
        const auto width=moving.right-moving.left,height=moving.bottom-moving.top;
        moving={neighbor.left-width-7,neighbor.top,neighbor.left-7,neighbor.top+height};
        widget_snap::BeginInteraction(first.Handle());
        SendMessageW(first.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));
        Expect(moving.right==neighbor.left-8&&moving.top==neighbor.top,
               "shared widgets actively snap to neighbors using the original eight pixel gap");
        MSG pending{};
        while(PeekMessageW(&pending,nullptr,0,0,PM_REMOVE)){TranslateMessage(&pending);DispatchMessageW(&pending);}
        Expect(GetPropW(first.Handle(),widget_snap::ActiveEdges)&&GetPropW(second.Handle(),widget_snap::ActiveEdges),
               "both aligned shared widgets expose their guide edges");
        SendMessageW(first.Handle(),WM_EXITSIZEMOVE,0,0);
        originalProcedure=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(first.Handle(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(ObserveDrag)));
        POINT expected{40,50};Expect(ClientToScreen(first.Handle(),&expected)!=FALSE,"screen coordinates");
        SendMessageW(first.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,50));
        Expect(dragCount==1&&dragPoint==MAKELPARAM(expected.x,expected.y),"drag must pass screen coordinates to nonclient message");
        auto locked=first.Bounds();locked.locked=true;Expect(first.Configure(locked),"lock window");
        SendMessageW(first.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,50));
        Expect(dragCount==1,"locked window cannot start dragging");
        SetWindowLongPtrW(first.Handle(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(originalProcedure));
        SendMessageW(first.Handle(),WM_CLOSE,0,0);Expect(actions==1,"close dispatches persistence action");
        first.Stop();Expect(!first.Handle()&&stats().users==1,"closing one does not close the other");
        Expect(second.Paint(),"remaining widget still draws");
        second.Stop();shared=stats();
        Expect(shared.users==0&&shared.devices==0&&shared.captures==0,"last lease releases shared resources");
        for(int i=0;i<3;++i){Expect(first.Start(),"restart");first.Stop();}
        Expect(stats().users==0,"repeated lifecycle releases leases");
    }
    FreeLibrary(glass);CoUninitialize();
    std::cout<<"native widget shared material lifecycle passed\n";
}
