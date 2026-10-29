#pragma once
#include "platform/windows/weather/weather_worker.h"
#include "core/weather/weather_location.h"
#include <optional>
#include <filesystem>
namespace lume::platform::windows {
enum class WeatherCacheKind { Display, Provider, Location };
struct WeatherCachePort {
    std::function<std::optional<std::string>(WeatherCacheKind, std::size_t)> read;
    std::function<bool(WeatherCacheKind, std::string_view)> write;
};
// No filesystem access until read/write. Files are fixed names within this
// directory; reads are bounded and writes replace atomically.
WeatherCachePort LocalWeatherCache(std::filesystem::path directory);
weather::Millis WeatherNow();
struct WeatherBackendOptions {
    WeatherCachePort cache;
    weather::HttpTransport transport;
    weather::Clock clock;
    weather::LocalTime local;
    // Tests disable the system (Wi-Fi) fix and supply the computer's country
    // and clock; empty uses Windows.
    bool systemLocation = true;
    std::optional<weather::LocationEnvironment> environment;
};
// Empty transport/clock/local use real WinHTTP, system UTC and Windows ICU.
// Construct this factory's result on WeatherWorker, never on the UI thread.
std::unique_ptr<IWeatherBackend> CreateWeatherBackend(WeatherBackendOptions = {});
}
