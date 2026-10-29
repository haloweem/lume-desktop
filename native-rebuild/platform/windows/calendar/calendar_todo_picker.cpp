#include "platform/windows/calendar/calendar_todo_picker.h"
#include "platform/windows/calendar/calendar_todo_form_layout.h"
#include "platform/windows/calendar/calendar_todo_ui.h"
#include "platform/windows/rounded_window_surface.h"
#include "core/todo/todo_time.h"
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <utility>
namespace lume::platform::windows {
namespace {
constexpr wchar_t kClass[]=L"LumeCalendarTodoPicker";
enum Id { Previous=200,Year,Month,Mode,Next,Hour,Minute,Now,Done,Cancel,Number,None,Decrease,Increase,DayFirst=300,MonthFirst=400,ChoiceFirst=500 };
}
struct CalendarTodoPicker::Impl : std::enable_shared_from_this<Impl> {
    HWND window=nullptr,owner=nullptr,trigger=nullptr;
    RECT host{};unsigned dpi=96;bool numeric=false,choice=false,monthView=false,updating=false,closing=false;
    std::vector<std::wstring> options;int chosen=-1;ChoiceChanged choiceChanged;
    HWND pendingSource=nullptr;int pendingAction=0;
    todo::LocalDateTime value{},today{};
    todo::ReminderDraft reminder{};
    DateChanged dateChanged;ReminderChanged reminderChanged;
    CurrentTime nowProvider;
    std::unique_ptr<todo_ui::Surface> surface;
    std::vector<HWND> controls,tabs;
    std::array<calendar::DayCell,42> days{};
    float width=304,height=306,contentHeight=306,scroll=0;
    std::wstring error;
    HWND Get(int id) const{return GetDlgItem(window,id);}
    HWND Add(const wchar_t *kind,const wchar_t *text,int id,DWORD style=0) {
        auto w=surface->Add(window,window,kind,text,id,style);
        controls.push_back(w);
        return w;
    }
    void Close(bool restore) {
        auto keep=shared_from_this();
        if(closing)return;
        closing=true;
        const auto old=window,focus=trigger;
        window=nullptr;
        if(old){SetWindowLongPtrW(old,GWLP_USERDATA,0);DestroyWindow(old);}
        pendingAction=0;pendingSource=nullptr;
        controls.clear();tabs.clear();dateChanged={};reminderChanged={};choiceChanged={};nowProvider={};surface.reset();
        if(restore&&IsWindow(focus)&&IsWindowEnabled(focus))SetFocus(focus);
        closing=false;
    }
    bool Open(HINSTANCE instance,const calendar::Preferences &prefs) {
        surface=std::make_unique<todo_ui::Surface>(dpi,prefs);
        WNDCLASSEXW wc{};
        wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=Proc;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=kClass;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,kClass,
            choice?L"重复":numeric?L"提前提醒分钟数":L"选择日期时间",WS_POPUP|WS_CLIPCHILDREN,
            host.left,host.top,1,1,owner,nullptr,instance,this);
        if(!window)return false;
        if(choice) {
            for(std::size_t i=0;i<options.size();++i)Add(L"BUTTON",options[i].c_str(),ChoiceFirst+static_cast<int>(i),BS_OWNERDRAW);
        } else if(numeric) {
            Add(L"BUTTON",L"−",Decrease,BS_OWNERDRAW);
            auto input=Add(L"EDIT",L"",Number,ES_NUMBER|ES_CENTER|ES_AUTOHSCROLL|WS_BORDER);
            SendMessageW(input,EM_SETLIMITTEXT,6,0);
            Add(L"BUTTON",L"+",Increase,BS_OWNERDRAW);
            Add(L"BUTTON",L"不提醒",None,BS_AUTOCHECKBOX);
            Add(L"BUTTON",L"取消",Cancel,BS_OWNERDRAW);
            Add(L"BUTTON",L"完成",Done,BS_OWNERDRAW);
            auto n=reminder.kind==todo::ReminderKind::Relative&&reminder.leadMilliseconds>=0&&
                reminder.leadMilliseconds<=525600LL*60000&&reminder.leadMilliseconds%60000==0?
                reminder.leadMilliseconds/60000:10;
            SetWindowTextW(input,std::to_wstring(n).c_str());
            SendMessageW(Get(None),BM_SETCHECK,reminder.kind==todo::ReminderKind::None?BST_CHECKED:BST_UNCHECKED,0);
            ToggleNone();
        } else {
            Add(L"BUTTON",L"‹",Previous,BS_OWNERDRAW);
            Add(L"EDIT",L"",Year,ES_NUMBER|ES_CENTER|WS_BORDER);
            Add(L"EDIT",L"",Month,ES_NUMBER|ES_CENTER|WS_BORDER);
            Add(L"BUTTON",L"▦",Mode,BS_OWNERDRAW);
            Add(L"BUTTON",L"›",Next,BS_OWNERDRAW);
            for(int i=0;i<42;++i)Add(L"BUTTON",L"",DayFirst+i,BS_OWNERDRAW);
            for(int i=0;i<12;++i)Add(L"BUTTON",(std::to_wstring(i+1)+L"月").c_str(),MonthFirst+i,BS_OWNERDRAW);
            Add(L"EDIT",L"",Hour,ES_NUMBER|ES_CENTER|WS_BORDER);
            Add(L"EDIT",L"",Minute,ES_NUMBER|ES_CENTER|WS_BORDER);
            Add(L"BUTTON",L"此刻",Now,BS_OWNERDRAW);
            Add(L"BUTTON",L"完成",Done,BS_OWNERDRAW);
            SendMessageW(Get(Year),EM_SETLIMITTEXT,4,0);
            for(auto id:{Month,Hour,Minute})SendMessageW(Get(id),EM_SETLIMITTEXT,2,0);
            WriteInputs();
        }
        Layout();surface->Attach(window,true);
        surface->Show(Get(choice?ChoiceFirst+std::max(chosen,0):numeric?(reminder.kind==todo::ReminderKind::None?None:Number):Year));
        return true;
    }
    void ToggleNone() {
        bool none=SendMessageW(Get(None),BM_GETCHECK,0,0)==BST_CHECKED;
        for(int id:{Number,Decrease,Increase})EnableWindow(Get(id),!none);
    }
    void WriteInputs() {
        updating=true;
        SetWindowTextW(Get(Year),std::to_wstring(value.date.year).c_str());
        SetWindowTextW(Get(Month),std::to_wstring(value.date.month).c_str());
        SetWindowTextW(Get(Hour),std::to_wstring(value.hour).c_str());
        SetWindowTextW(Get(Minute),std::to_wstring(value.minute).c_str());
        updating=false;
    }
    bool ReadInputs() {
        auto next=value;
        if(!todo_ui::Number(Get(Year),1,9999,next.date.year)||
           !todo_ui::Number(Get(Month),1,12,next.date.month)||
           !todo_ui::Number(Get(Hour),0,23,next.hour)||
           !todo_ui::Number(Get(Minute),0,59,next.minute)) {
            Error(L"请输入有效年、月、小时和分钟");return false;
        }
        next.date.day=std::min(next.date.day,calendar::DaysInMonth(next.date.year,next.date.month));
        next.second=0;next.millisecond=0;value=next;
        return true;
    }
    void Error(std::wstring message){error=std::move(message);InvalidateRect(window,nullptr,FALSE);}
    void Place(int id,float x,float y,float w,float h){todo_ui::Move(Get(id),{x,y-scroll,w,h},dpi);tabs.push_back(Get(id));}
    void Layout() {
        if(!window)return;
        updating=true;tabs.clear();
        RECT anchor{};GetWindowRect(trigger,&anchor);
        const float scale=96.f/static_cast<float>(dpi);
        const CalendarRect hostDip{0,0,(host.right-host.left)*scale,(host.bottom-host.top)*scale};
        const CalendarRect anchorDip{(anchor.left-host.left)*scale,(anchor.top-host.top)*scale,(anchor.right-anchor.left)*scale,(anchor.bottom-anchor.top)*scale};
        contentHeight=choice?16.f+34.f*static_cast<float>(options.size()):numeric?147:TodoDatePickerHeight(value.date.year,value.date.month,monthView);
        auto rect=PlaceTodoPicker(hostDip,anchorDip,numeric||choice?std::clamp(anchorDip.width,252.f,304.f):304.f,contentHeight);
        width=rect.width;height=rect.height;
        scroll=std::clamp(scroll,0.f,std::max(0.f,contentHeight-height));
        SetWindowPos(window,nullptr,host.left+todo_ui::Px(rect.x,dpi),host.top+todo_ui::Px(rect.y,dpi),
                     todo_ui::Px(width,dpi),todo_ui::Px(height,dpi),SWP_NOZORDER|SWP_NOACTIVATE);
        ConfigureRoundedWindow(window,8.f*dpi/96.f);
        if(choice) {
            for(std::size_t i=0;i<options.size();++i)Place(ChoiceFirst+static_cast<int>(i),11,11.f+34.f*static_cast<float>(i),width-22,30);
        }
        else if(numeric) {
            const float x=(width-228)/2;
            Place(Decrease,x,19,28,28);Place(Number,x+36,17,86,32);Place(Increase,x+130,19,28,28);
            Place(None,11,63,width-22,30);Place(Cancel,11,110,62,26);Place(Done,width-73,110,62,26);
        }
        else {
            Place(Previous,11,11,28,28);Place(Year,45,11,64,28);Place(Month,130,11,36,28);Place(Mode,width-79,11,28,28);Place(Next,width-39,11,28,28);
            days=calendar::MonthGrid(value.date.year,value.date.month,today.date,0);
            const int count=TodoPickerWeekCount(value.date.year,value.date.month)*7;
            const float cw=(width-22-18)/7, mw=(width-22-18)/4;
            for(int i=0;i<42;++i) {
                HWND b=Get(DayFirst+i);
                const bool visible=!monthView&&i<count;
                ShowWindow(b,visible?SW_SHOWNA:SW_HIDE);
                if(visible) {
                    SetWindowTextW(b,std::to_wstring(days[static_cast<std::size_t>(i)].date.day).c_str());
                    EnableWindow(b,days[static_cast<std::size_t>(i)].selectable);
                    Place(DayFirst+i,11+(i%7)*(cw+3),69.f+static_cast<float>((i/7)*29),cw,26);
                }
            }
            for(int i=0;i<12;++i) {
                HWND b=Get(MonthFirst+i);
                ShowWindow(b,monthView?SW_SHOWNA:SW_HIDE);
                if(monthView)Place(MonthFirst+i,11+(i%4)*(mw+6),47.f+static_cast<float>((i/4)*38),mw,32);
            }
            const float timeY=contentHeight-84;
            Place(Hour,(width-160)/2,timeY,52,28);Place(Minute,(width+16)/2,timeY,52,28);
            Place(Now,11,contentHeight-37,62,26);Place(Done,width-73,contentHeight-37,62,26);
        }
        updating=false;InvalidateRect(window,nullptr,TRUE);surface->Refresh();
    }
    bool Publish(bool finish) {
        if(!ReadInputs())return false;
        const auto callback=dateChanged;
        const auto selected=value;
        auto keep=shared_from_this();
        if(callback&&!callback(selected)) {
            if(window)Error(L"该本地时间无效，请重新选择");
            return false;
        }
        if(!window)return true;
        Error(L"");
        if(finish)Close(true);else Layout();
        return true;
    }
    void Action(int id) {
        if(choice) {
            if(id<ChoiceFirst||id>=ChoiceFirst+static_cast<int>(options.size()))return;
            const auto callback=choiceChanged;
            Close(true);
            if(callback)callback(id-ChoiceFirst);
            return;
        }
        if(numeric) {
            if(id==Cancel){Close(true);return;}
            if(id==None){ToggleNone();return;}
            if(id==Decrease||id==Increase) {
                int n=10;todo_ui::Number(Get(Number),0,525600,n);
                n=std::clamp(n+(id==Decrease?-1:1),0,525600);
                SetWindowTextW(Get(Number),std::to_wstring(n).c_str());
                return;
            }
            if(id==Done) {
                int minutes=0;
                const bool none=SendMessageW(Get(None),BM_GETCHECK,0,0)==BST_CHECKED;
                if(!none&&!todo_ui::Number(Get(Number),0,525600,minutes)) {
                    Error(L"请输入 0～525600 的整数");return;
                }
                const auto callback=reminderChanged;
                const todo::ReminderDraft next{none?todo::ReminderKind::None:todo::ReminderKind::Relative,
                                               static_cast<todo::Millis>(minutes)*60000,0,false};
                Close(true);
                if(callback)callback(next);
            }
            return;
        }
        if(id==Done){Publish(true);return;}
        if(id==Now){
            const auto callback=nowProvider;
            auto selected=callback?callback():std::optional<todo::LocalDateTime>(today);
            if(!window)return;
            if(!selected){Error(L"当前时间不可用，请重试");return;}
            today=*selected;value=*selected;value.second=0;value.millisecond=0;
            monthView=false;WriteInputs();Publish(false);return;
        }
        if(!ReadInputs())return;
        if(id==Mode){monthView=!monthView;Layout();return;}
        if(id==Previous||id==Next) {
            int delta=id==Previous?-1:1;value.date.day=1;
            if(monthView)value.date.year=std::clamp(value.date.year+delta,1,9999);
            else {
                int index=(value.date.year-1)*12+value.date.month-1;
                index=std::clamp(index+delta,0,9999*12-1);
                value.date.year=index/12+1;value.date.month=index%12+1;
            }
            WriteInputs();Layout();return;
        }
        if(id>=MonthFirst&&id<MonthFirst+12) {
            value.date.month=id-MonthFirst+1;value.date.day=1;monthView=false;
            WriteInputs();Layout();SetFocus(Get(DayFirst));return;
        }
        if(id>=DayFirst&&id<DayFirst+42) {
            auto cell=days[static_cast<std::size_t>(id-DayFirst)];
            if(!cell.selectable)return;
            value.date=cell.date;WriteInputs();Publish(false);
        }
    }
    LRESULT Key(WPARAM key,HWND source) {
        if(key==VK_ESCAPE){Close(true);return 1;}
        if(key==VK_TAB){todo_ui::Fence(tabs,source,GetKeyState(VK_SHIFT)<0);return 1;}
        const int id=GetDlgCtrlID(source);
        if(key==VK_RETURN) {
            if(id==Number||id==Year||id==Month||id==Hour||id==Minute)Action(Done);else Action(id);
            return 1;
        }
        const int base=choice?(id>=ChoiceFirst&&id<ChoiceFirst+static_cast<int>(options.size())?ChoiceFirst:0):
            id>=MonthFirst&&id<MonthFirst+12?MonthFirst:id>=DayFirst&&id<DayFirst+42?DayFirst:0;
        if(base) {
            const int count=base==ChoiceFirst?static_cast<int>(options.size()):base==MonthFirst?12:TodoPickerWeekCount(value.date.year,value.date.month)*7;
            const int columns=base==ChoiceFirst?1:base==MonthFirst?4:7;
            int delta=key==VK_LEFT?-1:key==VK_RIGHT?1:key==VK_UP?-columns:key==VK_DOWN?columns:0;
            int next=key==VK_HOME?0:key==VK_END?count-1:(id-base+delta+count)%count;
            if(delta||key==VK_HOME||key==VK_END){SetFocus(Get(base+next));return 1;}
        }
        return 0;
    }
    void Draw(HDC dc) {
        RECT client{};GetClientRect(window,&client);
        surface->Background(dc,client);
        if(numeric)surface->Label(dc,L"分钟前",{(width-228)/2+168,19-scroll,60,28});
        else if(!choice) {
            surface->Label(dc,L"年",{110,11-scroll,18,28});
            surface->Label(dc,L"月",{168,11-scroll,18,28});
            if(!monthView) {
                const wchar_t *names=L"日一二三四五六";
                const float cw=(width-22-18)/7;
                for(int i=0;i<7;++i)
                    surface->Label(dc,std::wstring(1,names[i]),{11+i*(cw+3),47-scroll,cw,14},DT_CENTER,11);
            }
            surface->Label(dc,L"时",{(width-160)/2+57,contentHeight-84-scroll,20,28});
            surface->Label(dc,L"分",{(width+16)/2+57,contentHeight-84-scroll,20,28});
        }
        if(!error.empty())surface->Label(dc,error,{78,contentHeight-37-scroll,width-156,26},DT_CENTER,10);
    }
    void Paint() {
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);
        Draw(dc);EndPaint(window,&ps);surface->Refresh();
    }
    static LRESULT CALLBACK Proc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto *raw=reinterpret_cast<Impl*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE) {
            raw=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            raw->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(raw));
        }
        if(!raw)return DefWindowProcW(w,m,wp,lp);
        auto self=raw->shared_from_this();
        switch(m){
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:self->Paint();return 0;
        case WM_PRINTCLIENT:self->Draw(reinterpret_cast<HDC>(wp));return 0;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:return self->surface->Color(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp));
        case WM_DRAWITEM: {
            auto &item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            int id=static_cast<int>(item.CtlID);
            bool selected=id>=DayFirst&&id<DayFirst+42?
                self->days[static_cast<std::size_t>(id-DayFirst)].date==self->value.date:
                id>=MonthFirst&&id<MonthFirst+12?id-MonthFirst+1==self->value.date.month:
                self->choice&&id-ChoiceFirst==self->chosen;
            const auto *day=id>=DayFirst&&id<DayFirst+42?&self->days[static_cast<std::size_t>(id-DayFirst)]:nullptr;
            self->surface->Button(item,selected,CLR_INVALID,todo_ui::Surface::Appearance::Plain,
                                  day&&day->today,day&&!day->currentMonth);return TRUE;
        }
        case WM_COMMAND:
            // BUTTON may still be processing BM_CLICK / mouse-up. Let that
            // notification return before an action destroys or relayouts it.
            if(HIWORD(wp)==BN_CLICKED&&!self->pendingAction&&lp&&
               reinterpret_cast<HWND>(lp)==self->Get(LOWORD(wp))) {
                self->pendingAction=LOWORD(wp);self->pendingSource=reinterpret_cast<HWND>(lp);
                PostMessageW(w,todo_ui::ActionMessage,wp,lp);
            }
            else if(HIWORD(wp)==EN_KILLFOCUS&&!self->numeric&&!self->choice&&!self->updating) {
                int id=LOWORD(wp);
                if(self->ReadInputs()) {
                    if(id==Hour||id==Minute)self->Publish(false);else self->Layout();
                }
            }
            return 0;
        case todo_ui::ActionMessage: {
            const int id=self->pendingAction;const auto source=self->pendingSource;
            self->pendingAction=0;self->pendingSource=nullptr;
            if(id&&id==LOWORD(wp)&&source==reinterpret_cast<HWND>(lp)&&source==self->Get(id))self->Action(id);
            return 0;
        }
        case todo_ui::KeyMessage:return self->Key(wp,reinterpret_cast<HWND>(lp));
        case todo_ui::FocusMessage: {
            HWND focus=reinterpret_cast<HWND>(lp);
            RECT r{};GetWindowRect(focus,&r);
            POINT p{r.left,r.top};ScreenToClient(w,&p);
            float top=static_cast<float>(p.y)*96/self->dpi;
            float bottom=top+static_cast<float>(r.bottom-r.top)*96/self->dpi;
            float old=self->scroll;
            if(top<0)self->scroll+=top;
            else if(bottom>self->height)self->scroll+=bottom-self->height;
            if(self->scroll!=old)self->Layout();
            return 0;
        }
        case WM_MOUSEWHEEL:
            self->scroll=std::clamp(self->scroll-GET_WHEEL_DELTA_WPARAM(wp)/120.f*48,
                                   0.f,std::max(0.f,self->contentHeight-self->height));
            self->Layout();return 0;
        case WM_ACTIVATEAPP:if(!wp)self->Close(false);return 0;
        case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE&&reinterpret_cast<HWND>(lp)!=w)self->Close(false);return 0;
        case WM_CLOSE:self->Close(true);return 0;
        case WM_NCDESTROY:self->window=nullptr;SetWindowLongPtrW(w,GWLP_USERDATA,0);return DefWindowProcW(w,m,wp,lp);
        default:return DefWindowProcW(w,m,wp,lp);
        }
    }
};
CalendarTodoPicker::CalendarTodoPicker()=default;
CalendarTodoPicker::~CalendarTodoPicker(){Close();}
bool CalendarTodoPicker::OpenDate(HINSTANCE instance,HWND owner,HWND trigger,RECT host,unsigned dpi,
    const calendar::Preferences &prefs,todo::LocalDateTime value,todo::LocalDateTime now,DateChanged callback,CurrentTime nowProvider) {
    Close();
    auto p=std::make_shared<Impl>();
    impl_=p;p->owner=owner;p->trigger=trigger;p->host=host;p->dpi=dpi;
    p->value=value;p->today=now;p->dateChanged=std::move(callback);p->nowProvider=std::move(nowProvider);
    return p->Open(instance,prefs);
}
bool CalendarTodoPicker::OpenReminder(HINSTANCE instance,HWND owner,HWND trigger,RECT host,unsigned dpi,
    const calendar::Preferences &prefs,todo::ReminderDraft value,ReminderChanged callback) {
    Close();
    auto p=std::make_shared<Impl>();
    impl_=p;p->owner=owner;p->trigger=trigger;p->host=host;p->dpi=dpi;
    p->numeric=true;p->reminder=value;p->reminderChanged=std::move(callback);
    return p->Open(instance,prefs);
}
bool CalendarTodoPicker::OpenChoice(HINSTANCE instance,HWND owner,HWND trigger,RECT host,unsigned dpi,
    const calendar::Preferences &prefs,std::vector<std::wstring> options,int chosen,ChoiceChanged callback) {
    Close();
    if(options.empty())return false;
    auto p=std::make_shared<Impl>();
    impl_=p;p->owner=owner;p->trigger=trigger;p->host=host;p->dpi=dpi;
    p->choice=true;p->options=std::move(options);p->chosen=chosen;p->choiceChanged=std::move(callback);
    return p->Open(instance,prefs);
}
void CalendarTodoPicker::Close(bool restore){auto p=std::move(impl_);if(p)p->Close(restore);}
HWND CalendarTodoPicker::Handle() const noexcept{return impl_?impl_->window:nullptr;}
bool CalendarTodoPicker::IsOpen() const noexcept{return Handle()!=nullptr;}
}
