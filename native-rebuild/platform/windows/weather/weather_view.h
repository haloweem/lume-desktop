#pragma once
#include "platform/windows/weather/weather_worker.h"
#include "platform/windows/widget_ui/widget_canvas.h"
namespace lume::platform::windows {
struct WeatherLayout {
    float left=0,top=0,contentWidth=0,hourWidth=0,hourTop=0,hourHeight=0;
};
WeatherLayout WeatherLayoutFor(float width,float height);
unsigned WeatherIconResource(int code,bool day);
std::wstring WeatherTemperature(double value);
std::wstring WeatherHourLabel(std::string_view);
std::wstring WeatherStatus(const WeatherReading&);
std::wstring WeatherText(std::string_view);
// Fit only the displayed label; callers retain the full source for tooltips.
std::wstring FitWeatherLabel(WidgetCanvas&,const std::wstring&,float width);
void DrawWeather(WidgetCanvas&,float width,float height,const WeatherReading&,std::string_view city);
}
