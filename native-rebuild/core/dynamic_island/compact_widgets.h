#pragma once
#include <array>
#include <string_view>
namespace lume::dynamic_island {
enum class CompactWidget { None,Lunar,Calendar,Date,Weekday,Clock,DayProgress,WeekProgress,MonthProgress,
    QuarterProgress,YearProgress,Network,Power,Battery,Cpu,Memory,Disk,CpuMemory,Files,
    Weather,TemperatureRange,Humidity,WindSpeed,WindDirection,Uv,SunriseSunset,Pressure,
    Temperature,Visibility,FeelsLike,Precipitation,Count };
inline constexpr std::array<std::string_view,31> CompactWidgetIds{
    "none","lunar","calendar","date","weekday","clock","dayProgress","weekProgress","monthProgress",
    "quarterProgress","yearProgress","network","power","battery","cpu","memory","disk","cpuMemory","files",
    "weather","temperatureRange","humidity","windSpeed","windDirection","uv","sunriseSunset","pressure",
    "temperature","visibility","feelsLike","precipitation"};
inline constexpr bool ValidCompactWidget(int value){return value>=0&&value<static_cast<int>(CompactWidget::Count);}
inline CompactWidget ParseCompactWidget(std::string_view id) {
    for(std::size_t i=0;i<CompactWidgetIds.size();++i)if(CompactWidgetIds[i]==id)return static_cast<CompactWidget>(i);
    return CompactWidget::Count;
}
inline std::string_view CompactWidgetId(CompactWidget widget) {
    return ValidCompactWidget(static_cast<int>(widget))?CompactWidgetIds[static_cast<std::size_t>(widget)]:"none";
}
}
