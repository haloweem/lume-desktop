#include "core/weather/weather_location.h"
#include "weather_fixtures.h"
#include <deque>
#include <cstdlib>
#include <iostream>
using namespace lume::weather;
using namespace weather_fixture;
namespace {
void Expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
struct Harness {
    Millis now = Now;
    std::deque<HttpResponse> replies;
    std::vector<HttpRequest> requests;
    Cancellation cancel;
    bool cancelOnRequest = false;
    WeatherLocation location{
        [&](const HttpRequest& r, const Cancellation&) {
            requests.push_back(r);
            if (cancelOnRequest) cancel.Cancel();
            Expect(!replies.empty(), "no unexpected location request");
            auto result = std::move(replies.front()); replies.pop_front(); return result;
        }, [&] { return now; }, Local, [](std::string_view text) { return std::string(text); }};
    LocationResult Resolve(std::string_view city = "auto") { return location.Resolve(city, cancel); }
};
struct DeviceHarness {
    Millis now = Now;
    std::deque<HttpResponse> replies;
    std::vector<HttpRequest> requests;
    Cancellation cancel;
    std::optional<Location> deviceLocation = Location{31.385123, 120.981234, {}, {}};
    WeatherLocation location{
        [&](const HttpRequest& r, const Cancellation&) {
            requests.push_back(r);
            Expect(!replies.empty(), "no unexpected device location request");
            auto result = std::move(replies.front()); replies.pop_front(); return result;
        }, [&] { return now; }, Local, [](std::string_view text) { return std::string(text); },
        [&](Millis) { return deviceLocation; }};
    LocationResult Resolve() { return location.Resolve("auto", cancel); }
};
HttpResponse Good(const Json& value) { return {200, value.dump(), {}, {}, {}}; }
HttpResponse Ip(std::string name = u8"昆山", std::string zone = "Asia/Shanghai") {
    return Good({{"city", name}, {"latitude", 31.385123}, {"longitude", 120.981234}, {"timezone", zone}});
}
void FixedAndCached() {
    Harness h;
    auto named = h.Resolve("beijing");
    Expect(named.location && named.location->latitude == 39.9042 &&
        named.location->timezone == "Asia/Shanghai" && h.requests.empty(), "named city never uses IP geolocation");
    Expect(h.Resolve("fictional").error == ProviderError::InvalidLocation && h.requests.empty(), "invalid city rejected");
    h.replies = {Ip()};
    auto first = h.Resolve();
    Expect(first.location && first.location->name == u8"昆山" &&
        first.location->longitude == 120.981234 && h.requests.size() == 1 &&
        h.requests[0].url == "https://api.ip.sb/geoip" && h.requests[0].direct, "original primary IP provider, coordinates and a direct (unproxied) request");
    h.now += 6 * Hour - 60000;
    Expect(h.Resolve().location.has_value() && h.requests.size() == 1, "IP location cached for 6 hours");
    h.now += 60000;
    h.replies = {Ip(u8"上海")};
    Expect(h.Resolve().location->name == u8"上海" && h.requests.size() == 2, "IP cache expires on boundary");
    h.now -= 60000;
    h.replies = {Ip()};
    Expect(h.Resolve().location->name == u8"昆山" && h.requests.size() == 3, "clock rollback does not retain future IP cache");
}
void FallbackAndNames() {
    Harness h;
    h.replies = {HttpResponse{}, Good({{"city", "Kunshan"}, {"loc", "31.385123,120.981234"}, {"timezone", "Asia/Shanghai"}}),
        Good({{"locality", u8"昆山市"}, {"city", u8"苏州市"}})};
    auto result = h.Resolve();
    Expect(result.location && result.location->name == u8"昆山市" && h.requests.size() == 3 &&
        h.requests[1].url == "https://ipinfo.io/json" &&
        h.requests[2].url.find("https://api-bdc.io/data/reverse-geocode-client?") == 0 &&
        h.requests[1].direct && !h.requests[2].direct,
        "IP fallback and Chinese reverse-geocoding preserve original priority; only IP lookups go direct");
    Harness secondary;
    secondary.replies = {Ip("Kunshan"), HttpResponse{}, Good({{"address", {{"county", u8"昆山市"}}}})};
    Expect(secondary.Resolve().location->name == u8"昆山市" &&
        secondary.requests[2].url.find("https://nominatim.openstreetmap.org/reverse?") == 0,
        "secondary reverse-geocoder used when primary is unavailable");
    Harness noName;
    noName.replies = {Ip("Unknown"), HttpResponse{}, HttpResponse{}};
    Expect(noName.Resolve().location->name == u8"当前位置", "failed translation still retains valid coordinates");
    Harness noZone;
    noZone.replies = {Good({{"city", u8"昆山"}, {"latitude",31}, {"longitude",120}})};
    Expect(noZone.Resolve().location->timezone == "UTC", "missing automatic timezone falls back explicitly to UTC");
    Harness badZone;
    badZone.replies = {Ip(u8"昆山", "Invented/Place")};
    const auto fallbackZone = badZone.Resolve();
    Expect(fallbackZone.location && fallbackZone.location->timezone == "UTC" && badZone.requests.size() == 1,
        "original discards invalid timezone without discarding real coordinates");
}
void InvalidAndCancelled() {
    for (const Json& bad : {Json{{"city",u8"昆山"},{"latitude",999},{"longitude",120}},
        Json{{"city",u8"昆山"},{"loc","31.2,120.5junk"}}, Json{{"city",u8"昆山"},{"latitude","31"},{"longitude",120}},
        Json{{"city",u8"昆山"},{"latitude",31},{"longitude",120},{"success",false}}}) {
        Harness h; h.replies = {Good(bad), Ip()};
        Expect(h.Resolve().location.has_value() && h.requests.size() == 2,
            "invalid coordinates and provider errors cannot become a cached location");
    }
    Harness failed;
    failed.replies = {HttpResponse{}, HttpResponse{}};
    Expect(!failed.Resolve().location, "no silent fallback to a made-up city");
    Harness cancelled; cancelled.cancel.Cancel();
    Expect(cancelled.Resolve().error == ProviderError::Cancelled && cancelled.requests.empty(), "cancelled location performs no IO");
    Harness during; during.cancelOnRequest = true; during.replies = {Ip()};
    Expect(during.Resolve().error == ProviderError::Cancelled && during.requests.size() == 1,
        "cancelled IP result cannot enter cache or trigger secondary requests");
}
void DeviceLocationWins() {
    DeviceHarness h;
    h.replies = {Good({{"locality", u8"昆山市"}})};
    const auto result = h.Resolve();
    Expect(result.location && result.location->name == u8"昆山市" &&
        h.requests.size() == 1 &&
        h.requests[0].url.find("https://api-bdc.io/data/reverse-geocode-client?") == 0,
        "system location is preferred over proxy-dependent IP geolocation");
}
void DeviceLocationUnavailableFallsBack() {
    DeviceHarness h;
    h.deviceLocation.reset();
    h.replies = {Ip()};
    const auto result = h.Resolve();
    Expect(result.location && result.location->name == u8"昆山" &&
        h.requests.size() == 1 && h.requests[0].url == "https://api.ip.sb/geoip",
        "unavailable system location preserves the original IP fallback");
}
// A computer in China (region CN, UTC+8) behind a VPN or proxy.
struct ChinaHarness {
    Millis now = Now;
    std::deque<HttpResponse> replies;
    std::vector<HttpRequest> requests;
    Cancellation cancel;
    std::optional<Location> deviceLocation;
    std::optional<std::string> stored;
    WeatherLocation location{
        [&](const HttpRequest& r, const Cancellation&) {
            requests.push_back(r);
            Expect(!replies.empty(), "no unexpected location request");
            auto result = std::move(replies.front()); replies.pop_front(); return result;
        }, [&] { return now; }, Local, [](std::string_view text) { return std::string(text); },
        [&](Millis) { return deviceLocation; },
        LocationEnvironment{{"CN"}, [](Millis time) { return Local(time, "Asia/Shanghai"); }},
        LocationStore{[&] { return stored; }, [&](std::string_view text) { stored = std::string(text); return true; }}};
    LocationResult Resolve() { return location.Resolve("auto", cancel); }
};
HttpResponse IpAt(std::string name, double latitude, double longitude, std::string zone, std::string country) {
    Json value{{"city", name}, {"latitude", latitude}, {"longitude", longitude}, {"timezone", zone}};
    if (!country.empty()) value["country_code"] = country;
    return Good(value);
}
HttpResponse Tokyo() { return IpAt(u8"东京", 35.6893, 139.6899, "Asia/Tokyo", "JP"); }
HttpResponse HongKong() { return IpAt(u8"香港", 22.3193, 114.1694, "Asia/Hong_Kong", "HK"); }
HttpResponse Kunshan() { return IpAt(u8"昆山", 31.385123, 120.981234, "Asia/Shanghai", "CN"); }
HttpResponse Beijing() { return IpAt(u8"北京", 39.9042, 116.4074, "Asia/Shanghai", "CN"); }
void ProxyExitsRejected() {
    ChinaHarness h;
    h.replies = {Tokyo(), HongKong()};
    const auto exits = h.Resolve();
    Expect(!exits.location && h.requests.size() == 2,
        "IP results from another country (VPN exits, even in the same UTC offset) never become the city");
    h.now += 10 * 60000;
    h.replies = {Good({{"city", u8"东京"}, {"latitude", 35.6893}, {"longitude", 139.6899}, {"timezone", "Asia/Tokyo"}}),
        Kunshan()};
    const auto real = h.Resolve();
    Expect(real.location && real.location->name == u8"昆山" && h.requests.size() == 4,
        "a result whose UTC offset disagrees with the computer is dropped; the real address is used");
}
void FarMoveNeedsConfirmation() {
    ChinaHarness h;
    h.replies = {Kunshan()};
    Expect(h.Resolve().location->name == u8"昆山", "first trusted IP location");
    h.now += 6 * Hour;
    h.replies = {Beijing()};
    Expect(h.Resolve().location->name == u8"昆山" && h.requests.size() == 2, "a single far result does not move the city");
    h.now += 9 * 60000;
    Expect(h.Resolve().location->name == u8"昆山" && h.requests.size() == 2, "the unconfirmed result waits before retrying");
    h.now += 60000;
    h.replies = {Beijing()};
    Expect(h.Resolve().location->name == u8"北京" && h.requests.size() == 3, "the same far result twice moves the city");
    h.now += 6 * Hour;
    h.replies = {IpAt(u8"昌平", 40.2207, 116.2312, "Asia/Shanghai", "CN")};
    Expect(h.Resolve().location->name == u8"昌平", "nearby results update at once");
}
void DeviceFixOutranksIp() {
    ChinaHarness h;
    h.deviceLocation = Location{31.385123, 120.981234, u8"昆山", "Asia/Shanghai"};
    Expect(h.Resolve().location->name == u8"昆山" && h.requests.empty(), "system fix needs no IP lookup");
    h.deviceLocation.reset();
    h.now += 30 * 60000;
    Expect(h.Resolve().location->name == u8"昆山" && h.requests.empty(),
        "a recent system fix is kept when the system service is briefly unavailable");
    h.now += Day;
    h.replies = {Beijing()};
    Expect(h.Resolve().location->name == u8"昆山", "after a day, IP may move it only with confirmation");
    h.deviceLocation = Location{39.9042, 116.4074, u8"北京", "Asia/Shanghai"};
    h.now += 10 * 60000;
    Expect(h.Resolve().location->name == u8"北京", "a new system fix moves the city at once");
}
void TrustedLocationPersists() {
    ChinaHarness first;
    first.replies = {Kunshan()};
    Expect(first.Resolve().location && first.stored, "trusted location is saved");
    ChinaHarness restarted;
    restarted.stored = first.stored;
    restarted.replies = {Tokyo(), Tokyo()};
    const auto offline = restarted.Resolve();
    Expect(offline.location && offline.location->name == u8"昆山",
        "after a restart behind a VPN, the saved city is kept instead of the exit's");
    restarted.now += 10 * 60000;
    restarted.replies = {Beijing()};
    Expect(restarted.Resolve().location->name == u8"昆山", "the restored city is an anchor for far moves");
    restarted.location.Invalidate();
    restarted.replies = {HttpResponse{}, HttpResponse{}};
    Expect(restarted.Resolve().location->name == u8"昆山", "refresh keeps the trusted city when lookups fail");
    ChinaHarness corrupt;
    corrupt.stored = std::string("{\"version\":1,\"latitude\":999}");
    corrupt.replies = {HttpResponse{}, HttpResponse{}};
    Expect(!corrupt.Resolve().location, "a corrupt saved location is ignored");
}
}
int main() {
    FixedAndCached(); FallbackAndNames(); InvalidAndCancelled();
    DeviceLocationWins(); DeviceLocationUnavailableFallsBack();
    ProxyExitsRejected(); FarMoveNeedsConfirmation(); DeviceFixOutranksIp(); TrustedLocationPersists();
    std::cout << "weather automatic location, fixed cities, Chinese names, cache and cancellation passed\n";
}
