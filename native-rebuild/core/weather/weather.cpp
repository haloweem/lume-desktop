#include "core/weather/weather.h"
#include "core/calendar/calendar_date.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
namespace lume::weather {
namespace {
bool Text(std::string_view text, std::size_t max) {
    return !text.empty() && text.size() <= max &&
        std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool Number(double value, double min, double max) {
    return std::isfinite(value) && value >= min && value <= max;
}
bool Instant(Millis value) { return value >= 0 && value <= 253402300799999LL; }
int Digits(std::string_view value, std::size_t start, std::size_t length) {
    if (start + length > value.size()) return -1;
    int result = 0;
    for (std::size_t i = start; i < start + length; ++i) {
        if (value[i] < '0' || value[i] > '9') return -1;
        result = result * 10 + value[i] - '0';
    }
    return result;
}
int BeforeYear(int year) {
    const int previous = year - 1;
    return 365 * previous + previous / 4 - previous / 100 + previous / 400;
}
bool SourceUrl(const Snapshot& value) {
    if (value.sourceUrl.empty()) return true;
    if (value.source == Source::MetNorway)
        return value.sourceUrl == "https://api.met.no/weatherapi/locationforecast/2.0/compact";
    constexpr std::string_view prefix = "https://www.qweather.com/weather/";
    if (value.sourceUrl.compare(0, prefix.size(), prefix) != 0 ||
        value.sourceUrl.size() > 200 || value.sourceUrl.size() <= prefix.size() + 5 ||
        value.sourceUrl.substr(value.sourceUrl.size() - 5) != ".html") return false;
    const auto slug = std::string_view(value.sourceUrl).substr(prefix.size(),
        value.sourceUrl.size() - prefix.size() - 5);
    return std::all_of(slug.begin(), slug.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}
}
const std::array<City, 8>& Cities() {
    static const std::array<City, 8> cities{{
        {"beijing", u8"北京", 39.9042, 116.4074}, {"shanghai", u8"上海", 31.2304, 121.4737},
        {"guangzhou", u8"广州", 23.1291, 113.2644}, {"shenzhen", u8"深圳", 22.5431, 114.0579},
        {"hangzhou", u8"杭州", 30.2741, 120.1551}, {"chengdu", u8"成都", 30.5728, 104.0668},
        {"wuhan", u8"武汉", 30.5928, 114.3055}, {"nanjing", u8"南京", 32.0603, 118.7969}
    }};
    return cities;
}
bool ValidCity(std::string_view id) {
    return id == "auto" || std::any_of(Cities().begin(), Cities().end(),
        [id](const auto& city) { return city.id == id; });
}
std::optional<Location> NamedLocation(std::string_view id) {
    for (const auto& city : Cities())
        if (city.id == id) return Location{city.latitude, city.longitude, city.name, "Asia/Shanghai"};
    return {};
}
bool ValidLocation(const Location& value) {
    return Number(value.latitude, -90, 90) && Number(value.longitude, -180, 180) &&
        (value.name.empty() || Text(value.name, 240)) &&
        (value.timezone.empty() || Text(value.timezone, 128));
}
std::optional<Millis> ParseLocalMinute(std::string_view text, int offsetMinutes) {
    if (text.size() != 16 || text[4] != '-' || text[7] != '-' ||
        text[10] != 'T' || text[13] != ':' || offsetMinutes < -1440 || offsetMinutes > 1440) return {};
    calendar::Date date{Digits(text, 0, 4), Digits(text, 5, 2), Digits(text, 8, 2)};
    const int hour = Digits(text, 11, 2), minute = Digits(text, 14, 2);
    if (!calendar::IsValidDate(date) || hour < 0 || hour > 23 || minute < 0 || minute > 59) return {};
    const auto days = BeforeYear(date.year) + calendar::DayOfYear(date) - 1 - BeforeYear(1970);
    return days * Day + hour * Hour + (minute - offsetMinutes) * 60000LL;
}
std::optional<Millis> ParseUtc(std::string_view text) {
    if (text.size() < 20 || text.size() > 30 || text.back() != 'Z' || text[16] != ':') return {};
    const auto minute = ParseLocalMinute(text.substr(0, 16), 0);
    const int second = Digits(text, 17, 2);
    if (!minute || second < 0 || second > 59) return {};
    int fractional = 0;
    if (text.size() != 20) {
        if (text[19] != '.' || text.size() < 22) return {};
        for (std::size_t i = 20; i < text.size() - 1; ++i) {
            if (text[i] < '0' || text[i] > '9') return {};
            if (i < 23) fractional += (text[i] - '0') * (i == 20 ? 100 : i == 21 ? 10 : 1);
        }
    }
    return *minute + second * 1000 + fractional;
}
std::optional<std::string> FormatOffset(Millis time, int offsetMinutes) {
    // Leave a day on either side for explicit offset conversion; arithmetic stays bounded.
    if (time < -62135683200000LL || time > 253402387199999LL ||
        offsetMinutes < -1440 || offsetMinutes > 1440) return {};
    const auto local = time + offsetMinutes * 60000LL;
    auto days = local / Day;
    auto remaining = local % Day;
    if (remaining < 0) { --days; remaining += Day; }
    const auto date = calendar::AddDays({1970, 1, 1}, static_cast<int>(days));
    if (!date) return {};
    char text[24]{};
    std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d", date->year, date->month,
        date->day, static_cast<int>(remaining / Hour), static_cast<int>((remaining % Hour) / 60000));
    return std::string(text);
}
std::string ConditionLabel(int code) {
    static const std::map<int, const char*> labels{
        {0,u8"晴"},{1,u8"晴间多云"},{2,u8"多云"},{3,u8"阴"},{45,u8"雾"},{48,u8"雾凇"},
        {51,u8"小毛毛雨"},{53,u8"毛毛雨"},{55,u8"强毛毛雨"},{56,u8"轻冻毛毛雨"},{57,u8"冻毛毛雨"},
        {61,u8"小雨"},{63,u8"中雨"},{65,u8"大雨"},{66,u8"轻冻雨"},{67,u8"冻雨"},
        {71,u8"小雪"},{73,u8"中雪"},{75,u8"大雪"},{77,u8"米雪"},{80,u8"小阵雨"},
        {81,u8"阵雨"},{82,u8"强阵雨"},{85,u8"小阵雪"},{86,u8"强阵雪"},
        {95,u8"雷雨"},{96,u8"雷雨伴冰雹"},{99,u8"强雷雨伴冰雹"}
    };
    const auto found = labels.find(code);
    return found == labels.end() ? u8"未知天气" : found->second;
}
bool ValidSnapshot(const Snapshot& value) {
    const auto code = [](int c) { return c >= 0 && c <= 999; };
    if (!ValidCity(value.cityId) || !ValidLocation(value.location) || !Text(value.cityName, 240) ||
        !Text(value.timezone, 128) || !Instant(value.updatedAt) || !Instant(value.sourceUpdatedAt) ||
        !Instant(value.validAt) || !Number(value.temperature, -100, 70) || !Number(value.humidity, 0, 100) ||
        (value.apparentTemperature && !Number(*value.apparentTemperature, -120, 100)) ||
        (value.windSpeed && !Number(*value.windSpeed, 0, 540)) || !code(value.weatherCode) ||
        !Text(value.condition, 160) || !SourceUrl(value) ||
        value.hourly.size() > 6 || value.forecast.empty() || value.forecast.size() > 3 ||
        (value.source != Source::Qweather && value.source != Source::MetNorway) ||
        (value.source == Source::Qweather && (value.remainingDay || value.timezone != "Asia/Shanghai"))) return false;
    for (const auto& hour : value.hourly)
        if (!ParseLocalMinute(hour.time, 0) || !Number(hour.temperature, -100, 70) ||
            !code(hour.weatherCode) || !Text(hour.condition, 160)) return false;
    for (std::size_t i = 0; i < value.forecast.size(); ++i) {
        const auto& day = value.forecast[i];
        if (day.date.size() != 10 || !ParseLocalMinute(day.date + "T00:00", 0) ||
            !Number(day.high, -100, 70) || !Number(day.low, -100, day.high) ||
            !code(day.weatherCode) || !Text(day.condition, 160) ||
            (i && day.date <= value.forecast[i - 1].date)) return false;
    }
    return true;
}
}
