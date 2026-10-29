#include "platform/windows/calendar/calendar_todo_form_layout.h"
#include "core/calendar/calendar_date.h"
#include <algorithm>
namespace lume::platform::windows {
float CalendarTodoFormLayout::MaximumScroll() const noexcept { return std::max(0.f,contentHeight-viewport.height); }
float CalendarTodoFormLayout::ScrollToReveal(float current, CalendarRect control) const noexcept {
    if(control.y<current) current=control.y;
    else if(control.Bottom()>current+viewport.height) current=control.Bottom()-viewport.height;
    return std::clamp(current,0.f,MaximumScroll());
}
CalendarTodoFormLayout CalculateTodoFormLayout(float width, float height) noexcept {
    CalendarTodoFormLayout out;
    const bool compact=height<=400;
    const float pad=compact?13.f:19.f, margin=compact?11.f:15.f;
    const float panelWidth=std::max(0.f,std::min(440.f,width-30.f));
    out.panel={(width-panelWidth)/2,margin,panelWidth,std::max(0.f,height-2*margin)};
    const float left=out.panel.x+pad, inner=std::max(0.f,panelWidth-2*pad);
    out.heading={left,margin+pad,inner,44};
    out.feedback={left,out.panel.Bottom()-pad-14,inner,14};
    out.footer={left,out.feedback.y-40,inner,32};
    out.viewport={left,out.heading.Bottom()+8,inner,std::max(0.f,out.footer.y-8-out.heading.Bottom()-8)};
    out.title={0,19,inner,33};out.start={76,60.5f,std::max(0.f,inner-76),34};
    out.end={76,106.5f,std::max(0.f,inner-76),34};out.reminder={76,152.5f,std::max(0.f,inner-76),34};
    out.repeat={76,198.5f,std::max(0.f,inner-76),34};
    out.colors={0,242,inner,65};out.notes={0,332,inner,40};
    return out;
}
CalendarRect PlaceTodoPicker(CalendarRect host, CalendarRect anchor, float width, float height) noexcept {
    width=std::min(width,std::max(0.f,host.width-16));height=std::min(height,std::max(0.f,host.height-16));
    float top=anchor.Bottom()+6+height<=host.Bottom()-8?anchor.Bottom()+6:anchor.y-height-6;
    return {std::clamp(anchor.x,host.x+8,host.Right()-width-8),std::clamp(top,host.y+8,host.Bottom()-height-8),width,height};
}
int TodoPickerWeekCount(int year, int month) noexcept {
    const int days=calendar::DaysInMonth(year,month), start=calendar::Weekday({year,month,1});
    return days>0&&start>=0?(start+days+6)/7:0;
}
float TodoDatePickerHeight(int year, int month, bool monthView) noexcept {
    return monthView?243.f:161.f+29.f*static_cast<float>(TodoPickerWeekCount(year,month));
}
}
