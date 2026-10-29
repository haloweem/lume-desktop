#pragma once
#include "core/calendar/calendar_types.h"
#include <array>
namespace lume::platform::windows {
// All geometry is DIP. No HWND, drawing objects or persisted state belongs here.
struct CalendarRect {
    float x=0,y=0,width=0,height=0;
    float Right() const noexcept { return x+width; }
    float Bottom() const noexcept { return y+height; }
    bool Contains(float px,float py) const noexcept { return px>=x&&py>=y&&px<Right()&&py<Bottom(); }
};
enum class CalendarHitKind { Blank,Previous,Title,Next,Create,List,Day,Today,View };
struct CalendarHit { CalendarHitKind kind=CalendarHitKind::Blank; int day=-1; };
struct CalendarLayout {
    CalendarRect content,header,weekdays,days,summary,agenda,previous,title,next,create,list,picker;
    // Todo theme ("待办日历"): date heading, today button, next-item card, the
    // selected day's schedule and the upcoming list below.
    CalendarRect boardHeading,boardToday,boardNext,boardDay,boardUpcoming;
    // The 全/日/周/月 switcher, and the area the day/week/month views fill.
    std::array<CalendarRect,4> boardViews{};
    CalendarRect boardBody;
    std::array<CalendarRect,42> cells{};
    float cornerRadius=22,monthFont=34,yearFont=18,weekdayFont=14,dayFont=22,summaryFont=140;
    float lunarHeight=16,numberDiameter=34,dayGap=2,summaryBottomPadding=20,summaryLunarFont=16,summaryRemainingFont=14;
    bool compactPicker=false;
    CalendarHit HitTest(float x,float y) const noexcept;
    CalendarRect Highlight(int index) const noexcept;
};
CalendarLayout CalculateCalendarLayout(calendar::Theme theme,float width,float height,float titleWidth=0,bool agendaOpen=false,
    calendar::BoardView view=calendar::BoardView::All) noexcept;
enum CalendarEdge : unsigned { CalendarEdgeNone=0,CalendarEdgeLeft=1,CalendarEdgeRight=2,CalendarEdgeTop=4,CalendarEdgeBottom=8 };
} // namespace lume::platform::windows
