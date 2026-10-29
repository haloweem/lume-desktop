#include "platform/windows/calendar/calendar_snap.h"
namespace lume::platform::windows {
CalendarRect ResolveCalendarMove(CalendarRect s,const std::vector<CalendarRect>& t,CalendarRect work,CalendarSnapState& state) {
    return widget::ResolveWidgetMove(s,t,work,state);
}
CalendarSnapState DetectCalendarResize(CalendarRect s,unsigned edges,const std::vector<CalendarRect>& t) {
    return widget::DetectWidgetResize(s,edges,t);
}
}
