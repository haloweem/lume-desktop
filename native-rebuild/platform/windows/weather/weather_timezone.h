#pragma once
#include "core/weather/weather.h"
#include <memory>
namespace lume::platform::windows {
// Worker-owned, one cached IANA calendar. The Windows ICU module and calendar
// are released when the weather worker stops; invalid zones never become GMT.
class WeatherTimezone final {
public:
    WeatherTimezone();
    ~WeatherTimezone();
    WeatherTimezone(const WeatherTimezone&) = delete;
    WeatherTimezone& operator=(const WeatherTimezone&) = delete;
    std::optional<std::string> Local(weather::Millis utc, std::string_view ianaZone);
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
