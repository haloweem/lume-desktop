#include "platform/windows/clock/native_clock_window.h"
#include "platform/windows/clock/clock_renderer.h"
#include "platform/windows/clock/clock_window_bounds.h"
#include "platform/windows/calendar/native_calendar_window.h"
#include "platform/windows/widget_ui/widget_snap_protocol.h"
#include "platform/windows/widget_ui/LumeWidgetColorPicker.h"
#include "platform/windows/widget_ui/LumeWidgetPositionEditor.h"
#include "widget_snap_message_probe.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace {
void WriteBitmap(const lume::platform::windows::ClockBitmap& bitmap, const char* name) {
    const char* destination=std::getenv("LUME_CLOCK_PROBE_OUTPUT");
    if (!destination) return;
    std::filesystem::create_directories(destination);
    std::ofstream stream(std::filesystem::path(destination)/(std::string(name)+".bmp"),std::ios::binary);
    BITMAPFILEHEADER file{};
    file.bfType=0x4d42; file.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+static_cast<DWORD>(bitmap.pixels.size()*4);
    BITMAPINFOHEADER header{};
    header.biSize=sizeof(header); header.biWidth=static_cast<LONG>(bitmap.width);
    header.biHeight=-static_cast<LONG>(bitmap.height); header.biPlanes=1;
    header.biBitCount=32; header.biCompression=BI_RGB;
    stream.write(reinterpret_cast<const char*>(&file),sizeof(file));
    stream.write(reinterpret_cast<const char*>(&header),sizeof(header));
    // Flatten the diagnostic artifact only; the renderer's original transparent pixels stay intact.
    for (auto pixel:bitmap.pixels) {
      const unsigned inverse=255-(pixel>>24);
      const unsigned background=32*inverse/255;
      const auto opaque=0xff000000u | (std::min(255u,((pixel>>16)&255)+background)<<16) |
          (std::min(255u,((pixel>>8)&255)+background)<<8) | std::min(255u,(pixel&255)+background);
      stream.write(reinterpret_cast<const char*>(&opaque),4);
    }
}
UINT expectedPopupCount=0;
BOOL CALLBACK FindSystemMenu(HWND window,LPARAM data) {
    DWORD process=0;GetWindowThreadProcessId(window,&process);
    wchar_t className[32]{};GetClassNameW(window,className,32);
    if(process==GetCurrentProcessId()&&std::wstring(className)==L"#32768"){
        *reinterpret_cast<HWND*>(data)=window;return FALSE;
    }
    return TRUE;
}
void CALLBACK CancelPopup(HWND,UINT,UINT_PTR timer,DWORD) {
    HWND menu=nullptr;EnumThreadWindows(GetCurrentThreadId(),FindSystemMenu,reinterpret_cast<LPARAM>(&menu));
    if(menu) { ++expectedPopupCount; KillTimer(nullptr,timer); EndMenu(); }
}
const wchar_t* quittingModalClass=nullptr;
bool quittingModalWasVisible=false;
int modalExitCode=0;
void CALLBACK QuitModal(HWND,UINT,UINT_PTR timer,DWORD) {
    KillTimer(nullptr,timer);
    const HWND dialog=FindWindowW(quittingModalClass,nullptr);
    quittingModalWasVisible=dialog && IsWindowVisible(dialog);
    PostQuitMessage(modalExitCode);
}
void CALLBACK MissingQuitWatchdog(HWND,UINT,UINT_PTR timer,DWORD) {
    KillTimer(nullptr,timer);
    PostQuitMessage(-777);
}
}

void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
namespace {
void PumpSnapMessages() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,lume::platform::windows::widget_snap::Message,
        lume::platform::windows::widget_snap::Message+16,PM_REMOVE))DispatchMessageW(&message);
}
struct RestoreCursor {
    RestoreCursor(){GetCursorPos(&position);}
    ~RestoreCursor(){SetCursorPos(position.x,position.y);}
    POINT position{};
};
int SnapRegression(unsigned scenario) {
    using namespace lume;
    using namespace lume::platform::windows;
    RestoreCursor cursor;
    unsigned failures=0,positionActions=0;
    const auto verify=[&](bool valid,const char* text){
        if(!valid){++failures;std::cerr<<"FAIL: "<<text<<'\n';}
    };
    clock::Appearance appearance;appearance.x=80;appearance.y=120;
    NativeClockWindow clockWindow(GetModuleHandleW(nullptr),[&](clock::Action action){
        if(action.type==clock::ActionType::Position)++positionActions;
    });
    Check(clockWindow.Configure(clock::Style::Welfare,appearance)&&clockWindow.Start(),"clock snap regression source starts");
    calendar::Preferences preferences;preferences.enabled=true;preferences.material=calendar::Material::FrostedGlass;
    preferences.Active()->hasPosition=true;preferences.Active()->x=800;preferences.Active()->y=160;
    NativeCalendarWindow calendarWindow(GetModuleHandleW(nullptr),[](calendar::Action){},[]{});
    Check(calendarWindow.Configure(preferences)&&calendarWindow.Start(),"clock snap regression peer starts");
    RECT clockBounds{},calendarBounds{};
    GetWindowRect(clockWindow.Handle(),&clockBounds);GetWindowRect(calendarWindow.Handle(),&calendarBounds);
    if(scenario){
        test::WidgetSnapMessageProbe messages(clockWindow.Handle());
        Check(messages.Attached(),"observe real calendar-to-clock snap messages");
        const auto width=calendarBounds.right-calendarBounds.left,height=calendarBounds.bottom-calendarBounds.top;
        RECT sizing{clockBounds.left-width-8,clockBounds.top,clockBounds.left-8,clockBounds.top+height};
        SendMessageW(calendarWindow.Handle(),WM_ENTERSIZEMOVE,0,0);
        SendMessageW(calendarWindow.Handle(),WM_SIZING,WMSZ_RIGHT,reinterpret_cast<LPARAM>(&sizing));
        SendMessageW(calendarWindow.Handle(),WM_SIZE,SIZE_RESTORED,0);
        PumpSnapMessages();
        Check(GetPropW(clockWindow.Handle(),widget_snap::ActiveEdges)&&GetPropW(calendarWindow.Handle(),widget_snap::ActiveEdges),
            "calendar to clock handoff begins with both guides");
        if(scenario==2){
            const auto firstMessages=messages.Count();
            for(unsigned repeat=0;repeat<3;++repeat){
                SendMessageW(calendarWindow.Handle(),WM_SIZING,WMSZ_RIGHT,reinterpret_cast<LPARAM>(&sizing));
                PumpSnapMessages();
            }
            verify(messages.Count()==firstMessages,"unchanged calendar target and edge signature does not enqueue peer messages");
        }else{
            SendMessageW(clockWindow.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,20));
            PumpSnapMessages();
            verify(!GetPropW(clockWindow.Handle(),widget_snap::ActiveEdges),
                "clock drag start clears the prior calendar's received guide");
            verify(!GetPropW(calendarWindow.Handle(),widget_snap::ActiveEdges),
                "clock drag start clears the previous calendar source guide");
            SendMessageW(clockWindow.Handle(),WM_LBUTTONUP,0,MAKELPARAM(20,20));
        }
        SendMessageW(calendarWindow.Handle(),WM_EXITSIZEMOVE,0,0);
    }else{
        Check(SetCursorPos(clockBounds.left+20,clockBounds.top+20)!=FALSE,"place cursor inside owned clock");
        SendMessageW(clockWindow.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,20));
        Check(GetCapture()==clockWindow.Handle(),"clock owns the in-flight drag");
        const auto width=clockBounds.right-clockBounds.left,height=clockBounds.bottom-clockBounds.top;
        RECT moving{calendarBounds.left-width-7,calendarBounds.top,calendarBounds.left-7,calendarBounds.top+height};
        SendMessageW(clockWindow.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));PumpSnapMessages();
        Check(GetPropW(clockWindow.Handle(),widget_snap::ActiveEdges)&&GetPropW(calendarWindow.Handle(),widget_snap::ActiveEdges),
            "in-flight clock drag highlights both peers");
        appearance.locked=true;
        Check(clockWindow.Configure(clock::Style::Welfare,appearance),"lock the clock during its drag");
        PumpSnapMessages();
        verify(GetCapture()!=clockWindow.Handle(),"locking an in-flight clock drag releases mouse capture");
        verify(!GetPropW(clockWindow.Handle(),widget_snap::ActiveEdges)&&!GetPropW(calendarWindow.Handle(),widget_snap::ActiveEdges),
            "locking an in-flight clock drag clears both guide owners");
        RECT lockedBounds{},afterMove{};GetWindowRect(clockWindow.Handle(),&lockedBounds);
        Check(SetCursorPos(clockBounds.left+70,clockBounds.top+60)!=FALSE,"advance cursor after lock");
        SendMessageW(clockWindow.Handle(),WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(70,60));
        GetWindowRect(clockWindow.Handle(),&afterMove);
        verify(EqualRect(&lockedBounds,&afterMove)!=FALSE,"a locked in-flight clock cannot continue moving");
        SendMessageW(clockWindow.Handle(),WM_LBUTTONUP,0,MAKELPARAM(70,60));
        verify(positionActions==0,"a canceled clock drag cannot persist a position on button release");
    }
    clockWindow.Stop();calendarWindow.Stop();PumpSnapMessages();
    return failures?1:0;
}
}
int main(int argc,char** argv) {
  try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    using namespace lume;
    using namespace lume::platform::windows;
    if(argc==2&&(std::strcmp(argv[1],"--snap-lock")==0||std::strcmp(argv[1],"--snap-handoff")==0||
        std::strcmp(argv[1],"--snap-calendar-dedup")==0))
        return SnapRegression(std::strcmp(argv[1],"--snap-calendar-dedup")==0?2u:
            std::strcmp(argv[1],"--snap-handoff")==0?1u:0u);
    Check(argc==1,"Usage: lume-native-clock-window-test [--snap-lock|--snap-handoff|--snap-calendar-dedup]");
    Check(SnapRegression(0)==0&&SnapRegression(1)==0&&SnapRegression(2)==0,"clock snap lock, handoff and calendar dedup regressions");
    for(unsigned dpi:{96u,120u,144u,168u,192u,288u}) {
      const SIZE size{MulDiv(721,static_cast<int>(dpi),96),MulDiv(165,static_cast<int>(dpi),96)};
      const auto grip=ClockGrip(size,dpi);
      // Laptops, 1080p, 4K, portrait, left/above secondary screens and taskbar offsets.
      for(RECT work:{RECT{0,0,1366,728},RECT{0,0,1920,1040},RECT{0,0,3840,2080},
          RECT{-1920,0,0,1040},RECT{0,-2160,3840,0},RECT{2560,80,4000,2560},RECT{60,40,1920,1080}}) {
        const LONG y=work.top+100;
        for(LONG x:{work.left-size.cx/2,work.right-size.cx/2}) {
          RECT crossing{x,y,x+size.cx,y+size.cy};
          Check(ClockHasGrip(crossing,work,grip),"matrix keeps partially visible canvas at both horizontal edges");
          auto kept=RecoverClockPosition({x,y},size,work,grip);
          Check(kept.x==x&&kept.y==y,"matrix retains reachable edge coordinates");
        }
        for(POINT lost:{POINT{-32768,-32768},POINT{32767,32767}}) {
          auto recovered=RecoverClockPosition(lost,size,work,grip);
          Check(ClockHasGrip({recovered.x,recovered.y,recovered.x+size.cx,recovered.y+size.cy},work,grip),
                "matrix recovers disconnected screen placement with visible grip");
        }
      }
    }
    SYSTEMTIME time{2026,9,4,24,15,6,0,0};
    ClockRenderer renderer;
    Check(renderer.Initialize(), "private original fonts must load");
    for (clock::Style style : {clock::Style::Welfare,clock::Style::Mond,clock::Style::InsideDream}) {
      ClockBitmap bitmap;
      Check(renderer.Render(style, clock::Appearance{},time,96,bitmap), "all original TTF/OTF faces render");
      std::size_t ink = 0, transparent = 0;
      for (auto pixel : bitmap.pixels) { if (pixel >> 24) ++ink; else ++transparent; }
      Check(ink > 500 && transparent > ink, "layered bitmap has text and transparent background");
      WriteBitmap(bitmap,clock::StyleName(style));
      clock::Appearance invisible; invisible.customColor=true; invisible.alpha=0;
      Check(renderer.Render(style,invisible,time,144,bitmap), "DPI render succeeds");
      for (auto pixel : bitmap.pixels) Check(pixel == 0, "zero alpha retains transparency");
    }
    NativeClockWindow* active = nullptr;
    unsigned actions=0;
    clock::Action lastAction{};
    NativeClockWindow window(GetModuleHandleW(nullptr), [&](clock::Action action) {
      ++actions; lastAction=action;
      if (action.type == clock::ActionType::Enable && !action.value) active->Stop();
    });
    active=&window;
    Check(!window.Handle() && !window.IsRunning(), "initial state stopped");
    Check(window.Configure(clock::Style::Welfare,clock::Appearance{}), "configure valid");
    clock::Appearance invalid; invalid.scale=49;
    Check(!window.Configure(clock::Style::Mond,invalid), "reject invalid appearance");
    Check(window.Start() && window.Handle() && window.IsRunning(), "starts native layered window");
    const auto ex=GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE);
    Check((ex & WS_EX_LAYERED) && (ex & WS_EX_TOOLWINDOW) && (ex & WS_EX_NOACTIVATE), "desktop window styles");
    Check(!(ex & WS_EX_TOPMOST), "clock is not always on top");
    Check(widget_snap::Compatible(window.Handle()),"clock advertises generic snap protocol");
    calendar::Preferences calendarPrefs;calendarPrefs.enabled=true;calendarPrefs.material=calendar::Material::FrostedGlass;
    NativeCalendarWindow calendarWindow(GetModuleHandleW(nullptr),[](calendar::Action){},[]{});
    Check(calendarWindow.Configure(calendarPrefs)&&calendarWindow.Start(),"owned calendar neighbor starts");
    SendMessageW(window.Handle(),widget_snap::Message,widget_snap::Left,reinterpret_cast<LPARAM>(calendarWindow.Handle()));
    Check(reinterpret_cast<UINT_PTR>(GetPropW(window.Handle(),widget_snap::ActiveEdges))==widget_snap::Left,"calendar can highlight owned clock edge");
    SendMessageW(window.Handle(),widget_snap::Message,0,reinterpret_cast<LPARAM>(calendarWindow.Handle()));
    Check(!GetPropW(window.Handle(),widget_snap::ActiveEdges),"calendar clears owned clock edge");
    RECT clockBounds{},calendarBounds{};GetWindowRect(window.Handle(),&clockBounds);GetWindowRect(calendarWindow.Handle(),&calendarBounds);
    const int proposedWidth=calendarBounds.right-calendarBounds.left,proposedHeight=calendarBounds.bottom-calendarBounds.top;
    RECT proposed{clockBounds.left-proposedWidth-8,clockBounds.top,clockBounds.left-8,clockBounds.top+proposedHeight};
    SendMessageW(calendarWindow.Handle(),WM_ENTERSIZEMOVE,0,0);
    SendMessageW(calendarWindow.Handle(),WM_SIZING,WMSZ_RIGHT,reinterpret_cast<LPARAM>(&proposed));
    MSG snapMessage{};while(PeekMessageW(&snapMessage,nullptr,0,0,PM_REMOVE)){TranslateMessage(&snapMessage);DispatchMessageW(&snapMessage);}
    Check(reinterpret_cast<UINT_PTR>(GetPropW(window.Handle(),widget_snap::ActiveEdges))==widget_snap::Left,"resize detection highlights actual adjacent clock edge");
    SendMessageW(calendarWindow.Handle(),WM_EXITSIZEMOVE,0,0);
    while(PeekMessageW(&snapMessage,nullptr,0,0,PM_REMOVE)){TranslateMessage(&snapMessage);DispatchMessageW(&snapMessage);}
    Check(!GetPropW(window.Handle(),widget_snap::ActiveEdges),"resize end clears neighboring highlight");
    SendMessageW(window.Handle(),widget_snap::Message,widget_snap::Left|widget_snap::Linger,reinterpret_cast<LPARAM>(calendarWindow.Handle()));
    const auto deadline=GetTickCount64()+310;do{while(PeekMessageW(&snapMessage,nullptr,0,0,PM_REMOVE)){TranslateMessage(&snapMessage);DispatchMessageW(&snapMessage);}Sleep(5);}while(GetTickCount64()<deadline);
    Check(!GetPropW(window.Handle(),widget_snap::ActiveEdges),"drag linger guide expires after 260ms");
    RECT movingClock{calendarBounds.left-(clockBounds.right-clockBounds.left)-7,calendarBounds.top,
                     calendarBounds.left-7,calendarBounds.top+(clockBounds.bottom-clockBounds.top)};
    SendMessageW(window.Handle(),WM_MOVING,0,reinterpret_cast<LPARAM>(&movingClock));
    Check(movingClock.right==calendarBounds.left-8&&movingClock.top==calendarBounds.top,
          "clock actively snaps toward another widget using the shared physical pixel policy");
    while(PeekMessageW(&snapMessage,nullptr,0,0,PM_REMOVE)){TranslateMessage(&snapMessage);DispatchMessageW(&snapMessage);}
    Check(GetPropW(window.Handle(),widget_snap::ActiveEdges)&&GetPropW(calendarWindow.Handle(),widget_snap::ActiveEdges),
          "active clock movement highlights both aligned widgets");
    window.Suspend();
    while(PeekMessageW(&snapMessage,nullptr,0,0,PM_REMOVE)){TranslateMessage(&snapMessage);DispatchMessageW(&snapMessage);}
    Check(!GetPropW(window.Handle(),widget_snap::ActiveEdges)&&!GetPropW(calendarWindow.Handle(),widget_snap::ActiveEdges),
          "suspending the active snap source clears both guide owners");
    window.Resume();
    calendarWindow.Stop();
    // Transparent clock canvas may straddle a screen edge; it must not be
    // forced wholly into one monitor on move, refresh, or restart.
    for(auto edgeStyle:{clock::Style::Welfare,clock::Style::Mond,clock::Style::InsideDream}) {
      Check(window.Configure(edgeStyle,clock::Appearance{}),"prepare edge test style");
      MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
      Check(GetMonitorInfoW(MonitorFromWindow(window.Handle(),MONITOR_DEFAULTTONEAREST),&monitor)!=FALSE,"clock test monitor");
      RECT initial{};GetWindowRect(window.Handle(),&initial);
      clock::Appearance edge;edge.x=monitor.rcWork.right-(initial.right-initial.left)/2;
      edge.y=monitor.rcWork.top+80;
      Check(window.Configure(edgeStyle,edge),"configure clock across monitor edge");
      RECT crossed{};GetWindowRect(window.Handle(),&crossed);
      Check(crossed.right>monitor.rcWork.right,"clock canvas can straddle right screen edge");
      SendMessageW(window.Handle(),WM_TIMECHANGE,0,0);
      RECT refreshed{};GetWindowRect(window.Handle(),&refreshed);
      Check(refreshed.left==crossed.left,"minute refresh preserves edge position");
      window.Stop();Check(window.Start(),"restart clock at edge");
      GetWindowRect(window.Handle(),&refreshed);
      Check(refreshed.left==crossed.left,"restart preserves edge position");
      edge.x=monitor.rcWork.left-(initial.right-initial.left)/2;
      Check(window.Configure(edgeStyle,edge),"configure clock across left screen edge");
      GetWindowRect(window.Handle(),&refreshed);
      Check(refreshed.left<monitor.rcWork.left&&refreshed.right>monitor.rcWork.left,"clock can use left edge without canvas clamp");
      edge.x=monitor.rcWork.left+80;edge.y=monitor.rcWork.top-30;
      Check(window.Configure(edgeStyle,edge),"configure partially above screen");
      GetWindowRect(window.Handle(),&refreshed);
      Check(refreshed.top<monitor.rcWork.top&&refreshed.bottom>monitor.rcWork.top,"clock can use top edge without canvas clamp");
      edge.x=32767;edge.y=32767;
      Check(window.Configure(edgeStyle,edge),"recover inaccessible saved clock");
      GetWindowRect(window.Handle(),&refreshed);
      Check(MonitorFromRect(&refreshed,MONITOR_DEFAULTTONULL)!=nullptr,"fully offscreen clock remains recoverable");
      Check(window.Configure(clock::Style::Welfare,clock::Appearance{}),"restore isolated test placement");
    }
    window.Center();
    Check(actions==1 && lastAction.type==clock::ActionType::Position, "center emits position for controller acceptance");
    const auto beforeCancel=actions;
    const UINT_PTR cancelTimer=SetTimer(nullptr,0,30,CancelPopup);
    Check(cancelTimer!=0,"cancel probe timer");
    SendMessageW(window.Handle(),WM_CONTEXTMENU,0,MAKELPARAM(100,100));
    KillTimer(nullptr,cancelTimer);
    Check(expectedPopupCount==1 && actions==beforeCancel,"original popup cancellation leaves settings untouched");
    clock::Appearance locked; locked.locked=true;
    Check(window.Configure(clock::Style::Welfare,locked),"lock clock");
    SendMessageW(window.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,20));
    Check(GetCapture()!=window.Handle(),"locked clock does not capture drag");
    const auto lockedCancelTimer=SetTimer(nullptr,0,30,CancelPopup);
    SendMessageW(window.Handle(),WM_CONTEXTMENU,0,MAKELPARAM(100,100));
    KillTimer(nullptr,lockedCancelTimer);
    Check(expectedPopupCount==2 && actions==beforeCancel,"locked clock still supports original context menu");
    ULONG_PTR graphicsToken=0;
    Gdiplus::GdiplusStartupInput graphicsInput;
    Check(Gdiplus::GdiplusStartup(&graphicsToken,&graphicsInput,nullptr)==Gdiplus::Ok,"shared picker graphics startup");
    for (bool colorPicker : {true,false}) {
      quittingModalClass=colorPicker ? L"LumeWidgetColorPickerWindow" : L"LumeWidgetPositionEditorWindow";
      quittingModalWasVisible=false;
      modalExitCode=colorPicker ? 71 : 72;
      const auto quitTimer=SetTimer(nullptr,0,30,QuitModal);
      Check(quitTimer!=0,"modal quit probe timer");
      LumeWidgetColor color{12,34,56,78};
      int positionX=-123,positionY=456;
      const bool accepted=colorPicker ? LumeWidgetColorPicker::Show(window.Handle(),color) :
          LumeWidgetPositionEditor::Show(window.Handle(),positionX,positionY);
      KillTimer(nullptr,quitTimer);
      Check(quittingModalWasVisible && !accepted,"shared modal exits without accepting changes on WM_QUIT");
      Check(color.red==12 && color.green==34 && color.blue==56 && color.alpha==78 &&
          positionX==-123 && positionY==456,"modal exit preserves original values");
      Check(!FindWindowW(quittingModalClass,nullptr),"modal quit destroys dialog HWND");
      Check(IsWindowEnabled(window.Handle()),"modal quit restores owner interaction");
      // PostQuitMessage synthesizes a low-priority WM_QUIT only after pending messages
      // have been consumed. A single filtered PeekMessage can return false even when
      // the quit is correctly queued. Exercise the application's actual message loop.
      const auto missingQuitTimer=SetTimer(nullptr,0,500,MissingQuitWatchdog);
      Check(missingQuitTimer!=0,"forwarded quit watchdog timer");
      MSG quit{};
      BOOL result=0;
      while ((result=GetMessageW(&quit,nullptr,0,0))>0) {
        TranslateMessage(&quit);
        DispatchMessageW(&quit);
      }
      KillTimer(nullptr,missingQuitTimer);
      Check(result==0 && quit.message==WM_QUIT && static_cast<int>(quit.wParam)==modalExitCode,
          "shared modal forwards WM_QUIT exit code to application loop");
    }
    Gdiplus::GdiplusShutdown(graphicsToken);
    window.Suspend(); Check(!window.IsRunning(), "suspends and releases rendering");
    window.Resume(); Check(window.IsRunning(), "resumes");
    SendMessageW(window.Handle(),WM_CLOSE,0,0);
    Check(!window.Handle() && !window.IsRunning(), "close callback can synchronously destroy window");
    Check(window.Start(), "restart after close");
    window.Dispose(); window.Dispose();
    Check(!window.Handle(), "dispose idempotent");
    std::cout << "Native clock original font rendering and lifecycle passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
