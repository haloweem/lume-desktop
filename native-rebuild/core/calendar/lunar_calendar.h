#pragma once

#include "core/calendar/calendar_date.h"

#include <optional>
#include <string_view>
#include <vector>

namespace lume::calendar {
struct LunarHoliday {
    std::string_view name;
    bool work = false;
};

struct LunarDetails {
    std::string_view label;
    std::string_view lunarDate;
    std::vector<std::string_view> festivals;
    std::string_view solarTerm;
    std::optional<LunarHoliday> holiday;
};

// UTF-8 views refer to immutable program-lifetime storage. Invalid Gregorian
// dates and dates outside 1900-01-01 through 2100-12-31 return empty details.
// Holidays reproduce only the source's published schedules, never predictions.
LunarDetails LookupLunar(Date date);
}  // namespace lume::calendar
