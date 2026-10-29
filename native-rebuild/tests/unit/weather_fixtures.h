#pragma once
#include "core/weather/weather.h"
#include "vendor/nlohmann/json.hpp"
namespace weather_fixture {
using namespace lume::weather;
using Json = nlohmann::json;
constexpr Millis Now = 1789616100000; // 2026-09-17T03:35:00Z, fixed source fixture.
const Location Shanghai{31.385123, 120.981234, u8"昆山", "Asia/Shanghai"};
inline std::optional<std::string> Local(Millis time, std::string_view zone) {
    if (zone == "Asia/Shanghai" || zone == "Asia/Hong_Kong") return FormatOffset(time, 480);
    if (zone == "Asia/Tokyo") return FormatOffset(time, 540);
    if (zone == "America/New_York") return FormatOffset(time, -240); // fixture is in DST
    if (zone == "UTC") return FormatOffset(time, 0);
    return {};
}
inline Json MetBody() {
    Json points = Json::array();
    for (int i = 0; i < 80; ++i) {
        const auto time = *FormatOffset(Now - 95 * 60000 + i * Hour, 0) + ":00Z";
        points.push_back({{"time", time}, {"data", {
            {"instant", {{"details", {{"air_temperature", 20 + i % 12},
                {"relative_humidity", 60}, {"wind_speed", 4}}}}},
            {"next_1_hours", {{"summary", {{"symbol_code",
                i % 2 ? "lightrainshowers_day" : "partlycloudy_night"}}}}}
        }}});
    }
    return {{"properties", {{"meta", {{"updated_at", "2026-09-17T01:00:00Z"}}},
        {"timeseries", points}}}};
}
inline std::string PublicPage(int start = 3, std::string date = "2026-09-22", std::string observed = "02:30") {
    Json hours = Json::array();
    for (int i = 0; i < 24; ++i) {
        const int hour = (start + i) % 24;
        hours.push_back({{"time", std::to_string(hour > 12 ? hour - 12 : hour) + (hour > 12 ? "pm" : "am")},
            {"tmp", std::to_string(23 + i % 5)}, {"code", hour >= 6 && hour < 18 ? "101" : "151"}});
    }
    std::string page = u8"<h1 class='c-submenu__location'>昆山市</h1>"
        "<div class='c-city-weather-current'><p class='current-time'>" + date + " " + observed +
        u8"</p><div class='current-live'><div class='current-live__item'><img src='https://static.qweather.com/151.png'></div>"
        "<div class='current-live__item'><p>22°</p><p>多云</p></div></div>"
        "<div class='current-basic___item'><p>90%</p><p>相对湿度</p></div>"
        "<div class='current-basic___item'><p>24°</p><p>体感温度</p></div></div>"
        "<div class='c-city-weather-forecast'>";
    const auto midnight = *ParseLocalMinute(date + "T00:00", 0);
    for (int i = 0; i < 3; ++i) {
        const auto day = *FormatOffset(midnight + i * Day, 0);
        page += "<a class='city-forecast-tabs__row'><div class='date'><p>" + std::string(u8"今天") + "</p><p>" +
            day.substr(5, 2) + u8"月" + day.substr(8, 2) + u8"日</p></div>"
            "<img class='icon' src='https://static.qweather.com/101.png'><span class='temp'>29°</span><span class='temp last'>22°</span></a>";
    }
    return page + "</div><script>\nwindow.hour24 = " + hours.dump() + ";</script>";
}
}
