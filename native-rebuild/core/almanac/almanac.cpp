#include "core/almanac/almanac.h"
#include "core/calendar/lunar_calendar.h"
#include <cstdint>
namespace lume::almanac {
namespace {
#include "core/almanac/almanac_data.inc"
int BeforeYear(int year){const int y=year-1;return y*365+y/4-y/100+y/400;}
std::string_view String(std::uint16_t index){return kStrings+kOffsets[index];}
std::vector<std::string_view> Activities(std::uint16_t index){
    const auto span=kLists[index];std::vector<std::string_view> out;out.reserve(span.count);
    for(std::uint32_t i=0;i<span.count;++i)out.push_back(String(kItems[span.offset+i]));
    return out;
}
}
std::optional<Day> Lookup(calendar::Date date){
    if(date.year<1900||date.year>2100||!calendar::IsValidDate(date))return {};
    const auto index=BeforeYear(date.year)-BeforeYear(1900)+calendar::DayOfYear(date)-1;
    constexpr std::string_view week[]={u8"日",u8"一",u8"二",u8"三",u8"四",u8"五",u8"六"};
    Day day;day.lunarDate=calendar::LookupLunar(date).lunarDate;
    day.year=String(kYears[kDayCycles[index*3]]);
    day.cycle=std::string(String(kCycles[kDayCycles[index*3+1]]))+u8"月 "+
        std::string(String(kCycles[kDayCycles[index*3+2]]))+u8"日 周"+std::string(week[calendar::Weekday(date)]);
    day.yi=Activities(kDayLists[index*2]);day.ji=Activities(kDayLists[index*2+1]);
    return day;
}
}
