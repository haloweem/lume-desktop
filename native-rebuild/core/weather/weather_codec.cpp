#include "core/weather/weather.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace lume::weather {
namespace {
using Json = nlohmann::json;
[[noreturn]] void Invalid() { throw std::runtime_error("Invalid weather data"); }
Json Parse(std::string_view text, std::size_t max) {
    if (text.empty() || text.size() > max) Invalid();
    return Json::parse(text.begin(), text.end(), [](int depth, Json::parse_event_t, Json&) {
        if (depth > 32) Invalid();
        return true;
    });
}
double Number(const Json& value, double min, double max) {
    if (!value.is_number()) Invalid();
    const double number = value.get<double>();
    if (!std::isfinite(number) || number < min || number > max) Invalid();
    return number;
}
int Code(const Json& value) {
    const double number = Number(value, 0, 999);
    if (number != std::floor(number)) Invalid();
    return static_cast<int>(number);
}
Millis Timestamp(const Json& value) {
    if (!value.is_string()) Invalid();
    const auto time = ParseUtc(value.get_ref<const std::string&>());
    if (!time || *time < 0) Invalid();
    return *time;
}
Millis Milliseconds(const Json& value) {
    if (!value.is_number_integer() || (value.is_number_unsigned() &&
        value.get<std::uint64_t>() > 253402300799999ULL)) Invalid();
    const auto time = value.get<Millis>();
    if (time < 0 || time > 253402300799999LL) Invalid();
    return time;
}
std::string Text(const Json& value) { return value.get<std::string>(); }
bool Ends(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}
struct Condition { int code; std::string label; std::optional<bool> day; };
Condition MetCondition(const Json& value) {
    const auto text = Text(value);
    std::string plain = text;
    std::optional<bool> day;
    for (auto suffix : {"_day", "_night", "_polartwilight"}) {
        if (Ends(plain, suffix)) {
            plain.resize(plain.size() - std::char_traits<char>::length(suffix));
            if (std::string_view(suffix) != "_polartwilight") day = std::string_view(suffix) == "_day";
            break;
        }
    }
    const bool thunder = Ends(plain, "andthunder");
    if (thunder) plain.resize(plain.size() - 10);
    if (plain.compare(0, 7, "lightss") == 0) plain.erase(5, 1);
    static const std::map<std::string, int> symbols{
        {"clearsky",0},{"fair",1},{"partlycloudy",2},{"cloudy",3},{"fog",45},
        {"lightrain",61},{"rain",63},{"heavyrain",65},{"lightrainshowers",80},{"rainshowers",81},
        {"heavyrainshowers",82},{"lightsnow",71},{"snow",73},{"heavysnow",75},
        {"lightsnowshowers",85},{"snowshowers",85},{"heavysnowshowers",86},
        {"lightsleet",71},{"sleet",73},{"heavysleet",75},
        {"lightsleetshowers",85},{"sleetshowers",85},{"heavysleetshowers",86}
    };
    const auto found = symbols.find(plain);
    if (found == symbols.end()) Invalid();
    const int code = thunder ? 95 : found->second;
    return {code, thunder ? u8"雷阵雨" : plain.find("sleet") != std::string::npos ? u8"雨夹雪" : ConditionLabel(code), day};
}
Location ReadLocation(const Json& value) {
    return {Number(value.at("latitude"), -90, 90), Number(value.at("longitude"), -180, 180),
        Text(value.at("name")), Text(value.at("timezone"))};
}
}
std::optional<Snapshot> ParseMet(std::string_view body, const Location& location,
    std::string_view cityId, Millis now, Millis fetchedAt, const LocalTime& local) {
    try {
        if (!ValidLocation(location) || !ValidCity(cityId) || !local ||
            now < 0 || now > 253402300799999LL || fetchedAt < 0 || fetchedAt > now) return {};
        const auto root = Parse(body, ProviderMaxBytes);
        const auto& properties = root.at("properties");
        const auto published = Timestamp(properties.at("meta").at("updated_at"));
        if (published > now + 10 * 60000 || now - published > 12 * Hour) return {};
        const auto& series = properties.at("timeseries");
        if (!series.is_array() || series.size() < 6 || series.size() > 300) return {};
        std::vector<Millis> times;
        times.reserve(series.size());
        for (const auto& point : series) {
            const auto time = Timestamp(point.at("time"));
            if ((!times.empty() && time <= times.back()) || !point.at("data").is_object()) return {};
            times.push_back(time);
        }
        const auto after = std::upper_bound(times.begin(), times.end(), now);
        const auto index = after == times.begin() ? 0 :
            static_cast<std::size_t>(after - times.begin() - 1);
        if (std::abs(times[index] - now) > Hour || series.size() - index < 6) return {};
        Snapshot result;
        result.cityId = cityId;
        result.cityName = location.name.empty() ? u8"当前位置" : location.name;
        result.location = location;
        result.timezone = location.timezone.empty() ?
            (cityId == "auto" ? "UTC" : "Asia/Shanghai") : location.timezone;
        result.location.timezone = result.timezone;
        result.source = Source::MetNorway;
        result.sourceUrl = "https://api.met.no/weatherapi/locationforecast/2.0/compact";
        result.updatedAt = fetchedAt;
        result.sourceUpdatedAt = published;
        result.validAt = times[index];
        result.remainingDay = true;
        auto localTime = [&](Millis time) {
            const auto formatted = local(time, result.timezone);
            if (!formatted || !ParseLocalMinute(*formatted, 0)) Invalid();
            return *formatted;
        };
        for (std::size_t i = 0; i < 6; ++i) {
            if (times[index + i] != times[index] + static_cast<Millis>(i) * Hour) return {};
            const auto& data = series[index + i].at("data");
            const auto temperature = Number(data.at("instant").at("details").at("air_temperature"), -100, 70);
            const auto state = MetCondition(data.at("next_1_hours").at("summary").at("symbol_code"));
            result.hourly.push_back({localTime(times[index + i]), temperature, state.code, state.label, state.day});
        }
        for (std::size_t i = index; i < series.size(); ++i) {
            const auto date = localTime(times[i]).substr(0, 10);
            auto found = std::find_if(result.forecast.begin(), result.forecast.end(),
                [&](const auto& day) { return day.date == date; });
            if (found == result.forecast.end() && result.forecast.size() == 3) break;
            const auto& data = series[i].at("data");
            const auto temperature = Number(data.at("instant").at("details").at("air_temperature"), -100, 70);
            if (found != result.forecast.end()) {
                found->high = std::max(found->high, temperature);
                found->low = std::min(found->low, temperature);
            } else {
                const auto& period = data.contains("next_1_hours") ? data.at("next_1_hours") :
                    data.contains("next_6_hours") ? data.at("next_6_hours") : data.at("next_12_hours");
                const auto state = MetCondition(period.at("summary").at("symbol_code"));
                result.forecast.push_back({date, temperature, temperature, state.code, state.label});
            }
        }
        const auto& details = series[index].at("data").at("instant").at("details");
        result.temperature = result.hourly.front().temperature;
        result.humidity = Number(details.at("relative_humidity"), 0, 100);
        result.windSpeed = Number(details.at("wind_speed"), 0, 150) * 3.6;
        result.weatherCode = result.hourly.front().weatherCode;
        result.condition = result.hourly.front().condition;
        const int hour = std::stoi(result.hourly.front().time.substr(11, 2));
        result.isDay = result.hourly.front().isDay.value_or(hour >= 6 && hour < 18);
        if (!ValidSnapshot(result)) return {};
        return result;
    } catch (const std::exception&) { return {}; }
}
std::optional<std::string> EncodeCache(const Snapshot& value) {
    try {
        if (!ValidSnapshot(value)) return {};
        Json snapshot{
            {"cityId", value.cityId}, {"cityName", value.cityName},
            {"location", {{"latitude", value.location.latitude}, {"longitude", value.location.longitude},
                {"name", value.location.name}, {"timezone", value.location.timezone}}},
            {"source", value.source == Source::Qweather ? "qweather-page" : "met-norway"},
            {"dataKind", value.source == Source::Qweather ? "observation" : "forecast"},
            {"sourceUrl", value.sourceUrl}, {"timezone", value.timezone},
            {"updatedAt", value.updatedAt}, {"sourceUpdatedAt", value.sourceUpdatedAt}, {"validAt", value.validAt},
            {"temperature", value.temperature}, {"humidity", value.humidity},
            {"weatherCode", value.weatherCode}, {"condition", value.condition}, {"isDay", value.isDay},
            {"hourly", Json::array()}, {"forecast", Json::array()}
        };
        if (value.remainingDay) snapshot["forecastRange"] = "remaining-day";
        if (value.apparentTemperature) snapshot["apparentTemperature"] = *value.apparentTemperature;
        if (value.windSpeed) snapshot["windSpeed"] = *value.windSpeed;
        for (const auto& hour : value.hourly) {
            Json row{{"time", hour.time}, {"temperature", hour.temperature}, {"weatherCode", hour.weatherCode},
                {"condition", hour.condition}};
            if (hour.isDay) row["isDay"] = *hour.isDay;
            snapshot["hourly"].push_back(row);
        }
        for (const auto& day : value.forecast)
            snapshot["forecast"].push_back({{"date", day.date}, {"high", day.high}, {"low", day.low},
                {"weatherCode", day.weatherCode}, {"condition", day.condition}});
        auto text = Json{{"version", 2}, {"city", value.cityId}, {"snapshot", snapshot}}.dump();
        return text.size() <= SnapshotMaxBytes ? std::optional<std::string>(std::move(text)) : std::nullopt;
    } catch (const std::exception&) { return {}; }
}
std::optional<Snapshot> DecodeCache(std::string_view text, std::string_view cityId, Millis now) {
    try {
        if (!ValidCity(cityId) || now < 0 || now > 253402300799999LL) return {};
        const auto root = Parse(text, SnapshotMaxBytes);
        if (root.at("version") != 2 || Text(root.at("city")) != cityId) return {};
        const auto& input = root.at("snapshot");
        Snapshot result;
        result.cityId = Text(input.at("cityId"));
        if (result.cityId != cityId) return {};
        result.cityName = Text(input.at("cityName"));
        result.location = ReadLocation(input.at("location"));
        const auto source = Text(input.at("source"));
        if (source != "qweather-page" && source != "met-norway") return {};
        result.source = source == "qweather-page" ? Source::Qweather : Source::MetNorway;
        if (Text(input.at("dataKind")) != (result.source == Source::Qweather ? "observation" : "forecast")) return {};
        result.sourceUrl = Text(input.at("sourceUrl"));
        result.timezone = Text(input.at("timezone"));
        result.updatedAt = Milliseconds(input.at("updatedAt"));
        result.sourceUpdatedAt = Milliseconds(input.at("sourceUpdatedAt"));
        result.validAt = Milliseconds(input.at("validAt"));
        if (result.updatedAt > now || now - result.updatedAt > Day) return {};
        result.temperature = Number(input.at("temperature"), -100, 70);
        result.humidity = Number(input.at("humidity"), 0, 100);
        if (input.contains("apparentTemperature")) result.apparentTemperature = Number(input.at("apparentTemperature"), -120, 100);
        if (input.contains("windSpeed")) result.windSpeed = Number(input.at("windSpeed"), 0, 540);
        result.weatherCode = Code(input.at("weatherCode"));
        result.condition = Text(input.at("condition"));
        result.isDay = input.at("isDay").get<bool>();
        if (input.contains("forecastRange")) {
            if (Text(input.at("forecastRange")) != "remaining-day") return {};
            result.remainingDay = true;
        }
        const auto& hours = input.at("hourly");
        const auto& days = input.at("forecast");
        if (!hours.is_array() || hours.size() > 6 || !days.is_array() || days.empty() || days.size() > 3) return {};
        for (const auto& hour : hours) {
            ForecastHour entry{Text(hour.at("time")), Number(hour.at("temperature"), -100, 70),
                Code(hour.at("weatherCode")), Text(hour.at("condition")), {}};
            if (hour.contains("isDay")) entry.isDay = hour.at("isDay").get<bool>();
            result.hourly.push_back(std::move(entry));
        }
        for (const auto& day : days)
            result.forecast.push_back({Text(day.at("date")), Number(day.at("high"), -100, 70),
                Number(day.at("low"), -100, 70), Code(day.at("weatherCode")), Text(day.at("condition"))});
        if (!ValidSnapshot(result)) return {};
        return result;
    } catch (const std::exception&) { return {}; }
}
} // namespace lume::weather
