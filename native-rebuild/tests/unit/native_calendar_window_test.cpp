#include "platform/windows/calendar/native_calendar_window.h"
#include "platform/windows/calendar/calendar_renderer.h"
#include "platform/windows/calendar/calendar_month_picker.h"
#include "platform/windows/calendar_runtime.h"
#include "platform/windows/widget_ui/widget_snap_protocol.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace lume;
using namespace lume::platform::windows;
namespace {
void Check(bool v,const char* m) { if(!v) throw std::runtime_error(m); }
void Pump() { MSG m{}; while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
void PumpFor(DWORD duration) { const auto until=GetTickCount64()+duration;do{Pump();Sleep(5);}while(GetTickCount64()<until); }
void PumpSnapMessages() {MSG m{};while(PeekMessageW(&m,nullptr,widget_snap::Message,widget_snap::Message,PM_REMOVE))DispatchMessageW(&m);}
HWND DayTooltip(HWND owner) {
    struct Search {HWND owner=nullptr,result=nullptr;} search{owner,nullptr};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL{
        auto& value=*reinterpret_cast<Search*>(data);
        wchar_t name[80]{};GetClassNameW(hwnd,name,80);
        if(GetWindow(hwnd,GW_OWNER)==value.owner&&std::wstring(name)==TOOLTIPS_CLASSW){value.result=hwnd;return FALSE;}
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
struct DayToolInfo {RECT rect{};std::wstring text;};
DayToolInfo DayTool(HWND tooltip,HWND owner,std::size_t index) {
    TTTOOLINFOW info{};info.cbSize=TTTOOLINFOW_V2_SIZE;info.hwnd=owner;info.uId=index+1;
    Check(SendMessageW(tooltip,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&info))!=FALSE,"date tooltip tool remains registered");
    wchar_t text[4096]{};info.lpszText=text;
    SendMessageW(tooltip,TTM_GETTEXTW,std::size(text),reinterpret_cast<LPARAM>(&info));
    Check(text[0]!=0,"date tooltip owns complete stable text");
    return {info.rect,text};
}
void CheckDayTooltips() {
    calendar::Preferences prefs;prefs.enabled=true;prefs.material=calendar::Material::FrostedGlass;prefs.theme=calendar::Theme::Split;
    NativeCalendarWindow window(GetModuleHandleW(nullptr),[](calendar::Action){},[]{},[]{return calendar::Date{2026,9,27};});
    Check(window.Configure(prefs)&&window.Start(),"day tooltip calendar starts");
    const auto hwnd=window.Handle(),tooltip=DayTooltip(hwnd);
    Check(tooltip&&SendMessageW(tooltip,TTM_GETTOOLCOUNT,0,0)==42,"calendar owns one tooltip with all date descriptions");
    Check(!IsWindowVisible(tooltip),"date descriptions wait for hover instead of showing automatically");
    const auto indexOf=[&](calendar::Date date){
        const auto& grid=window.Model().Grid();
        const auto found=std::find_if(grid.begin(),grid.end(),[&](const auto& day){return day.date==date;});
        Check(found!=grid.end(),"tooltip fixture date in current grid");return static_cast<std::size_t>(found-grid.begin());
    };
    const auto festival=indexOf({2026,9,19}),holiday=indexOf({2026,9,25});
    const auto full=DayTool(tooltip,hwnd,festival);
    Check(full.text.find(L"2026年9月19日")!=std::wstring::npos&&
        full.text.find(L"全民国防教育日")!=std::wstring::npos,
        "hover retains full date and long festival name after visible ellipsis");
    Check(DayTool(tooltip,hwnd,holiday).text.find(L"中秋节放假")!=std::wstring::npos,
        "hover preserves the original published holiday description");
    const auto hidden=DayTool(tooltip,hwnd,0).rect;
    Check(IsRectEmpty(&hidden),"split theme hidden adjacent-month day has no hover region");
    const auto originalRect=full.rect;
    prefs.Active()->width=480;prefs.Active()->height=390;
    Check(window.Configure(prefs,true,true),"tooltip window resizes");
    const auto resized=DayTool(tooltip,hwnd,festival).rect;
    const auto cell=window.Layout().cells[festival];
    const auto dpi=GetDpiForWindow(hwnd);
    Check(!EqualRect(&resized,&originalRect)&&resized.left==static_cast<LONG>(std::lround(cell.x*dpi/96))&&
        resized.right==static_cast<LONG>(std::lround(cell.Right()*dpi/96)),
        "tooltip follows resized cells in physical pixels at the owner DPI");
    prefs.theme=calendar::Theme::Spacious;
    Check(window.Configure(prefs,true,true),"tooltip theme changes");
    const auto before=DayTool(tooltip,hwnd,0).rect;
    Check(!IsRectEmpty(&before),"spacious theme reveals adjacent-month hover descriptions");
    const auto title=window.Layout().title;
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(static_cast<int>(std::lround((title.x+title.width/2)*dpi/96)),
        static_cast<int>(std::lround((title.y+title.height/2)*dpi/96))));
    Check(window.Picker().IsOpen(),"tooltip coverage fixture opens month picker");
    const auto covered=DayTool(tooltip,hwnd,0).rect;
    Check(IsRectEmpty(&covered),"month picker suppresses the covered date's tooltip");
    SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);
    const auto restored=DayTool(tooltip,hwnd,0).rect;
    Check(!window.Picker().IsOpen()&&EqualRect(&before,&restored),"closing picker restores date tooltip geometry");
    const auto next=window.Layout().next;
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(static_cast<int>(std::lround((next.x+next.width/2)*dpi/96)),
        static_cast<int>(std::lround((next.y+next.height/2)*dpi/96))));
    Check(window.Model().BrowseAnchor().month==10&&
        DayTool(tooltip,hwnd,festival).text.find(L"2026年9月19日")==std::wstring::npos,
        "month navigation refreshes full descriptions without polling");
    window.Stop();Check(!IsWindow(tooltip),"closing calendar destroys its tooltip and releases tool text");
    Check(window.Start(),"tooltip calendar can restart");
    const auto reopened=DayTooltip(window.Handle());Check(reopened!=nullptr,"restarted calendar has a tooltip");
    DestroyWindow(window.Handle());
    Check(!IsWindow(reopened)&&!window.IsRunning(),"external owner destruction also releases day tooltip");
}
NativeCalendarWindow* firstFrameCalendar=nullptr;
unsigned monthFirstShows=0;
LRESULT CALLBACK ObserveMonthFirstShow(int code,WPARAM wp,LPARAM lp) {
    if(code>=0&&firstFrameCalendar) {
        const auto& message=*reinterpret_cast<CWPSTRUCT*>(lp);
        if(message.message==WM_SHOWWINDOW&&message.wParam&&message.hwnd==firstFrameCalendar->Picker().Handle()) {
            ++monthFirstShows;
            Check(firstFrameCalendar->Picker().MaterialReady()==
                (firstFrameCalendar->MaterialState()==WidgetMaterialState::Ready),
                "month picker inherits parent material before its first show");
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
struct InputDelivery {UINT downMessage=WM_LBUTTONDOWN,upMessage=WM_LBUTTONUP;bool down=false,up=false;};
class InputIsolation final {
public:
    explicit InputIsolation(HWND window):window_(window),wasTopmost_((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0){
        tracing_=std::getenv("LUME_CALENDAR_INPUT_TRACE")!=nullptr;
        Check(SetWindowSubclass(window_,Trace,79,reinterpret_cast<DWORD_PTR>(this))!=FALSE,"guard input-isolation window ordering");
        if(tracing_)Log("before topmost");
        Check(SetWindowPos(window_,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,
            "isolate own OS input window above unrelated desktop overlays");
        if(tracing_)Log("after topmost");
        Check(SetWindowPos(window_,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,
            "place the isolated window at the front of its topmost band");
        if(tracing_)Log("after top");
    }
    ~InputIsolation(){Restore();}
    void Restore(){
        if(window_&&IsWindow(window_))RemoveWindowSubclass(window_,Trace,79);
        if(window_&&!wasTopmost_&&IsWindow(window_)){
            desktop_level::PlacementScope placement(window_);
            SetWindowPos(window_,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        }
        window_=nullptr;
    }
private:
    void Log(const char* step)const{
        std::cout<<"input isolation "<<step<<" exStyle="<<GetWindowLongPtrW(window_,GWL_EXSTYLE)
            <<" foreground="<<GetForegroundWindow()<<" focus="<<GetFocus()<<'\n';
    }
    static LRESULT CALLBACK Trace(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<InputIsolation*>(data);
        if(self.tracing_&&(message==WM_WINDOWPOSCHANGING||message==WM_WINDOWPOSCHANGED)){
            const auto& position=*reinterpret_cast<WINDOWPOS*>(lp);
            if(!(position.flags&SWP_NOZORDER))std::cout<<"input isolation message="<<message
                <<" insertAfter="<<position.hwndInsertAfter<<" flags="<<position.flags
                <<" exStyle="<<GetWindowLongPtrW(window,GWL_EXSTYLE)<<'\n';
        }else if(self.tracing_&&(message==WM_ACTIVATE||message==WM_STYLECHANGED))
            self.Log(message==WM_ACTIVATE?"WM_ACTIVATE":"WM_STYLECHANGED");
        const auto result=DefSubclassProc(window,message,wp,lp);
        if(message==WM_WINDOWPOSCHANGED&&self.retainBand_&&!self.repairing_&&
           !(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)){
            // Repair a completed demotion, preserving ordinary ordering
            // within the band so the month popup remains above its owner.
            // Real clicks must still activate the target and acquire focus.
            self.repairing_=true;
            Check(SetWindowPos(window,HWND_TOPMOST,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"restore the isolated input band");
            Check(SetWindowPos(window,HWND_TOP,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"restore the isolated input order");
            self.repairing_=false;
        }
        return result;
    }
    HWND window_=nullptr;bool wasTopmost_=false,tracing_=false;
    bool retainBand_=true,repairing_=false;
};
void ChallengeInputIsolation(HWND window,HWND blocker,POINT point,const char* stage){
    if(!blocker)return;
    Check(SetWindowPos(blocker,HWND_TOPMOST,point.x-20,point.y-20,40,40,SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE,
        "position the owned obstruction for the next actual input path");
    Check(SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,
        "inject a later ordering request after the one-time promotion");
    std::cout<<"input isolation controlled reorder stage="<<stage
        <<" exStyle="<<GetWindowLongPtrW(window,GWL_EXSTYLE)<<'\n';
}
void CheckPointTarget(HWND expected,POINT point,const char* why){
    const auto actual=WindowFromPoint(point);
    if(actual!=expected){
        char targetClass[128]{},foregroundClass[128]{};DWORD targetProcess=0;
        GetClassNameA(actual,targetClass,128);GetWindowThreadProcessId(actual,&targetProcess);
        GetClassNameA(GetForegroundWindow(),foregroundClass,128);
        RECT expectedBounds{},actualBounds{};GetWindowRect(expected,&expectedBounds);GetWindowRect(actual,&actualBounds);
        std::cerr<<"input target mismatch point="<<point.x<<','<<point.y<<" expected="<<expected
            <<" expectedExStyle="<<GetWindowLongPtrW(expected,GWL_EXSTYLE)
            <<" expectedBounds="<<expectedBounds.left<<','<<expectedBounds.top<<','<<expectedBounds.right<<','<<expectedBounds.bottom
            <<" actual="<<actual<<" class="<<targetClass<<" pid="<<targetProcess<<" ownPid="<<GetCurrentProcessId()
            <<" actualExStyle="<<GetWindowLongPtrW(actual,GWL_EXSTYLE)
            <<" actualBounds="<<actualBounds.left<<','<<actualBounds.top<<','<<actualBounds.right<<','<<actualBounds.bottom
            <<" foreground="<<GetForegroundWindow()<<" foregroundClass="<<foregroundClass
            <<" focus="<<GetFocus()<<" capture="<<GetCapture()<<'\n';
        unsigned printed=0;
        for(auto candidate=GetTopWindow(nullptr);candidate&&printed<12;candidate=GetWindow(candidate,GW_HWNDNEXT)){
            RECT bounds{};GetWindowRect(candidate,&bounds);
            if(!IsWindowVisible(candidate)||!PtInRect(&bounds,point))continue;
            char name[128]{};GetClassNameA(candidate,name,128);
            std::cerr<<"  point z-order "<<printed++<<" hwnd="<<candidate<<" class="<<name
                <<" exStyle="<<GetWindowLongPtrW(candidate,GWL_EXSTYLE)<<'\n';
        }
    }
    Check(actual==expected,why);
}
void WaitPointTarget(HWND expected,POINT point,const char* why){
    const auto started=GetTickCount64();
    while(WindowFromPoint(point)!=expected&&GetTickCount64()-started<1000){Pump();Sleep(5);}
    const auto waited=GetTickCount64()-started;
    if(waited)std::cout<<"input target readiness waited "<<waited<<" ms, expectedExStyle="
        <<GetWindowLongPtrW(expected,GWL_EXSTYLE)<<'\n';
    CheckPointTarget(expected,point,why);
}
LRESULT CALLBACK ObserveInput(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto& delivery=*reinterpret_cast<InputDelivery*>(data);
    const auto result=DefSubclassProc(w,m,wp,lp);
    if(m==delivery.downMessage)delivery.down=true;
    if(m==delivery.upMessage)delivery.up=true;
    return result;
}
void DeliverInput(HWND w,INPUT (&input)[2],UINT down=WM_LBUTTONDOWN,UINT up=WM_LBUTTONUP) {
    InputDelivery delivery;delivery.downMessage=down;delivery.upMessage=up;
    Check(SetWindowSubclass(w,ObserveInput,23,reinterpret_cast<DWORD_PTR>(&delivery))!=FALSE,"observe own queued input delivery");
    const bool inserted=SendInput(2,input,sizeof(INPUT))==2;
    const auto deadline=GetTickCount64()+1000;
    do {Pump();if(delivery.down&&delivery.up)break;Sleep(5);}while(GetTickCount64()<deadline);
    Pump();RemoveWindowSubclass(w,ObserveInput,23);
    if(!inserted||!delivery.down||!delivery.up){
        POINT cursor{};GetCursorPos(&cursor);
        std::cerr<<"input delivery inserted="<<inserted<<" message="<<down<<" down="<<delivery.down<<" up="<<delivery.up
            <<" target="<<w<<" cursorTarget="<<WindowFromPoint(cursor)<<" foreground="<<GetForegroundWindow()
            <<" focus="<<GetFocus()<<" capture="<<GetCapture()<<" cursor="<<cursor.x<<','<<cursor.y<<'\n';
    }
    Check(inserted&&delivery.down&&delivery.up,"own OS input down and up both processed before assertion");
}
struct DpiTransition {LONG x=0;UINT dpi=96;bool fired=false;};
LRESULT CALLBACK SimulateMonitorDpi(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto& transition=*reinterpret_cast<DpiTransition*>(data);
    if(message==WM_WINDOWPOSCHANGED&&!transition.fired&&reinterpret_cast<WINDOWPOS*>(lp)->x==transition.x) {
        transition.fired=true;RECT suggested{};GetWindowRect(window,&suggested);
        SendMessageW(window,WM_DPICHANGED,MAKELONG(transition.dpi,transition.dpi),reinterpret_cast<LPARAM>(&suggested));
    }
    return DefSubclassProc(window,message,wp,lp);
}
void Save(const CalendarBitmap& bitmap,const char* name) {
    const char* output=std::getenv("LUME_CALENDAR_PROBE_OUTPUT"); if(!output) return;
    std::filesystem::create_directories(output);
    std::ofstream f(std::filesystem::path(output)/(std::string(name)+".bmp"),std::ios::binary);
    BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+static_cast<DWORD>(bitmap.pixels.size()*4);
    BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=static_cast<LONG>(bitmap.width);
    info.biHeight=-static_cast<LONG>(bitmap.height); info.biPlanes=1; info.biBitCount=32;
    f.write(reinterpret_cast<const char*>(&file),sizeof(file)); f.write(reinterpret_cast<const char*>(&info),sizeof(info));
    for(auto p:bitmap.pixels) {
      const auto bg=32*(255-(p>>24))/255;
      const auto flat=0xff000000u|(std::min(255u,((p>>16)&255)+bg)<<16)|(std::min(255u,((p>>8)&255)+bg)<<8)|std::min(255u,(p&255)+bg);
      f.write(reinterpret_cast<const char*>(&flat),4);
    }
}
void LayoutJson(std::ofstream& out,const CalendarLayout& l,calendar::Theme theme,int width,int height,bool first) {
    if(!out)return;
    if(!first)out<<',';
    out<<"{\"theme\":\""<<calendar::ThemeName(theme)<<"\",\"width\":"<<width<<",\"height\":"<<height<<",\"selectors\":{";
    const std::pair<const char*,CalendarRect> rectangles[]={{"#calendar",l.content},{".calendar-header",l.header},{"#previous",l.previous},{"#month",l.title},{"#next",l.next},{".weekdays",l.weekdays},{"#days",l.days},{"#calendar-summary",l.summary},{"#month-picker",l.picker}};
    bool firstRect=true;
    for(const auto& [name,r]:rectangles){if(!firstRect)out<<',';firstRect=false;out<<'"'<<name<<"\":{\"x\":"<<r.x<<",\"y\":"<<r.y<<",\"width\":"<<r.width<<",\"height\":"<<r.height<<'}';}
    out<<"}}";
}
void Capture(HWND window,const char* name) {
    RECT r{}; GetClientRect(window,&r);
    HDC screen=GetDC(window), dc=CreateCompatibleDC(screen); void* pixels=nullptr;
    BITMAPINFO i{}; i.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); i.bmiHeader.biWidth=r.right;
    i.bmiHeader.biHeight=-r.bottom; i.bmiHeader.biPlanes=1; i.bmiHeader.biBitCount=32;
    HBITMAP bitmap=CreateDIBSection(screen,&i,DIB_RGB_COLORS,&pixels,nullptr,0);
    auto old=SelectObject(dc,bitmap);
    Check(PrintWindow(window,dc,PW_CLIENTONLY|2)!=FALSE,"own window PrintWindow");
    CalendarBitmap frame; frame.width=static_cast<unsigned>(r.right); frame.height=static_cast<unsigned>(r.bottom);
    frame.pixels.assign(static_cast<std::uint32_t*>(pixels),static_cast<std::uint32_t*>(pixels)+frame.width*frame.height);
    Check(std::any_of(frame.pixels.begin(),frame.pixels.end(),[](auto p){return (p&0xffffff)!=0;}),"own window capture contains pixels");
    Save(frame,name); SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,screen);
}
struct PopupProbe { HWND owner=nullptr; unsigned count=0; bool stop=false; NativeCalendarWindow* window=nullptr; } probe;
BOOL CALLBACK CancelOwnPopup(HWND h,LPARAM) {
    DWORD pid=0; GetWindowThreadProcessId(h,&pid); wchar_t cls[80]{}; GetClassNameW(h,cls,80);
    if(pid==GetCurrentProcessId()&&GetWindow(h,GW_OWNER)==probe.owner&&std::wstring(cls)==L"LumeWidgetMenuWindow") {
      ++probe.count; SendMessageW(h,WM_CLOSE,0,0); if(probe.stop) probe.window->Stop(); return FALSE;
    } return TRUE;
}
void CALLBACK CancelTimer(HWND,UINT,UINT_PTR,DWORD) { EnumWindows(CancelOwnPopup,0); }
}
int main(int argc,char** argv) {
 try {
    const bool inputProbe=argc==2&&std::string(argv[1])=="--input-probe";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    calendar::Preferences prefs; prefs.enabled=true;prefs.material=calendar::Material::FrostedGlass;
    calendar::CalendarModel model({2026,9,25});
    CalendarRenderer renderer; Check(renderer.Initialize(),"system font renderer initializes");
    if(!inputProbe)CheckDayTooltips();
    std::ofstream layoutJson;
    if(const char* dir=std::getenv("LUME_CALENDAR_PROBE_OUTPUT")){std::filesystem::create_directories(dir);layoutJson.open(std::filesystem::path(dir)/"native-layout.json");layoutJson<<"{\"fixedDate\":\"2026-09-25\",\"results\":[";}
    bool firstLayout=true;
    for(auto theme:{calendar::Theme::Spacious,calendar::Theme::Split}) {
      prefs.theme=theme; model.SetTheme(theme);
      for(unsigned dpi:{96u,144u,192u}) {
        CalendarBitmap frame; Check(renderer.Render(prefs,model,dpi,frame),"renderer produces both themes at DPI");
        Check(frame.width==820*dpi/96&&frame.height==500*dpi/96,"DPI bitmap dimensions");
        for(auto p:frame.pixels) Check((p&255)<=(p>>24)&&((p>>8)&255)<=(p>>24)&&((p>>16)&255)<=(p>>24),"premultiplied BGRA");
        Check(frame.pixels.front()==0,"rounded transparent corner");
        if(dpi==96) Save(frame,theme==calendar::Theme::Spacious?"spacious-renderer":"split-renderer");
      }
      for(const auto& dimensions:{std::pair<int,int>{820,500},{320,320},{480,390},{600,420},{1440,720}}) {
        prefs.Active()->width=dimensions.first;prefs.Active()->height=dimensions.second;
        CalendarLayout geometry;CalendarBitmap frame;
        Check(renderer.Render(prefs,model,96,frame,{},&geometry),"reference breakpoint renderer");
        LayoutJson(layoutJson,geometry,theme,dimensions.first,dimensions.second,firstLayout);firstLayout=false;
        const auto name=std::string(calendar::ThemeName(theme))+"-"+std::to_string(dimensions.first)+"x"+std::to_string(dimensions.second);
        Save(frame,name.c_str());
        CalendarPickerRenderState state{2026,9,11,false,{},{2026,9,1},{2026,9,25},0,false};
        CalendarBitmap popup;Check(renderer.RenderPicker(prefs,state,geometry.picker.width,geometry.picker.height,96,popup),"picker renderer at breakpoint");
        Save(popup,(name+"-picker").c_str());
      }
      prefs.Active()->width=820;prefs.Active()->height=500;
    }
    if(layoutJson)layoutJson<<"]}";
    CalendarBitmap white,red;
    prefs.theme=calendar::Theme::Spacious;model.SetTheme(prefs.theme);
    Check(renderer.Render(prefs,model,96,white),"default colors render");
    prefs.textColor={0xff0000,128};prefs.highlightColor={0x00ff00,128};
    Check(renderer.Render(prefs,model,96,red)&&red.pixels!=white.pixels,"text and highlight RGBA change rendered pixels");
    prefs.textColor={0xffffff,255};prefs.highlightColor={0x0a84ff,255};
    prefs.theme=calendar::Theme::Spacious;
    std::vector<calendar::Action> actions; NativeCalendarWindow* active=nullptr;
    NativeCalendarWindow window(GetModuleHandleW(nullptr),[&](calendar::Action a){ actions.push_back(a); if(a.type==calendar::ActionType::Enable&&!a.value) active->Stop(); },[]{});
    active=&window; Check(window.Configure(prefs),"configure"); Check(actions.empty(),"Configure never calls action");
    const auto foreground=GetForegroundWindow(); Check(window.Start(),"start");
    Check(actions.empty(),"Start queues initial position without synchronous callback");
    const auto hwnd=window.Handle(); const auto dpi=GetDpiForWindow(hwnd);
    const auto ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE), style=GetWindowLongPtrW(hwnd,GWL_STYLE);
    Check((ex&WS_EX_LAYERED)&&(ex&WS_EX_TOOLWINDOW)&&!(ex&WS_EX_TOPMOST)&&(style&WS_POPUP),"layered popup desktop styles");
    Check(GetForegroundWindow()==foreground,"startup does not steal focus");
    RECT bounds{}; GetWindowRect(hwnd,&bounds);
    Check(bounds.right-bounds.left==MulDiv(820,static_cast<int>(dpi),96)&&bounds.bottom-bounds.top==MulDiv(500,static_cast<int>(dpi),96),"default DIP size");
    if(inputProbe)std::cout<<"calendar input probe: date tooltip "<<(DayTooltip(hwnd)?"attached":"absent")<<'\n';
    Pump(); Check(actions.size()==1&&actions.back().type==calendar::ActionType::Bounds,"posted initial position");
    Capture(hwnd,"spacious-window");
    // Exercise real OS activation and queued keyboard delivery, not only SendMessage.
    auto clickCell=window.Layout().cells[18];POINT restoreCursor{};GetCursorPos(&restoreCursor);
    ShowWindow(hwnd,SW_SHOWNOACTIVATE);SetWindowPos(hwnd,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    POINT click{bounds.left+MulDiv(static_cast<int>(clickCell.x+clickCell.width/2),static_cast<int>(dpi),96),bounds.top+MulDiv(static_cast<int>(clickCell.y+clickCell.height/2),static_cast<int>(dpi),96)};
    HWND blocker=nullptr;
    if(inputProbe){
        WNDCLASSW obstruction{};obstruction.hInstance=GetModuleHandleW(nullptr);obstruction.lpfnWndProc=DefWindowProcW;
        obstruction.lpszClassName=L"LumeOwnedInputOcclusionProbe";obstruction.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        Check(RegisterClassW(&obstruction)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"register controlled input obstruction");
        blocker=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,obstruction.lpszClassName,L"Owned input-isolation probe",
            WS_POPUP,click.x-20,click.y-20,40,40,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(blocker!=nullptr,"create controlled topmost input obstruction");
        ShowWindow(blocker,SW_SHOWNOACTIVATE);UpdateWindow(blocker);
        SetWindowPos(hwnd,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        CheckPointTarget(blocker,click,"ordinary HWND_TOP cannot isolate input from a topmost obstruction");
        std::cout<<"calendar input probe: own topmost blocker reproduces the pre-SendInput target mismatch\n";
    }
    const auto beforeIsolationForeground=GetForegroundWindow();
    InputIsolation inputIsolation(hwnd);
    Check(GetForegroundWindow()==beforeIsolationForeground,"input isolation does not pre-activate the calendar");
    if(blocker){
        WaitPointTarget(hwnd,click,"temporary topmost isolation restores own input target above the controlled blocker");
    }
    SetCursorPos(click.x,click.y);INPUT mouse[2]{};mouse[0].type=INPUT_MOUSE;mouse[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;mouse[1].type=INPUT_MOUSE;mouse[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    ChallengeInputIsolation(hwnd,blocker,click,"date");
    WaitPointTarget(hwnd,click,"OS input target is own isolated calendar");
    DeliverInput(hwnd,mouse);
    if(blocker)ShowWindow(blocker,SW_HIDE);
    Check(GetForegroundWindow()==hwnd&&GetFocus()==hwnd,"actual date click activates keyboard focus");
    const auto clicked=window.Model().Selected();INPUT key[2]{};key[0].type=INPUT_KEYBOARD;key[0].ki.wVk=VK_RIGHT;key[1]=key[0];key[1].ki.dwFlags=KEYEVENTF_KEYUP;
    Check(GetForegroundWindow()==hwnd&&GetFocus()==hwnd,"OS keyboard target remains own isolated calendar");
    DeliverInput(hwnd,key,WM_KEYDOWN,WM_KEYUP);
    Check(window.Model().Selected()==*calendar::AddDays(clicked,1),"actual queued keyboard changes date");SetCursorPos(restoreCursor.x,restoreCursor.y);
    const auto original=window.Model().Selected(); SendMessageW(hwnd,WM_KEYDOWN,VK_RIGHT,0);
    Check(window.Model().Selected()==*calendar::AddDays(original,1),"date keyboard movement");
    SendMessageW(hwnd,WM_KEYDOWN,VK_DOWN,0); Check(window.Model().Selected()==*calendar::AddDays(original,8),"week keyboard movement");
    const auto selectedBeforeTab=window.Model().Selected();SendMessageW(hwnd,WM_KEYDOWN,VK_TAB,0);SendMessageW(hwnd,WM_KEYDOWN,VK_RIGHT,0);
    Check(window.Model().Selected()==*calendar::AddDays(selectedBeforeTab,2),"arrow movement starts from tab-focused date");
    auto firstRow=window.Layout().cells[3];const auto expectedClamped=window.Model().Grid()[0].date;
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(MulDiv(static_cast<int>(firstRow.x+firstRow.width/2),static_cast<int>(dpi),96),MulDiv(static_cast<int>(firstRow.y+firstRow.height/2),static_cast<int>(dpi),96)));
    SendMessageW(hwnd,WM_KEYDOWN,VK_UP,0);Check(window.Model().Selected()==expectedClamped,"day arrow navigation clamps to first grid cell");
    auto l=window.Layout(); auto title=l.title;
    for(int n=0;n<100&&window.MaterialState()!=WidgetMaterialState::Ready;++n)PumpFor(50);
    Check(window.MaterialState()==WidgetMaterialState::Ready,"parent material ready for first-show regression");
    firstFrameCalendar=&window;
    const auto firstShowHook=SetWindowsHookExW(WH_CALLWNDPROC,ObserveMonthFirstShow,nullptr,GetCurrentThreadId());
    Check(firstShowHook!=nullptr,"month first-show observer installed");
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(MulDiv(static_cast<int>(title.x+3),static_cast<int>(dpi),96),MulDiv(static_cast<int>(title.y+3),static_cast<int>(dpi),96)));
    Check(window.Picker().IsOpen(),"month title opens independent picker");
    auto& picker=window.Picker(); const auto oldAnchor=window.Model().BrowseAnchor();
    SendMessageW(picker.Handle(),WM_KEYDOWN,VK_RIGHT,0);
    SendMessageW(picker.Handle(),WM_LBUTTONDOWN,0,MAKELPARAM(MulDiv(150,static_cast<int>(dpi),96),MulDiv(25,static_cast<int>(dpi),96)));
    SendMessageW(picker.Handle(),WM_KEYDOWN,VK_RETURN,0);
    Check(picker.FocusedMonth()==oldAnchor.month,"year Enter returns focus to original browse month");
    SendMessageW(picker.Handle(),WM_KEYDOWN,VK_NEXT,0);
    Check(picker.DraftYear()==oldAnchor.year+1&&window.Model().BrowseAnchor()==oldAnchor,"year edits stay draft");
    SendMessageW(picker.Handle(),WM_KEYDOWN,VK_HOME,0); SendMessageW(picker.Handle(),WM_KEYDOWN,VK_LEFT,0);
    Check(picker.FocusedMonth()==12,"picker wraps horizontal month navigation");
    SendMessageW(picker.Handle(),WM_KEYDOWN,VK_DOWN,0); Check(picker.FocusedMonth()==4,"picker wraps vertical month navigation");
    SendMessageW(picker.Handle(),WM_KEYDOWN,VK_RETURN,0);
    Check(!picker.IsOpen()&&window.Model().BrowseAnchor()==calendar::Date{oldAnchor.year+1,4,1},"picker commits month and year");
    Check(GetFocus()==hwnd,"selection restores calendar focus");
    title=window.Layout().title;
    const auto clickTitle=[&](){SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(MulDiv(static_cast<int>(title.x+3),static_cast<int>(dpi),96),MulDiv(static_cast<int>(title.y+3),static_cast<int>(dpi),96)));};
    clickTitle(); Check(picker.IsOpen(),"picker reopens");
    SendMessageW(picker.Handle(),WM_LBUTTONDOWN,0,MAKELPARAM(MulDiv(150,static_cast<int>(dpi),96),MulDiv(25,static_cast<int>(dpi),96)));
    for(wchar_t ch:std::wstring(L"9999"))SendMessageW(picker.Handle(),WM_CHAR,ch,0);
    SendMessageW(picker.Handle(),WM_KEYDOWN,VK_RETURN,0);SendMessageW(picker.Handle(),WM_KEYDOWN,VK_NEXT,0);
    Check(picker.DraftYear()==9999,"year upper boundary");
    SendMessageW(picker.Handle(),WM_LBUTTONDOWN,0,MAKELPARAM(MulDiv(150,static_cast<int>(dpi),96),MulDiv(25,static_cast<int>(dpi),96)));
    SendMessageW(picker.Handle(),WM_CHAR,L'0',0);SendMessageW(picker.Handle(),WM_KEYDOWN,VK_RETURN,0);
    Check(picker.DraftYear()==9999,"invalid zero year restores valid draft");
    SendMessageW(picker.Handle(),WM_LBUTTONDOWN,0,MAKELPARAM(MulDiv(150,static_cast<int>(dpi),96),MulDiv(25,static_cast<int>(dpi),96)));
    SendMessageW(picker.Handle(),WM_KEYDOWN,VK_HOME,0);SendMessageW(picker.Handle(),WM_KEYDOWN,VK_DELETE,0);SendMessageW(picker.Handle(),WM_KEYDOWN,VK_RETURN,0);
    Check(picker.DraftYear()==999,"year edit Home/Delete preserves remaining digits");
    SendMessageW(picker.Handle(),WM_KEYDOWN,VK_ESCAPE,0);
    Check(!picker.IsOpen()&&GetFocus()==hwnd&&window.Model().BrowseAnchor()==calendar::Date{oldAnchor.year+1,4,1},"Escape discards draft and restores trigger");
    clickTitle(); SendMessageW(picker.Handle(),WM_KEYDOWN,VK_TAB,0);SendMessageW(picker.Handle(),WM_KEYDOWN,VK_TAB,0);
    Check(!picker.IsOpen(),"Tab can leave picker without focus trap");
    clickTitle();SendMessageW(picker.Handle(),WM_ACTIVATE,WA_INACTIVE,0);Check(!picker.IsOpen(),"picker closes on external activation");
    clickTitle();Check(picker.IsOpen(),"picker open before real trigger toggle");
    SetCursorPos(bounds.left+MulDiv(static_cast<int>(title.x+3),static_cast<int>(dpi),96),bounds.top+MulDiv(static_cast<int>(title.y+3),static_cast<int>(dpi),96));
    POINT triggerPoint{};GetCursorPos(&triggerPoint);ChallengeInputIsolation(hwnd,blocker,triggerPoint,"title");
    WaitPointTarget(hwnd,triggerPoint,"OS toggle target is own isolated calendar");
    DeliverInput(hwnd,mouse);
    if(blocker)ShowWindow(blocker,SW_HIDE);
    Check(!picker.IsOpen(),"real second trigger click closes without reopening");SetCursorPos(restoreCursor.x,restoreCursor.y);
    // A queued click can arrive after the physical button has been released.
    // Classify its intent during WM_MOUSEACTIVATE, before WA_INACTIVE closes popup.
    clickTitle();Check(picker.IsOpen(),"queued trigger regression opens picker");
    SetCursorPos(bounds.left+MulDiv(static_cast<int>(title.x+3),static_cast<int>(dpi),96),bounds.top+MulDiv(static_cast<int>(title.y+3),static_cast<int>(dpi),96));
    SendMessageW(hwnd,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(hwnd),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
    if(picker.IsOpen())SendMessageW(picker.Handle(),WM_ACTIVATE,WA_INACTIVE,reinterpret_cast<LPARAM>(hwnd));
    clickTitle();Check(!picker.IsOpen(),"released physical button cannot reopen queued trigger toggle");SetCursorPos(restoreCursor.x,restoreCursor.y);
    clickTitle();const auto beforeCovered=window.Model().Selected();
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(MulDiv(340,static_cast<int>(dpi),96),MulDiv(140,static_cast<int>(dpi),96)));
    Check(window.Model().Selected()==beforeCovered,"picker hides complete intersecting day hit region");
    SendMessageW(hwnd,WM_LBUTTONUP,0,0);
    clickTitle();SendMessageW(picker.Handle(),WM_KEYDOWN,VK_NEXT,0);SendMessageW(picker.Handle(),WM_KEYDOWN,VK_TAB,0);SendMessageW(picker.Handle(),WM_KEYDOWN,VK_RETURN,0);
    Check(!picker.IsOpen()&&window.Model().BrowseAnchor().year==window.Model().Today().year&&window.Model().BrowseAnchor().month==window.Model().Today().month,"return this month commits the actual current month");
    clickTitle();picker.SetMaterialReady(true);
    RECT pickerBounds{};GetWindowRect(picker.Handle(),&pickerBounds);
    // First month cell, away from its text and any visible border.
    POINT monthBlank{pickerBounds.left+MulDiv(20,static_cast<int>(dpi),96),
        pickerBounds.top+MulDiv(62,static_cast<int>(dpi),96)};
    InputIsolation pickerInputIsolation(picker.Handle());
    SetCursorPos(monthBlank.x,monthBlank.y);
    ChallengeInputIsolation(picker.Handle(),blocker,monthBlank,"month");
    WaitPointTarget(picker.Handle(),monthBlank,"native-ready picker blank month pixels retain OS hit testing");
    Check(SendInput(2,mouse,sizeof(INPUT))==2,"blank month cell receives actual mouse input");PumpFor(100);
    Check(!picker.IsOpen()&&window.Model().BrowseAnchor().month==1,"blank month cell selects January rather than clicking through");
    pickerInputIsolation.Restore();
    if(blocker)DestroyWindow(blocker);
    Check(monthFirstShows>5,"month first-show regression covers repeated menu switching");
    UnhookWindowsHookEx(firstShowHook);firstFrameCalendar=nullptr;
    SetCursorPos(restoreCursor.x,restoreCursor.y);
    inputIsolation.Restore();
    Check(!(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TOPMOST),"OS input fixture restores the calendar's desktop level");
    if(inputProbe){
        window.Stop();
        std::cout<<"calendar actual date, keyboard and month-picker OS input probe passed\n";
        return 0;
    }
    prefs.locked=true; prefs.Active()->hasPosition=true; prefs.Active()->x=bounds.left; prefs.Active()->y=bounds.top;
    auto before=actions.size(); Check(window.Configure(prefs)&&actions.size()==before,"locked configure no callbacks");
    SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,MulDiv(3,static_cast<int>(dpi),96)));
    Check(GetCapture()!=hwnd,"locked blank cannot drag");
    POINT edge{bounds.left+1,bounds.top+(bounds.bottom-bounds.top)/2};
    Check(SendMessageW(hwnd,WM_NCHITTEST,0,MAKELPARAM(edge.x,edge.y))==HTLEFT,"lock preserves resize");
    edge={bounds.left+MulDiv(10,static_cast<int>(dpi),96),bounds.top+MulDiv(10,static_cast<int>(dpi),96)};
    Check(SendMessageW(hwnd,WM_NCHITTEST,0,MAKELPARAM(edge.x,edge.y))==HTTOPLEFT,"14 DIP corner resize target");
    edge={bounds.left+MulDiv(7,static_cast<int>(dpi),96),bounds.top+(bounds.bottom-bounds.top)/2};
    Check(SendMessageW(hwnd,WM_NCHITTEST,0,MAKELPARAM(edge.x,edge.y))==HTLEFT,"8 DIP resize edge target");
    SendMessageW(hwnd,WM_ENTERSIZEMOVE,0,0); SetWindowPos(hwnd,nullptr,bounds.left+30,bounds.top+20,600,400,SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageW(hwnd,WM_EXITSIZEMOVE,0,0); Check(actions.back().type==calendar::ActionType::Bounds,"bounds emitted only at end");
    Check(window.Configure(prefs),"rollback old prefs"); GetWindowRect(hwnd,&bounds);
    Check(bounds.left==prefs.Active()->x&&bounds.right-bounds.left==MulDiv(820,static_cast<int>(dpi),96),"save rollback restores window bounds");
    RECT suggested{bounds.left+25,bounds.top+20,bounds.left+25+1230,bounds.top+20+750};
    SendMessageW(hwnd,WM_DPICHANGED,MAKELONG(144,144),reinterpret_cast<LPARAM>(&suggested));Pump();
    GetWindowRect(hwnd,&bounds);Check(bounds.left==suggested.left&&bounds.top==suggested.top&&bounds.right-bounds.left==1230,"DPI change respects suggested physical origin and DIP width");
    suggested={prefs.Active()->x,prefs.Active()->y,prefs.Active()->x+MulDiv(820,static_cast<int>(dpi),96),prefs.Active()->y+MulDiv(500,static_cast<int>(dpi),96)};
    SendMessageW(hwnd,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));Pump();
    prefs.theme=calendar::Theme::Split; Check(window.Configure(prefs),"switch theme"); Capture(hwnd,"split-window");
    const auto beforeHidden=window.Model().Selected();const auto hiddenCell=window.Layout().cells[0];
    if(!window.Model().Grid()[0].currentMonth){SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(MulDiv(static_cast<int>(hiddenCell.x+hiddenCell.width/2),static_cast<int>(dpi),96),MulDiv(static_cast<int>(hiddenCell.y+hiddenCell.height/2),static_cast<int>(dpi),96)));Check(window.Model().Selected()==beforeHidden,"split invisible neighboring date cannot be clicked");}
    probe={hwnd,0,false,&window}; auto timer=SetTimer(nullptr,0,30,CancelTimer);
    SendMessageW(hwnd,WM_CONTEXTMENU,0,MAKELPARAM(bounds.left+10,bounds.top+10)); KillTimer(nullptr,timer);
    Check(probe.count==1,"owned original menu cancellation");
    probe.stop=true;timer=SetTimer(nullptr,0,30,CancelTimer);
    SendMessageW(hwnd,WM_CONTEXTMENU,0,MAKELPARAM(bounds.left+10,bounds.top+10));KillTimer(nullptr,timer);
    Check(!window.IsRunning(),"shutdown while modal original menu is active");
    Check(window.Start(),"restart after modal shutdown");
    SendMessageW(window.Handle(),WM_CLOSE,0,0); Check(!window.Handle()&&!window.IsRunning(),"synchronous shutdown callback safe");
    for(int i=0;i<3;++i) { Check(window.Start(),"repeat start"); window.Stop(); window.Stop(); }
    NativeCalendarRuntime runtime(GetModuleHandleW(nullptr),[](calendar::Action){},[]{});
    Check(runtime.Apply(prefs)&&runtime.IsRunning(),"runtime start"); runtime.Shutdown();
    Check(runtime.Apply(prefs)&&runtime.IsRunning(),"runtime restart after Shutdown"); prefs.enabled=false;
    Check(runtime.Apply(prefs)&&!runtime.IsRunning(),"runtime disabled"); runtime.Shutdown();
    calendar::Preferences initial;initial.enabled=true;initial.material=calendar::Material::FrostedGlass;
    calendar::CalendarController* controller=nullptr;unsigned writes=0;
    NativeCalendarRuntime failedSave(GetModuleHandleW(nullptr),[&](calendar::Action a){controller->Handle(a);},[]{});
    calendar::CalendarController failing(initial,failedSave,[&](const calendar::Preferences&){++writes;return false;});controller=&failing;
    Check(failing.Restore(),"controller starts before failed initial position save");
    for(int i=0;i<20;++i){MSG msg{};if(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    Check(writes==1&&failing.Error()==calendar::ControllerError::Save,"failed initial save does not create posted reentry loop");
    auto beforeBounds=failedSave.CurrentBounds();
    Check(!failing.Handle({calendar::ActionType::Bounds,beforeBounds.x-31,beforeBounds.y+17,640,420}),"failed save rejects changed bounds");
    auto restored=failedSave.CurrentBounds();Check(restored.x==beforeBounds.x&&restored.y==beforeBounds.y&&restored.width==820&&restored.height==500,"actual controller save failure restores size and position");
    failing.Shutdown();
    calendar::Preferences offscreen;offscreen.enabled=true;offscreen.Active()->hasPosition=true;offscreen.Active()->x=-20000;offscreen.Active()->y=-20000;
    NativeCalendarRuntime resetRuntime(GetModuleHandleW(nullptr),[](calendar::Action){},[]{});calendar::Preferences saved;
    calendar::CalendarController resetController(offscreen,resetRuntime,[&](const calendar::Preferences& p){saved=p;return true;});
    Check(resetController.Restore(),"reset scenario starts");
    Check(resetController.Handle({calendar::ActionType::Bounds,-20000,-20000,820,500}),"drag can leave work area");
    Check(resetRuntime.CurrentBounds().x==-20000,"ordinary Bounds not clamped");
    Check(resetController.Handle({calendar::ActionType::Reset}),"same-theme reset");
    Check(resetRuntime.CurrentBounds().x==-20000&&saved.Active()->x==-20000&&saved.Active()->y==-20000,"same-theme reset preserves current physical position");
    Check(resetController.Handle({calendar::ActionType::SelectTheme,1})&&resetController.Handle({calendar::ActionType::Bounds,-15000,-16000,640,400}),"split moves offscreen");
    Check(resetController.Handle({calendar::ActionType::Reset})&&resetRuntime.CurrentBounds().x==-15000&&resetRuntime.CurrentBounds().y==-16000&&saved.Active()->x==-15000,"cross-theme reset retains live position without clamp");
    resetController.Shutdown();
    calendar::Preferences rollbackPrefs;rollbackPrefs.enabled=true;rollbackPrefs.theme=calendar::Theme::Split;rollbackPrefs.Active()->hasPosition=true;rollbackPrefs.Active()->x=-20000;rollbackPrefs.Active()->y=-20000;
    bool acceptSave=true;NativeCalendarRuntime rollbackRuntime(GetModuleHandleW(nullptr),[](calendar::Action){},[]{});
    calendar::CalendarController rollbackController(rollbackPrefs,rollbackRuntime,[&](const calendar::Preferences&){return acceptSave;});
    Check(rollbackController.Restore(),"rollback scenario initial clamped window");const auto initialClamped=rollbackRuntime.CurrentBounds();acceptSave=false;
    Check(!rollbackController.Handle({calendar::ActionType::SelectTheme,0})&&rollbackRuntime.CurrentBounds().x==initialClamped.x&&rollbackRuntime.CurrentBounds().y==initialClamped.y,"failed theme change restores actual initially clamped position");
    acceptSave=true;Check(rollbackController.Handle({calendar::ActionType::Bounds,-15000,-16000,640,400}),"prepare offscreen rollback bounds");acceptSave=false;
    Check(!rollbackController.Handle({calendar::ActionType::Reset})&&rollbackRuntime.CurrentBounds().x==-15000&&rollbackRuntime.CurrentBounds().y==-16000&&rollbackRuntime.CurrentBounds().width==640,"failed cross-theme Reset restores offscreen geometry");
    Check(!rollbackController.Handle({calendar::ActionType::SelectTheme,0})&&rollbackRuntime.CurrentBounds().x==-15000&&rollbackRuntime.CurrentBounds().y==-16000,"failed cross-theme selection restores offscreen geometry");
    Check(!rollbackController.Handle({calendar::ActionType::Enable,0})&&rollbackRuntime.IsRunning()&&rollbackRuntime.CurrentBounds().x==-15000,"failed disable restarts at prior actual position");
    rollbackController.Shutdown();
    bool adaptedSave=true;unsigned adaptedSaves=0;
    NativeCalendarRuntime adaptedRuntime(GetModuleHandleW(nullptr),[](calendar::Action){},[]{});
    calendar::CalendarController adaptedController(rollbackPrefs,adaptedRuntime,[&](const calendar::Preferences&){++adaptedSaves;return adaptedSave;});
    Check(adaptedController.Restore(),"system geometry scenario starts from persisted offscreen profile");
    const auto fitted=adaptedRuntime.CurrentBounds();
    Check(adaptedController.Handle({calendar::ActionType::Lock,1})&&adaptedRuntime.CurrentBounds().x==fitted.x&&adaptedRuntime.CurrentBounds().y==fitted.y,"successful lock preserves startup-adapted geometry");
    Check(adaptedController.Handle({calendar::ActionType::TextColor,0xff0000,255})&&adaptedRuntime.CurrentBounds().x==fitted.x,"successful appearance preserves startup-adapted geometry");
    RECT adaptedRect{fitted.x+41,fitted.y+27,fitted.x+41+1230,fitted.y+27+750};
    SendMessageW(adaptedRuntime.Handle(),WM_DPICHANGED,MAKELONG(144,144),reinterpret_cast<LPARAM>(&adaptedRect));Pump();
    Check(adaptedRuntime.CurrentBounds().x==adaptedRect.left&&adaptedController.Handle({calendar::ActionType::HighlightColor,0xffaa00,255})&&adaptedRuntime.CurrentBounds().x==adaptedRect.left,"successful appearance preserves DPI-adapted origin");
    adaptedSave=false;const auto saveCount=adaptedSaves;
    Check(!adaptedController.Handle({calendar::ActionType::Lock,0})&&adaptedRuntime.CurrentBounds().x==adaptedRect.left&&adaptedRuntime.CurrentBounds().y==adaptedRect.top&&adaptedSaves==saveCount+1,"failed setting rolls back to DPI-adapted geometry with one save");
    Check(!adaptedController.Handle({calendar::ActionType::SelectTheme,0})&&adaptedRuntime.CurrentBounds().x==adaptedRect.left&&adaptedRuntime.CurrentBounds().y==adaptedRect.top,"failed theme rolls back to DPI-adapted geometry");
    adaptedSave=true;Check(adaptedController.Handle({calendar::ActionType::TextColor,0x33aaff,255})&&adaptedRuntime.CurrentBounds().x==adaptedRect.left&&adaptedRuntime.CurrentBounds().y==adaptedRect.top,"successful appearance after rollback retains adapted geometry");
    adaptedSave=true;Check(adaptedController.Handle({calendar::ActionType::Bounds,-12000,-13000,640,400}),"persist offscreen move before display topology refresh");
    SendMessageW(adaptedRuntime.Handle(),WM_DISPLAYCHANGE,0,0);const auto displayFitted=adaptedRuntime.CurrentBounds();
    Check(displayFitted.x!=-12000&&adaptedController.Handle({calendar::ActionType::Lock,0})&&adaptedRuntime.CurrentBounds().x==displayFitted.x,"successful setting preserves display-adapted geometry");
    adaptedSave=false;
    Check(!adaptedController.Handle({calendar::ActionType::SelectTheme,0})&&adaptedRuntime.CurrentBounds().x==displayFitted.x&&adaptedRuntime.CurrentBounds().y==displayFitted.y,"failed theme restores display-adapted geometry");
    const auto beforeRejectedDrag=adaptedRuntime.CurrentBounds();SetWindowPos(adaptedRuntime.Handle(),nullptr,displayFitted.x+35,displayFitted.y+45,0,0,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSIZE);
    Check(!adaptedController.Handle({calendar::ActionType::Bounds,displayFitted.x+35,displayFitted.y+45,640,400})&&adaptedRuntime.CurrentBounds().x==beforeRejectedDrag.x&&adaptedRuntime.CurrentBounds().y==beforeRejectedDrag.y,"failed drag restores confirmed geometry rather than unsaved HWND location");
    adaptedSave=true;Check(adaptedController.Handle({calendar::ActionType::SelectTheme,0})&&adaptedRuntime.CurrentBounds().x==beforeRejectedDrag.x&&adaptedRuntime.CurrentBounds().y==beforeRejectedDrag.y&&adaptedController.Snapshot().Active()->x==beforeRejectedDrag.x,"first theme inherits adapted live geometry and saves it once");
    adaptedController.Shutdown();
    {
      calendar::CalendarController* dragController=nullptr;bool saveDrag=true;
      NativeCalendarRuntime crossing(GetModuleHandleW(nullptr),[&](calendar::Action a){dragController->Handle(a);},[]{});
      calendar::Preferences cp;cp.enabled=true;cp.material=calendar::Material::FrostedGlass;cp.Active()->hasPosition=true;cp.Active()->x=100;cp.Active()->y=100;cp.Active()->width=320;cp.Active()->height=320;
      calendar::CalendarController cc(cp,crossing,[&](const calendar::Preferences&){return saveDrag;});dragController=&cc;Check(cc.Restore(),"cross-DPI drag source");
      const auto stable=crossing.CurrentBounds();SendMessageW(crossing.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,2));
      RECT crossingRect{stable.x+100,stable.y+100,stable.x+580,stable.y+580};SendMessageW(crossing.Handle(),WM_DPICHANGED,MAKELONG(144,144),reinterpret_cast<LPARAM>(&crossingRect));Pump();saveDrag=false;
      SendMessageW(crossing.Handle(),WM_LBUTTONUP,0,0);
      Check(crossing.CurrentBounds().x==stable.x&&crossing.CurrentBounds().y==stable.y&&cc.Error()==calendar::ControllerError::Save,"cross-DPI drag failed save restores pre-interaction position");
      SendMessageW(crossing.Handle(),WM_ENTERSIZEMOVE,0,0);SendMessageW(crossing.Handle(),WM_DPICHANGED,MAKELONG(192,192),reinterpret_cast<LPARAM>(&crossingRect));Pump();SendMessageW(crossing.Handle(),WM_EXITSIZEMOVE,0,0);
      Check(crossing.CurrentBounds().x==stable.x&&crossing.CurrentBounds().y==stable.y&&cc.Error()==calendar::ControllerError::Save,"cross-DPI resize failed save restores pre-interaction position");
      SendMessageW(crossing.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,2));SendMessageW(crossing.Handle(),WM_DPICHANGED,MAKELONG(144,144),reinterpret_cast<LPARAM>(&crossingRect));SendMessageW(crossing.Handle(),WM_LBUTTONUP,0,0);Pump();
      Check(crossing.CurrentBounds().x==stable.x&&crossing.CurrentBounds().y==stable.y,"queued DPI refresh cannot overwrite completed drag rollback");
      SendMessageW(crossing.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,2));SendMessageW(crossing.Handle(),WM_DPICHANGED,MAKELONG(192,192),reinterpret_cast<LPARAM>(&crossingRect));
      HWND thief=CreateWindowExW(0,L"STATIC",L"DPI capture helper",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);Check(thief!=nullptr,"DPI capture helper created");SetCapture(thief);Pump();
      Check(crossing.CurrentBounds().x==stable.x&&crossing.CurrentBounds().y==stable.y,"cross-DPI canceled drag restores pre-interaction position");ReleaseCapture();DestroyWindow(thief);
      SendMessageW(crossing.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,2));SendMessageW(crossing.Handle(),WM_DPICHANGED,MAKELONG(192,192),reinterpret_cast<LPARAM>(&crossingRect));Pump();
      DpiTransition transition{stable.x,96,false};Check(SetWindowSubclass(crossing.Handle(),SimulateMonitorDpi,1,reinterpret_cast<DWORD_PTR>(&transition))!=FALSE,"self-owned monitor transition subclass");
      SendMessageW(crossing.Handle(),WM_LBUTTONUP,0,0);Pump();RemoveWindowSubclass(crossing.Handle(),SimulateMonitorDpi,1);
      Check(transition.fired&&crossing.CurrentBounds().x==stable.x&&crossing.CurrentBounds().width==320&&crossing.CurrentBounds().height==320,"rollback keeps DIP size when SetWindowPos synchronously changes monitor DPI");cc.Shutdown();
    }
    {
      calendar::Preferences dragPrefs;dragPrefs.enabled=true;dragPrefs.Active()->hasPosition=true;dragPrefs.Active()->x=100;dragPrefs.Active()->y=100;dragPrefs.Active()->width=320;dragPrefs.Active()->height=320;
      NativeCalendarWindow dragged(GetModuleHandleW(nullptr),[](calendar::Action){},[]{}),neighbor(GetModuleHandleW(nullptr),[](calendar::Action){},[]{});
      Check(dragged.Configure(dragPrefs)&&dragged.Start(),"capture cancellation source starts");RECT sourceRect{};GetWindowRect(dragged.Handle(),&sourceRect);
      dragPrefs.Active()->x=sourceRect.right+120;Check(neighbor.Configure(dragPrefs)&&neighbor.Start(),"capture cancellation neighbor starts");RECT targetRect{};GetWindowRect(neighbor.Handle(),&targetRect);
      HWND captureThief=CreateWindowExW(0,L"STATIC",L"Calendar capture test",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);Check(captureThief!=nullptr,"self-owned capture helper");
      POINT priorCursor{};GetCursorPos(&priorCursor);SetCursorPos(sourceRect.left+20,sourceRect.top+2);
      SendMessageW(dragged.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,2));Check(GetCapture()==dragged.Handle(),"source owns drag capture");
      SetCursorPos(sourceRect.left+20+targetRect.left-sourceRect.right-8,targetRect.top+2);
      SendMessageW(dragged.Handle(),WM_MOUSEMOVE,MK_LBUTTON,0);PumpSnapMessages();
      Check(GetPropW(dragged.Handle(),widget_snap::ActiveEdges)&&GetPropW(neighbor.Handle(),widget_snap::ActiveEdges),"drag highlights both owned windows before capture loss");
      SetCapture(captureThief);PumpSnapMessages();
      Check(!GetPropW(dragged.Handle(),widget_snap::ActiveEdges)&&!GetPropW(neighbor.Handle(),widget_snap::ActiveEdges),"capture cancellation clears both alignment guides");
      auto canceled=dragged.CurrentBounds();Check(canceled.x==sourceRect.left&&canceled.y==sourceRect.top,"capture cancellation restores confirmed position");
      ReleaseCapture();DestroyWindow(captureThief);SetCursorPos(priorCursor.x,priorCursor.y);dragged.Stop();neighbor.Stop();
    }
    calendar::Preferences disabledReset;Check(resetRuntime.ApplyReset(disabledReset)&&!disabledReset.Active()->hasPosition&&!resetRuntime.IsRunning(),"disabled reset neither creates position nor starts window");
    calendar::Date fakeToday{2026,9,25};NativeCalendarWindow overnight(GetModuleHandleW(nullptr),[](calendar::Action){},[]{},[&](){return fakeToday;});
    Check(overnight.Configure(initial)&&overnight.Start(),"controlled calendar starts before midnight");overnight.Stop();fakeToday={2026,9,26};
    Check(overnight.Start()&&overnight.Model().Today()==fakeToday&&overnight.Model().BrowseAnchor()==fakeToday,"reopen refreshes today before first paint after stopped midnight");overnight.Stop();
    Check(overnight.Start(),"window restarts for external destroy");
    auto overnightTitle=overnight.Layout().title;const auto overnightDpi=static_cast<int>(GetDpiForWindow(overnight.Handle()));SendMessageW(overnight.Handle(),WM_LBUTTONDOWN,0,MAKELPARAM(MulDiv(static_cast<int>(overnightTitle.x+3),overnightDpi,96),MulDiv(static_cast<int>(overnightTitle.y+3),overnightDpi,96)));
    Check(overnight.Picker().IsOpen(),"picker opens for external destroy");DestroyWindow(overnight.Picker().Handle());
    Check(!overnight.Picker().IsOpen(),"external picker destroy updates component handle");
    DestroyWindow(overnight.Handle());Check(!overnight.IsRunning()&&overnight.Start(),"external calendar destroy permits clean restart");overnight.Stop();
    std::cout<<"Native calendar rendering, interaction, DPI and lifecycle passed\n";
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
