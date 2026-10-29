#pragma once
#include "core/weather/weather_provider.h"
#include <vector>
namespace lume::weather {
using PlaceNormalizer = std::function<std::string(std::string_view)>;
using DeviceLocation = std::function<std::optional<Location>(Millis)>;
struct LocationResult {
    std::optional<Location> location;
    ProviderError error = ProviderError::Unavailable;
};
// What the computer itself says about where it is, used to recognise an IP
// result that is really a VPN or proxy exit: Windows region and locale
// countries (ISO 3166-1 alpha-2, upper case) and the system wall clock.
struct LocationEnvironment {
    std::vector<std::string> countries;
    std::function<std::optional<std::string>(Millis)> localTime;  // YYYY-MM-DDTHH:mm
};
// Persists the last trusted automatic location across restarts.
struct LocationStore {
    std::function<std::optional<std::string>()> load;
    std::function<bool(std::string_view)> save;
};
// Worker-owned. Named cities need no network. Automatic location prefers the
// system (Wi-Fi) fix, which a VPN cannot move; IP geolocation goes direct,
// drops results whose country or UTC offset disagree with the computer, and
// only moves a trusted location after two consecutive far results.
class WeatherLocation final {
public:
    WeatherLocation(HttpTransport, Clock, LocalTime, PlaceNormalizer, DeviceLocation = {},
                    LocationEnvironment = {}, LocationStore = {});
    LocationResult Resolve(std::string_view cityId, const Cancellation&);
    // Forces the next Resolve to look again; the trusted location is kept.
    void Invalidate();
private:
    enum class Origin { Device, Ip };
    struct Trusted {
        Location location;
        Origin origin = Origin::Ip;
        Millis at = 0;
    };
    void Restore();
    void Accept(Location, Origin, Millis now);
    HttpTransport transport_;
    Clock clock_;
    LocalTime local_;
    PlaceNormalizer normalize_;
    DeviceLocation device_;
    LocationEnvironment environment_;
    LocationStore store_;
    std::optional<Trusted> trusted_;
    std::optional<Location> pending_;
    Millis checkedAt_ = 0, validUntil_ = 0;
    bool restored_ = false;
};
}
