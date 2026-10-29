#include "platform/windows/weather/weather_timezone.h"
#include <cstdlib>
#include <iostream>
using namespace lume::weather;
using lume::platform::windows::WeatherTimezone;
namespace {
void Expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
}
int main() {
    WeatherTimezone zone;
    const auto january = *ParseUtc("2026-01-15T03:35:00Z");
    const auto july = *ParseUtc("2026-07-15T03:35:00Z");
    Expect(zone.Local(january, "Asia/Shanghai") == "2026-01-15T11:35", "named Chinese city timezone");
    Expect(zone.Local(january, "America/New_York") == "2026-01-14T22:35" &&
        zone.Local(july, "America/New_York") == "2026-07-14T23:35",
        "weather location follows winter and summer offsets independently from Windows local zone");
    Expect(zone.Local(*ParseUtc("2026-03-08T06:59:00Z"), "America/New_York") == "2026-03-08T01:59" &&
        zone.Local(*ParseUtc("2026-03-08T07:00:00Z"), "America/New_York") == "2026-03-08T03:00",
        "spring DST gap follows actual location");
    Expect(zone.Local(*ParseUtc("2026-11-01T05:59:00Z"), "America/New_York") == "2026-11-01T01:59" &&
        zone.Local(*ParseUtc("2026-11-01T06:00:00Z"), "America/New_York") == "2026-11-01T01:00",
        "autumn DST repeated hour is preserved");
    Expect(zone.Local(january, "Asia/Kathmandu") == "2026-01-15T09:20", "quarter hour timezone");
    Expect(zone.Local(july, "UTC") == "2026-07-15T03:35", "explicit UTC fallback supported");
    for (auto name : {"", "Invented/Place", "America/New_York\r\n", "UTCjunk", "../Asia/Shanghai", "GMT+25:00"})
        Expect(!zone.Local(january, name), "invalid timezone never silently uses GMT or system zone");
    Expect(zone.Local(july, "Asia/Shanghai") == "2026-07-15T11:35", "bad zone does not poison future requests");
    for (int i = 0; i < 100; ++i) {
        WeatherTimezone transient;
        Expect(transient.Local(january, "Asia/Shanghai").has_value(), "timezone can reopen after release");
    }
    std::cout << "Windows IANA timezone, DST, invalid zone and repeated lifecycle passed\n";
}
