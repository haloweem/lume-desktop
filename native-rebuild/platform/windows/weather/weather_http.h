#pragma once
#include "core/weather/weather_provider.h"
namespace lume::platform::windows {
// Each request owns its WinHTTP session and async operation. Idle weather owns
// no HTTP handles. Cancellation is checked while waiting and between reads.
class WeatherHttp final {
public:
    explicit WeatherHttp(bool allowLoopbackForTests = false) : loopback_(allowLoopbackForTests) {}
    weather::HttpResponse Get(const weather::HttpRequest&, const weather::Cancellation&) const;
private:
    bool loopback_;
};
} // namespace lume::platform::windows
