#include "platform/windows/calendar/calendar_todo_editor.h"
#include "platform/windows/calendar/calendar_todo_draft.h"
#include "platform/windows/calendar/calendar_todo_picker.h"
#include "platform/windows/calendar/calendar_todo_form_layout.h"
#include "platform/windows/calendar/calendar_todo_ui.h"
#include "platform/windows/calendar/calendar_todo_view.h"
#include "platform/windows/rounded_window_surface.h"
#include <windowsx.h>
#include <algorithm>
#include <utility>
namespace lume::platform::windows {
namespace {
constexpr wchar_t kClass[]=L"LumeCalendarTodoEditor",kFormClass[]=L"LumeCalendarTodoForm";
constexpr UINT kOutsideClick=WM_APP+532;
enum Id { Title=101,Start,End,Lead,Notes,SaveButton,CancelButton,CloseButton,Repeat,ColorFirst=120 };
bool Trigger(int id) { return id==Start||id==End||id==Lead||id==Repeat; }
constexpr COLORREF kColors[]={CLR_INVALID,RGB(255,105,97),RGB(255,163,72),RGB(245,214,92),RGB(107,216,155),RGB(101,180,255)};
// Offered rules; a stored rule outside these still shows its own label.
const std::optional<todo::Recurrence> kRepeats[]={std::nullopt,todo::Recurrence{todo::Frequency::Daily,1},
    todo::Recurrence{todo::Frequency::Weekly,1},todo::Recurrence{todo::Frequency::Monthly,1}};
constexpr const wchar_t *kColorNames[]={L"无颜色",L"红色",L"橙色",L"黄色",L"绿色",L"蓝色"};
std::wstring DateText(const std::optional<todo::LocalDateTime> &value) {
    if(!value)return L"未设置";
    wchar_t text[64]{};
    swprintf(text,64,L"%04d/%02d/%02d %02d:%02d",value->date.year,value->date.month,
             value->date.day,value->hour,value->minute);
    return text;
}
}
struct CalendarTodoEditor::Impl : std::enable_shared_from_this<Impl> {
    HWND window=nullptr,owner=nullptr,returnFocus=nullptr,viewport=nullptr,form=nullptr,title=nullptr,notes=nullptr;
    HINSTANCE instance=nullptr;
    RECT host{};
    unsigned dpi=96;
    bool ownerWasEnabled=false,busy=false,closing=false,editing=false;
    const todo::ILocalTimePort *time=nullptr;
    todo::Millis now=0;
    calendar::Preferences prefs;
    CalendarTodoFormLayout layout;
    float scroll=0;
    std::wstring feedback;
    std::unique_ptr<CalendarTodoDraft> draft;
    std::unique_ptr<todo_ui::Surface> surface;
    CalendarTodoPicker picker;
    Save save;
    Closed closed;
    Now nowProvider;
    HWND pickerTrigger=nullptr,suppressTrigger=nullptr,pendingSource=nullptr;
    ULONGLONG suppressUntil=0;
    int pendingAction=0;
    std::vector<HWND> tabs;
    HHOOK mouseHook=nullptr,messageHook=nullptr;
    // Hooks are thread-specific and live only while this thread owns an editor.
    // Both real mouse messages and posted own-window messages use the same bounded route.
    static inline thread_local std::vector<std::weak_ptr<Impl>> activeEditors;
    static bool IsMouseDown(UINT message) {
        return message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||message==WM_MBUTTONDOWN||
               message==WM_NCLBUTTONDOWN||message==WM_NCRBUTTONDOWN||message==WM_NCMBUTTONDOWN;
    }
    static void RouteOutside(POINT point) {
        for(auto entry:activeEditors) {
            auto state=entry.lock();
            if(!state||!state->window||state->busy||!PtInRect(&state->host,point))continue;
            RECT bounds{};
            GetWindowRect(state->window,&bounds);
            if(PtInRect(&bounds,point))continue;
            if(state->picker.IsOpen()) {
                GetWindowRect(state->picker.Handle(),&bounds);
                if(PtInRect(&bounds,point))continue;
            }
            PostMessageW(state->window,kOutsideClick,0,0);
        }
    }
    static LRESULT CALLBACK MouseHook(int code,WPARAM wp,LPARAM lp) {
        if(code==HC_ACTION&&IsMouseDown(static_cast<UINT>(wp)))
            RouteOutside(reinterpret_cast<MOUSEHOOKSTRUCT*>(lp)->pt);
        return CallNextHookEx(nullptr,code,wp,lp);
    }
    static LRESULT CALLBACK MessageHook(int code,WPARAM wp,LPARAM lp) {
        if(code==HC_ACTION&&wp==PM_REMOVE) {
            const auto &message=*reinterpret_cast<MSG*>(lp);
            if(IsMouseDown(message.message)) {
                POINT point{GET_X_LPARAM(message.lParam),GET_Y_LPARAM(message.lParam)};
                if(message.message==WM_LBUTTONDOWN||message.message==WM_RBUTTONDOWN||message.message==WM_MBUTTONDOWN)
                    ClientToScreen(message.hwnd,&point);
                RouteOutside(point);
            }
        }
        return CallNextHookEx(nullptr,code,wp,lp);
    }
    bool WatchOutside() {
        activeEditors.push_back(weak_from_this());
        mouseHook=SetWindowsHookExW(WH_MOUSE,MouseHook,nullptr,GetCurrentThreadId());
        messageHook=SetWindowsHookExW(WH_GETMESSAGE,MessageHook,nullptr,GetCurrentThreadId());
        return mouseHook&&messageHook;
    }
    void StopWatchingOutside() {
        if(mouseHook)UnhookWindowsHookEx(mouseHook);
        if(messageHook)UnhookWindowsHookEx(messageHook);
        mouseHook=nullptr;messageHook=nullptr;
        activeEditors.erase(std::remove_if(activeEditors.begin(),activeEditors.end(),[this](const auto &entry){
            auto state=entry.lock();return !state||state.get()==this;
        }),activeEditors.end());
    }
    HWND Get(int id) const {
        return GetDlgItem(id==SaveButton||id==CancelButton||id==CloseButton?window:form,id);
    }
    void Close(bool restore) {
        auto keep=shared_from_this();
        if(closing)return;
        closing=true;
        const auto old=window,oldOwner=owner,focus=returnFocus;
        const bool enable=ownerWasEnabled;
        auto notify=std::move(closed);
        StopWatchingOutside();
        picker.Close(false);
        window=nullptr;
        ownerWasEnabled=false;
        if(old) {
            SetWindowLongPtrW(old,GWLP_USERDATA,0);
            if(form)SetWindowLongPtrW(form,GWLP_USERDATA,0);
            if(viewport)SetWindowLongPtrW(viewport,GWLP_USERDATA,0);
            DestroyWindow(old);
        }
        form=nullptr;viewport=nullptr;title=nullptr;notes=nullptr;
        pendingAction=0;pendingSource=nullptr;
        tabs.clear();surface.reset();save={};nowProvider={};
        if(enable&&IsWindow(oldOwner))EnableWindow(oldOwner,TRUE);
        if(restore&&IsWindow(oldOwner)&&IsWindowEnabled(oldOwner)) {
            SetActiveWindow(oldOwner);
            SetFocus(IsWindow(focus)?focus:oldOwner);
        }
        closing=false;
        if(notify)notify();
    }
    bool Open() {
        WNDCLASSEXW wc{};
        wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=Proc;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=kClass;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        wc.lpszClassName=kFormClass;wc.lpfnWndProc=FormProc;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        const float scale=96.f/static_cast<float>(dpi);
        layout=CalculateTodoFormLayout((host.right-host.left)*scale,(host.bottom-host.top)*scale);
        surface=std::make_unique<todo_ui::Surface>(dpi,prefs);
        returnFocus=GetFocus();
        window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,kClass,editing?L"编辑日程":L"新建日程",
            WS_POPUP|WS_CLIPCHILDREN,host.left+todo_ui::Px(layout.panel.x,dpi),
            host.top+todo_ui::Px(layout.panel.y,dpi),todo_ui::Px(layout.panel.width,dpi),
            todo_ui::Px(layout.panel.height,dpi),owner,nullptr,instance,this);
        if(!window)return false;
        viewport=CreateWindowExW(WS_EX_CONTROLPARENT,kFormClass,L"日程可滚动表单",
            WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,window,nullptr,instance,this);
        form=CreateWindowExW(WS_EX_CONTROLPARENT,kFormClass,L"日程字段",
            WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1,1,viewport,nullptr,instance,this);
        title=surface->Add(form,window,L"EDIT",todo_ui::Wide(draft->title).c_str(),Title,ES_AUTOHSCROLL);
        SendMessageW(title,EM_SETLIMITTEXT,500,0);
        for(auto item:{std::pair<int,const wchar_t*>{Start,L"开始时间"},{End,L"结束时间"},{Lead,L"提前提醒"},{Repeat,L"重复"}})
            surface->Add(form,window,L"BUTTON",item.second,item.first,BS_OWNERDRAW);
        for(int i=0;i<6;++i)surface->Add(form,window,L"BUTTON",kColorNames[i],ColorFirst+i,BS_OWNERDRAW);
        notes=surface->Add(form,window,L"EDIT",todo_ui::Wide(draft->notes).c_str(),Notes,
                           ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN);
        SendMessageW(notes,EM_SETLIMITTEXT,10000,0);
        surface->Add(window,window,L"BUTTON",editing?L"保存":L"添加",SaveButton,BS_OWNERDRAW);
        surface->Add(window,window,L"BUTTON",L"取消编辑",CancelButton,BS_OWNERDRAW);
        surface->Add(window,window,L"BUTTON",L"×",CloseButton,BS_OWNERDRAW);
        ShowWindow(Get(CancelButton),editing?SW_SHOWNA:SW_HIDE);
        ConfigureRoundedWindow(window,8.f*dpi/96.f);
        tabs={title,Get(Start),Get(End),Get(Lead),Get(Repeat)};
        for(int i=0;i<6;++i)tabs.push_back(Get(ColorFirst+i));
        tabs.push_back(notes);tabs.push_back(Get(CancelButton));
        tabs.push_back(Get(SaveButton));tabs.push_back(Get(CloseButton));
        Position();Refresh();
        surface->Attach(window);
        if(!WatchOutside())return false;
        ownerWasEnabled=IsWindowEnabled(owner)!=FALSE;
        if(ownerWasEnabled)EnableWindow(owner,FALSE);
        surface->Show(title);
        return true;
    }
    void Position() {
        const auto origin=layout.panel;
        auto relative=[&](CalendarRect r){r.x-=origin.x;r.y-=origin.y;return r;};
        todo_ui::Move(viewport,relative(layout.viewport),dpi);
        todo_ui::Move(form,{0,-scroll,layout.viewport.width,layout.contentHeight},dpi);
        auto inset=[](CalendarRect r){r.x+=9;r.y+=7;r.width-=18;r.height-=14;return r;};
        todo_ui::Move(title,inset(layout.title),dpi);
        todo_ui::Move(Get(Start),layout.start,dpi);todo_ui::Move(Get(End),layout.end,dpi);
        todo_ui::Move(Get(Lead),layout.reminder,dpi);todo_ui::Move(Get(Repeat),layout.repeat,dpi);
        todo_ui::Move(notes,inset(layout.notes),dpi);
        for(int i=0;i<6;++i)todo_ui::Move(Get(ColorFirst+i),{i*37.f,layout.colors.y+19,30,30},dpi);
        auto footer=relative(layout.footer);
        todo_ui::Move(Get(CancelButton),{footer.Right()-138,footer.y+2,65,30},dpi);
        todo_ui::Move(Get(SaveButton),{footer.Right()-65,footer.y+2,65,30},dpi);
        auto heading=relative(layout.heading);
        todo_ui::Move(Get(CloseButton),{heading.Right()-32,heading.y+6,32,32},dpi);
    }
    void Refresh() {
        if(!window)return;
        SetWindowTextW(Get(Start),DateText(draft->start).c_str());SetWindowTextW(Get(End),DateText(draft->end).c_str());
        auto lead=draft->reminder;
        std::wstring value=lead.kind==todo::ReminderKind::None?L"不提醒":
            lead.kind==todo::ReminderKind::Relative&&!lead.legacyRelative?
                std::to_wstring(lead.leadMilliseconds/60000)+L" 分钟前":L"保留原提醒";
        SetWindowTextW(Get(Lead),value.c_str());
        SetWindowTextW(Get(Repeat),TodoRepeatText(draft->recurrence).c_str());InvalidateRect(form,nullptr,TRUE);InvalidateRect(window,nullptr,FALSE);
    }
    void Scroll(float value) {
        scroll=std::clamp(value,0.f,layout.MaximumScroll());
        todo_ui::Move(form,{0,-scroll,layout.viewport.width,layout.contentHeight},dpi);
        InvalidateRect(viewport,nullptr,TRUE);
        surface->Refresh();
    }
    void Focus(HWND control) {
        if(picker.IsOpen()&&!IsChild(picker.Handle(),control))picker.Close(false);
        if(!IsChild(form,control))return;
        RECT r{};GetWindowRect(control,&r);
        POINT p{r.left,r.top};ScreenToClient(form,&p);
        const float scale=96.f/static_cast<float>(dpi);
        Scroll(layout.ScrollToReveal(scroll,{p.x*scale,p.y*scale,(r.right-r.left)*scale,(r.bottom-r.top)*scale}));
    }
    void Feedback(std::wstring message) {
        feedback=std::move(message);
        if(window)InvalidateRect(window,nullptr,FALSE);
    }
    void Submit() {
        if(busy||!window)return;
        draft->title=todo_ui::Utf8(todo_ui::Text(title));draft->notes=todo_ui::Utf8(todo_ui::Text(notes));
        todo::Command command;
        auto error=draft->Build(command);
        if(error!=todo::Error::None) {
            Feedback(error==todo::Error::InvalidTime?L"时间无效，结束时间必须晚于开始时间":
                error==todo::Error::InvalidReminder?L"请输入 0～525600 的整数提醒分钟数":
                L"标题不能为空且不超过500字；备注不超过10000字");
            return;
        }
        picker.Close(false);busy=true;
        for(auto w:tabs)EnableWindow(w,FALSE);
        const auto callback=save;
        auto keep=shared_from_this();
        todo::CommandResult result;
        try {if(callback)result=callback(command);} catch(...) {result.error=todo::Error::WriteFailed;}
        if(!window)return;
        busy=false;
        if(result.ok){Close(false);return;}
        for(auto w:tabs)EnableWindow(w,TRUE);
        Feedback(L"保存失败，请重试；填写内容已保留");SetFocus(Get(SaveButton));
    }
    void Pick(int id) {
        HWND trigger=Get(id);
        if(suppressTrigger==trigger&&GetTickCount64()<=suppressUntil){suppressTrigger=nullptr;return;}
        suppressTrigger=nullptr;
        if(picker.IsOpen()){
            const bool same=pickerTrigger==trigger;
            picker.Close(same);if(same)return;
        }
        pickerTrigger=trigger;
        const auto weak=weak_from_this();
        if(id==Repeat) {
            int chosen=-1;
            for(int i=0;i<4;++i)if(kRepeats[i]==draft->recurrence)chosen=i;
            picker.OpenChoice(instance,window,trigger,host,dpi,prefs,{L"不重复",L"每天",L"每周",L"每月"},chosen,
                [weak](int index) {
                    auto self=weak.lock();
                    if(!self||!self->window||index<0||index>3)return;
                    self->draft->SetRecurrence(kRepeats[index]);self->Refresh();
                });
            return;
        }
        if(id==Lead) {
            picker.OpenReminder(instance,window,trigger,host,dpi,prefs,draft->reminder,
                [weak](todo::ReminderDraft value) {
                    auto self=weak.lock();
                    if(!self||!self->window)return;
                    self->draft->SetReminder(value);self->Refresh();
                });
            return;
        }
        auto today=CurrentTime();
        if(!window)return;
        auto value=id==Start?draft->start:draft->end;
        if(!today)return;
        if(!value)value=today;
        picker.OpenDate(instance,window,trigger,host,dpi,prefs,*value,*today,
            [weak,id](todo::LocalDateTime selected) {
                auto self=weak.lock();
                if(!self||!self->window)return false;
                bool okay=true;
                if(id==Start)okay=self->draft->SetStart(selected);
                else {
                    self->draft->SetEnd(selected);
                    okay=todo::ResolveLocal(selected,*self->time).has_value();
                }
                self->Refresh();
                self->Feedback(okay?L"":L"该本地时间无效，请重新选择");
                return okay;
            },[weak](){auto self=weak.lock();return self&&self->window?self->CurrentTime():std::nullopt;});
    }
    std::optional<todo::LocalDateTime> CurrentTime() {
        const auto callback=nowProvider;
        todo::Millis instant=now;
        try {if(callback)instant=callback();}catch(...){return {};}
        if(!window)return {};
        const auto local=time->ToLocal(instant);
        return window?local:std::nullopt;
    }
    void Mouse(HWND source) {
        const int id=GetDlgCtrlID(source);
        if(!picker.IsOpen()||!Trigger(id))return;
        // WM_MOUSEACTIVATE precedes popup deactivation, including queued clicks
        // whose physical button is already up when they are dispatched.
        suppressTrigger=pickerTrigger==source?source:nullptr;
        picker.Close(false);
        // Material release can wait for a capture worker. Start the click fence
        // after that wait so the same queued click cannot immediately reopen it.
        suppressUntil=GetTickCount64()+500;
    }
    void Action(int id) {
        if(busy)return;
        if(id==SaveButton){Submit();return;}
        if(id==CloseButton||id==CancelButton){Close(true);return;}
        if(Trigger(id)){Pick(id);return;}
        if(id>=ColorFirst&&id<ColorFirst+6){draft->color=static_cast<todo::Color>(id-ColorFirst);Refresh();}
    }
    LRESULT Key(WPARAM key,HWND source) {
        if(key==VK_ESCAPE) {
            if(picker.IsOpen())picker.Close(true);
            else if(!busy)Close(true);
            return 1;
        }
        if(key==VK_TAB){todo_ui::Fence(tabs,source,GetKeyState(VK_SHIFT)<0);return 1;}
        if(key==VK_RETURN) {
            if(source==notes&&GetKeyState(VK_CONTROL)>=0)return 0;
            const auto id=GetDlgCtrlID(source);
            if(id==Title||source==notes)Submit();else Action(id);
            return 1;
        }
        return 0;
    }
    void Draw(HWND w,HDC dc,bool fields) {
        RECT client{};GetClientRect(w,&client);
        surface->Clear(dc,client);
        if(fields) {
            surface->Label(dc,L"日程内容",{0,0,layout.viewport.width,14},DT_LEFT,11);
            surface->Label(dc,L"开始时间",{0,58,72,40});
            surface->Label(dc,L"结束时间",{0,104,72,40});
            surface->Label(dc,L"提醒",{0,150,72,40});
            surface->Label(dc,L"重复",{0,196,72,40});
            const float colors=layout.colors.y;
            surface->Label(dc,L"颜色标注",{0,colors,100,14},DT_LEFT,11);
            for(int i=0;i<6;++i)surface->Label(dc,kColorNames[i],{i*37.f,colors+51,30,14},DT_CENTER,10);
            surface->Label(dc,L"备注",{0,layout.notes.y-19,layout.viewport.width,14},DT_LEFT,11);
            surface->EditFrame(dc,layout.title);surface->EditFrame(dc,layout.notes);
            for(int y:{97,143,189,235})surface->Separator(dc,static_cast<float>(y),layout.viewport.width);
        } else if(w==window) {
            surface->Background(dc,client);
            const float x=layout.heading.x-layout.panel.x,y=layout.heading.y-layout.panel.y;
            auto date=draft->start?draft->start->date:calendar::Date{};
            surface->Label(dc,std::to_wstring(date.year)+L"年"+std::to_wstring(date.month)+L"月"+
                std::to_wstring(date.day)+L"日",{x,y,layout.heading.width-40,16});
            surface->Label(dc,editing?L"编辑日程":L"新建日程",{x,y+20,layout.heading.width-40,24},DT_LEFT,18);
            surface->Label(dc,L"本机保存",{layout.footer.x-layout.panel.x,layout.footer.y-layout.panel.y,
                layout.footer.width-148,32},DT_LEFT,11);
            surface->Label(dc,feedback.empty()?L"Lume 运行时按设置提醒，日程仅存本机":feedback,
                {layout.feedback.x-layout.panel.x,layout.feedback.y-layout.panel.y,layout.feedback.width,14},DT_LEFT,11);
        }
    }
    void Paint(HWND w,bool fields) {
        PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);
        Draw(w,dc,fields);EndPaint(w,&ps);surface->Refresh();
    }
    static LRESULT CALLBACK FormProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto *raw=reinterpret_cast<Impl*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE) {
            raw=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(raw));
        }
        if(!raw)return DefWindowProcW(w,m,wp,lp);
        auto self=raw->shared_from_this();
        switch(m) {
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:self->Paint(w,w==self->form);return 0;
        case WM_PRINTCLIENT:self->Draw(w,reinterpret_cast<HDC>(wp),w==self->form);return 0;
        case WM_COMMAND:case WM_DRAWITEM:case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:
            return SendMessageW(self->window,m,wp,lp);
        case WM_MOUSEWHEEL:self->Scroll(self->scroll-GET_WHEEL_DELTA_WPARAM(wp)/120.f*48);return 0;
        case WM_LBUTTONDOWN:if(self->picker.IsOpen())self->picker.Close(false);return 0;
        default:return DefWindowProcW(w,m,wp,lp);
        }
    }
    static LRESULT CALLBACK Proc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto *raw=reinterpret_cast<Impl*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE) {
            raw=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            raw->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(raw));
        }
        if(!raw)return DefWindowProcW(w,m,wp,lp);
        auto self=raw->shared_from_this();
        switch(m) {
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:self->Paint(w,false);return 0;
        case WM_PRINTCLIENT:self->Draw(w,reinterpret_cast<HDC>(wp),false);return 0;
        case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:return self->surface->Color(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp));
        case WM_DRAWITEM: {
            const auto &item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            const int id=static_cast<int>(item.CtlID),color=id-ColorFirst;
            using A=todo_ui::Surface::Appearance;
            self->surface->Button(item,color>=0&&color<6&&static_cast<int>(self->draft->color)==color,
                color>=0&&color<6?kColors[color]:CLR_INVALID,
                color>=0&&color<6?A::Swatch:id==SaveButton?A::Filled:Trigger(id)?A::Schedule:A::Plain,
                false,false,id==CloseButton?24:13);
            return TRUE;
        }
        case WM_COMMAND:
            if(HIWORD(wp)==BN_CLICKED&&!self->pendingAction&&lp&&
               reinterpret_cast<HWND>(lp)==self->Get(LOWORD(wp))) {
                self->pendingAction=LOWORD(wp);self->pendingSource=reinterpret_cast<HWND>(lp);
                PostMessageW(w,todo_ui::ActionMessage,wp,lp);
            }
            return 0;
        case todo_ui::ActionMessage: {
            const int id=self->pendingAction;const auto source=self->pendingSource;
            self->pendingAction=0;self->pendingSource=nullptr;
            if(id&&id==LOWORD(wp)&&source==reinterpret_cast<HWND>(lp)&&source==self->Get(id))self->Action(id);
            return 0;
        }
        case todo_ui::MouseMessage:self->Mouse(reinterpret_cast<HWND>(lp));return 0;
        case todo_ui::KeyMessage:return self->Key(wp,reinterpret_cast<HWND>(lp));
        case todo_ui::FocusMessage:self->Focus(reinterpret_cast<HWND>(lp));return 0;
        case WM_MOUSEWHEEL:self->Scroll(self->scroll-GET_WHEEL_DELTA_WPARAM(wp)/120.f*48);return 0;
        case WM_LBUTTONDOWN:if(self->picker.IsOpen())self->picker.Close(false);return 0;
        case WM_ACTIVATEAPP:if(!wp)self->picker.Close(false);return 0;
        case kOutsideClick:if(!self->busy)self->Close(true);return 0;
        case WM_CLOSE:if(!self->busy)self->Close(true);return 0;
        case WM_NCDESTROY:
            self->window=nullptr;SetWindowLongPtrW(w,GWLP_USERDATA,0);self->Close(false);
            return DefWindowProcW(w,m,wp,lp);
        default:return DefWindowProcW(w,m,wp,lp);
        }
    }
};
CalendarTodoEditor::CalendarTodoEditor()=default;
CalendarTodoEditor::~CalendarTodoEditor(){if(impl_)impl_->closed={};Close(false);}
bool CalendarTodoEditor::Open(HINSTANCE instance,HWND owner,RECT host,unsigned dpi,
    const calendar::Preferences &prefs,const todo::ILocalTimePort &time,todo::Command seed,
    todo::Millis now,std::optional<todo::TaskView> existing,Save save,Closed closed,Now nowProvider) {
    if(IsOpen())return false;
    if(!IsWindow(owner)||dpi==0||host.right<=host.left||host.bottom<=host.top||!save)return false;
    auto state=std::make_shared<Impl>();
    impl_=state;state->instance=instance;state->owner=owner;state->host=host;
    state->dpi=dpi;state->prefs=prefs;state->time=&time;state->now=now;
    state->editing=seed.type==todo::CommandType::Update;
    state->draft=std::make_unique<CalendarTodoDraft>(time,std::move(seed),now,existing);
    state->save=std::move(save);state->closed=std::move(closed);
    state->nowProvider=std::move(nowProvider);
    if(state->Open())return true;
    state->closed={};state->Close(false);
    return false;
}
void CalendarTodoEditor::Close(bool restore){auto state=std::move(impl_);if(state)state->Close(restore);}
HWND CalendarTodoEditor::Handle() const noexcept{return impl_?impl_->window:nullptr;}
bool CalendarTodoEditor::IsOpen() const noexcept{return Handle()!=nullptr;}
}
