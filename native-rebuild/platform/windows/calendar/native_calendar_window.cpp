#include "platform/windows/calendar/native_calendar_window.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/widget_ui/StdAfx.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/widget_ui/LumeWidgetColorPicker.h"
#include "platform/windows/widget_ui/widget_snap_protocol.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "platform/windows/widget_ui/widget_placement.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
namespace lume::platform::windows {
namespace {
constexpr wchar_t kClass[]=L"LumeNativeDesktopCalendar";
constexpr UINT_PTR kMidnightTimer=1,kHighlightTimer=2,kRemoteHighlightTimer=3,kAgendaTimer=4;
constexpr UINT kInitialBounds=WM_APP+0x581,kDpiRefresh=WM_APP+0x582;
calendar::Date Today() {SYSTEMTIME t{};GetLocalTime(&t);return {t.wYear,t.wMonth,t.wDay};}
void DesktopLevel(HWND w) {desktop_level::Place(w,true);}
CalendarRect BoundsRect(RECT r) {return {static_cast<float>(r.left),static_cast<float>(r.top),static_cast<float>(r.right-r.left),static_cast<float>(r.bottom-r.top)};}
struct Neighbors {HWND own;std::vector<CalendarRect> values;std::vector<HWND> handles;};
BOOL CALLBACK CollectNeighbor(HWND w,LPARAM data) {
    auto& out=*reinterpret_cast<Neighbors*>(data);DWORD pid=0;GetWindowThreadProcessId(w,&pid);
    if(w==out.own||pid!=GetCurrentProcessId()||!IsWindowVisible(w))return TRUE;
    wchar_t cls[96]{};GetClassNameW(w,cls,96);const std::wstring name(cls);
    // Generic widget identity only: no clock implementation dependency.
    if(!lume::window_ids::HasDesktopClassPrefix(name.c_str())&&!GetPropW(w,lume::window_ids::DesktopWidgetProp))return TRUE;
    RECT r{};if(GetWindowRect(w,&r)){out.values.push_back(BoundsRect(r));out.handles.push_back(w);}return TRUE;
}
Neighbors NeighborsOf(HWND w) {Neighbors out{w,{},{}};EnumWindows(CollectNeighbor,reinterpret_cast<LPARAM>(&out));return out;}
CalendarRect WorkArea(HWND w) {MONITORINFO i{};i.cbSize=sizeof(i);GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&i);return BoundsRect(i.rcWork);}
unsigned EdgeForSizing(WPARAM edge) {
    switch(edge) {case WMSZ_LEFT:return CalendarEdgeLeft;case WMSZ_RIGHT:return CalendarEdgeRight;case WMSZ_TOP:return CalendarEdgeTop;
    case WMSZ_BOTTOM:return CalendarEdgeBottom;case WMSZ_TOPLEFT:return CalendarEdgeTop|CalendarEdgeLeft;case WMSZ_TOPRIGHT:return CalendarEdgeTop|CalendarEdgeRight;
    case WMSZ_BOTTOMLEFT:return CalendarEdgeBottom|CalendarEdgeLeft;case WMSZ_BOTTOMRIGHT:return CalendarEdgeBottom|CalendarEdgeRight;default:return 0;}
}
class MenuGraphics {public:MenuGraphics(){Gdiplus::GdiplusStartupInput input;valid=Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok;}~MenuGraphics(){if(valid)Gdiplus::GdiplusShutdown(token);}bool valid=false;private:ULONG_PTR token=0;};
}
NativeCalendarWindow::NativeCalendarWindow(HINSTANCE instance,std::function<void(calendar::Action)> handler,std::function<void()> settings,std::function<calendar::Date()> todayProvider):instance_(instance),handler_(std::move(handler)),onOpenSettings_(std::move(settings)),todayProvider_(todayProvider?std::move(todayProvider):Today),model_(todayProvider_()){}
NativeCalendarWindow::~NativeCalendarWindow(){*alive_=false;Stop();}
bool NativeCalendarWindow::Configure(const calendar::Preferences& prefs,bool preservePosition,bool explicitBounds) {
    if(!calendar::ValidPreferences(prefs))return false;
    const auto old=preferences_;const auto priorBounds=confirmedBounds_;const bool themeChanged=old.theme!=prefs.theme;
    preferences_=prefs;model_.SetTheme(prefs.theme);
    if(!AgendaPane()){view_.agendaOpen=false;view_.agendaFocus=-1;}
    if(window_) {
        applying_=true;picker_.Close();CancelTodoEditor();
        const auto* geometry=!themeChanged&&!explicitBounds&&!preservePosition&&priorBounds?&*priorBounds:nullptr;
        const bool ok=ApplyBounds((themeChanged&&!preservePosition)||(!geometry&&!prefs.Active()->hasPosition),geometry)&&Paint();applying_=false;
        if(!ok){preferences_=old;model_.SetTheme(old.theme);ApplyBounds(false,priorBounds?&*priorBounds:nullptr);Paint();return false;}
        if(explicitBounds||preservePosition){dpiPending_=false;MSG refresh{};while(PeekMessageW(&refresh,window_,kDpiRefresh,kDpiRefresh,PM_REMOVE)) {}}
        confirmedBounds_=CurrentBounds();
        // One flag per stored profile; the todo theme shares the spacious one.
        const std::size_t index=preferences_.theme==calendar::Theme::Spacious?0:1;
        if(!prefs.Active()->hasPosition&&!initialSubmitted_[index]&&!initialPending_){initialPending_=true;PostMessageW(window_,kInitialBounds,0,0);}
        ArmAgendaTimer();
    }
    return true;
}
bool NativeCalendarWindow::Start(bool preservePosition) {
    const auto alive=alive_;const auto refresh=todoPort_.refreshTime;
    if(refresh)refresh();
    if(!*alive)return false;
    RefreshTodos();
    if(!*alive)return false;
    if(window_)return true;
    model_.UpdateToday(todayProvider_());
    renderer_=std::make_unique<CalendarRenderer>();if(!renderer_->Initialize()){renderer_.reset();return false;}
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance_;wc.lpfnWndProc=WindowProc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=kClass;
    if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){renderer_.reset();return false;}
    const auto& profile=*preferences_.Active();
    window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,kClass,L"Lume Calendar",WS_POPUP,profile.x,profile.y,1,1,nullptr,nullptr,instance_,this);
    if(!window_){renderer_.reset();return false;}
    SetPropW(window_,lume::window_ids::DesktopWidgetProp,reinterpret_cast<HANDLE>(1));SetPropW(window_,widget_snap::Capability,reinterpret_cast<HANDLE>(1));dpi_=GetDpiForWindow(window_);if(!dpi_)dpi_=96;
    if(!ApplyBounds(!preservePosition)||!Paint()){Stop();return false;}
    confirmedBounds_=CurrentBounds();
    DesktopLevel(window_);ArmMidnightTimer();
    initialSubmitted_={};initialPending_=!profile.hasPosition;
    if(initialPending_)PostMessageW(window_,kInitialBounds,0,0);
    return true;
}
void NativeCalendarWindow::Stop() {
    pendingTodoId_.clear();
    stopping_=true;ClearSnap();picker_.Close();CancelTodoEditor();dayTooltip_.Stop();
    material_.Detach();
    if(window_)KillTimer(window_,kAgendaTimer);
    if(window_) {const auto old=window_;KillTimer(old,kMidnightTimer);KillTimer(old,kHighlightTimer);dragging_=false;sizing_=false;if(GetCapture()==old)ReleaseCapture();DestroyWindow(old);window_=nullptr;}
    renderer_.reset();bitmap_={};view_={};snap_={};confirmedBounds_.reset();interactionBounds_.reset();remoteEdges_=0;remoteSource_=nullptr;initialPending_=false;stopping_=false;
}
calendar::Profile NativeCalendarWindow::CurrentBounds() const {
    auto result=*preferences_.Active();if(!window_)return result;
    RECT r{};GetWindowRect(window_,&r);result.x=r.left;result.y=r.top;result.width=MulDiv(r.right-r.left,96,static_cast<int>(dpi_));result.height=MulDiv(r.bottom-r.top,96,static_cast<int>(dpi_));result.hasPosition=true;return result;
}
bool NativeCalendarWindow::ApplyBounds(bool clamp,const calendar::Profile* bounds) {
    if(!window_)return false;
    auto profile=bounds?*bounds:*preferences_.Active();
    MONITORINFO i{};i.cbSize=sizeof(i);POINT position{profile.x,profile.y};
    auto monitor=profile.hasPosition?MonitorFromPoint(position,MONITOR_DEFAULTTONEAREST):MonitorFromWindow(window_,MONITOR_DEFAULTTOPRIMARY);
    const bool fit=(clamp||!profile.hasPosition)&&GetMonitorInfoW(monitor,&i);
    if(fit&&!profile.hasPosition) {
        // First placement: the default size shrinks on small displays, and
        // the calendar goes beside the widgets already on the desktop.
        const double scale=widget_placement::MonitorFitScale(monitor);
        profile.width=window::FitLength(profile.width,scale,320);profile.height=window::FitLength(profile.height,scale,320);
        position=widget_placement::FirstPosition(window_,monitor,i.rcWork,
            {MulDiv(profile.width,static_cast<int>(dpi_),96),MulDiv(profile.height,static_cast<int>(dpi_),96)},dpi_);
    }
    for(int attempt=0;attempt<3;++attempt) {
        const unsigned appliedDpi=dpi_;
        int width=MulDiv(profile.width,static_cast<int>(appliedDpi),96),height=MulDiv(profile.height,static_cast<int>(appliedDpi),96);
        if(fit) {
            // A size saved on a larger display shrinks to this work area.
            const int minimum=MulDiv(320,static_cast<int>(appliedDpi),96);
            const auto r=window::FitInside({position.x,position.y,width,height},widget_placement::ToRect(i.rcWork),minimum,minimum);
            position={r.x,r.y};width=r.width;height=r.height;
        }
        if(!SetWindowPos(window_,nullptr,position.x,position.y,width,height,SWP_NOACTIVATE|SWP_NOZORDER))return false;
        if(dpi_==appliedDpi)return true;
    }
    // The DPI kept changing while the window moved between monitors (mixed-
    // DPI setups). Keep the last placement; WM_DPICHANGED refits it later.
    // Failing here stopped the calendar from starting at all.
    return true;
}
bool NativeCalendarWindow::Paint() {
    if(!window_||!renderer_||stopping_)return false;
    auto renderPrefs=preferences_;const auto bounds=CurrentBounds();renderPrefs.Active()->width=std::clamp(bounds.width,320,1440);renderPrefs.Active()->height=std::clamp(bounds.height,320,720);
    if(view_.cellScrollMonth!=model_.BrowseAnchor()){view_.cellScroll={};view_.cellScrollMonth=model_.BrowseAnchor();}
    view_.pickerVisible=picker_.IsOpen();auto renderState=view_;renderState.alignedEdges|=remoteEdges_;
    renderState.materialReady=preferences_.material!=calendar::Material::Solid&&material_.State()==WidgetMaterialState::Ready;
    picker_.SetMaterialReady(renderState.materialReady);
    renderState.todos=&todos_;renderState.time=todoPort_.time;
    widget_snap::SetEdges(window_,renderState.alignedEdges);
    if(!renderer_->Render(renderPrefs,model_,dpi_,bitmap_,renderState,&layout_,&agenda_))return false;
    view_.agendaScroll=std::min(view_.agendaScroll,agenda_.maxScroll);
    if(view_.agendaFocus>=static_cast<int>(agenda_.rows.size())*3){
        view_.agendaFocus=agenda_.rows.empty()?-1:static_cast<int>(agenda_.rows.size())*3-1;
        if(agenda_.rows.empty())view_.focus=46;
    }
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=static_cast<LONG>(bitmap_.width);info.bmiHeader.biHeight=-static_cast<LONG>(bitmap_.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);void* pixels=nullptr;HBITMAP dib=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);bool ok=false;
    if(memory&&dib&&pixels) {std::memcpy(pixels,bitmap_.pixels.data(),bitmap_.pixels.size()*4);auto old=SelectObject(memory,dib);POINT source{};SIZE size{static_cast<LONG>(bitmap_.width),static_cast<LONG>(bitmap_.height)};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};ok=UpdateLayeredWindow(window_,screen,nullptr,&size,memory,&source,0,&blend,ULW_ALPHA)!=FALSE;SelectObject(memory,old);}
    if(dib)DeleteObject(dib);
    if(memory)DeleteDC(memory);
    if(screen)ReleaseDC(nullptr,screen);
    if(ok){
        UpdateMaterial();
        if(dayTooltip_.Attach(window_))dayTooltip_.Update(model_,layout_,dpi_,view_.pickerVisible);
    }
    return ok;
}
void NativeCalendarWindow::UpdateMaterial() {
    if(!window_||stopping_)return;
    // The "普通" material draws its own opaque panel.
    if(preferences_.material==calendar::Material::Solid){if(material_.State()!=WidgetMaterialState::Detached)material_.Detach();return;}
    LumeMaterialParameters p{};p.size=sizeof(p);p.owner=window_;
    p.frosted=preferences_.material==calendar::Material::FrostedGlass;
    const auto rgb=preferences_.textColor.rgb;
    p.opticalProfile=1;p.lightForeground=299*((rgb>>16)&255)+587*((rgb>>8)&255)+114*(rgb&255)>=128000;
    BOOL animation=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0);p.reducedMotion=!animation;
    POINT mouse{};GetCursorPos(&mouse);ScreenToClient(window_,&mouse);RECT client{};GetClientRect(window_,&client);
    p.contactX=std::clamp(static_cast<float>(mouse.x)/std::max(1L,client.right)*2-1,-1.f,1.f);
    p.contactY=std::clamp(static_cast<float>(mouse.y)/std::max(1L,client.bottom)*2-1,-1.f,1.f);
    p.pressed=(GetAsyncKeyState(VK_LBUTTON)&0x8000)&&PtInRect(&client,mouse)&&GetForegroundWindow()==window_;
    // Away from the pane the highlight rests at the material's top-left key.
    if(!PtInRect(&client,mouse)){p.contactX=-.62f;p.contactY=-.85f;}
    for(const auto r:{layout_.previous,layout_.next}){
        const auto q=layout_.picker;
        const bool covered=picker_.IsOpen()&&r.x<q.Right()&&r.Right()>q.x&&r.y<q.Bottom()&&r.Bottom()>q.y;
        if(!covered)p.panels[p.panelCount++]={r.x,r.y,r.width,r.height,-1};
    }
    if(picker_.IsOpen()){const auto r=layout_.picker;p.panels[p.panelCount++]={r.x,r.y,r.width,r.height,8};}
    if(material_.State()==WidgetMaterialState::Detached)material_.Attach(p);
    else material_.Update(p);
}
void NativeCalendarWindow::ArmMidnightTimer() {
    if(!window_)return;
    SYSTEMTIME t{};GetLocalTime(&t);const UINT delay=((23u-t.wHour)*3600u+(59u-t.wMinute)*60u+60u-t.wSecond)*1000u-t.wMilliseconds;
    KillTimer(window_,kMidnightTimer);SetTimer(window_,kMidnightTimer,std::max(1u,delay),nullptr);
}
void NativeCalendarWindow::RefreshToday(){
    const auto alive=alive_;const auto refresh=todoPort_.refreshTime;
    if(refresh)refresh();
    if(!*alive)return;
    model_.UpdateToday(todayProvider_());RefreshTodos();
    if(!*alive)return;
    Paint();if(*alive)ArmMidnightTimer();
}
void NativeCalendarWindow::Dispatch(calendar::Action action){const auto handler=handler_;if(handler)handler(action);}
void NativeCalendarWindow::ClearSnap(bool linger) {
    for(const auto& [target,edges]:snapTargets_)widget_snap::Notify(target,window_,linger?edges:0,linger);
    if(!linger)snapTargets_.clear();
}
void NativeCalendarWindow::NotifySnap(const CalendarSnapState& state,const std::vector<HWND>& handles) {
    std::vector<std::pair<HWND,unsigned>> next;
    for(const auto& match:{state.horizontal,state.vertical}) {
        if(!match.active||match.target<0||static_cast<std::size_t>(match.target)>=handles.size())continue;
        const auto target=handles[static_cast<std::size_t>(match.target)];
        auto existing=std::find_if(next.begin(),next.end(),[&](const auto& entry){return entry.first==target;});
        if(existing==next.end())next.emplace_back(target,match.targetEdge);else existing->second|=match.targetEdge;
    }
    for(const auto& [target,edges]:snapTargets_){(void)edges;if(std::none_of(next.begin(),next.end(),[&](const auto& entry){return entry.first==target;}))widget_snap::Notify(target,window_,0);}
    for(const auto& target:next)
        if(std::find(snapTargets_.begin(),snapTargets_.end(),target)==snapTargets_.end())
            widget_snap::Notify(target.first,window_,target.second);
    snapTargets_=std::move(next);
}
void NativeCalendarWindow::CommitBounds(bool automatic) {
    if(!window_||applying_)return;
    const auto p=CurrentBounds();Dispatch({calendar::ActionType::Bounds,std::clamp(p.x,-32768,32767),std::clamp(p.y,-32768,32767),std::clamp(p.width,320,1440),std::clamp(p.height,320,720),automatic});
}
void NativeCalendarWindow::ShowMenu(POINT position) {
    if(menuOpen_||!window_)return;
    dayTooltip_.Hide();picker_.Close();MenuGraphics graphics;if(!graphics.valid)return;
    menuOpen_=true;const auto alive=alive_;const auto owner=window_;
    const auto command=LumeWidgetMenu::ShowActions(owner,position,false,false,false,false,preferences_.material!=calendar::Material::LiquidGlass,true,preferences_.locked,true,static_cast<int>(preferences_.theme),preferences_.material==calendar::Material::Solid);
    if(!*alive)return;
    menuOpen_=false;if(window_!=owner||!IsWindow(owner))return;
    switch(command) {
    case 1:Dispatch({calendar::ActionType::Enable,0});return;
    case 8:Dispatch({calendar::ActionType::Material,0});return;
    case 9:Dispatch({calendar::ActionType::Material,1});return;
    case 10:Dispatch({calendar::ActionType::Reset});return;
    case 11:{const auto callback=onOpenSettings_;if(callback)callback();return;}
    case 12:Dispatch({calendar::ActionType::Lock,preferences_.locked?0:1});return;
    case 13:Dispatch({calendar::ActionType::SelectTheme,0});return;
    case 14:Dispatch({calendar::ActionType::SelectTheme,1});return;
    case 15:Dispatch({calendar::ActionType::SelectTheme,2});return;
    case 20:Dispatch({calendar::ActionType::Material,2});return;
    case 21:case 22:case 23:{
        // Like the clock: 21 background (the solid material), 23 text, 22 accent.
        const auto current=command==21?preferences_.panelColor:command==23?preferences_.textColor:preferences_.highlightColor;
        LumeWidgetColor color{static_cast<BYTE>((current.rgb>>16)&255),static_cast<BYTE>((current.rgb>>8)&255),static_cast<BYTE>(current.rgb&255),static_cast<BYTE>(current.alpha)};
        const COLORREF defaults=command==21?RGB(44,44,46):command==23?RGB(255,255,255):RGB(10,132,255);
        if(!LumeWidgetColorPicker::Show(owner,color,defaults)||!*alive||window_!=owner)return;
        const auto type=command==21?calendar::ActionType::PanelColor:command==23?calendar::ActionType::TextColor:calendar::ActionType::HighlightColor;
        Dispatch({type,(color.red<<16)|(color.green<<8)|color.blue,color.alpha});
        return;
    }
    default:return;
    }
}
void NativeCalendarWindow::TogglePicker() {
    dayTooltip_.Hide();
    if(suppressPickerClick_){suppressPickerClick_=false;return;}
    if(picker_.IsOpen()){picker_.Close();return;}
    view_.focus=1;view_.keyboardFocus=false;SetFocus(window_);
    RECT bounds{};GetWindowRect(window_,&bounds);auto r=layout_.picker;r.x=static_cast<float>(bounds.left)+r.x*dpi_/96;r.y=static_cast<float>(bounds.top)+r.y*dpi_/96;
    picker_.Open(instance_,window_,r,dpi_,preferences_,model_.BrowseAnchor(),model_.Today(),[this](int year,int month){model_.SetMonth(year,month);
        if(TimeGridShown()&&(model_.Selected().year!=year||model_.Selected().month!=month)){model_.SelectDate({year,month,1});view_.boardScroll=-1;}
        Paint();},[this](bool restore){view_.focus=1;view_.keyboardFocus=restore;POINT p{};GetCursorPos(&p);ScreenToClient(window_,&p);const bool mouseClosing=!restore&&(GetAsyncKeyState(VK_LBUTTON)<0);closedPickerClickUntil_=mouseClosing?GetTickCount64()+250:0;suppressPickerClick_=mouseClosing&&layout_.title.Contains(static_cast<float>(p.x)*96/dpi_,static_cast<float>(p.y)*96/dpi_);if(!stopping_)Paint();},material_.State()==WidgetMaterialState::Ready);Paint();
}
void NativeCalendarWindow::Activate(CalendarHit hit) {
    view_.agendaFocus=-1;
    if(hit.kind==CalendarHitKind::Create){view_.focus=45;OpenTodo();return;}
    if(hit.kind==CalendarHitKind::List){view_.focus=46;ToggleAgenda();return;}
    if(hit.kind==CalendarHitKind::Title){TogglePicker();return;}
    if(hit.kind==CalendarHitKind::Today){picker_.Close();model_.ReturnToToday();model_.SelectDate(model_.Today());view_.boardScroll=-1;view_.focus=-1;view_.keyboardFocus=false;SetFocus(window_);Paint();return;}
    if(hit.kind==CalendarHitKind::View){
        picker_.Close();view_.boardScroll=-1;
        if(hit.day!=static_cast<int>(preferences_.boardView))Dispatch({calendar::ActionType::BoardView,hit.day});
        return;
    }
    // 日 and 周 step by a day or a week; the month follows the selection.
    if(TimeGridShown()&&(hit.kind==CalendarHitKind::Previous||hit.kind==CalendarHitKind::Next)){
        picker_.Close();
        const int step=(preferences_.boardView==calendar::BoardView::Week?7:1)*(hit.kind==CalendarHitKind::Next?1:-1);
        if(const auto day=calendar::AddDays(model_.Selected(),step)){model_.SetMonth(day->year,day->month);model_.SelectDate(*day);}
        view_.focus=hit.kind==CalendarHitKind::Next?2:0;view_.keyboardFocus=false;SetFocus(window_);Paint();return;
    }
    picker_.Close();
    if(hit.kind==CalendarHitKind::Previous){model_.StepMonth(-1);view_.focus=0;}
    if(hit.kind==CalendarHitKind::Next){model_.StepMonth(1);view_.focus=2;}
    if(hit.kind==CalendarHitKind::Day&&hit.day>=0) {const auto day=model_.Grid()[static_cast<std::size_t>(hit.day)];if(day.selectable){model_.SelectDate(day.date);view_.focus=hit.day+3;view_.agendaScroll=0;view_.boardDayScroll=0;view_.boardScroll=-1;}}
    view_.keyboardFocus=false;SetFocus(window_);Paint();
}
void NativeCalendarWindow::Key(UINT key) {
    if(key==VK_ESCAPE){if(picker_.IsOpen())picker_.Close(true);else if(view_.agendaOpen)ToggleAgenda();return;}
    view_.keyboardFocus=true;
    if(key==VK_TAB) {
        // The todo theme has no list button (46) or agenda pane to tab through.
        const int step=GetKeyState(VK_SHIFT)<0?-1:1,base=view_.todoAvailable&&AgendaPane()?47:view_.todoAvailable?46:45;
        const int count=base+(view_.agendaOpen&&AgendaPane()?static_cast<int>(agenda_.rows.size())*3:0);
        int focus=view_.agendaFocus<0?view_.focus:base+view_.agendaFocus;
        focus=(focus+step+count)%count;view_.focus=focus<base?focus:46;view_.agendaFocus=focus<base?-1:focus-base;
        RevealAgendaFocus();
        Paint();return;
    }
    if(AgendaKey(key))return;
    if(key==VK_RETURN||key==VK_SPACE) {
        if(view_.focus==45){OpenTodo();return;}
        if(view_.focus==46){ToggleAgenda();return;}
        if(view_.focus==0){Activate({CalendarHitKind::Previous,-1});view_.keyboardFocus=true;Paint();return;}
        if(view_.focus==1){TogglePicker();return;}
        if(view_.focus==2){Activate({CalendarHitKind::Next,-1});view_.keyboardFocus=true;Paint();return;}
        if(view_.focus>=3&&view_.focus<45){const auto day=model_.Grid()[static_cast<std::size_t>(view_.focus-3)];if(day.selectable){model_.SelectDate(day.date);view_.agendaScroll=0;}Paint();return;}
    }
    const int delta=key==VK_LEFT?-1:key==VK_RIGHT?1:key==VK_UP?-7:key==VK_DOWN?7:0;
    if(!delta||view_.focus<3||view_.focus>=45)return;
    int target=std::clamp(view_.focus-3+delta,0,41);
    while(target>=0&&target<42&&!model_.Grid()[static_cast<std::size_t>(target)].selectable)target+=delta<0?1:-1;
    if(target>=0&&target<42){model_.SelectDate(model_.Grid()[static_cast<std::size_t>(target)].date);view_.focus=target+3;Paint();}
}
LRESULT CALLBACK NativeCalendarWindow::WindowProc(HWND w,UINT message,WPARAM wp,LPARAM lp) {
    auto* self=reinterpret_cast<NativeCalendarWindow*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<NativeCalendarWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window_=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(w,message,wp,lp);
    if(message==WM_WINDOWPOSCHANGING)desktop_level::Changing(lp);  // stays at desktop level when activated
    if(message==WM_NCDESTROY)desktop_level::Forget(w);
    if(message==desktop_level::Message()){if(IsWindowVisible(w))desktop_level::Place(w,false);return 0;}
    switch(message) {
    case LumeMaterialStatusChangedMessage:{self->Paint();const auto callback=self->onMaterialChanged_;if(callback)callback();return 0;}
    case widget_snap::CancelMessage:{
        const auto edges=self->view_.alignedEdges|self->remoteEdges_;
        KillTimer(w,kHighlightTimer);KillTimer(w,kRemoteHighlightTimer);
        self->ClearSnap();self->snap_={};self->view_.alignedEdges=0;self->remoteEdges_=0;self->remoteSource_=nullptr;
        if(edges)self->Paint();
        return 0;
    }
    case widget_snap::Message:{
        const auto source=reinterpret_cast<HWND>(lp);const unsigned edges=static_cast<unsigned>(wp)&15u;
        if(!edges&&self->remoteSource_!=source)return 0;
        const bool changed=self->remoteEdges_!=edges;
        self->remoteSource_=edges?source:nullptr;self->remoteEdges_=edges;KillTimer(w,kRemoteHighlightTimer);
        if(wp&widget_snap::Linger)SetTimer(w,kRemoteHighlightTimer,260,nullptr);
        if(changed)self->Paint();
        return 0;
    }
    case WM_MOUSEACTIVATE:
        desktop_level::Select(w);
        // OS sends this before deactivating the owned month popup. Physical
        // button state may already be released when queued input is dispatched.
        if(self->picker_.IsOpen()&&HIWORD(lp)==WM_LBUTTONDOWN){
            POINT point{};GetCursorPos(&point);ScreenToClient(w,&point);
            const bool title=self->layout_.title.Contains(static_cast<float>(point.x)*96/self->dpi_,static_cast<float>(point.y)*96/self->dpi_);
            self->picker_.Close();self->closedPickerClickUntil_=GetTickCount64()+250;
            self->suppressPickerClick_=title;
        }
        return MA_ACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(w,&ps);EndPaint(w,&ps);return 0;}
    case WM_PRINTCLIENT:case WM_PRINT: {
        if(!self->bitmap_.pixels.empty()){BITMAPINFO i{};i.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);i.bmiHeader.biWidth=static_cast<LONG>(self->bitmap_.width);i.bmiHeader.biHeight=-static_cast<LONG>(self->bitmap_.height);i.bmiHeader.biPlanes=1;i.bmiHeader.biBitCount=32;SetDIBitsToDevice(reinterpret_cast<HDC>(wp),0,0,self->bitmap_.width,self->bitmap_.height,0,0,0,self->bitmap_.height,self->bitmap_.pixels.data(),&i,DIB_RGB_COLORS);}return 0;
    }
    case kInitialBounds:{self->initialPending_=false;const std::size_t index=self->preferences_.theme==calendar::Theme::Spacious?0:1;if(!self->preferences_.Active()->hasPosition&&!self->initialSubmitted_[index]){self->initialSubmitted_[index]=true;self->CommitBounds(true);}return 0;}
    case kOpenTodo:self->OpenRequestedTodo();return 0;
    case WM_TIMER:
        if(wp==kMidnightTimer){KillTimer(w,kMidnightTimer);self->RefreshToday();}
        if(wp==kHighlightTimer){KillTimer(w,kHighlightTimer);self->view_.alignedEdges=0;self->ClearSnap();self->Paint();}
        if(wp==kRemoteHighlightTimer){KillTimer(w,kRemoteHighlightTimer);self->remoteEdges_=0;self->remoteSource_=nullptr;self->Paint();}
        if(wp==kAgendaTimer){if(self->SchedulesShown()&&IsWindowVisible(w)){if(self->RefreshTodoClock())self->Paint();}else KillTimer(w,kAgendaTimer);}return 0;
    case WM_SHOWWINDOW:if(wp)self->ArmAgendaTimer();else KillTimer(w,kAgendaTimer);break;
    case WM_TIMECHANGE:self->RefreshToday();return 0;
    case WM_POWERBROADCAST:if(wp==PBT_APMRESUMEAUTOMATIC||wp==PBT_APMRESUMESUSPEND)self->RefreshToday();return TRUE;
    case WM_SETTINGCHANGE:
        // Taskbar resized, moved or set to auto-hide: stay in the new work area.
        if(wp==SPI_SETWORKAREA){const auto confirmed=self->confirmedBounds_;if(self->ApplyBounds(true,confirmed?&*confirmed:nullptr))self->confirmedBounds_=self->CurrentBounds();}
        break;
    case WM_DISPLAYCHANGE:{const auto confirmed=self->confirmedBounds_;if(self->ApplyBounds(true,confirmed?&*confirmed:nullptr))self->confirmedBounds_=self->CurrentBounds();self->RefreshToday();return 0;}
    case WM_DPICHANGED:
        self->dpi_=HIWORD(wp);
        if(lp){self->dpiSuggested_=*reinterpret_cast<RECT*>(lp);self->dpiPending_=true;}
        PostMessageW(w,kDpiRefresh,0,0);return 0;
    case kDpiRefresh:
        self->picker_.Close();self->CancelTodoEditor();
        if(self->dpiPending_){const auto r=self->dpiSuggested_;self->dpiPending_=false;const auto p=*self->preferences_.Active();SetWindowPos(w,nullptr,r.left,r.top,MulDiv(p.width,static_cast<int>(self->dpi_),96),MulDiv(p.height,static_cast<int>(self->dpi_),96),SWP_NOZORDER|SWP_NOACTIVATE);}
        else self->ApplyBounds(true,self->confirmedBounds_?&*self->confirmedBounds_:nullptr);
        self->confirmedBounds_=self->CurrentBounds();self->Paint();return 0;
    case WM_GETMINMAXINFO:{auto* p=reinterpret_cast<MINMAXINFO*>(lp);p->ptMinTrackSize={MulDiv(320,static_cast<int>(self->dpi_),96),MulDiv(320,static_cast<int>(self->dpi_),96)};p->ptMaxTrackSize={MulDiv(1440,static_cast<int>(self->dpi_),96),MulDiv(720,static_cast<int>(self->dpi_),96)};return 0;}
    case WM_NCHITTEST: {
        RECT r{};GetWindowRect(w,&r);const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp),edge=MulDiv(8,static_cast<int>(self->dpi_),96),corner=MulDiv(14,static_cast<int>(self->dpi_),96);
        const bool left=x<r.left+edge,right=x>=r.right-edge,top=y<r.top+edge,bottom=y>=r.bottom-edge;
        if(x<r.left+corner&&y<r.top+corner)return HTTOPLEFT;
        if(x>=r.right-corner&&y<r.top+corner)return HTTOPRIGHT;
        if(x<r.left+corner&&y>=r.bottom-corner)return HTBOTTOMLEFT;
        if(x>=r.right-corner&&y>=r.bottom-corner)return HTBOTTOMRIGHT;
        if(left)return HTLEFT;
        if(right)return HTRIGHT;
        if(top)return HTTOP;
        if(bottom)return HTBOTTOM;
        return HTCLIENT;
    }
    case WM_ENTERSIZEMOVE:self->dayTooltip_.Hide();self->picker_.Close();widget_snap::BeginInteraction(w);self->interactionBounds_=self->confirmedBounds_;self->sizing_=true;self->snap_={};return 0;
    case WM_SIZING: {
        auto& r=*reinterpret_cast<RECT*>(lp);const auto edges=EdgeForSizing(wp);const int min=MulDiv(320,static_cast<int>(self->dpi_),96),maxW=MulDiv(1440,static_cast<int>(self->dpi_),96),maxH=MulDiv(720,static_cast<int>(self->dpi_),96);
        const int width=std::clamp(static_cast<int>(r.right-r.left),min,maxW),height=std::clamp(static_cast<int>(r.bottom-r.top),min,maxH);
        if(edges&CalendarEdgeLeft)r.left=r.right-width;else r.right=r.left+width;
        if(edges&CalendarEdgeTop)r.top=r.bottom-height;else r.bottom=r.top+height;
        const auto neighbors=NeighborsOf(w);auto align=DetectCalendarResize(BoundsRect(r),edges,neighbors.values);self->NotifySnap(align,neighbors.handles);self->view_.alignedEdges=(align.horizontal.active?align.horizontal.edge:0)|(align.vertical.active?align.vertical.edge:0);return TRUE;
    }
    case WM_SIZE:if(self->sizing_&&!self->applying_)self->Paint();return 0;
    case WM_EXITSIZEMOVE:{self->sizing_=false;self->view_.alignedEdges=0;self->ClearSnap();self->Paint();const auto alive=self->alive_;self->CommitBounds();if(*alive)self->interactionBounds_.reset();return 0;}
    case WM_LBUTTONDOWN: {
        self->dayTooltip_.Hide();
        self->UpdateMaterial();
        if(self->menuOpen_)return 0;
        if(self->AgendaClick(static_cast<float>(GET_X_LPARAM(lp))*96/self->dpi_,static_cast<float>(GET_Y_LPARAM(lp))*96/self->dpi_))return 0;
        auto hit=self->layout_.HitTest(static_cast<float>(GET_X_LPARAM(lp))*96/self->dpi_,static_cast<float>(GET_Y_LPARAM(lp))*96/self->dpi_);
        self->view_.pressedNavigation=hit.kind==CalendarHitKind::Previous?0:hit.kind==CalendarHitKind::Next?2:-1;
        if(hit.kind==CalendarHitKind::Day&&(self->picker_.IsOpen()||GetTickCount64()<self->closedPickerClickUntil_)){
            const auto cell=self->layout_.cells[static_cast<std::size_t>(hit.day)],popup=self->layout_.picker;
            if(cell.x<popup.Right()&&cell.Right()>popup.x&&cell.y<popup.Bottom()&&cell.Bottom()>popup.y)hit={};
        }
        self->closedPickerClickUntil_=0;
        if(hit.kind==CalendarHitKind::Day&&self->preferences_.theme!=calendar::Theme::Spacious&&!self->model_.Grid()[static_cast<std::size_t>(hit.day)].currentMonth)hit={};
        if(hit.kind!=CalendarHitKind::Blank){self->Activate(hit);return 0;}
        self->picker_.Close();if(!self->preferences_.locked){widget_snap::BeginInteraction(w);self->interactionBounds_=self->confirmedBounds_;self->dragging_=true;self->snap_={};GetCursorPos(&self->dragCursor_);GetWindowRect(w,&self->dragOrigin_);SetCapture(w);}return 0;
    }
    case WM_MOUSEMOVE:
        self->UpdateMaterial();
        if(self->dragging_&&GetCapture()==w){POINT c{};GetCursorPos(&c);auto r=BoundsRect(self->dragOrigin_);r.x+=static_cast<float>(c.x-self->dragCursor_.x);r.y+=static_cast<float>(c.y-self->dragCursor_.y);const auto neighbors=NeighborsOf(w);r=ResolveCalendarMove(r,neighbors.values,WorkArea(w),self->snap_);self->NotifySnap(self->snap_,neighbors.handles);SetWindowPos(w,nullptr,static_cast<int>(r.x),static_cast<int>(r.y),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);const auto highlight=(self->snap_.horizontal.active?self->snap_.horizontal.edge:0)|(self->snap_.vertical.active?self->snap_.vertical.edge:0);if(highlight!=self->view_.alignedEdges){self->view_.alignedEdges=highlight;self->Paint();}}
        else {const auto hit=self->layout_.HitTest(static_cast<float>(GET_X_LPARAM(lp))*96/self->dpi_,static_cast<float>(GET_Y_LPARAM(lp))*96/self->dpi_);const int hover=hit.kind==CalendarHitKind::Previous?0:hit.kind==CalendarHitKind::Title?1:hit.kind==CalendarHitKind::Next?2:-1;if(hover!=self->view_.hover){self->view_.hover=hover;self->Paint();}TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT),TME_LEAVE,w,0};TrackMouseEvent(&track);}return 0;
    case WM_MOUSELEAVE:self->view_.hover=-1;self->view_.pressedNavigation=-1;self->Paint();return 0;
    case WM_LBUTTONUP:self->view_.pressedNavigation=-1;self->UpdateMaterial();if(self->dragging_){self->dragging_=false;ReleaseCapture();self->ClearSnap(true);if(self->view_.alignedEdges)SetTimer(w,kHighlightTimer,260,nullptr);const auto alive=self->alive_;self->CommitBounds();if(*alive)self->interactionBounds_.reset();}else self->Paint();return 0;
    case WM_CANCELMODE:self->view_.pressedNavigation=-1;self->Paint();return DefWindowProcW(w,message,wp,lp);
    case WM_CAPTURECHANGED:self->view_.pressedNavigation=-1;if(self->dragging_){self->dragging_=false;KillTimer(w,kHighlightTimer);self->view_.alignedEdges=0;self->snap_={};self->ClearSnap();const auto stable=self->ConfirmedBounds();self->ApplyBounds(false,stable?&*stable:nullptr);self->dpiPending_=false;MSG refresh{};while(PeekMessageW(&refresh,w,kDpiRefresh,kDpiRefresh,PM_REMOVE)){}self->confirmedBounds_=self->CurrentBounds();self->interactionBounds_.reset();}self->Paint();return 0;
    case WM_KEYDOWN:self->Key(static_cast<UINT>(wp));return 0;
    case WM_MOUSEWHEEL:
        if(self->preferences_.theme==calendar::Theme::Todo){
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);
            const float x=static_cast<float>(p.x)*96/self->dpi_,y=static_cast<float>(p.y)*96/self->dpi_;
            const float delta=-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp))/WHEEL_DELTA;
            auto& v=self->view_;const auto& a=self->agenda_;
            if(self->TimeGridShown()){
                const float base=v.boardScroll<0?a.scroll:v.boardScroll;
                if(GET_KEYSTATE_WPARAM(wp)&MK_CONTROL){
                    // Zoom around the pointer, from the whole day up to three times the hour height.
                    const float old=std::max(v.boardZoom,a.minZoom),next=std::clamp(old*(delta<0?1.2f:1/1.2f),a.minZoom,3.0f);
                    const float anchor=std::clamp(y-a.heading.y,0.0f,a.heading.height);
                    v.boardZoom=next;v.boardScroll=std::max(0.0f,(base+anchor)*next/old-anchor);
                }else v.boardScroll=std::clamp(base+delta*48,0.0f,a.maxScroll);
                self->Paint();return 0;
            }
            if(self->preferences_.boardView==calendar::BoardView::Month){
                const auto hit=self->layout_.HitTest(x,y);
                if(hit.kind!=CalendarHitKind::Day||hit.day<0)return 0;
                auto& cell=v.cellScroll[static_cast<std::size_t>(hit.day)];
                cell=std::clamp(cell+delta*38,0.0f,a.cellMaxScroll[static_cast<std::size_t>(hit.day)]);
                self->Paint();return 0;
            }
            if(self->preferences_.boardView==calendar::BoardView::All){
                if(self->layout_.boardDay.Contains(x,y))v.boardDayScroll=std::clamp(v.boardDayScroll+delta*34,0.0f,a.dayMaxScroll);
                else if(self->layout_.boardUpcoming.Contains(x,y))v.boardUpcomingScroll=std::clamp(v.boardUpcomingScroll+delta*50,0.0f,a.upcomingMaxScroll);
                else return 0;
                self->Paint();return 0;
            }
            return 0;
        }
        if(self->view_.agendaOpen){self->AgendaScroll(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp))/WHEEL_DELTA*48);return 0;}break;
    case WM_KILLFOCUS:self->view_.pressedNavigation=-1;self->view_.keyboardFocus=false;if(reinterpret_cast<HWND>(wp)!=self->picker_.Handle())self->picker_.Close();self->Paint();return 0;
    case WM_CONTEXTMENU:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(p.x==-1&&p.y==-1)GetCursorPos(&p);self->ShowMenu(p);return 0;}
    case WM_CLOSE:self->Dispatch({calendar::ActionType::Enable,0});return 0;
    case WM_NCDESTROY:
        self->dayTooltip_.Stop();
        self->material_.Detach();
        KillTimer(w,kMidnightTimer);KillTimer(w,kHighlightTimer);KillTimer(w,kRemoteHighlightTimer);KillTimer(w,kAgendaTimer);self->CancelTodoEditor();
        self->ClearSnap();RemovePropW(w,lume::window_ids::DesktopWidgetProp);RemovePropW(w,widget_snap::Capability);RemovePropW(w,widget_snap::ActiveEdges);
        SetWindowLongPtrW(w,GWLP_USERDATA,0);self->window_=nullptr;self->picker_.Close();self->renderer_.reset();self->bitmap_={};
        self->dragging_=false;self->sizing_=false;self->view_={};self->snap_={};self->confirmedBounds_.reset();self->interactionBounds_.reset();self->remoteEdges_=0;self->remoteSource_=nullptr;break;
    default:break;
    }
    return DefWindowProcW(w,message,wp,lp);
}
} // namespace lume::platform::windows
