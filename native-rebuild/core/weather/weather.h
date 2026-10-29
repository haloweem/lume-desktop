#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lume::weather {
using Millis = std::int64_t;
constexpr Millis Hour = 3600000;
constexpr Millis Day = 24 * Hour;
constexpr std::size_t ProviderMaxBytes = 512 * 1024;
constexpr std::size_t SnapshotMaxBytes = 128 * 1024;
struct Location {
    double latitude = 0, longitude = 0;
    std::string name, timezone;
};
struct City {
    const char* id;
    const char* name;
    double latitude, longitude;
};
const std::array<City, 8>& Cities();
bool ValidCity(std::string_view);
std::optional<Location> NamedLocation(std::string_view);
bool ValidLocation(const Location&);
enum class Source { Qweather, MetNorway };
struct ForecastHour {
    std::string time;
    double temperature = 0;
    int weatherCode = 999;
    std::string condition;
    std::optional<bool> isDay;
};
struct ForecastDay {
    std::string date;
    double high = 0, low = 0;
    int weatherCode = 999;
    std::string condition;
};
struct Snapshot {
    std::string cityId, cityName;
    Location location;
    Source source = Source::MetNorway;
    std::string sourceUrl, timezone;
    Millis updatedAt = 0, sourceUpdatedAt = 0, validAt = 0;
    double temperature = 0, humidity = 0;
    std::optional<double> apparentTemperature, windSpeed;
    int weatherCode = 999;
    std::string condition;
    bool isDay = true, remainingDay = false;
    std::vector<ForecastHour> hourly;
    std::vector<ForecastDay> forecast;
};
// A platform timezone port, never the computer's implicit local timezone.
// Input is UTC milliseconds, output is YYYY-MM-DDTHH:mm in the requested IANA zone.
using LocalTime = std::function<std::optional<std::string>(Millis, std::string_view)>;
std::optional<Millis> ParseUtc(std::string_view);
std::optional<Millis> ParseLocalMinute(std::string_view, int offsetMinutes);
std::optional<std::string> FormatOffset(Millis, int offsetMinutes);
std::string ConditionLabel(int code);
bool ValidSnapshot(const Snapshot&);

std::optional<Snapshot> ParseMet(std::string_view body, const Location&,
    std::string_view cityId, Millis now, Millis fetchedAt, const LocalTime&);
// slug is the page's location (e.g. "tianhe-101280109"); when given, a district
// is shown with its city ("广州 · 天河区").
std::optional<Snapshot> ParseQweather(std::string_view html, const Location&,
    std::string_view cityId, Millis now, Millis fetchedAt, std::string_view slug = {});
// One validated display snapshot, matched to city and bounded to 24 hours.
std::optional<std::string> EncodeCache(const Snapshot&);
std::optional<Snapshot> DecodeCache(std::string_view text, std::string_view cityId, Millis now);
} // namespace lume::weather
