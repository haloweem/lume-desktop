#include "platform/windows/calendar/calendar_layout.h"
#include "platform/windows/calendar/calendar_snap.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace lume::platform::windows;
using lume::calendar::Theme;
namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool Near(float a, float b) { return std::abs(a-b)<0.002f; }
void Rect(CalendarRect r,float x,float y,float w,float h) {
    Check(Near(r.x,x)&&Near(r.y,y)&&Near(r.width,w)&&Near(r.height,h),"reference rectangle");
}
void Inside(CalendarRect r,float w,float h) {
    Check(r.x>=0 && r.y>=0 && r.width>=0 && r.height>=0 && r.Right()<=w+0.01f && r.Bottom()<=h+0.01f,"bounded geometry");
}
}
int main() {
 try {
    auto a=CalculateCalendarLayout(Theme::Spacious,820,500);
    Rect(a.content,29,25,762,454); Rect(a.header,29,25,762,52);
    Rect(a.weekdays,29,87,762,24); Rect(a.days,29,121,762,358);
    Check(Near(a.cornerRadius,30.5555556f),"original responsive radius");
    Rect(a.picker,29,85,304,231);
    Rect(a.title,73,25,123.015625f,52);
    auto b=CalculateCalendarLayout(Theme::Split,820,500);
    Rect(b.content,27,25,766,454); Rect(b.header,27,25,766,48);
    Rect(b.summary,27,81,316.595745f,398);
    Rect(b.weekdays,365.595745f,81,427.404255f,24);
    Rect(b.days,365.595745f,113,427.404255f,366);
    Rect(b.picker,27,81,304,231);
    Rect(b.title,71,29,105.203125f,40);
    Rect(CalculateCalendarLayout(Theme::Spacious,320,320).title,43,17,73.859375f,32);
    Rect(CalculateCalendarLayout(Theme::Split,320,320).title,43,25,95.640625f,32);
    for(auto theme:{Theme::Spacious,Theme::Split}) for(int w:{320,360,380,480,600,680,820,1440})
      for(int h:{320,360,390,400,420,460,500,720}) {
        auto l=CalculateCalendarLayout(theme,static_cast<float>(w),static_cast<float>(h));
        for(auto r:{l.content,l.header,l.weekdays,l.days,l.summary,l.previous,l.title,l.next,l.create,l.list,l.picker}) Inside(r,static_cast<float>(w),static_cast<float>(h));
        for(auto r:l.cells) { Inside(r,static_cast<float>(w),static_cast<float>(h)); Check(r.width>0&&r.height>0,"all six rows remain visible"); }
        if(w<=600) Check(l.monthFont==20&&l.yearFont==12&&l.previous.width==26&&l.create.width==26,"final narrow header override");
        if(w<=360||h<=360) Check(l.picker.height==203,"compact picker");
        if(theme==Theme::Split&&w<=480) Check(l.header.height==48&&l.weekdays.height==24&&l.dayFont==14&&l.summaryFont==56,"split final CSS cascade");
        auto center=l.cells[17]; Check(l.HitTest(center.x+center.width/2,center.y+center.height/2).day==17,"cell hit test");
        auto hi=l.Highlight(17); Check(hi.width<=52&&hi.height<=52,"square highlight maximum");
      }
    // The Todo board has a different header and body geometry from the two
    // original calendar themes. Keep its four views bounded independently so
    // a narrow window cannot move controls or month cells outside the widget.
    for(auto view:{lume::calendar::BoardView::All,lume::calendar::BoardView::Day,
                   lume::calendar::BoardView::Week,lume::calendar::BoardView::Month})
      for(int w:{320,380,440,480,600,820,1440}) for(int h:{320,360,390,420,500,720}) {
        const auto l=CalculateCalendarLayout(Theme::Todo,static_cast<float>(w),static_cast<float>(h),0,false,view);
        for(auto r:{l.content,l.boardHeading,l.boardToday,l.boardNext,l.boardDay,l.boardUpcoming,
                    l.boardBody,l.create,l.picker,l.weekdays,l.days})
            Inside(r,static_cast<float>(w),static_cast<float>(h));
        for(const auto& r:l.boardViews) { Inside(r,static_cast<float>(w),static_cast<float>(h)); Check(r.width>0&&r.height>0,"todo view switcher remains usable"); }
        for(const auto& r:l.cells) if(r.width>0&&r.height>0) Inside(r,static_cast<float>(w),static_cast<float>(h));
        Check(l.boardToday.width==52&&l.create.width==28,"todo action controls keep stable dimensions");
        Check(l.HitTest(l.boardToday.x+1,l.boardToday.y+1).kind==CalendarHitKind::Today,"todo today hit test");
        Check(l.HitTest(l.boardViews[static_cast<std::size_t>(view)].x+1,
                        l.boardViews[static_cast<std::size_t>(view)].y+1).kind==CalendarHitKind::View,
              "todo view switcher hit test");
        if(view==lume::calendar::BoardView::Month) {
            Check(l.weekdays.width>0&&l.days.height>0,"todo month grid remains visible");
            Check(l.HitTest(l.cells[17].x+l.cells[17].width/2,l.cells[17].y+l.cells[17].height/2).kind==CalendarHitKind::Day,
                  "todo month cell hit test");
        } else if(view!=lume::calendar::BoardView::All) {
            Check(l.boardBody.width>0&&l.boardBody.height>0,"todo time view body remains visible");
        } else {
            Check(l.boardNext.width>0&&l.boardDay.width>0,"todo all view schedule panes remain visible");
        }
      }
    // Snap calculations deliberately use physical pixels, independent of DPI.
    CalendarSnapState snap;
    std::vector<CalendarRect> targets{{500,100,200,200}};
    auto move=ResolveCalendarMove({193,100,300,200},targets,{0,0,1920,1080},snap);
    Check(move.x==192 && snap.horizontal.active,"8px adjacency snap");
    move=ResolveCalendarMove({203,100,300,200},targets,{0,0,1920,1080},snap);
    Check(move.x==192,"sticky match held through 12 physical px");
    move=ResolveCalendarMove({205,100,300,200},targets,{0,0,1920,1080},snap);
    Check(move.x==205,"sticky match releases beyond 12px");
    snap={}; move=ResolveCalendarMove({5,800,300,200},{},{0,0,1920,1080},snap);
    Check(move.x==0,"work area move snapping");
    auto alignment=DetectCalendarResize({191,100,300,200},CalendarEdgeRight,targets);
    Check(alignment.horizontal.active,"resize highlights alignment within 2px");
    alignment=DetectCalendarResize({189,100,300,200},CalendarEdgeRight,targets);
    Check(!alignment.horizontal.active,"resize has no 8px move threshold");
    alignment=DetectCalendarResize({0,0,300,200},CalendarEdgeLeft,{});
    Check(!alignment.horizontal.active,"resize never snaps work area");
    snap={}; move=ResolveCalendarMove({193,311,300,200},targets,{-500,-500,2500,2000},snap);
    Check(move.x==193,"nonadjacent widgets excluded beyond perpendicular gap 10");
    std::cout<<"Calendar layout breakpoints and physical snapping passed\n";
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
