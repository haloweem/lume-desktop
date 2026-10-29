#include "core/calendar/lunar_calendar.h"

#include <cstdint>

namespace lume::calendar {
namespace {
#include "core/calendar/lunar_data.inc"

constexpr int DaysBeforeYear(int year) noexcept {
    const int previous = year - 1;
    return 365 * previous + previous / 4 - previous / 100 + previous / 400;
}

std::string_view String(std::uint16_t index) noexcept {
    return kStringPool + kStringOffsets[index];
}
}  // namespace

LunarDetails LookupLunar(Date date) {
    if (date.year < 1900 || date.year > 2100 || !IsValidDate(date)) return {};
    const int index = DaysBeforeYear(date.year) - DaysBeforeYear(1900) + DayOfYear(date) - 1;
    const auto& packed = kDetails[kDays[index]];
    LunarDetails result;
    result.label = String(packed.label);
    result.lunarDate = String(packed.lunarDate);
    result.solarTerm = String(packed.solarTerm);
    const auto& festivals = kFestivalLists[packed.festivals];
    result.festivals.reserve(festivals.count);
    for (std::uint16_t i = 0; i < festivals.count; ++i) {
        result.festivals.push_back(String(kFestivalItems[festivals.offset + i]));
    }
    if (packed.holiday != 0) {
        const auto& holiday = kHolidays[packed.holiday];
        result.holiday = LunarHoliday{String(holiday.name), holiday.work != 0};
    }
    return result;
}
}  // namespace lume::calendar
