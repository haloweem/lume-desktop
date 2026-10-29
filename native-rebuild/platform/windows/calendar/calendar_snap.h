#pragma once
#include "platform/windows/calendar/calendar_layout.h"
#include "core/widget/snap_geometry.h"
#include <vector>
namespace lume::platform::windows {
// Physical pixel policy, shared by drag and resize without owning a window.
using CalendarSnapMatch=widget::SnapMatch;
using CalendarSnapState=widget::SnapState;
CalendarRect ResolveCalendarMove(CalendarRect source,const std::vector<CalendarRect>& targets,CalendarRect workArea,CalendarSnapState& state);
CalendarSnapState DetectCalendarResize(CalendarRect source,unsigned edges,const std::vector<CalendarRect>& targets);
}
