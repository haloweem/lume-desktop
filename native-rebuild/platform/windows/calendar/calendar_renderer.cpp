#include "platform/windows/calendar/calendar_renderer.h"
#include "platform/windows/widget_ui/ui_font.h"
#include "platform/windows/calendar/calendar_css_shadow.h"
#include "core/calendar/lunar_calendar.h"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
namespace lume::platform::windows {
namespace {
template<class T> class Com {
public:
    ~Com() { if(p_) p_->Release(); }
    T* get() const { return p_; } T* operator->() const { return p_; }
    T** put() { if(p_) p_->Release(); p_=nullptr; return &p_; }
private: T* p_=nullptr;
};
D2D1_RECT_F Rect(CalendarRect r) { return D2D1::RectF(r.x,r.y,r.Right(),r.Bottom()); }
D2D1_COLOR_F Color(calendar::Rgba c,float opacity=1) {
    return D2D1::ColorF(static_cast<float>((c.rgb>>16)&255)/255,static_cast<float>((c.rgb>>8)&255)/255,static_cast<float>(c.rgb&255)/255,static_cast<float>(c.alpha)/255*opacity);
}
bool Intersects(CalendarRect a,CalendarRect b) { return a.x<b.Right()&&a.Right()>b.x&&a.y<b.Bottom()&&a.Bottom()>b.y; }
class Canvas {
public:
    Canvas(IDWriteFactory* w,ID2D1Factory* d,IWICImagingFactory* factory,float width,float height,unsigned dpi):write(w) {
        pixelWidth=static_cast<UINT>(std::lround(width*dpi/96)),pixelHeight=static_cast<UINT>(std::lround(height*dpi/96));
        if(!pixelWidth||!pixelHeight) return;
        if(FAILED(factory->CreateBitmap(pixelWidth,pixelHeight,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.put()))) return;
        auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),static_cast<float>(dpi),static_cast<float>(dpi));
        if(FAILED(d->CreateWicBitmapRenderTarget(bitmap.get(),properties,target.put()))||FAILED(target->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),brush.put()))) return;
        target->BeginDraw(); target->Clear(D2D1::ColorF(0,0,0,0)); target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); ready=true;
    }
    bool Finish(CalendarBitmap& out) {
        if(!ready||FAILED(target->EndDraw())) return false;
        out.width=pixelWidth; out.height=pixelHeight; out.pixels.resize(static_cast<std::size_t>(pixelWidth)*pixelHeight);
        return SUCCEEDED(bitmap->CopyPixels(nullptr,pixelWidth*4,static_cast<UINT>(out.pixels.size()*4),reinterpret_cast<BYTE*>(out.pixels.data())));
    }
    float Measure(const std::wstring& text,float size,DWRITE_FONT_WEIGHT weight=DWRITE_FONT_WEIGHT_SEMI_BOLD) {
        Com<IDWriteTextFormat> format; Com<IDWriteTextLayout> layout; DWRITE_TEXT_METRICS metrics{};
        if(FAILED(write->CreateTextFormat(ui_font::Family(text),nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",format.put()))) return 0;
        if(FAILED(write->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),format.get(),2000,300,layout.put()))||FAILED(layout->GetMetrics(&metrics))) return 0;
        return metrics.widthIncludingTrailingWhitespace;
    }
    float Paragraph(const std::wstring& text,CalendarRect r,float size,D2D1_COLOR_F color,bool draw=true,bool strike=false,bool wrap=true) {
        if(r.width<=0)return 0;
        Com<IDWriteTextFormat> format;Com<IDWriteTextLayout> layout;DWRITE_TEXT_METRICS metrics{};
        if(FAILED(write->CreateTextFormat(ui_font::Family(text),nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",format.put())))return 0;
        format->SetWordWrapping(wrap?DWRITE_WORD_WRAPPING_EMERGENCY_BREAK:DWRITE_WORD_WRAPPING_NO_WRAP);
        const float line=!wrap?14.0f:size==13?17.0f:size==10?14.0f:size*1.5f;
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,line,size*1.13f);
        if(FAILED(write->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),format.get(),r.width,200000,layout.put()))||FAILED(layout->GetMetrics(&metrics)))return 0;
        if(strike)layout->SetStrikethrough(TRUE,{0,static_cast<UINT32>(text.size())});
        if(draw){brush->SetColor(color);target->DrawTextLayout(D2D1::Point2F(r.x,r.y),layout.get(),brush.get());}
        return metrics.height;
    }
    void Clip(CalendarRect r){target->PushAxisAlignedClip(Rect(r),D2D1_ANTIALIAS_MODE_ALIASED);}
    void Unclip(){target->PopAxisAlignedClip();}
    void Text(const std::wstring& text,CalendarRect r,float size,D2D1_COLOR_F color,DWRITE_FONT_WEIGHT weight=DWRITE_FONT_WEIGHT_NORMAL,bool center=true,bool ellipsis=false) {
        if(!ready||r.width<=0||r.height<=0) return;
        Com<IDWriteTextFormat> format;
        if(FAILED(write->CreateTextFormat(ui_font::Family(text),nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",format.put()))) return;
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); format->SetTextAlignment(center?DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING);
        Com<IDWriteInlineObject> sign;
        if(ellipsis&&SUCCEEDED(write->CreateEllipsisTrimmingSign(format.get(),sign.put()))){
            const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};
            format->SetTrimming(&trimming,sign.get());
        }
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); brush->SetColor(color);
        target->DrawText(text.c_str(),static_cast<UINT32>(text.size()),format.get(),Rect(r),brush.get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void Rounded(CalendarRect r,float radius,D2D1_COLOR_F c,float stroke=0) {
        if(!ready) return;
        brush->SetColor(c); auto rect=D2D1::RoundedRect(Rect(r),radius,radius);
        if(stroke) target->DrawRoundedRectangle(rect,brush.get(),stroke); else target->FillRoundedRectangle(rect,brush.get());
    }
    void Line(float x,float y,float x2,float y2,D2D1_COLOR_F c,float thickness=1) { if(!ready) return; brush->SetColor(c); target->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F(x2,y2),brush.get(),thickness); }
    void Shadow(CalendarRect panel,const CalendarCssShadow& shadow,unsigned dpi) {
        if(!ready||shadow.pixels.empty())return;
        Com<ID2D1Bitmap> image;
        const auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),static_cast<float>(dpi),static_cast<float>(dpi));
        if(FAILED(target->CreateBitmap(D2D1::SizeU(shadow.width,shadow.height),shadow.pixels.data(),shadow.width*4,properties,image.put())))return;
        const float scale=96.f/dpi,padding=shadow.padding*scale;
        target->DrawBitmap(image.get(),D2D1::RectF(panel.x-padding,panel.y-padding,
            panel.x-padding+shadow.width*scale,panel.y-padding+shadow.height*scale));
    }
    void Chevron(CalendarRect r,bool right,D2D1_COLOR_F c) {
        const float x=r.x+r.width/2,y=r.y+r.height/2,k=right?1.0f:-1.0f;
        Line(x-k*3,y-5,x+k*3,y,c,2.6f); Line(x+k*3,y,x-k*3,y+5,c,2.6f);
    }
    bool ready=false;
private:
    IDWriteFactory* write;
    UINT pixelWidth=0,pixelHeight=0;
    Com<IWICBitmap> bitmap; Com<ID2D1RenderTarget> target; Com<ID2D1SolidColorBrush> brush;
};
const wchar_t* kDays[]={L"日",L"一",L"二",L"三",L"四",L"五",L"六"};
calendar::Date WeekStart(calendar::Date day,int weekStartsOn){
    const int weekday=calendar::Weekday(day);
    const auto first=calendar::AddDays(day,-((weekday-weekStartsOn+7)%7));
    return first?*first:day;
}
D2D1_COLOR_F Mark(todo::Color color,float alpha=1){
    const unsigned colors[]={0,0xff6961,0xffa348,0xf5d65c,0x6bd89b,0x65b4ff};
    const auto i=static_cast<unsigned>(color);
    return D2D1::ColorF(colors[i<6?i:0],color==todo::Color::None?0:alpha);
}
void DrawAgenda(Canvas& c,const CalendarLayout& layout,const calendar::CalendarModel& model,
    const calendar::Preferences& prefs,const CalendarRenderState& view,float width,CalendarAgendaLayout* output){
    auto a=CalculateAgendaLayout(layout,width);const auto text=Color(prefs.textColor);const bool compact=width<=600;
    if(!view.todoStatus.empty())a.list.height=std::max(0.0f,a.list.height-a.status.height);
    c.Line(a.area.x,a.area.y,a.area.x,a.area.Bottom(),D2D1::ColorF(1,1,1,.2f));
    // The selected day is already highlighted in the month grid; the pane lists only its schedule.
    const auto d=model.Selected();
    std::vector<std::size_t> daily=view.todos?view.todos->ForDay(d):std::vector<std::size_t>{};
    if(view.todos)std::stable_sort(daily.begin(),daily.end(),[&](auto x,auto y){const auto& p=view.todos->Snapshot().tasks[x];const auto& q=view.todos->Snapshot().tasks[y];
        if(p.completed!=q.completed)return p.completed<q.completed;
        return (p.schedule?p.schedule->startAt:INT64_MAX)<(q.schedule?q.schedule->startAt:INT64_MAX);});
    c.Text(L"日程"+(daily.empty()?L"":L"  "+std::to_wstring(daily.size())),{a.list.x,a.list.y-26,a.list.width,18},13,text,DWRITE_FONT_WEIGHT_SEMI_BOLD,false);
    c.Line(a.list.x,a.list.y-1,a.list.Right(),a.list.y-1,D2D1::ColorF(1,1,1,.15f));
    float offset=0;
    for(const auto index:daily){
        const auto& task=view.todos->Snapshot().tasks[index];CalendarAgendaRow row;row.task=index;
        const float contentX=a.list.x+(compact?23.0f:25.0f),contentWidth=std::max(1.0f,a.list.width-(compact?29.0f:90.0f));
        const auto title=TodoWide(task.title),time=view.time?TodoScheduleText(task,*view.time,view.now):L"";
        const float titleH=std::max(17.0f,c.Paragraph(title,{0,0,contentWidth-6,0},13,text,false));
        const float timeH=time.empty()?0:5+c.Paragraph(time,{0,0,contentWidth-6,0},11,text,false);
        std::wstring notes=TodoWide(task.notes);
        if(!task.schedule&&task.reminderAt&&view.time){const auto stamp=view.time->ToLocal(*task.reminderAt);if(stamp)notes=TodoDateText(*stamp)+(notes.empty()?L"":L" · "+notes);}
        const float noteH=notes.empty()?0:5+c.Paragraph(notes,{0,0,contentWidth-6,0},10,text,false);
        const float contentH=6+titleH+timeH+noteH;
        row.row={a.list.x,a.list.y+offset,a.list.width,16+std::max(contentH,24.0f)+(compact?31.0f:0)};
        row.check={a.list.x+6,row.row.y+13,14,14};row.title={contentX+3,row.row.y+11,contentWidth-6,titleH};
        row.time={row.title.x,row.title.Bottom()+5,row.title.width,std::max(0.0f,timeH-5)};
        row.notes={row.title.x,row.title.Bottom()+timeH+5,row.title.width,std::max(0.0f,noteH-5)};
        row.edit={row.row.Right()-60,compact?row.row.y+8+contentH+3:row.row.y+8,26,28};
        row.remove={row.edit.Right()+2,row.edit.y,26,28};a.rows.push_back(row);offset+=row.row.height+6;
    }
    a.totalHeight=std::max(0.0f,offset-6);a.maxScroll=std::max(0.0f,a.totalHeight-a.list.height);
    const float scroll=std::clamp(view.agendaScroll,0.0f,a.maxScroll);
    c.Clip(a.list);
    if(daily.empty())c.Text(L"当天暂无日程",{a.list.x,a.list.y+12,a.list.width,20},13,Color(prefs.textColor,.65f),DWRITE_FONT_WEIGHT_NORMAL,false);
    for(std::size_t n=0;n<a.rows.size();++n){auto& row=a.rows[n];for(auto* r:{&row.row,&row.check,&row.title,&row.time,&row.notes,&row.edit,&row.remove})r->y-=scroll;
        if(!Intersects(row.row,a.list))continue;
        const auto& task=view.todos->Snapshot().tasks[row.task];c.Rounded(row.row,6,Mark(task.color,.34f));
        if(!task.projected)c.Rounded(row.check,2,task.completed?Color(prefs.highlightColor):D2D1::ColorF(1,1,1,.6f),task.completed?0.f:1.f);
        if(task.completed){c.Line(row.check.x+3,row.check.y+7,row.check.x+6,row.check.y+10,text,1.5f);c.Line(row.check.x+6,row.check.y+10,row.check.x+12,row.check.y+3,text,1.5f);}
        c.Paragraph(TodoWide(task.title),row.title,13,Color(prefs.textColor,task.completed?.55f:1),true,task.completed);
        if(view.time)c.Paragraph(TodoScheduleText(task,*view.time,view.now),row.time,11,Color(prefs.textColor,.85f));
        std::wstring notes=TodoWide(task.notes);if(!task.schedule&&task.reminderAt&&view.time){auto stamp=view.time->ToLocal(*task.reminderAt);if(stamp)notes=TodoDateText(*stamp)+(notes.empty()?L"":L" · "+notes);}
        c.Paragraph(notes,row.notes,10,Color(prefs.textColor,.65f));
        const float x=row.edit.x+6,y=row.edit.y+8;c.Line(x,y+10,x+10,y,text,1.5f);c.Line(x+2,y+12,x+12,y+2,text,1.5f);c.Line(x,y+10,x+2,y+12,text,1.5f);
        auto bin=row.remove;bin.x+=8;bin.y+=8;bin.width=10;bin.height=12;c.Rounded(bin,1,Color(prefs.textColor,.8f),1);c.Line(bin.x-2,bin.y-2,bin.Right()+2,bin.y-2,text,1);
        if(view.keyboardFocus&&view.agendaFocus/3==static_cast<int>(n)&&view.agendaFocus>=0){auto r=view.agendaFocus%3==0?row.check:view.agendaFocus%3==1?row.edit:row.remove;c.Rounded(r,3,Color(prefs.highlightColor),2);}
    }
    c.Unclip();
    if(a.maxScroll>0&&a.list.height>0){const float thumb=std::max(16.0f,a.list.height*a.list.height/a.totalHeight);c.Rounded({a.list.Right()-3,a.list.y+(a.list.height-thumb)*scroll/a.maxScroll,3,thumb},1.5f,D2D1::ColorF(1,1,1,.3f));}
    if(!view.todoStatus.empty())c.Paragraph(view.todoStatus,a.status,11,D2D1::ColorF(1,.706f,.667f));
    if(output)*output=std::move(a);
}
// Helpers shared by the todo theme's views.
std::wstring Clock(const todo::LocalDateTime& s){wchar_t b[8]{};swprintf(b,8,L"%02d:%02d",s.hour,s.minute);return std::wstring(b);}
std::vector<std::size_t> Sorted(const CalendarTodoView& todos,calendar::Date day){
    auto list=todos.ForDay(day);
    std::stable_sort(list.begin(),list.end(),[&](auto x,auto y){const auto& p=todos.Snapshot().tasks[x];const auto& q=todos.Snapshot().tasks[y];
        if(p.completed!=q.completed)return p.completed<q.completed;
        return (p.schedule?p.schedule->startAt:INT64_MAX)<(q.schedule?q.schedule->startAt:INT64_MAX);});
    return list;
}
D2D1_COLOR_F Tint(const todo::TaskView& t,D2D1_COLOR_F accent,float alpha){
    // Items without a colour label stay neutral rather than taking the accent.
    (void)accent;
    if(TodoColorRgb(t.color))return Mark(t.color,alpha);
    return D2D1::ColorF(1,1,1,alpha*.35f);
}
// A single-line title; completed items are dimmed by the caller and struck through here.
void Title(Canvas& c,const std::wstring& text,CalendarRect r,float size,D2D1_COLOR_F color,DWRITE_FONT_WEIGHT weight,bool strike){
    c.Text(text,r,size,color,weight,false,true);
    if(!strike||r.width<=0)return;
    const float width=std::min(r.width,c.Measure(text,size,weight));
    c.Line(r.x,r.y+r.height/2+.5f,r.x+width,r.y+r.height/2+.5f,color,1);
}
void ScrollThumb(Canvas& c,CalendarRect area,float scroll,float maxScroll){
    const float total=area.height+maxScroll,thumb=std::max(16.0f,area.height*area.height/total);
    c.Rounded({area.Right()+4,area.y+(area.height-thumb)*scroll/maxScroll,3,thumb},1.5f,D2D1::ColorF(1,1,1,.3f));
}
CalendarRect Clipped(CalendarRect r,CalendarRect bounds){
    const float x=std::max(r.x,bounds.x),y=std::max(r.y,bounds.y);
    return {x,y,std::max(0.0f,std::min(r.Right(),bounds.Right())-x),std::max(0.0f,std::min(r.Bottom(),bounds.Bottom())-y)};
}
// 月: the whole month with item titles, no times.
void DrawBoardMonth(Canvas& c,const CalendarLayout& l,const calendar::CalendarModel& model,
    const calendar::Preferences& prefs,const CalendarRenderState& view,CalendarAgendaLayout& a){
    const auto text=Color(prefs.textColor),dim=Color(prefs.textColor,.6f),accent=Color(prefs.highlightColor);
    const auto grid=D2D1::ColorF(1,1,1,.08f);
    for(int i=0;i<7;++i)c.Text(std::wstring(L"星期")+kDays[(i+calendar::WeekStartsOn(prefs.theme))%7],{l.weekdays.x+static_cast<float>(i)*l.weekdays.width/7,l.weekdays.y,l.weekdays.width/7,l.weekdays.height},l.weekdayFont,text,DWRITE_FONT_WEIGHT_SEMI_BOLD);
    for(int i=0;i<42;++i){
        const auto& cell=model.Grid()[static_cast<std::size_t>(i)];const auto r=l.cells[static_cast<std::size_t>(i)];
        if(r.width<=0)continue;
        c.Line(r.x,r.y,r.Right(),r.y,grid);
        if(i%7)c.Line(r.x,r.y,r.x,r.Bottom(),grid);
        if(!cell.currentMonth)continue;  // Other months' days stay blank.
        const float alpha=1.0f;
        if(cell.date==model.Selected())c.Rounded({r.x+1,r.y+1,r.width-2,r.height-2},4,accent,1.5f);
        // Day number and lunar day; festivals in blue.
        const auto lunar=calendar::LookupLunar(cell.date);
        const auto number=std::to_wstring(cell.date.day);
        const float nw=std::max(18.0f,c.Measure(number,13)+6);
        const CalendarRect badge{r.x+3,r.y+3,nw,18};
        if(cell.today)c.Rounded(badge,9,accent);
        c.Text(number,badge,13,cell.today?D2D1::ColorF(1,1,1,1):Color(prefs.textColor,alpha),DWRITE_FONT_WEIGHT_SEMI_BOLD);
        const bool festival=!lunar.festivals.empty()||!lunar.solarTerm.empty();
        if(r.width>70)c.Text(TodoWide(std::string(lunar.label)),{badge.Right()+4,r.y+3,std::max(0.0f,r.Right()-badge.Right()-8),18},10,
            festival?D2D1::ColorF(0x409cffu,alpha):Color(prefs.textColor,.55f*alpha),DWRITE_FONT_WEIGHT_NORMAL,false,true);
        if(view.keyboardFocus&&view.focus==i+3)c.Rounded({r.x+1,r.y+1,r.width-2,r.height-2},4,text,1.5f);
        if(!view.todos||!view.todoAvailable)continue;
        // Items as coloured bars filling the cell, without times.
        const auto list=Sorted(*view.todos,cell.date);
        constexpr float barH=17,barGap=2;
        // Items past the cell's height scroll inside it with the wheel.
        const CalendarRect area{r.x,r.y+24,r.width,std::max(0.0f,r.height-26)};
        const auto slot=static_cast<std::size_t>(i);
        a.cellMaxScroll[slot]=std::max(0.0f,list.size()*(barH+barGap)-barGap-area.height);
        const float scroll=std::clamp(view.cellScroll[slot],0.0f,a.cellMaxScroll[slot]);
        if(area.height<=0)continue;
        c.Clip(area);
        for(std::size_t j=0;j<list.size();++j){
            const auto index=list[j];const auto& t=view.todos->Snapshot().tasks[index];
            const CalendarRect bar{r.x+2,area.y+j*(barH+barGap)-scroll,r.width-(a.cellMaxScroll[slot]>0?9.0f:4.0f),barH};
            if(!Intersects(bar,area))continue;
            c.Rounded(bar,3,Tint(t,accent,(t.completed?.28f:.62f)*alpha));
            c.Clip(bar);
            Title(c,TodoWide(t.title),{bar.x+5,bar.y,bar.width-7,bar.height},11,
                Color(prefs.textColor,(t.completed?.6f:1)*alpha),DWRITE_FONT_WEIGHT_NORMAL,t.completed);
            c.Unclip();
            CalendarAgendaRow row;row.task=index;row.row=Clipped(bar,area);row.edit=row.row;a.rows.push_back(row);
        }
        c.Unclip();
        if(a.cellMaxScroll[slot]>0){
            const float total=area.height+a.cellMaxScroll[slot],thumb=std::max(10.0f,area.height*area.height/total);
            c.Rounded({r.Right()-5,area.y+(area.height-thumb)*scroll/a.cellMaxScroll[slot],3,thumb},1.5f,D2D1::ColorF(1,1,1,.35f));
        }
    }
}
// 日 / 周: an hour grid with each scheduled item as a block, like a planner.
void DrawTimeGrid(Canvas& c,const CalendarLayout& l,const calendar::CalendarModel& model,
    const calendar::Preferences& prefs,const CalendarRenderState& view,CalendarAgendaLayout& a){
    const auto text=Color(prefs.textColor),dim=Color(prefs.textColor,.6f),accent=Color(prefs.highlightColor);
    const auto body=l.boardBody;if(body.width<=60||body.height<=60)return;
    const bool week=prefs.boardView==calendar::BoardView::Week;
    const auto selected=model.Selected(),today=model.Today();
    const auto first=week?WeekStart(selected,calendar::WeekStartsOn(prefs.theme)):selected;
    std::vector<calendar::Date> dates;
    for(int k=0;k<(week?7:1);++k)if(const auto d=calendar::AddDays(first,k))dates.push_back(*d);
    const bool usable=view.todos&&view.todoAvailable;
    auto local=[&](todo::Millis at){return view.time?view.time->ToLocal(at):std::optional<todo::LocalDateTime>{};};
    const float gutter=46,headH=44,colW=(body.width-gutter)/static_cast<float>(std::max<std::size_t>(1,dates.size()));
    auto column=[&](std::size_t k){return body.x+gutter+static_cast<float>(k)*colW;};
    // Column headers: weekday and date; today in the accent colour.
    for(std::size_t k=0;k<dates.size();++k){
        const auto d=dates[k];const float x=column(k);const int wd=calendar::Weekday(d);
        c.Text(std::wstring(L"周")+kDays[wd<0?0:wd],{x+8,body.y,colW-8,16},11,dim,DWRITE_FONT_WEIGHT_NORMAL,false);
        c.Text(std::to_wstring(d.day),{x+8,body.y+16,colW-8,26},20,d==today?accent:text,DWRITE_FONT_WEIGHT_SEMI_BOLD,false);
        if(week&&d==selected)c.Rounded({x+6,body.y+headH-3,colW-12,2},1,accent);
    }
    // Items without a time: one line per day above the hours.
    std::vector<std::vector<std::size_t>> timed(dates.size()),untimed(dates.size());
    for(std::size_t k=0;k<dates.size()&&usable;++k)for(const auto i:Sorted(*view.todos,dates[k]))
        (view.todos->Snapshot().tasks[i].schedule&&view.time?timed:untimed)[k].push_back(i);
    const bool strip=std::any_of(untimed.begin(),untimed.end(),[](const auto& v){return !v.empty();});
    const float stripH=strip?24.0f:0.0f;
    if(strip){
        c.Text(L"全天",{body.x,body.y+headH,gutter-6,stripH},10,dim);
        for(std::size_t k=0;k<dates.size();++k){
            if(untimed[k].empty())continue;
            const auto index=untimed[k][0];const auto& t=view.todos->Snapshot().tasks[index];
            const bool more=untimed[k].size()>1;
            const CalendarRect chip{column(k)+2,body.y+headH+3,colW-4-(more?22.0f:0.0f),18};
            c.Rounded(chip,4,Tint(t,accent,t.completed?.2f:.34f));
            c.Clip(chip);Title(c,TodoWide(t.title),{chip.x+6,chip.y,chip.width-8,chip.height},11,Color(prefs.textColor,t.completed?.55f:1),DWRITE_FONT_WEIGHT_NORMAL,t.completed);c.Unclip();
            if(more)c.Text(L"+"+std::to_wstring(untimed[k].size()-1),{chip.Right()+2,chip.y,20,chip.height},10,dim,DWRITE_FONT_WEIGHT_NORMAL,false);
            CalendarAgendaRow row;row.task=index;row.row=chip;row.edit=chip;a.rows.push_back(row);
        }
    }
    const CalendarRect grid{body.x,body.y+headH+stripH+(strip?4.0f:0.0f),body.width,std::max(0.0f,body.Bottom()-(body.y+headH+stripH+(strip?4.0f:0.0f)))};
    const float base=week?44.0f:52.0f;
    // Ctrl+wheel zooms; at the smallest the whole day fits.
    a.minZoom=std::min(1.0f,grid.height/24/base);a.heading=grid;
    const float hourH=base*std::clamp(view.boardZoom,a.minZoom,3.0f),total=24*hourH;
    a.totalHeight=total;a.maxScroll=std::max(0.0f,total-grid.height);
    float scroll=view.boardScroll;
    if(scroll<0){
        // Start just above the earliest item (or the current hour today), 08:00 otherwise.
        float hour=8;
        for(std::size_t k=0;k<dates.size();++k)for(const auto i:timed[k])
            if(const auto s=local(view.todos->Snapshot().tasks[i].schedule->startAt))hour=std::min(hour,static_cast<float>(s->hour));
        if(view.now&&std::find(dates.begin(),dates.end(),today)!=dates.end())if(const auto n=local(view.now))hour=std::min(hour,static_cast<float>(n->hour));
        scroll=(hour-.5f)*hourH;
    }
    scroll=std::clamp(scroll,0.0f,a.maxScroll);a.scroll=scroll;
    c.Line(body.x,grid.y,body.Right(),grid.y,D2D1::ColorF(1,1,1,.12f));
    c.Clip(grid);
    for(int hour=0;hour<=24;++hour){
        const float y=grid.y+hour*hourH-scroll;
        if(y<grid.y-hourH||y>grid.Bottom()+hourH)continue;
        // Zoomed out: label every second or third hour.
        const int every=hourH>=24?1:hourH>=13?2:3;
        if(hour>0&&hour<24&&hour%every==0){wchar_t label[8]{};swprintf(label,8,L"%02d:00",hour);c.Text(label,{body.x,y-8,gutter-8,16},10,dim,DWRITE_FONT_WEIGHT_NORMAL,false);}
        c.Line(body.x+gutter,y,body.Right(),y,D2D1::ColorF(1,1,1,.07f));
    }
    for(std::size_t k=0;k<dates.size();++k)c.Line(column(k),grid.y,column(k),grid.Bottom(),D2D1::ColorF(1,1,1,.06f));
    for(std::size_t k=0;k<dates.size();++k){
        // Overlapping items share the column side by side.
        struct Block{std::size_t task;float start,end;int lane=0,lanes=1;};
        std::vector<Block> blocks;
        for(const auto i:timed[k]){
            const auto& t=view.todos->Snapshot().tasks[i];const auto s=local(t.schedule->startAt),e=local(t.schedule->endAt);
            if(!s)continue;
            const float start=s->date==dates[k]?s->hour*60.0f+s->minute:0.0f;
            const float end=e&&e->date==dates[k]?e->hour*60.0f+e->minute:1440.0f;
            blocks.push_back({i,start,std::max(end,start+15)});
        }
        std::sort(blocks.begin(),blocks.end(),[](const Block& p,const Block& q){return p.start<q.start;});
        for(std::size_t b=0;b<blocks.size();){
            std::size_t e=b;float end=blocks[b].end;std::vector<float> lanes;
            for(;e<blocks.size()&&(e==b||blocks[e].start<end);++e){
                auto& block=blocks[e];int lane=0;
                while(lane<static_cast<int>(lanes.size())&&lanes[static_cast<std::size_t>(lane)]>block.start)++lane;
                if(lane==static_cast<int>(lanes.size()))lanes.push_back(block.end);else lanes[static_cast<std::size_t>(lane)]=block.end;
                block.lane=lane;end=std::max(end,block.end);
            }
            for(std::size_t m=b;m<e;++m)blocks[m].lanes=static_cast<int>(lanes.size());
            b=e;
        }
        for(const auto& block:blocks){
            const auto& t=view.todos->Snapshot().tasks[block.task];
            const float laneW=(colW-4)/static_cast<float>(block.lanes);
            const CalendarRect r{column(k)+2+block.lane*laneW,grid.y+block.start/60*hourH-scroll+1,laneW-2,std::max(10.0f,(block.end-block.start)/60*hourH-2)};
            if(!Intersects(r,grid))continue;
            c.Rounded(r,6,Tint(t,accent,t.completed?.2f:.34f));
            c.Rounded({r.x,r.y+2,3,r.height-4},1.5f,Tint(t,accent,t.completed?.55f:1));
            const auto s=local(t.schedule->startAt),e=local(t.schedule->endAt);
            const auto foreground=Color(prefs.textColor,t.completed?.55f:1);
            c.Clip(Clipped(r,grid));
            const std::wstring start=s?Clock(*s):L"";
            if(r.height>=14)
            Title(c,(start.empty()||r.width<90?L"":start+L" ")+TodoWide(t.title),{r.x+8,r.y+2,r.width-10,16},12,foreground,DWRITE_FONT_WEIGHT_SEMI_BOLD,t.completed);
            if(r.height>=38&&s&&e)c.Text(start+L" - "+Clock(*e),{r.x+8,r.y+19,r.width-10,15},11,Color(prefs.textColor,.65f),DWRITE_FONT_WEIGHT_NORMAL,false,true);
            c.Unclip();
            const auto visible=Clipped(r,grid);
            CalendarAgendaRow row;row.task=block.task;row.row=visible;row.edit=visible;a.rows.push_back(row);
        }
        // The current time on today's column.
        if(dates[k]==today&&view.now)if(const auto n=local(view.now)){
            const float y=grid.y+(n->hour*60.0f+n->minute)/60*hourH-scroll;
            c.Line(column(k),y,column(k)+colW,y,D2D1::ColorF(0xff453au,1),1.5f);
            c.Rounded({column(k)-3,y-3,6,6},3,D2D1::ColorF(0xff453au,1));
        }
    }
    c.Unclip();
    if(!usable)c.Text(L"日程暂不可用",{grid.x,grid.y+12,grid.width,20},13,dim);
}
// Todo theme: the selected day, the next item and upcoming items around a compact
// month. Every row it draws is reported through the agenda layout so clicks
// reuse the calendar's complete/edit commands.
void DrawBoard(Canvas& c,const CalendarLayout& l,const calendar::CalendarModel& model,
    const calendar::Preferences& prefs,const CalendarRenderState& view,CalendarAgendaLayout* output){
    const auto text=Color(prefs.textColor),dim=Color(prefs.textColor,.6f),accent=Color(prefs.highlightColor);
    CalendarAgendaLayout a;a.area=l.content;a.list=l.content;
    const auto selected=model.Selected(),today=model.Today();
    const bool usable=view.todos&&view.todoAvailable;
    auto tasksOn=[&](calendar::Date d){return view.todos?view.todos->ForDay(d):std::vector<std::size_t>{};};
    auto task=[&](std::size_t i)->const todo::TaskView&{return view.todos->Snapshot().tasks[i];};
    auto local=[&](todo::Millis at){return view.time?view.time->ToLocal(at):std::optional<todo::LocalDateTime>{};};
    auto clock=[](const todo::LocalDateTime& s){wchar_t b[8]{};swprintf(b,8,L"%02d:%02d",s.hour,s.minute);return std::wstring(b);};
    auto dayName=[&](calendar::Date d){
        if(d==today)return std::wstring(L"今天");
        if(const auto next=calendar::AddDays(today,1);next&&d==*next)return std::wstring(L"明天");
        const int wd=calendar::Weekday(d);return std::wstring(L"周")+kDays[wd<0?0:wd];
    };
    auto when=[&](const todo::TaskView& t,calendar::Date d){
        if(!t.schedule)return dayName(d);
        const auto s=local(t.schedule->startAt);return s?dayName(s->date)+L" "+clock(*s):dayName(d);
    };
    // Heading: the selected day, with a way back to today.
    const int wd=calendar::Weekday(selected);
    c.Text(std::wstring(L"星期")+kDays[wd<0?0:wd],{l.boardHeading.x,l.boardHeading.y,l.boardHeading.width-60,30},24,text,DWRITE_FONT_WEIGHT_BOLD,false);
    c.Text(std::to_wstring(selected.year)+L"年"+std::to_wstring(selected.month)+L"月"+std::to_wstring(selected.day)+L"日",
        {l.boardHeading.x,l.boardHeading.y+32,l.boardHeading.width,20},14,dim,DWRITE_FONT_WEIGHT_NORMAL,false);
    c.Text(L"今天",l.boardToday,13,accent,DWRITE_FONT_WEIGHT_SEMI_BOLD);
    {   // 全 / 日 / 周 / 月
        const wchar_t* names[]={L"全",L"日",L"周",L"月"};const auto& v=l.boardViews;
        c.Rounded({v[0].x,v[0].y,v[3].Right()-v[0].x,v[0].height},8,D2D1::ColorF(1,1,1,.08f));
        for(int i=0;i<4;++i){const auto r=v[static_cast<std::size_t>(i)];const bool on=static_cast<int>(prefs.boardView)==i;
            if(on)c.Rounded({r.x+2,r.y+2,r.width-4,r.height-4},6,accent);
            c.Text(names[i],r,12,on?D2D1::ColorF(1,1,1,1):dim,DWRITE_FONT_WEIGHT_SEMI_BOLD);}
    }
    if(prefs.boardView!=calendar::BoardView::All){
        const auto plus=Color(prefs.highlightColor,view.todoAvailable?1.0f:.3f);
        const float px=l.create.x+l.create.width/2,py=l.create.y+l.create.height/2;
        c.Line(px-8,py,px+8,py,plus,2);c.Line(px,py-8,px,py+8,plus,2);
        a.area=a.list=l.boardBody;
        if(prefs.boardView==calendar::BoardView::Month)DrawBoardMonth(c,l,model,prefs,view,a);
        else DrawTimeGrid(c,l,model,prefs,view,a);
        if(!view.todoStatus.empty()&&!usable)c.Paragraph(view.todoStatus,{l.boardBody.x,l.boardBody.Bottom()-16,l.boardBody.width,16},11,D2D1::ColorF(1,.706f,.667f));
        if(output)*output=std::move(a);
        return;
    }
    // Next item: the earliest unfinished scheduled task that has not ended, up to two weeks ahead.
    std::optional<std::size_t> next;
    for(int k=0;k<14&&usable&&view.time&&view.now;++k){
        const auto d=calendar::AddDays(today,k);if(!d)break;
        for(const auto i:tasksOn(*d)){const auto& t=task(i);
            if(t.completed||!t.schedule||t.schedule->endAt<=view.now)continue;
            if(!next||t.schedule->startAt<task(*next).schedule->startAt)next=i;}
        if(next)break;
    }
    if(const auto card=l.boardNext;card.height>0){
        c.Rounded(card,12,next?Color(prefs.highlightColor,.2f):D2D1::ColorF(1,1,1,.06f));
        c.Text(L"下一项",{card.x+14,card.y+10,card.width-28,18},12,next?accent:dim,DWRITE_FONT_WEIGHT_SEMI_BOLD,false);
        const CalendarRect line{card.x+14,card.Bottom()-36,card.width-28,26};
        if(next){
            const auto& t=task(*next);std::wstring status;
            if(t.schedule->startAt<=view.now)status=L"进行中";
            else {
                const auto minutes=(t.schedule->startAt-view.now+59999)/60000;
                if(minutes<60)status=std::to_wstring(minutes)+L" 分钟后开始";
                else if(minutes<24*60)status=std::to_wstring(minutes/60)+L" 小时"+(minutes%60?L" "+std::to_wstring(minutes%60)+L" 分钟":L"")+L"后开始";
                else status=when(t,today);
            }
            const float sw=c.Measure(status,13,DWRITE_FONT_WEIGHT_NORMAL)+2;
            c.Text(TodoWide(t.title),{line.x,line.y,std::max(0.0f,line.width-sw-10),line.height},16,text,DWRITE_FONT_WEIGHT_SEMI_BOLD,false,true);
            c.Text(status,{line.Right()-sw,line.y,sw,line.height},13,dim,DWRITE_FONT_WEIGHT_NORMAL,false);
            CalendarAgendaRow row;row.task=*next;row.row=card;row.edit=card;a.rows.push_back(row);
        } else c.Text(usable?L"近期没有安排":L"日程暂不可用",line,14,dim,DWRITE_FONT_WEIGHT_NORMAL,false);
    }
    // The selected day's schedule: unfinished first, then by start time.
    if(const auto day=l.boardDay;day.height>24){
        auto list=tasksOn(selected);
        std::stable_sort(list.begin(),list.end(),[&](auto x,auto y){const auto& p=task(x);const auto& q=task(y);
            if(p.completed!=q.completed)return p.completed<q.completed;
            return (p.schedule?p.schedule->startAt:INT64_MAX)<(q.schedule?q.schedule->startAt:INT64_MAX);});
        c.Text(list.empty()?std::wstring(L"日程"):L"日程  "+std::to_wstring(list.size()),{day.x,day.y,day.width-36,18},12,dim,DWRITE_FONT_WEIGHT_SEMI_BOLD,false);
        if(list.empty())c.Text(usable?L"这一天没有安排。":L"日程暂不可用",{day.x,day.y+26,day.width,20},13,dim,DWRITE_FONT_WEIGHT_NORMAL,false);
        const float rowH=30;const CalendarRect area{day.x,day.y+24,day.width,std::max(0.0f,day.height-24)};
        a.dayMaxScroll=std::max(0.0f,list.size()*(rowH+4)-4-area.height);
        const float scroll=std::clamp(view.boardDayScroll,0.0f,a.dayMaxScroll);
        c.Clip(area);
        float y=area.y+2-scroll;
        for(const auto i:list){
            const CalendarRect r{day.x,y,day.width,rowH};y+=rowH+4;
            if(!Intersects(r,area))continue;
            const auto& t=task(i);
            c.Rounded(r,8,TodoColorRgb(t.color)?Mark(t.color,t.completed?.2f:.34f):D2D1::ColorF(1,1,1,.06f));
            if(TodoColorRgb(t.color))c.Rounded({r.x,r.y+5,3,r.height-10},1.5f,Mark(t.color,t.completed?.55f:1));
            const CalendarRect check{r.x+10,r.y+(rowH-16)/2,16,16};
            if(t.completed){
                c.Rounded(check,8,D2D1::ColorF(0x34c759u,1));
                c.Line(check.x+4,check.y+8.5f,check.x+7,check.y+11.5f,D2D1::ColorF(1,1,1,1),1.6f);
                c.Line(check.x+7,check.y+11.5f,check.x+12.5f,check.y+5,D2D1::ColorF(1,1,1,1),1.6f);
            } else c.Rounded(check,8,Color(prefs.textColor,t.projected?.35f:.7f),1.4f);
            const auto start=t.schedule?local(t.schedule->startAt):std::nullopt;
            const auto time=start?clock(*start):std::wstring{};
            const float tw=time.empty()?0:c.Measure(time,12,DWRITE_FONT_WEIGHT_NORMAL)+2;
            Title(c,TodoWide(t.title),{check.Right()+10,r.y,std::max(0.0f,r.Right()-check.Right()-20-tw),rowH},13,
                Color(prefs.textColor,t.completed?.55f:1),DWRITE_FONT_WEIGHT_MEDIUM,t.completed);
            if(tw>0)c.Text(time,{r.Right()-8-tw,r.y,tw,rowH},12,dim,DWRITE_FONT_WEIGHT_NORMAL,false);
            CalendarAgendaRow row;row.task=i;row.row=Clipped(r,area);row.edit=row.row;
            // A later occurrence is completed through its series, not ahead of time.
            if(!t.projected)row.check=Clipped({check.x-4,check.y-4,check.width+8,check.height+8},area);
            a.rows.push_back(row);
        }
        c.Unclip();
        if(a.dayMaxScroll>0)ScrollThumb(c,area,scroll,a.dayMaxScroll);
    }
    // Month on the right: today filled, the selected day ringed, a dot on days with todos.
    for(int i=0;i<7;++i)c.Text(kDays[(i+calendar::WeekStartsOn(prefs.theme))%7],{l.weekdays.x+static_cast<float>(i)*l.weekdays.width/7,l.weekdays.y,l.weekdays.width/7,l.weekdays.height},l.weekdayFont,dim);
    for(int i=0;i<42;++i){
        const auto& cell=model.Grid()[static_cast<std::size_t>(i)];
        const auto r=l.cells[static_cast<std::size_t>(i)];if(!cell.currentMonth||r.width<=0)continue;
        const float d=std::max(0.0f,std::min({r.width,r.height-6,l.numberDiameter}));
        const CalendarRect circle{r.x+(r.width-d)/2,r.y+(r.height-d)/2-2,d,d};
        if(cell.today)c.Rounded(circle,d/2,accent);
        else if(cell.date==selected)c.Rounded(circle,d/2,accent,1.6f);
        const int weekday=calendar::Weekday(cell.date);
        c.Text(std::to_wstring(cell.date.day),circle,l.dayFont,cell.today?D2D1::ColorF(1,1,1,1):Color(prefs.textColor,weekday==0||weekday==6?.72f:1),DWRITE_FONT_WEIGHT_SEMI_BOLD);
        if(!tasksOn(cell.date).empty())c.Rounded({r.x+r.width/2-2,std::min(circle.Bottom()+2,r.Bottom()-4),4,4},2,accent);
        if(view.keyboardFocus&&view.focus==i+3)c.Rounded(circle,d/2,text,1.5f);
    }
    // Upcoming: the next two weeks, two columns when there is room.
    if(const auto up=l.boardUpcoming;up.height>0){
        c.Line(up.x,up.y,up.Right(),up.y,D2D1::ColorF(1,1,1,.14f));
        c.Text(L"近期安排",{up.x,up.y+10,up.width-40,28},12,dim,DWRITE_FONT_WEIGHT_SEMI_BOLD,false);
        std::vector<std::pair<std::size_t,calendar::Date>> items;
        for(int k=0;k<14&&usable;++k){
            const auto d=calendar::AddDays(today,k);if(!d)break;
            auto list=tasksOn(*d);
            std::stable_sort(list.begin(),list.end(),[&](auto x,auto y){return (task(x).schedule?task(x).schedule->startAt:INT64_MAX)<(task(y).schedule?task(y).schedule->startAt:INT64_MAX);});
            for(const auto i:list){const auto& t=task(i);
                if(t.completed||(t.schedule&&view.now&&t.schedule->endAt<=view.now))continue;
                items.emplace_back(i,*d);}
        }
        const int columns=up.width>=520?2:1;const float itemH=42,gap=8,colGap=16;
        const float colW=(up.width-colGap*(columns-1))/columns;
        const CalendarRect area{up.x,up.y+42,up.width,std::max(0.0f,up.height-42)};
        const auto lines=(items.size()+columns-1)/columns;
        a.upcomingMaxScroll=std::max(0.0f,lines*(itemH+gap)-gap-area.height+2);
        const float scroll=std::clamp(view.boardUpcomingScroll,0.0f,a.upcomingMaxScroll);
        if(items.empty())c.Text(usable?L"未来两周没有安排":L"日程暂不可用",{up.x,up.y+44,up.width,20},13,dim,DWRITE_FONT_WEIGHT_NORMAL,false);
        c.Clip(area);
        for(int n=0;n<static_cast<int>(items.size());++n){
            const auto [i,d]=items[static_cast<std::size_t>(n)];const auto& t=task(i);
            const CalendarRect r{up.x+(n%columns)*(colW+colGap),area.y+2+(n/columns)*(itemH+gap)-scroll,colW,itemH};
            if(!Intersects(r,area))continue;
            c.Rounded({r.x,r.y+2,3,r.height-4},1.5f,TodoColorRgb(t.color)?Mark(t.color):D2D1::ColorF(1,1,1,.35f));
            c.Text(when(t,d),{r.x+12,r.y,r.width-12,16},11,dim,DWRITE_FONT_WEIGHT_NORMAL,false);
            c.Text(TodoWide(t.title),{r.x+12,r.y+17,r.width-12,22},14,text,DWRITE_FONT_WEIGHT_SEMI_BOLD,false,true);
            CalendarAgendaRow row;row.task=i;row.row=Clipped(r,area);row.edit=row.row;a.rows.push_back(row);
        }
        c.Unclip();
        if(a.upcomingMaxScroll>0)ScrollThumb(c,area,scroll,a.upcomingMaxScroll);
    }
    // New todo: at the top, beside 今天.
    {   const auto plus=Color(prefs.highlightColor,view.todoAvailable?1.0f:.3f);
        const float px=l.create.x+l.create.width/2,py=l.create.y+l.create.height/2;
        c.Line(px-8,py,px+8,py,plus,2);c.Line(px,py-8,px,py+8,plus,2);}
    if(!view.todoStatus.empty()&&!usable)c.Paragraph(view.todoStatus,{l.boardDay.x,l.boardDay.Bottom()-16,l.boardDay.width,16},11,D2D1::ColorF(1,.706f,.667f));
    if(output)*output=std::move(a);
}
}  // namespace
struct CalendarRenderer::State {
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Com<IDWriteFactory> write; Com<ID2D1Factory> draw; Com<IWICImagingFactory> wic;
    CalendarCssShadow pickerShadow;
    int shadowWidth=0,shadowHeight=0;unsigned shadowDpi=0;
    ~State() { wic.put(); draw.put(); write.put(); if(SUCCEEDED(apartment)) CoUninitialize(); }
};
CalendarRenderer::CalendarRenderer()=default;
CalendarRenderer::~CalendarRenderer()=default;
bool CalendarRenderer::Initialize() {
    if(state_) return true;
    auto candidate=std::make_unique<State>();
    if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(candidate->write.put())))||
       FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,candidate->draw.put()))||
       FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IWICImagingFactory),reinterpret_cast<void**>(candidate->wic.put())))) return false;
    state_=std::move(candidate); return true;
}
bool CalendarRenderer::Render(const calendar::Preferences& prefs,const calendar::CalendarModel& model,unsigned dpi,CalendarBitmap& bitmap,const CalendarRenderState& view,CalendarLayout* outputLayout,CalendarAgendaLayout* agenda) {
    if(!calendar::ValidPreferences(prefs)||dpi<48||dpi>768||!Initialize()) return false;
    const auto profile=*prefs.Active(); const float w=static_cast<float>(profile.width),h=static_cast<float>(profile.height);
    Canvas c(state_->write.get(),state_->draw.get(),state_->wic.get(),w,h,dpi); if(!c.ready) return false;
    auto l=CalculateCalendarLayout(prefs.theme,w,h,0,view.agendaOpen,prefs.boardView); const auto date=model.BrowseAnchor();
    const bool split=prefs.theme!=calendar::Theme::Spacious;
    const bool todoTheme=prefs.theme==calendar::Theme::Todo;
    const std::wstring month=std::to_wstring(date.month)+L"月",year=std::to_wstring(date.year)+L"年";
    std::wstring label=year+month;
    if(todoTheme&&prefs.boardView==calendar::BoardView::Week){
        const auto first=WeekStart(model.Selected(),calendar::WeekStartsOn(prefs.theme));const auto last=calendar::AddDays(first,6);
        label=std::to_wstring(first.month)+L"月"+std::to_wstring(first.day)+L"日 – "+
            (last&&last->month!=first.month?std::to_wstring(last->month)+L"月":L"")+(last?std::to_wstring(last->day)+L"日":L"");
    }
    const float labelGap=w<=600?4.0f:12.0f;
    const float monthWidth=c.Measure(month,l.monthFont),yearWidth=c.Measure(year,l.yearFont);
    const float titleWidth=split?c.Measure(label,l.monthFont):monthWidth+labelGap+yearWidth;
    l=CalculateCalendarLayout(prefs.theme,w,h,titleWidth,view.agendaOpen,prefs.boardView); if(outputLayout) *outputLayout=l;
    if(prefs.material==calendar::Material::Solid){
        // Even a fully transparent panel colour keeps blank areas clickable and draggable.
        c.Rounded({0,0,w,h},l.cornerRadius,D2D1::ColorF(0,0,0,1.0f/255));
        c.Rounded({0,0,w,h},l.cornerRadius,Color(prefs.panelColor));
        c.Rounded({.5f,.5f,w-1,h-1},l.cornerRadius-.5f,D2D1::ColorF(1,1,1,.08f),1);
    }
    // Alpha 1 preserves mouse hit testing over otherwise transparent blank areas.
    else c.Rounded({0,0,w,h},l.cornerRadius,D2D1::ColorF(0,0,0,1.0f/255));
    const auto text=Color(prefs.textColor),highlight=Color(prefs.highlightColor);
    auto covered=[&](CalendarRect r){return view.pickerVisible&&Intersects(r,l.picker);};
    if(view.hover==1&&!covered(l.title))c.Rounded(l.title,5,D2D1::ColorF(1,1,1,.07f));
    auto navigation=[&](CalendarRect r,bool next,bool disabled){
        if(covered(r)||r.width<=0)return;
        const int index=next?2:0;
        const bool active=!disabled&&view.pressedNavigation==index;
        const bool hover=!disabled&&(view.hover==index||(view.keyboardFocus&&view.focus==index));
        if(active||hover){
            c.Rounded(r,r.width/2,active?D2D1::ColorF(0,0,0,.12f):D2D1::ColorF(1,1,1,.08f));
            c.Rounded(r,r.width/2,D2D1::ColorF(1,1,1,active?.3f:.4f),1);
        }else if(!view.materialReady){
            c.Rounded(r,r.width/2,D2D1::ColorF(.706f,.773f,.788f,.14f));
            c.Rounded(r,r.width/2,D2D1::ColorF(1,1,1,.18f),1);
        }
        c.Chevron(r,next,Color(prefs.textColor,disabled?.3f:1));
    };
    navigation(l.previous,false,date.year==1&&date.month==1);
    navigation(l.next,true,date.year==9999&&date.month==12);
    if(!covered(l.title)) {
        if(split) c.Text(label,l.title,l.monthFont,text,DWRITE_FONT_WEIGHT_SEMI_BOLD,false);
        else { c.Text(month,{l.title.x,l.title.y,monthWidth,l.title.height},l.monthFont,text,DWRITE_FONT_WEIGHT_SEMI_BOLD,false); c.Text(year,{l.title.x+monthWidth+labelGap,l.title.y+l.monthFont*.19f,yearWidth,l.title.height-l.monthFont*.19f},l.yearFont,Color(prefs.textColor,.78f),DWRITE_FONT_WEIGHT_SEMI_BOLD,false); }
    }
    if(todoTheme) DrawBoard(c,l,model,prefs,view,agenda);
    else {
    const auto dim=Color(prefs.textColor,view.todoAvailable?1.0f:.3f);
    if(view.agendaOpen)c.Rounded(l.list,8,D2D1::ColorF(1,1,1,.16f));
    if(!covered(l.create)) { const float x=l.create.x+l.create.width/2,y=l.create.y+l.create.height/2; c.Line(x-7,y,x+7,y,dim,1.6f); c.Line(x,y-7,x,y+7,dim,1.6f); }
    if(!covered(l.list)) {
        const float x=l.list.x+l.list.width/2-7,y=l.list.y+l.list.height/2-7;
        c.Rounded({x,y,14,14},1,dim,1.6f); c.Line(x+9,y,x+9,y+14,dim,1.6f);
        c.Line(x+3,y+4,x+6,y+4,dim,1.6f);c.Line(x+3,y+8,x+6,y+8,dim,1.6f);
    }
    for(int i=0;i<7;++i) { CalendarRect r{l.weekdays.x+static_cast<float>(i)*l.weekdays.width/7,l.weekdays.y,l.weekdays.width/7,l.weekdays.height}; if(!covered(r)) c.Text(kDays[(i+calendar::WeekStartsOn(prefs.theme))%7],r,l.weekdayFont,Color(prefs.textColor,.72f),split?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_MEDIUM); }
    for(int i=0;i<42;++i) {
        const auto& day=model.Grid()[static_cast<std::size_t>(i)]; auto r=l.cells[static_cast<std::size_t>(i)]; if(covered(r)||(split&&!day.currentMonth)) continue;
        const auto lunar=calendar::LookupLunar(day.date);
        const auto tasks=view.todos?view.todos->ForDay(day.date):std::vector<std::size_t>{};
        const bool hasTodos=!tasks.empty();
        const int weekday=calendar::Weekday(day.date);const bool weekend=weekday==0||weekday==6;
        const float opacity=(day.currentMonth?1.0f:.38f)*(day.today?1.0f:weekend?.78f:1.0f);
        auto hi=l.Highlight(i);if(hasTodos){hi.y=r.y;hi.height=std::min(r.height,30.0f);}
        if(day.today) { c.Rounded(hi,8,highlight); auto inset=hi; inset.x+=.5f; inset.y+=.5f; inset.width-=1; inset.height-=1; c.Rounded(inset,7.5f,D2D1::ColorF(1,1,1,.22f),1); }
        else if(day.selectable&&day.date==model.Selected()) { hi.x+=1;hi.y+=1;hi.width-=2;hi.height-=2;c.Rounded(hi,7,highlight,2); }
        c.Clip(r);
        if(hasTodos){
            const float size=std::clamp(std::min(5+w*.0395f,(l.days.height/6-15)/1.3f),14.0f,24.0f);
            const float number=1.3f*size;
            c.Text(std::to_wstring(day.date.day),{r.x,r.y+1,r.width,number},size,Color(prefs.textColor,opacity),DWRITE_FONT_WEIGHT_SEMI_BOLD);
            const int capacity=std::clamp(static_cast<int>((r.height-number-4)/16),1,3);
            for(int j=0;j<std::min(capacity,static_cast<int>(tasks.size()));++j){
                const auto& task=view.todos->Snapshot().tasks[tasks[static_cast<std::size_t>(j)]];
                CalendarRect preview{r.x+2,r.y+1+number+2+j*16,r.width-4-(lunar.holiday&&view.agendaOpen&&w<=600?9:0),14};
                c.Rounded(preview,3,Mark(task.color,task.completed?.2f:.34f));c.Line(preview.x,preview.y,preview.x,preview.Bottom(),Mark(task.color,task.completed?.55f:1),2);
                c.Clip(preview);c.Paragraph(TodoWide(task.title),{preview.x+5,preview.y-1,preview.width-7,14},11,Color(prefs.textColor,task.completed?.55f:1),true,task.completed,false);c.Unclip();
            }
            if(tasks.size()>static_cast<std::size_t>(capacity)&&w>440)c.Text(L"+"+std::to_wstring(tasks.size()-capacity),{r.Right()-22,lunar.holiday?r.Bottom()-13:r.y+1,20,12},9,Color(prefs.textColor,.7f));
        }else{
            const float daySize=view.agendaOpen&&w<=600?13:l.dayFont;
            const float numberHeight=view.agendaOpen&&w<=600?20:l.numberDiameter;
            const bool festival=!lunar.festivals.empty()||!lunar.solarTerm.empty();
            const bool showLunar=!split||festival;
            // Reserve the caption line in every cell. Conditional group height
            // otherwise raises festival dates above their neighbours.
            const float lunarHeight=view.agendaOpen&&w<=600?12:l.lunarHeight;
            const float group=numberHeight+l.dayGap+lunarHeight;
            const float top=r.y+(r.height-group)/2;
            c.Text(std::to_wstring(day.date.day),{r.x,top,r.width,numberHeight},daySize,Color(prefs.textColor,opacity),split?static_cast<DWRITE_FONT_WEIGHT>(560):DWRITE_FONT_WEIGHT_SEMI_BOLD);
            const auto lunarColor=festival?D2D1::ColorF(0x81e6b7u,opacity):Color(prefs.textColor,opacity*.7f);
            // Original .day-lunar has 2px inline padding and text-overflow:
            // ellipsis. Centering an untrimmed line clips both Chinese ends.
            const float lunarSize=view.agendaOpen&&w<=600?8.0f:split?10.0f:w<=380||h<=390?9.0f:11.0f;
            if(showLunar)c.Text(TodoWide(std::string(lunar.label)),{r.x+2,top+numberHeight+l.dayGap,r.width-4,lunarHeight},lunarSize,lunarColor,DWRITE_FONT_WEIGHT_NORMAL,true,true);
        }
        if(lunar.holiday){const float size=w<=600?11.0f:16.0f;CalendarRect badge{r.Right()-size-1,view.agendaOpen&&w<=600?r.Bottom()-size:r.y,size,size};c.Rounded(badge,size/2,D2D1::ColorF(lunar.holiday->work?0xff7900u:0x3488ffu,day.currentMonth?1.0f:.38f));c.Text(lunar.holiday->work?L"班":L"休",badge,w<=600?8.0f:10.0f,D2D1::ColorF(1,1,1,day.currentMonth?1.0f:.38f));}
        c.Unclip();
        if(view.keyboardFocus&&view.focus==i+3) { auto focus=l.cells[static_cast<std::size_t>(i)]; focus.x+=1;focus.y+=1;focus.width-=2;focus.height-=2;c.Rounded(focus,8,highlight,2); }
    }
    if(split&&!view.agendaOpen) {
        const auto summary=model.SummaryDate(); const int elapsed=calendar::DayOfYear(summary),total=calendar::DaysInMonth(summary.year,2)==29?366:365;
        const float remainingFont=l.summaryRemainingFont;
        const float group=l.summaryFont*.9f+28+12+l.summaryLunarFont*(w<=480?1.5f:1.3f)+9+remainingFont*(w<=480?1.5f:1.3f);
        const float top=l.summary.y+(l.summary.height-l.summaryBottomPadding-group)/2;
        CalendarRect number{l.summary.x,top,l.summary.width,l.summaryFont*.9f};
        if(!covered(number)) c.Text(std::to_wstring(summary.day),number,l.summaryFont,text,static_cast<DWRITE_FONT_WEIGHT>(650));
        const auto lunar=calendar::LookupLunar(summary);
        const float lunarTop=number.Bottom()+28+12;
        c.Text(TodoWide(std::string(lunar.lunarDate))+L" 周"+kDays[calendar::Weekday(summary)],{l.summary.x,lunarTop,l.summary.width,l.summaryLunarFont*1.5f},l.summaryLunarFont,Color(prefs.textColor,.88f));
        CalendarRect remaining{l.summary.x,top+group-remainingFont*1.3f,l.summary.width,remainingFont*1.3f};
        if(!covered(remaining)) c.Text(std::to_wstring(elapsed)+L"/"+std::to_wstring(total)+L" 今年剩余"+std::to_wstring(total-elapsed)+L"天",remaining,remainingFont,Color(prefs.textColor,.58f));
    }
    if(view.agendaOpen)DrawAgenda(c,l,model,prefs,view,w,agenda);
    else if(agenda)*agenda={};
    }
    if(view.keyboardFocus&&(view.focus==45||view.focus==46))c.Rounded(view.focus==45?l.create:l.list,5,highlight,2);
    if(view.keyboardFocus&&view.focus==1&&!covered(l.title))c.Rounded(l.title,5,highlight,2);
    if(view.alignedEdges) {
        const auto tint=Color(prefs.highlightColor,.9f);const float inset=l.cornerRadius;
        if(view.alignedEdges&CalendarEdgeLeft)c.Line(1,inset,1,h-inset,tint,2);
        if(view.alignedEdges&CalendarEdgeRight)c.Line(w-1,inset,w-1,h-inset,tint,2);
        if(view.alignedEdges&CalendarEdgeTop)c.Line(inset,1,w-inset,1,tint,2);
        if(view.alignedEdges&CalendarEdgeBottom)c.Line(inset,h-1,w-inset,h-1,tint,2);
    }
    if(view.pickerVisible){
        const int width=static_cast<int>(std::lround(l.picker.width*dpi/96)),height=static_cast<int>(std::lround(l.picker.height*dpi/96));
        if(state_->shadowWidth!=width||state_->shadowHeight!=height||state_->shadowDpi!=dpi){
            state_->pickerShadow=MakeCalendarCssShadow(width,height,dpi/96.f);
            state_->shadowWidth=width;state_->shadowHeight=height;state_->shadowDpi=dpi;
        }
        c.Shadow(l.picker,state_->pickerShadow,dpi);
    }
    return c.Finish(bitmap);
}
bool CalendarRenderer::RenderPicker(const calendar::Preferences& prefs,const CalendarPickerRenderState& v,float width,float height,unsigned dpi,CalendarBitmap& bitmap) {
    if(!calendar::ValidPreferences(prefs)||dpi<48||dpi>768||!Initialize()) return false;
    Canvas c(state_->write.get(),state_->draw.get(),state_->wic.get(),width,height,dpi); if(!c.ready) return false;
    const bool compact=height<=203; const float p=compact?11.0f:15.0f,gap=compact?8.0f:12.0f,mg=compact?4.0f:6.0f,mh=compact?30.0f:32.0f;
    // Layered HWNDs need nonzero alpha in blank controls for mouse hit testing.
    c.Rounded({.5f,.5f,width-1,height-1},8,prefs.material==calendar::Material::Solid?Color(prefs.panelColor):
        v.materialReady?D2D1::ColorF(0,0,0,1.f/255):D2D1::ColorF(.706f,.773f,.788f,.14f));
    c.Rounded({.5f,.5f,width-1,height-1},8,D2D1::ColorF(1,1,1,.24f),1);
    const auto text=Color(prefs.textColor); const float available=width-2*p;
    const CalendarRect prev{p,p,30,30},next{width-p-30,p,30,30};
    const float yearLabelWidth=c.Measure(L"年",14,DWRITE_FONT_WEIGHT_NORMAL),yearGroupWidth=64+3+yearLabelWidth;
    const CalendarRect year{(width-yearGroupWidth)/2,p,64,30};
    c.Chevron(prev,false,Color(prefs.textColor,v.year==1?.3f:1)); c.Chevron(next,true,Color(prefs.textColor,v.year==9999?.3f:1));
    c.Text(v.editing?v.draft:std::to_wstring(v.year),year,17,text,DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.Text(L"年",{year.Right()+3,p,yearLabelWidth,30},14,text);
    if(v.focus==1) {
        const auto value=v.editing?v.draft:std::to_wstring(v.year);
        const float x=year.x+(year.width-c.Measure(value,17))/2;
        if(v.selectAll) c.Rounded({x,year.y+4,c.Measure(value,17),22},2,Color(prefs.highlightColor,.35f));
        else if(v.editing) {const float caret=x+c.Measure(value.substr(0,v.caret),17);c.Line(caret,year.y+5,caret,year.Bottom()-5,text);}
    }
    if(v.focus==1)c.Line(year.x,year.Bottom()-1,year.Right(),year.Bottom()-1,text);
    else if(v.focus==0||v.focus==2)c.Rounded(v.focus==0?prev:next,5,text,1);
    const float top=p+30+gap,cw=(available-3*mg)/4;
    for(int i=0;i<12;++i) { CalendarRect r{p+static_cast<float>(i%4)*(cw+mg),top+static_cast<float>(i/4)*(mh+mg),cw,mh};
        if(v.selected.year==v.year&&v.selected.month==i+1) c.Rounded(r,6,Color(prefs.highlightColor));
        if(v.today.year==v.year&&v.today.month==i+1) c.Rounded(r,6,D2D1::ColorF(1,1,1,.3f),1);
        if(v.focus==i+3) c.Rounded(r,6,text,1.5f);
        c.Text(std::to_wstring(i+1)+L"月",r,13,text,DWRITE_FONT_WEIGHT_MEDIUM);
    }
    const float footer=top+3*mh+2*mg+gap; c.Line(p,footer,width-p,footer,D2D1::ColorF(1,1,1,.16f));
    CalendarRect today{(width-96)/2,footer+(compact?8.0f:10.0f),96,28};
    if(v.focus==15) c.Rounded(today,6,text,1.5f);
    c.Text(L"回到本月",today,12,text,DWRITE_FONT_WEIGHT_MEDIUM);
    if(!c.Finish(bitmap))return false;
    for(unsigned y=0;y<bitmap.height;++y)for(unsigned x=0;x<bitmap.width;++x){
        auto& pixel=bitmap.pixels[static_cast<std::size_t>(y)*bitmap.width+x];
        const unsigned alpha=pixel>>24;
        const unsigned inset=static_cast<unsigned>(std::lround(CalendarCssInsetCoverage(x+.5f,y+.5f,
            static_cast<float>(bitmap.width),static_cast<float>(bitmap.height),dpi/96.f)*255));
        const unsigned extra=(inset*(255-alpha)+127)/255;
        pixel=((alpha+extra)<<24)|((((pixel>>16)&255)+extra)<<16)|((((pixel>>8)&255)+extra)<<8)|((pixel&255)+extra);
    }
    return true;
}
} // namespace lume::platform::windows
