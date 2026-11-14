#include "platform/windows/calendar/todo_sheet_window.h"
#include "core/todo/todo_sheet.h"
#include "core/todo/todo_time.h"
#include "core/todo/todo_model.h"
#include "platform/windows/calendar/calendar_todo_ui.h"
#include "platform/windows/calendar/calendar_todo_picker.h"
#include "platform/windows/rounded_window_surface.h"
#include "platform/windows/widget_ui/ui_font.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/widget_ui/window_layer_policy.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>
#include <cmath>
#include <utility>
#include <map>

namespace lume::platform::windows {
namespace {
using Json = nlohmann::json;
constexpr wchar_t kClass[] = L"LumeTodoSheetWindow", kBodyClass[] = L"LumeTodoSheetBody";
constexpr int kWidth = 560, kHeight = 820;
constexpr UINT_PTR kFloatTimer = 1;
constexpr COLORREF kPaper=RGB(28,28,32),kInput=RGB(38,38,43),kBorder=RGB(51,51,56),
    kText=RGB(241,241,242),kDim=RGB(155,155,157),kFaint=RGB(101,101,103),kAccent=RGB(91,123,234),kFooter=RGB(25,25,28);
enum Id { Title=101, Notes=105, SaveButton=106, CancelButton, CloseButton,
    Pending=110, Complete, AllDay, Timed, Date, Days, LessDays, MoreDays,
    Reminder, Repeat, More, StepInput, AddStep, DeleteButton, StartTime, EndTime,
    Today, Tomorrow, Weekend, Monday, ColorFirst=140, StepFirst=1000 };
constexpr std::array<const char*,6> kColorNames{"none","red","orange","yellow","green","blue"};
constexpr COLORREF kColors[]{CLR_INVALID,RGB(255,105,97),RGB(255,163,72),RGB(245,214,92),RGB(107,216,155),RGB(101,180,255)};
constexpr std::array<const char*,5> kRepeats{"none","daily","weekdays","weekly","monthly"};
constexpr std::array<int,9> kReminders{-1,0,5,10,15,30,60,1440,-2};
std::atomic<bool> english{false};
std::atomic<unsigned long long> nextStep{0};
std::wstring Tr(bool en,const wchar_t* zh,const wchar_t* translated) { return en?translated:zh; }
SYSTEMTIME NativeDate(calendar::Date date,int hour=0,int minute=0) {
    SYSTEMTIME value{};value.wYear=static_cast<WORD>(date.year);value.wMonth=static_cast<WORD>(date.month);
    value.wDay=static_cast<WORD>(date.day);value.wHour=static_cast<WORD>(hour);value.wMinute=static_cast<WORD>(minute);return value;
}
calendar::Date ParseDate(const std::string& text) {
    calendar::Date date{};if(std::sscanf(text.c_str(),"%d-%d-%d",&date.year,&date.month,&date.day)!=3)return {};
    return calendar::IsValidDate(date)?date:calendar::Date{};
}
std::string Clock(SYSTEMTIME value) {char text[8]{};std::snprintf(text,sizeof(text),"%02u:%02u",value.wHour,value.wMinute);return text;}
RECT Pixels(CalendarRect r,unsigned dpi){return {todo_ui::Px(r.x,dpi),todo_ui::Px(r.y,dpi),todo_ui::Px(r.Right(),dpi),todo_ui::Px(r.Bottom(),dpi)};}
void Round(HDC dc,CalendarRect rect,unsigned dpi,float radius,COLORREF fill,COLORREF border=CLR_INVALID){
    const float scale=dpi/96.f,x=rect.x*scale+.5f,y=rect.y*scale+.5f,w=rect.width*scale-1,h=rect.height*scale-1;
    const float diameter=std::min({radius*2*scale,w,h});if(diameter<=0)return;
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::GraphicsPath path;
    path.AddArc(x,y,diameter,diameter,180,90);path.AddArc(x+w-diameter,y,diameter,diameter,270,90);
    path.AddArc(x+w-diameter,y+h-diameter,diameter,diameter,0,90);path.AddArc(x,y+h-diameter,diameter,diameter,90,90);path.CloseFigure();
    if(fill!=CLR_INVALID){Gdiplus::SolidBrush brush(Gdiplus::Color(255,GetRValue(fill),GetGValue(fill),GetBValue(fill)));graphics.FillPath(&brush,&path);}
    if(border!=CLR_INVALID){Gdiplus::Pen pen(Gdiplus::Color(255,GetRValue(border),GetGValue(border),GetBValue(border)),scale);graphics.DrawPath(&pen,&path);}
}
void Text(HDC dc,const std::wstring& text,CalendarRect rect,unsigned dpi,float size,COLORREF color=kText,UINT flags=DT_LEFT,int weight=FW_NORMAL){
    direct_write::Style style;style.size=size*dpi/96.f;style.weight=weight;
    style.flags=flags|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX;direct_write::Draw(dc,text,Pixels(rect,dpi),color,style);
}
void Icon(HDC dc,int kind,CalendarRect rect,unsigned dpi,COLORREF color=kDim){
    Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.TranslateTransform(rect.x*dpi/96.f,rect.y*dpi/96.f);g.ScaleTransform(rect.width*dpi/1536.f,rect.height*dpi/1536.f);
    Gdiplus::Pen p(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)),1.3f);p.SetStartCap(Gdiplus::LineCapRound);p.SetEndCap(Gdiplus::LineCapRound);
    if(kind==CloseButton){g.DrawLine(&p,4.f,4.f,12.f,12.f);g.DrawLine(&p,12.f,4.f,4.f,12.f);}
    else if(kind==Date){g.DrawRectangle(&p,3.f,4.f,10.f,10.f);g.DrawLine(&p,3.f,7.f,13.f,7.f);g.DrawLine(&p,5.f,2.f,5.f,5.f);g.DrawLine(&p,11.f,2.f,11.f,5.f);}
    else if(kind==AllDay){g.DrawEllipse(&p,5.f,5.f,6.f,6.f);for(int i=0;i<8;++i){const float a=i*3.14159265f/4;g.DrawLine(&p,8+5*std::cos(a),8+5*std::sin(a),8+7*std::cos(a),8+7*std::sin(a));}}
    else if(kind==Repeat){g.DrawArc(&p,3.f,3.f,10.f,10.f,205.f,135.f);g.DrawArc(&p,3.f,3.f,10.f,10.f,25.f,135.f);g.DrawLine(&p,12.f,2.f,13.f,6.f);g.DrawLine(&p,9.f,5.f,13.f,6.f);g.DrawLine(&p,3.f,10.f,4.f,14.f);g.DrawLine(&p,3.f,10.f,7.f,11.f);}
    else if(kind==Reminder){g.DrawArc(&p,4.f,3.f,8.f,9.f,180.f,180.f);g.DrawLine(&p,4.f,7.f,3.f,12.f);g.DrawLine(&p,12.f,7.f,13.f,12.f);g.DrawLine(&p,3.f,12.f,13.f,12.f);g.DrawArc(&p,6.f,12.f,4.f,3.f,0.f,180.f);}
    else if(kind==More){g.DrawEllipse(&p,4.f,4.f,8.f,8.f);g.DrawEllipse(&p,7.f,7.f,2.f,2.f);for(int i=0;i<8;++i){const float a=i*3.14159265f/4;g.DrawLine(&p,8+5*std::cos(a),8+5*std::sin(a),8+6*std::cos(a),8+6*std::sin(a));}}
    else {g.DrawEllipse(&p,2.5f,2.5f,11.f,11.f);g.DrawLine(&p,8.f,5.f,8.f,8.f);g.DrawLine(&p,8.f,8.f,10.5f,9.5f);}
}
}

// Desktop entrypoints share the same native sheet and the existing command
// parser/repository. Only the manager's already-open board uses the HTML sheet.
struct TodoSheetWindow::Impl : std::enable_shared_from_this<Impl> {
    struct Step {std::string id;bool completed=false;HWND check=nullptr,title=nullptr,remove=nullptr;};
    struct Label {std::wstring text;CalendarRect bounds;};
    HWND window=nullptr,owner=nullptr,returnFocus=nullptr,viewport=nullptr,form=nullptr;
    HINSTANCE instance=nullptr;
    unsigned dpi=96;
    float width=560,height=820,scroll=0,contentHeight=0,viewportHeight=0;
    bool en=false,ownerWasEnabled=false,closing=false,busy=false,more=false,initializing=true,creationFailed=false;
    const todo::ILocalTimePort* time=nullptr;
    todo::Command seed;
    std::optional<todo::TaskView> existing;
    todo::Millis openedAt=0;
    Save save;
    Closed closed;
    Now nowProvider;
    Json draft,original;
    std::wstring feedback;
    std::unique_ptr<todo_ui::Surface> surface;
    CalendarTodoPicker picker;
    std::map<int,SYSTEMTIME> dates;
    std::map<int,std::vector<std::wstring>> choices;
    std::map<int,int> selections;
    std::vector<Step> steps;
    std::vector<HWND> tabs;
    std::vector<Label> labels;
    std::vector<CalendarRect> frames;
    CalendarRect scheduleFrame{},statusFrame{},durationFrame{},hintFrame{};
    float moreLine=0;
    HFONT titleFont=nullptr;
    ~Impl(){if(titleFont)DeleteObject(titleFont);}

    std::wstring T(const wchar_t* zh,const wchar_t* translated)const{return Tr(en,zh,translated);}
    HWND Get(int id)const {return GetDlgItem(id==SaveButton||id==CancelButton||id==CloseButton||id==DeleteButton?window:form,id);}
    todo::Millis Current()const{return nowProvider?nowProvider():openedAt;}
    calendar::Date TodayDate()const{const auto local=time->ToLocal(Current());return local?local->date:calendar::Date{};}
    void MakeDraft() {
        const auto today=TodayDate();auto day=today;
        if(seed.patch.schedule)if(const auto start=time->ToLocal(seed.patch.schedule->startAt))day=start->date;
        draft=existing?todo::SheetRow(*existing,*time):Json{{"id",""},{"title",""},{"notes",""},
            {"completed",false},{"color","none"},{"date",todo::SheetDate(day)},{"allDay",true},{"days",1},
            {"time","09:00"},{"end","10:00"},{"reminder",-1},{"frequency","none"},{"subtasks",Json::array()}};
        if(draft["date"]=="")draft["date"]=todo::SheetDate(day);
        if(draft["time"]=="")draft["time"]="09:00";
        if(draft["end"]=="")draft["end"]="10:00";
        original=draft;more=!draft["notes"].get<std::string>().empty()||!draft["subtasks"].empty();
    }
    HWND Add(int id,const wchar_t* kind,const std::wstring& text,DWORD style=0) {
        const auto control=surface->Add(id==SaveButton||id==CancelButton||id==CloseButton||id==DeleteButton?window:form,
            window,kind,text.c_str(),id,style);
        if(control)tabs.push_back(control);else creationFailed=true;
        return control;
    }
    HWND Button(int id,const std::wstring& text){return Add(id,L"BUTTON",text,BS_OWNERDRAW);}
    void SelectorText(int id) {
        if(dates.count(id)){const auto value=dates[id];wchar_t text[64]{};
            if(id==Date)swprintf_s(text,L"%04u/%02u/%02u",value.wYear,value.wMonth,value.wDay);
            else swprintf_s(text,L"%02u:%02u",value.wHour,value.wMinute);
            SetWindowTextW(Get(id),text);
        }else if(choices.count(id)&&selections[id]>=0&&static_cast<std::size_t>(selections[id])<choices[id].size())
            SetWindowTextW(Get(id),choices[id][static_cast<std::size_t>(selections[id])].c_str());
    }
    static LRESULT CALLBACK SelectorProc(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR subclass,DWORD_PTR data) {
        auto& self=*reinterpret_cast<Impl*>(data);const auto id=GetDlgCtrlID(control);
        if(message==DTM_GETSYSTEMTIME&&self.dates.count(id)){if(lp)*reinterpret_cast<SYSTEMTIME*>(lp)=self.dates[id];return GDT_VALID;}
        if(message==DTM_SETSYSTEMTIME&&self.dates.count(id)&&wp==GDT_VALID&&lp){self.dates[id]=*reinterpret_cast<SYSTEMTIME*>(lp);self.SelectorText(id);return TRUE;}
        if(message==CB_GETCURSEL&&self.choices.count(id))return self.selections[id];
        if(message==CB_SETCURSEL&&self.choices.count(id)){if(wp>=self.choices[id].size())return CB_ERR;self.selections[id]=static_cast<int>(wp);self.SelectorText(id);return self.selections[id];}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(control,SelectorProc,subclass);
        return DefSubclassProc(control,message,wp,lp);
    }
    void DateControl(int id,bool clock) {
        Button(id,L"");SetWindowSubclass(Get(id),SelectorProc,2,reinterpret_cast<DWORD_PTR>(this));
        const auto day=ParseDate(draft["date"].get<std::string>());
        int hour=0,minute=0;
        if(clock){const auto text=draft[id==StartTime?"time":"end"].get<std::string>();std::sscanf(text.c_str(),"%d:%d",&hour,&minute);}
        dates[id]=NativeDate(day,hour,minute);SelectorText(id);
    }
    void AddStepRow(const Json& item) {
        const auto index=static_cast<int>(steps.size());const int id=StepFirst+index*3;
        Step step;step.id=item["id"].get<std::string>();step.completed=item.value("completed",false);
        step.check=Button(id,step.completed?L"✓":L"○");
        step.title=Add(id+1,L"EDIT",todo_ui::Wide(item["title"].get<std::string>()),ES_AUTOHSCROLL);
        SendMessageW(step.title,EM_SETLIMITTEXT,500,0);
        step.remove=Button(id+2,L"×");steps.push_back(std::move(step));
    }
    bool CreateControls() {
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_DATE_CLASSES};if(!InitCommonControlsEx(&controls))return false;
        calendar::Preferences appearance;appearance.textColor.rgb=0xffffff;
        surface=std::make_unique<todo_ui::Surface>(dpi,appearance);
        surface->foreground=kText;surface->accent=kAccent;
        DeleteObject(surface->background);surface->background=CreateSolidBrush(kPaper);
        DeleteObject(surface->editBackground);surface->editBackground=CreateSolidBrush(kInput);
        viewport=CreateWindowExW(WS_EX_CONTROLPARENT,kBodyClass,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
            0,0,1,1,window,nullptr,instance,this);
        form=CreateWindowExW(WS_EX_CONTROLPARENT,kBodyClass,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
            0,0,1,1,viewport,nullptr,instance,this);
        if(!viewport||!form)return false;
        Add(Title,L"EDIT",todo_ui::Wide(draft["title"].get<std::string>()),ES_AUTOHSCROLL);
        SendMessageW(Get(Title),EM_SETLIMITTEXT,500,0);
        SendMessageW(Get(Title),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(en?L"Task name":L"输入任务名称"));
        titleFont=CreateFontW(-todo_ui::Px(16,dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,ui_font::Family(L"待办"));
        SendMessageW(Get(Title),WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
        Button(Pending,T(L"待办",L"To do"));Button(Complete,T(L"已完成",L"Done"));
        for(int i=0;i<6;++i)Button(ColorFirst+i,T(L"颜色标签",L"Colour"));
        Button(AllDay,T(L"全天",L"All day"));Button(Timed,T(L"指定时间",L"At a time"));
        DateControl(Date,false);DateControl(StartTime,true);DateControl(EndTime,true);
        Add(Days,L"EDIT",std::to_wstring(draft["days"].get<int>()),ES_NUMBER|ES_CENTER);SendMessageW(Get(Days),EM_SETLIMITTEXT,3,0);
        Button(LessDays,L"−");Button(MoreDays,L"+");
        Button(Today,T(L"今天",L"Today"));Button(Tomorrow,T(L"明天",L"Tomorrow"));
        Button(Weekend,T(L"本周末",L"Weekend"));Button(Monday,T(L"下周一",L"Monday"));
        Button(Reminder,L"");SetWindowSubclass(Get(Reminder),SelectorProc,2,reinterpret_cast<DWORD_PTR>(this));
        const wchar_t* remindersZh[]{L"不提醒",L"开始时",L"提前 5 分钟",L"提前 10 分钟",L"提前 15 分钟",L"提前 30 分钟",L"提前 1 小时",L"提前 1 天",L"保留原提醒"};
        const wchar_t* remindersEn[]{L"None",L"At start",L"5 minutes before",L"10 minutes before",L"15 minutes before",L"30 minutes before",L"1 hour before",L"1 day before",L"Keep reminder"};
        const auto reminder=draft["reminder"].get<int>();
        selections[Reminder]=-1;
        for(int i=0;i<9;++i){choices[Reminder].push_back(en?remindersEn[i]:remindersZh[i]);if(kReminders[i]==reminder)selections[Reminder]=i;}
        if(selections[Reminder]<0){
            const auto text=en?std::to_wstring(reminder)+L" minutes before":L"提前 "+std::to_wstring(reminder)+L" 分钟";
            choices[Reminder].push_back(text);selections[Reminder]=9;
        }
        Button(Repeat,L"");SetWindowSubclass(Get(Repeat),SelectorProc,2,reinterpret_cast<DWORD_PTR>(this));
        const wchar_t* repeatZh[]{L"不重复",L"每天",L"工作日",L"每周",L"每月"};
        const wchar_t* repeatEn[]{L"Never",L"Every day",L"Weekdays",L"Every week",L"Every month"};
        for(int i=0;i<5;++i){choices[Repeat].push_back(en?repeatEn[i]:repeatZh[i]);if(draft["frequency"]==kRepeats[i])selections[Repeat]=i;}
        SelectorText(Reminder);SelectorText(Repeat);
        Button(More,T(L"更多选项 · 子任务与备注",L"More options · sub-steps and notes"));
        for(const auto& step:draft["subtasks"])AddStepRow(step);
        Add(StepInput,L"EDIT",L"",ES_AUTOHSCROLL);SendMessageW(Get(StepInput),EM_SETLIMITTEXT,500,0);
        SendMessageW(Get(StepInput),EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(en?L"Add a sub-step, then press Enter":L"添加子任务，按回车确认"));
        Button(AddStep,T(L"添加",L"Add"));
        Add(Notes,L"EDIT",todo_ui::Wide(draft["notes"].get<std::string>()),ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL);
        SendMessageW(Get(Notes),EM_SETLIMITTEXT,10000,0);
        Button(SaveButton,existing?T(L"保存",L"Save"):T(L"添加任务",L"Add task"));Button(CancelButton,T(L"取消",L"Cancel"));
        Button(CloseButton,L"×");Button(DeleteButton,T(L"删除",L"Delete"));
        return !creationFailed&&std::all_of(tabs.begin(),tabs.end(),[](HWND control){return IsWindow(control)!=FALSE;});
    }
    void Move(HWND control,CalendarRect rect,bool visible=true) {
        ShowWindow(control,visible?SW_SHOWNA:SW_HIDE);todo_ui::Move(control,rect,dpi);
    }
    void LabelAt(const std::wstring& text,float y,float x=0,float length=0) {
        labels.push_back({text,{x,y,length>0?length:width-48,16}});
    }
    void Layout() {
        if(!form||!surface)return;
        RECT client{};GetClientRect(window,&client);width=client.right*96.f/dpi;height=client.bottom*96.f/dpi;
        const float inner=std::max(160.f,width-48),half=(inner-14)/2;
        viewportHeight=std::max(40.f,height-196);labels.clear();frames.clear();
        Move(viewport,{24,103,inner,viewportHeight});
        float y=0;
        const auto edit=[&](int id,CalendarRect rect,bool visible=true){const float inset=id==Title?16.f:12.f;
            const float vertical=id==Title?10.f:8.f;Move(Get(id),{rect.x+inset,rect.y+vertical,rect.width-2*inset,rect.height-2*vertical},visible);if(visible)frames.push_back(rect);};
        edit(Title,{0,y,inner,52});y+=68;
        const float colorLeft=existing?half+14:0;
        if(existing){LabelAt(T(L"状态",L"Status"),y,0,half);statusFrame={0,y+24,half,42};
            Move(Get(Pending),{4,y+28,(half-8)/2,34});Move(Get(Complete),{half/2,y+28,(half-8)/2,34});}
        else {ShowWindow(Get(Pending),SW_HIDE);ShowWindow(Get(Complete),SW_HIDE);}
        LabelAt(T(L"颜色标签",L"Colour"),y,colorLeft,half);y+=24;
        for(int i=0;i<6;++i)Move(Get(ColorFirst+i),{colorLeft+i*36.f,y,36,42});y+=58;
        LabelAt(T(L"排期",L"Schedule"),y);y+=24;
        scheduleFrame={0,y,inner,42};Move(Get(AllDay),{4,y+4,(inner-8)/2,34});Move(Get(Timed),{inner/2,y+4,(inner-8)/2,34});y+=58;
        LabelAt(T(L"日期",L"Date"),y,0,half);LabelAt(T(L"持续天数",L"Duration"),y,half+14,half);y+=24;
        Move(Get(Date),{0,y,half,42});durationFrame={half+14,y,half,42};Move(Get(LessDays),{half+16,y+4,34,34});
        Move(Get(Days),{half+54,y+8,std::max(30.f,half-116),26});Move(Get(MoreDays),{inner-36,y+4,34,34});y+=54;
        float quickX=0;for(int i=0;i<4;++i){const float quickWidth=en?(i>1?100.f:68.f):(i>1?66.f:54.f);Move(Get(Today+i),{quickX,y,quickWidth,30});quickX+=quickWidth+8;}y+=42;
        const bool timed=!draft["allDay"].get<bool>();
        if(timed){LabelAt(T(L"开始",L"Starts"),y,0,half);LabelAt(T(L"结束",L"Ends"),y,half+14,half);y+=24;}
        Move(Get(StartTime),{0,y,half,42},timed);Move(Get(EndTime),{half+14,y,half,42},timed);if(timed)y+=58;
        hintFrame={0,y,inner,34};y+=50;
        LabelAt(T(L"提醒",L"Reminder"),y,0,half);LabelAt(T(L"重复",L"Repeat"),y,half+14,half);y+=24;
        Move(Get(Reminder),{0,y,half,42});Move(Get(Repeat),{half+14,y,half,42});y+=58;
        moreLine=y;Move(Get(More),{-8,y+15,inner+16,36});y+=65;
        if(more){LabelAt(T(L"子任务",L"Sub-steps"),y);y+=24;}
        for(auto& step:steps){Move(step.check,{0,y,28,34},more);edit(GetDlgCtrlID(step.title),{36,y,inner-72,34},more);Move(step.remove,{inner-28,y,28,34},more);if(more)y+=42;}
        edit(StepInput,{0,y,inner-76,42},more);Move(Get(AddStep),{inner-68,y,68,42},more);
        if(more){y+=58;LabelAt(T(L"备注",L"Notes"),y);y+=24;}
        edit(Notes,{0,y,inner,100},more);if(more)y+=116;
        contentHeight=y;scroll=std::clamp(scroll,0.f,std::max(0.f,contentHeight-viewportHeight));
        todo_ui::Move(form,{0,-scroll,inner,contentHeight},dpi);
        Move(Get(CloseButton),{width-52,22,28,28});
        Move(Get(DeleteButton),{24,height-58,64,40},existing.has_value());
        const float saveWidth=existing?118.f:144.f;
        Move(Get(CancelButton),{width-94-saveWidth,height-58,62,40});Move(Get(SaveButton),{width-24-saveWidth,height-58,saveWidth,40});
        InvalidateRect(window,nullptr,FALSE);InvalidateRect(form,nullptr,FALSE);UpdateSave();
    }
    void Scroll(float value) {
        scroll=std::clamp(value,0.f,std::max(0.f,contentHeight-viewportHeight));
        todo_ui::Move(form,{0,-scroll,width-48,contentHeight},dpi);InvalidateRect(viewport,nullptr,FALSE);surface->Refresh();
    }
    void Focus(HWND control) {
        if(!IsChild(form,control))return;
        RECT rect{};GetWindowRect(control,&rect);MapWindowPoints(nullptr,form,reinterpret_cast<POINT*>(&rect),2);
        const float top=rect.top*96.f/dpi,bottom=rect.bottom*96.f/dpi;
        if(top<scroll)Scroll(top);else if(bottom>scroll+viewportHeight)Scroll(bottom-viewportHeight);
    }
    void UpdateSave(){if(Get(SaveButton))EnableWindow(Get(SaveButton),!busy&&!todo::Trim(todo_ui::Utf8(todo_ui::Text(Get(Title)))).empty());}
    Json Fields() {
        Json fields=draft;fields["title"]=todo_ui::Utf8(todo_ui::Text(Get(Title)));fields["notes"]=todo_ui::Utf8(todo_ui::Text(Get(Notes)));
        int days=0;if(!todo_ui::Number(Get(Days),1,366,days))return {};
        fields["days"]=days;SYSTEMTIME date{},start{},end{};
        if(SendMessageW(Get(Date),DTM_GETSYSTEMTIME,0,reinterpret_cast<LPARAM>(&date))!=GDT_VALID)return {};
        fields["date"]=todo::SheetDate({date.wYear,date.wMonth,date.wDay});
        SendMessageW(Get(StartTime),DTM_GETSYSTEMTIME,0,reinterpret_cast<LPARAM>(&start));
        SendMessageW(Get(EndTime),DTM_GETSYSTEMTIME,0,reinterpret_cast<LPARAM>(&end));
        fields["time"]=Clock(start);fields["end"]=Clock(end);
        const auto reminder=SendMessageW(Get(Reminder),CB_GETCURSEL,0,0),repeat=SendMessageW(Get(Repeat),CB_GETCURSEL,0,0);
        if(reminder>=0&&reminder<9)fields["reminder"]=kReminders[static_cast<std::size_t>(reminder)];
        if(repeat>=0&&repeat<5)fields["frequency"]=kRepeats[static_cast<std::size_t>(repeat)];
        fields["subtasks"]=Json::array();
        for(const auto& step:steps)fields["subtasks"].push_back({{"id",step.id},{"title",todo_ui::Utf8(todo_ui::Text(step.title))},{"completed",step.completed}});
        return fields;
    }
    void Result(const std::string& error) {
        busy=false;feedback=error=="invalid-time"?T(L"请检查日期和时间，结束时间需晚于开始。",L"Check the dates and times."):
            error=="limit"?T(L"待办或子任务数量已达上限。",L"The task or sub-step limit was reached."):
            error=="invalid-task"||error=="invalid"?T(L"请填写任务名称和有效的子任务。",L"Enter a task name and valid sub-steps."):
            T(L"待办暂时无法保存，请稍后再试。",L"The task could not be saved. Try again.");
        UpdateSave();if(window)InvalidateRect(window,nullptr,FALSE);
    }
    void SaveCommands(const Json& commands) {
        if(busy||!save||!commands.is_array()||commands.empty()||commands.size()>4)return Result("invalid");
        busy=true;
        const auto operation=save;
        for(const auto& item:commands){
            if(!item.is_object()||!item.contains("action")||!item["action"].is_string()||!item.contains("fields")||!item["fields"].is_object())return Result("invalid");
            const auto action=item["action"].get<std::string>();const auto& fields=item["fields"];
            todo::Command command;command.scope=seed.scope;
            if(action=="add"){
                if(existing)return Result("invalid");command.type=todo::CommandType::Add;
                const auto error=todo::ReadSheetPatch(fields,nullptr,true,*time,Current(),command.patch);if(!error.empty())return Result(error);
            }else{
                if(!existing||fields.value("id",std::string{})!=existing->id)return Result("invalid");command.id=existing->id;
                if(action=="update"){command.type=todo::CommandType::Update;const auto error=todo::ReadSheetPatch(fields,&*existing,false,*time,Current(),command.patch);if(!error.empty())return Result(error);}
                else if(action=="complete")command.type=todo::CommandType::Complete;
                else if(action=="restore")command.type=todo::CommandType::Restore;
                else return Result("invalid");
            }
            const auto result=operation(command);if(closing||!window)return;
            if(!result.ok)return Result(todo::SheetErrorName(result.error));
        }
        busy=false;Close(true);
    }
    void Submit() {
        if(busy||!window)return;
        if(!todo::Trim(todo_ui::Utf8(todo_ui::Text(Get(StepInput)))).empty())NewStep();
        const auto fields=Fields();if(!fields.is_object())return Result("invalid-time");
        Json patch=Json::object();
        for(const auto* key:{"title","notes","color","frequency","subtasks"})
            if(!existing||fields[key]!=original[key])patch[key]=fields[key];
        if((!existing||fields["reminder"]!=original["reminder"])&&fields["reminder"]!=-2)patch["reminder"]=fields["reminder"];
        bool moved=!existing;
        for(const auto* key:{"date","allDay","days"})moved=moved||fields[key]!=original[key];
        if(!fields["allDay"].get<bool>())for(const auto* key:{"time","end"})moved=moved||fields[key]!=original[key];
        if(moved){for(const auto* key:{"date","allDay","days"})patch[key]=fields[key];
            if(!fields["allDay"].get<bool>())for(const auto* key:{"time","end"})patch[key]=fields[key];}
        Json commands=Json::array();
        if(!existing)commands.push_back({{"action","add"},{"fields",std::move(patch)}});
        else{
            if(!patch.empty()){patch["id"]=existing->id;commands.push_back({{"action","update"},{"fields",std::move(patch)}});}
            if(fields["completed"]!=original["completed"])commands.push_back({{"action",fields["completed"].get<bool>()?"complete":"restore"},{"fields",{{"id",existing->id}}}});
        }
        if(commands.empty())Close(true);else SaveCommands(commands);
    }
    void Delete() {
        if(busy||!save||!existing)return;
        todo::Command command;command.scope=seed.scope;command.id=existing->id;command.type=todo::CommandType::Delete;
        busy=true;const auto operation=save;const auto result=operation(command);if(closing||!window)return;
        if(!result.ok)return Result(todo::SheetErrorName(result.error));busy=false;Close(true);
    }
    void NewStep() {
        const auto title=todo::Trim(todo_ui::Utf8(todo_ui::Text(Get(StepInput))));if(title.empty())return;
        if(steps.size()>=100)return Result("limit");
        AddStepRow({{"id","s"+std::to_string(GetTickCount64())+"-"+std::to_string(++nextStep)},{"title",title},{"completed",false}});
        SetWindowTextW(Get(StepInput),L"");Layout();Focus(Get(StepInput));SetFocus(Get(StepInput));
    }
    void Action(int id) {
        if(busy||closing)return;
        if(id==SaveButton)return Submit();if(id==DeleteButton)return Delete();
        if(id==CancelButton||id==CloseButton)return Close(true);
        if(dates.count(id)||choices.count(id)){
            if(picker.IsOpen()){picker.Close(false);return;}
            MONITORINFO monitor{sizeof(monitor)};if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))return;
            const auto weak=weak_from_this();calendar::Preferences appearance;appearance.textColor.rgb=0xffffff;appearance.material=calendar::Material::FrostedGlass;
            if(choices.count(id))picker.OpenChoice(instance,window,Get(id),monitor.rcWork,dpi,appearance,choices[id],selections[id],[weak,id](int choice){if(const auto self=weak.lock();self&&!self->closing){self->selections[id]=choice;self->SelectorText(id);}});
            else{const auto value=dates[id];const auto now=time->ToLocal(Current());if(!now)return;
                picker.OpenDate(instance,window,Get(id),monitor.rcWork,dpi,appearance,{{value.wYear,value.wMonth,value.wDay},value.wHour,value.wMinute},*now,
                    [weak,id](todo::LocalDateTime selected){if(const auto self=weak.lock();self&&!self->closing){self->dates[id]=NativeDate(selected.date,selected.hour,selected.minute);self->SelectorText(id);return true;}return false;});}
            return;
        }
        if(id==Pending||id==Complete)draft["completed"]=id==Complete;
        else if(id==AllDay||id==Timed){draft["allDay"]=id==AllDay;Layout();}
        else if(id>=ColorFirst&&id<ColorFirst+6){draft["color"]=kColorNames[static_cast<std::size_t>(id-ColorFirst)];Layout();}
        else if(id==More){more=!more;Layout();if(more){SetFocus(Get(StepInput));Focus(Get(StepInput));}}
        else if(id==AddStep)return NewStep();
        else if(id==LessDays||id==MoreDays){int days=1;todo_ui::Number(Get(Days),1,366,days);SetWindowTextW(Get(Days),std::to_wstring(std::clamp(days+(id==MoreDays?1:-1),1,366)).c_str());}
        else if(id>=Today&&id<=Monday){auto day=TodayDate();int count=id==Tomorrow?1:0;
            const int weekday=calendar::Weekday(day);if(id==Weekend)count=weekday==0?0:6-weekday;else if(id==Monday){count=(8-weekday)%7;if(!count)count=7;}
            if(const auto chosen=calendar::AddDays(day,count)){auto date=NativeDate(*chosen);SendMessageW(Get(Date),DTM_SETSYSTEMTIME,GDT_VALID,reinterpret_cast<LPARAM>(&date));}}
        else if(id>=StepFirst){const auto index=static_cast<std::size_t>((id-StepFirst)/3);if(index>=steps.size())return;
            if((id-StepFirst)%3==0){steps[index].completed=!steps[index].completed;SetWindowTextW(steps[index].check,steps[index].completed?L"✓":L"○");}
            else if((id-StepFirst)%3==2){
                auto& step=steps[index];for(const auto control:{step.check,step.title,step.remove}){tabs.erase(std::remove(tabs.begin(),tabs.end(),control),tabs.end());DestroyWindow(control);}
                steps.erase(steps.begin()+static_cast<std::ptrdiff_t>(index));
                for(std::size_t i=index;i<steps.size();++i){const auto base=StepFirst+static_cast<int>(i)*3;SetWindowLongPtrW(steps[i].check,GWLP_ID,base);SetWindowLongPtrW(steps[i].title,GWLP_ID,base+1);SetWindowLongPtrW(steps[i].remove,GWLP_ID,base+2);}
                Layout();SetFocus(Get(StepInput));
            }
        }
        if(form)RedrawWindow(form,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    LRESULT Key(WPARAM key,HWND source) {
        if(key==VK_ESCAPE){if(picker.IsOpen())picker.Close(true);else if(!busy)Close(true);return 1;}
        if(key==VK_TAB){todo_ui::Fence(tabs,source,GetKeyState(VK_SHIFT)<0);return 1;}
        if(key==VK_RETURN){
            if(source==Get(Notes)&&GetKeyState(VK_CONTROL)>=0)return 0;
            if(source==Get(StepInput)&&GetKeyState(VK_CONTROL)>=0)NewStep();
            else if(GetKeyState(VK_CONTROL)<0||source==Get(Title)||source==Get(Notes))Submit();
            else {wchar_t kind[16]{};GetClassNameW(source,kind,16);if(lstrcmpiW(kind,L"BUTTON")==0)Action(GetDlgCtrlID(source));else return 0;}
            return 1;
        }
        return 0;
    }
    std::wstring Day(calendar::Date day)const {
        if(day==TodayDate())return T(L"今天",L"today");
        wchar_t text[48]{};if(en)swprintf_s(text,L"%04d/%02d/%02d",day.year,day.month,day.day);
        else swprintf_s(text,L"%d月%d日",day.month,day.day);return text;
    }
    std::wstring ScheduleHint()const {
        const auto date=dates.at(Date);const calendar::Date first{date.wYear,date.wMonth,date.wDay};int count=1;todo_ui::Number(Get(Days),1,366,count);
        const auto last=calendar::AddDays(first,count-1).value_or(first);
        if(draft["allDay"].get<bool>())return count==1?T(L"全天 · 结束于 ",L"All day · ends ")+Day(last):
            T(L"全天 · ",L"All day · ")+Day(first)+T(L" 至 ",L" to ")+Day(last)+T(L"，共 ",L", ")+std::to_wstring(count)+T(L" 天",L" days");
        const auto start=todo_ui::Wide(Clock(dates.at(StartTime))),finish=todo_ui::Wide(Clock(dates.at(EndTime)));
        const auto endDay=count==1&&finish<=start?calendar::AddDays(first,1).value_or(last):last;
        return Day(first)+L" "+start+L" – "+(endDay==first?L"":Day(endDay)+L" ")+finish;
    }
    void DrawButton(const DRAWITEMSTRUCT& item) {
        const int id=static_cast<int>(item.CtlID);const float w=(item.rcItem.right-item.rcItem.left)*96.f/dpi,h=(item.rcItem.bottom-item.rcItem.top)*96.f/dpi;
        const auto dc=item.hDC;const bool disabled=(item.itemState&ODS_DISABLED)!=0,down=(item.itemState&ODS_SELECTED)!=0;
        const bool footer=id==SaveButton||id==CancelButton||id==DeleteButton;
        const bool segment=id==AllDay||id==Timed||id==Pending||id==Complete||id==LessDays||id==MoreDays;
        const auto brush=CreateSolidBrush(footer?kFooter:segment?kInput:kPaper);FillRect(dc,&item.rcItem,brush);DeleteObject(brush);
        if(id>=ColorFirst&&id<ColorFirst+6){
            const float s=dpi/96.f,x=(w-26)*.5f*s,y=(h-26)*.5f*s;Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const auto color=kColors[id-ColorFirst];if(color==CLR_INVALID){Gdiplus::HatchBrush hatch(Gdiplus::HatchStyleForwardDiagonal,Gdiplus::Color(255,67,67,71),Gdiplus::Color(255,28,28,32));g.FillEllipse(&hatch,x,y,26*s,26*s);}
            else {Gdiplus::SolidBrush fill(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)));g.FillEllipse(&fill,x,y,26*s,26*s);}
            Gdiplus::Pen outline(Gdiplus::Color(46,255,255,255),1.5f*s);g.DrawEllipse(&outline,x,y,26*s,26*s);
            if(draft["color"]==kColorNames[static_cast<std::size_t>(id-ColorFirst)]){Gdiplus::Pen ring(Gdiplus::Color(230,255,255,255),2*s);g.DrawEllipse(&ring,x-4*s,y-4*s,34*s,34*s);}return;
        }
        auto text=todo_ui::Text(item.hwndItem);COLORREF color=disabled?kFaint:kText;
        if(id==CloseButton){Icon(dc,id,{6,6,16,16},dpi,kDim);return;}
        if(id==SaveButton){
            Round(dc,{0,0,w,h},dpi,12,disabled?RGB(55,69,121):kAccent);
            Text(dc,text,{18,0,w-72,h},dpi,13,disabled?RGB(128,128,131):RGB(255,255,255));
            Round(dc,{w-61,(h-16)/2,45,16},dpi,6,disabled?RGB(65,78,127):RGB(121,147,238));
            Text(dc,L"Ctrl ↵",{w-61,(h-16)/2,45,16},dpi,10,disabled?RGB(126,133,162):RGB(232,237,255),DT_CENTER);return;
        }
        if(id==CancelButton||id==AddStep){Round(dc,{0,0,w,h},dpi,12,down?RGB(54,54,60):RGB(42,42,47));}
        else if(id==Pending||id==Complete||id==AllDay||id==Timed){
            const bool selected=(id==AllDay&&draft["allDay"].get<bool>())||(id==Timed&&!draft["allDay"].get<bool>())||
                (id==Pending&&!draft["completed"].get<bool>())||(id==Complete&&draft["completed"].get<bool>());
            if(selected)Round(dc,{0,0,w,h},dpi,9,id==Pending||id==Complete?RGB(58,58,66):kAccent);
            color=selected?RGB(255,255,255):kDim;
            if(id==AllDay||id==Timed){const float content=ui_font::IsChinese(text)?static_cast<float>(text.size())*13.f:static_cast<float>(text.size())*6.6f;
                const float left=(w-content-21)/2;Icon(dc,id,{left,(h-15)/2,15,15},dpi,color);Text(dc,text,{left+21,0,content+3,h},dpi,13,color);return;}
        }
        else if(dates.count(id)||choices.count(id)){
            Round(dc,{0,0,w,h},dpi,12,kInput,kBorder);
            const bool choice=choices.count(id)!=0;Text(dc,text,{choice?38.f:14.f,0,w-(choice?72.f:48.f),h},dpi,13,color);
            if(choice){Icon(dc,id,{13,(h-16)/2,16,16},dpi,kFaint);Text(dc,L"⌄",{w-28,0,16,h},dpi,14,kFaint,DT_CENTER);}
            else Icon(dc,id==Date?Date:Timed,{w-28,(h-14)/2,14,14},dpi,kFaint);return;
        }
        else if(id>=Today&&id<=Monday){Round(dc,{0,0,w,h},dpi,15,down?RGB(56,56,61):RGB(44,44,48));color=kDim;}
        else if(id==More){
            Icon(dc,More,{8,(h-16)/2,16,16},dpi,kDim);Text(dc,T(L"更多选项",L"More options"),{32,0,w-180,h},dpi,13,kText,DT_LEFT,FW_SEMIBOLD);
            Text(dc,T(L"子任务与备注",L"Sub-steps and notes"),{w-176,0,148,h},dpi,12,kFaint,DT_RIGHT);Text(dc,more?L"⌃":L"⌄",{w-24,0,16,h},dpi,14,kFaint,DT_CENTER);return;
        }
        else if(id==DeleteButton)color=RGB(255,105,97);
        Text(dc,text,{0,0,w,h},dpi,id>=Today&&id<=Monday?12.f:13.f,color,DT_CENTER);
        if(item.itemState&ODS_FOCUS)Round(dc,{1,1,w-2,h-2},dpi,9,CLR_INVALID,kAccent);
    }
    void Draw(HWND target,HDC dc) {
        if(!surface)return;RECT client{};GetClientRect(target,&client);surface->Clear(dc,client);
        if(target==form){
            for(const auto& label:labels)Text(dc,label.text,label.bounds,dpi,12,kDim,DT_LEFT,FW_SEMIBOLD);
            for(const auto& frame:frames)Round(dc,frame,dpi,12,kInput,kBorder);
            Round(dc,scheduleFrame,dpi,13,kInput,kBorder);if(existing)Round(dc,statusFrame,dpi,13,kInput,kBorder);Round(dc,durationFrame,dpi,12,kInput,kBorder);
            Text(dc,T(L"天",L"days"),{durationFrame.Right()-65,durationFrame.y,27,durationFrame.height},dpi,12,kFaint,DT_CENTER);
            Round(dc,hintFrame,dpi,11,RGB(33,35,48));Icon(dc,Timed,{12,hintFrame.y+10,14,14},dpi,RGB(183,198,255));Text(dc,ScheduleHint(),{33,hintFrame.y,hintFrame.width-44,hintFrame.height},dpi,12,RGB(183,198,255));
            const wchar_t* colorsZh[]{L"无",L"红",L"橙",L"黄",L"绿",L"蓝"};const wchar_t* colorsEn[]{L"None",L"Red",L"Orange",L"Yellow",L"Green",L"Blue"};
            for(std::size_t i=0;i<kColorNames.size();++i)if(draft["color"]==kColorNames[i])Text(dc,en?colorsEn[i]:colorsZh[i],{(existing?(width-48-14)/2+14:0)+(en?48.f:56.f),68,64,16},dpi,12,kFaint);
            Gdiplus::Graphics graphics(dc);Gdiplus::Pen pen(Gdiplus::Color(255,47,47,51),dpi/96.f);pen.SetDashStyle(Gdiplus::DashStyleDash);graphics.DrawLine(&pen,0.f,moreLine*dpi/96.f,(width-48)*dpi/96.f,moreLine*dpi/96.f);
        }
        else if(target==window){
            const auto foot=Pixels({0,height-73,width,73},dpi);const auto brush=CreateSolidBrush(kFooter);FillRect(dc,&foot,brush);DeleteObject(brush);
            Text(dc,existing?T(L"编辑任务",L"Edit task"):T(L"添加任务",L"Add task"),{24,22,width-100,26},dpi,19,kText,DT_LEFT,FW_BOLD);
            Text(dc,existing?T(L"保存后，日历、桌面清单和灵动岛会一起更新。",L"Changes appear in the calendar, desktop list and island together."):
                T(L"把想做的事，安排进每一天。",L"Give the things you want to do a place in your days."),{24,52,width-88,16},dpi,12,kDim);
            Gdiplus::Graphics graphics(dc);Gdiplus::Pen pen(Gdiplus::Color(255,47,47,51),dpi/96.f);pen.SetDashStyle(Gdiplus::DashStyleDash);graphics.DrawLine(&pen,0.f,84*dpi/96.f,width*dpi/96.f,84*dpi/96.f);
            pen.SetDashStyle(Gdiplus::DashStyleSolid);graphics.DrawLine(&pen,0.f,(height-73)*dpi/96.f,width*dpi/96.f,(height-73)*dpi/96.f);
            if(!feedback.empty())Text(dc,feedback,{24,height-101,width-48,20},dpi,11,RGB(255,137,137));
        }
    }
    void Close(bool restore) {
        auto keep=shared_from_this();if(closing)return;closing=true;
        const auto old=window,oldOwner=owner,focus=returnFocus;const bool enable=ownerWasEnabled;auto notify=std::move(closed);picker.Close(false);
        window=nullptr;ownerWasEnabled=false;
        if(old){KillTimer(old,kFloatTimer);SetWindowLongPtrW(old,GWLP_USERDATA,0);if(form)SetWindowLongPtrW(form,GWLP_USERDATA,0);if(viewport)SetWindowLongPtrW(viewport,GWLP_USERDATA,0);DestroyWindow(old);}
        form=nullptr;viewport=nullptr;tabs.clear();steps.clear();surface.reset();save={};nowProvider={};
        if(titleFont){DeleteObject(titleFont);titleFont=nullptr;}
        if(enable&&IsWindow(oldOwner))EnableWindow(oldOwner,TRUE);
        if(restore&&IsWindow(oldOwner)&&IsWindowEnabled(oldOwner)){SetActiveWindow(oldOwner);SetFocus(IsWindow(focus)?focus:oldOwner);}
        if(notify)notify();
    }
    void Handle(const std::string& text) {
        const auto message=Json::parse(text,nullptr,false);
        if(closing||!message.is_object()||message.value("version",0)!=1||message.value("scope",std::string{})!="todo-editor")return;
        const auto action=message.value("action",std::string{});
        if(action=="save")SaveCommands(message.value("commands",Json{}));else if(action=="delete")Delete();else if(action=="close")Close(true);
    }
    static bool CoveredByLume(HWND hwnd) {
        RECT mine{};GetWindowRect(hwnd,&mine);
        for(HWND above=GetWindow(hwnd,GW_HWNDPREV);above;above=GetWindow(above,GW_HWNDPREV)){
            DWORD pid=0;GetWindowThreadProcessId(above,&pid);const auto ownedBy=GetWindow(above,GW_OWNER);
            if(pid!=GetCurrentProcessId()||!IsWindowVisible(above)||ownedBy==hwnd||IsChild(hwnd,ownedBy)||GetAncestor(above,GA_ROOTOWNER)==hwnd)continue;
            RECT other{},overlap{};if(GetWindowRect(above,&other)&&IntersectRect(&overlap,&mine,&other))return true;
        }return false;
    }
    static void Float(HWND hwnd,bool above){if(above)SetTimer(hwnd,kFloatTimer,250,nullptr);else KillTimer(hwnd,kFloatTimer);
        SetWindowPos(hwnd,above?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);}
    static LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
        auto* raw=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){raw=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(raw));}
        if(!raw)return DefWindowProcW(hwnd,message,wp,lp);const auto self=raw->shared_from_this();
        switch(message){
        case WM_PAINT:{PAINTSTRUCT paint{};const auto dc=BeginPaint(hwnd,&paint);self->Draw(hwnd,dc);EndPaint(hwnd,&paint);return 0;}
        case WM_PRINTCLIENT:self->Draw(hwnd,reinterpret_cast<HDC>(wp));return 0;
        case WM_ERASEBKGND:return 1;
        case WM_COMMAND:
            if(hwnd!=self->window)return SendMessageW(self->window,message,wp,lp);
            if(HIWORD(wp)==BN_CLICKED&&lp)PostMessageW(self->window,todo_ui::ActionMessage,wp,lp);
            else if(HIWORD(wp)==EN_CHANGE&&!self->initializing)self->UpdateSave();return 0;
        case todo_ui::ActionMessage:
            if(IsWindow(reinterpret_cast<HWND>(lp))&&IsChild(hwnd,reinterpret_cast<HWND>(lp))&&GetDlgCtrlID(reinterpret_cast<HWND>(lp))==LOWORD(wp))self->Action(LOWORD(wp));return 0;
        case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:case WM_CTLCOLORLISTBOX:
            return self->surface?self->surface->Color(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)):DefWindowProcW(hwnd,message,wp,lp);
        case WM_DRAWITEM:{
            if(!self->surface)return FALSE;self->DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
        }
        case todo_ui::KeyMessage:return self->Key(wp,reinterpret_cast<HWND>(lp));
        case todo_ui::FocusMessage:self->Focus(reinterpret_cast<HWND>(lp));return 0;
        case WM_MOUSEWHEEL:self->Scroll(self->scroll-GET_WHEEL_DELTA_WPARAM(wp)/120.f*48);return 0;
        case WM_LBUTTONDOWN:
            if(hwnd==self->window&&GET_Y_LPARAM(lp)<todo_ui::Px(88,self->dpi)){ReleaseCapture();return SendMessageW(hwnd,WM_NCLBUTTONDOWN,HTCAPTION,lp);}return 0;
        case WM_ACTIVATEAPP:if(hwnd==self->window)Float(hwnd,wp!=FALSE);return 0;
        case WM_TIMER:if(hwnd==self->window&&wp==kFloatTimer&&CoveredByLume(hwnd))SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);return 0;
        case WM_DPICHANGED:{
            self->dpi=HIWORD(wp);if(self->surface){auto old=self->surface->font;self->surface->dpi=self->dpi;
                self->surface->font=CreateFontW(-todo_ui::Px(13,self->dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,ui_font::Family(L"待办"));
                for(auto control:self->tabs)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(self->surface->font),TRUE);if(old)DeleteObject(old);}
            if(self->titleFont)DeleteObject(self->titleFont);self->titleFont=CreateFontW(-todo_ui::Px(16,self->dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,ui_font::Family(L"待办"));SendMessageW(self->Get(Title),WM_SETFONT,reinterpret_cast<WPARAM>(self->titleFont),TRUE);
            const auto rect=*reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
            ConfigureRoundedWindow(hwnd,24.f*self->dpi/96.f,true);self->Layout();if(self->surface)self->surface->Refresh();return 0;
        }
        case WM_CLOSE:if(!self->busy)self->Close(true);return 0;
        case WM_DESTROY:
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);if(hwnd==self->window){self->window=nullptr;self->Close(false);}return 0;
        default:return DefWindowProcW(hwnd,message,wp,lp);
        }
    }
};

TodoSheetWindow::TodoSheetWindow()=default;
TodoSheetWindow::~TodoSheetWindow(){if(impl_)impl_->closed={};Close(false);}
void TodoSheetWindow::SetEnglish(bool value){english=value;}
bool TodoSheetWindow::Open(HINSTANCE instance,HWND owner,RECT host,unsigned dpi,const calendar::Preferences&,
    const todo::ILocalTimePort& time,todo::Command seed,todo::Millis now,std::optional<todo::TaskView> existing,Save save,Closed closed,Now nowProvider) {
    if(IsOpen()){SetForegroundWindow(Handle());return true;}
    if(!IsWindow(owner)||!dpi||!save||host.right<=host.left||host.bottom<=host.top||
        (seed.type==todo::CommandType::Update&&!existing)||(seed.type==todo::CommandType::Add&&!seed.patch.schedule))return false;
    auto state=std::make_shared<Impl>();state->instance=instance;state->owner=owner;state->returnFocus=GetFocus();
    state->dpi=dpi;state->en=english.load();state->time=&time;state->seed=std::move(seed);state->existing=std::move(existing);state->openedAt=now;
    state->save=std::move(save);state->closed=std::move(closed);state->nowProvider=std::move(nowProvider);state->MakeDraft();
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=Impl::Proc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=kClass;
    if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;wc.lpszClassName=kBodyClass;
    if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    MONITORINFO monitor{sizeof(monitor)};if(!GetMonitorInfoW(MonitorFromRect(&host,MONITOR_DEFAULTTOPRIMARY),&monitor))return false;
    const auto area=monitor.rcWork;const int width=std::min(MulDiv(kWidth,static_cast<int>(dpi),96),static_cast<int>(area.right-area.left));
    const int height=std::min(MulDiv(kHeight,static_cast<int>(dpi),96),static_cast<int>(area.bottom-area.top)-MulDiv(32,static_cast<int>(dpi),96));
    const int left=std::clamp(static_cast<int>((host.left+host.right)/2)-width/2,static_cast<int>(area.left),static_cast<int>(area.right)-width);
    const int top=area.top+(area.bottom-area.top-height)/2;
    auto caption=state->T(L"Lume 待办",L"Lume task");if(state->existing)caption+=L" · "+todo_ui::Wide(state->existing->title);
    state->window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,kClass,caption.c_str(),WS_POPUP|WS_CLIPCHILDREN,
        left,top,width,height,owner,nullptr,instance,state.get());if(!state->window)return false;impl_=state;
    window_layer::Register(state->window,window_layer::Kind::ExplicitTool,owner);
    ConfigureRoundedWindow(state->window,24.f*dpi/96.f,true);const BOOL dark=TRUE;DwmSetWindowAttribute(state->window,20,&dark,sizeof(dark));
    if(!state->CreateControls()){state->closed={};state->Close(false);impl_.reset();return false;}
    state->Layout();state->initializing=false;state->ownerWasEnabled=IsWindowEnabled(owner)!=FALSE;
    if(state->ownerWasEnabled)EnableWindow(owner,FALSE);
    const auto window=state->window;state->surface->AttachOpaque(window,24.f);state->surface->Show(state->Get(Title));
    if(state->closing||!IsWindow(window))return false;
    SetForegroundWindow(window);SetFocus(state->Get(Title));Impl::Float(window,true);
    return true;
}
void TodoSheetWindow::Close(bool restore){auto state=std::move(impl_);if(state)state->Close(restore);}
HWND TodoSheetWindow::Handle()const noexcept{return impl_?impl_->window:nullptr;}
bool TodoSheetWindow::IsOpen()const noexcept{return Handle()!=nullptr;}
bool TodoSheetWindow::Receive(std::string_view message){if(!IsOpen())return false;const auto state=impl_;state->Handle(std::string(message));return true;}
bool TodoSheetWindow::Receive(HWND window,std::string_view message){
    wchar_t name[64]{};if(!IsWindow(window)||!GetClassNameW(window,name,64)||std::wstring_view(name)!=kClass)return false;
    auto* raw=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(!raw)return false;
    const auto state=raw->shared_from_this();state->Handle(std::string(message));return true;
}
} // namespace lume::platform::windows
