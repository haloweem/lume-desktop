#include "weather_fixtures.h"
#include "vendor/nlohmann/json.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace lume::weather;
using namespace weather_fixture;
using Json = nlohmann::json;
namespace {
void Expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
void TimeAndCities() {
    Expect(ParseUtc("2026-09-17T03:35:00Z") == Now, "strict UTC time in milliseconds");
    Expect(ParseUtc("2026-09-17T03:35:00.123Z") == Now + 123, "fractional UTC seconds");
    Expect(ParseLocalMinute("2026-09-17T11:35", 480) == Now, "explicit local offset");
    Expect(FormatOffset(Now, -240) == "2026-09-16T23:35", "timezone midnight conversion");
    Expect(FormatOffset(-1, 0) == "1969-12-31T23:59", "negative timestamps floor correctly");
    for (auto text : {"2026-02-29T00:00:00Z", "2026-09-17T24:00:00Z", "2026-09-17T03:35:60Z",
         "2026-09-17T03:35:00+08:00", "2026-09-17T03:35:00Zjunk", "2026-09-17T03:35:00.Z"})
        Expect(!ParseUtc(text), "reject invalid or ambiguous UTC dates");
    Expect(ParseUtc("2024-02-29T00:00:00Z").has_value(), "leap day accepted");
    Expect(!FormatOffset(std::numeric_limits<Millis>::max(), 480), "time overflow rejected");
    Expect(!ParseLocalMinute("2026-09-17T03:35", 1441), "invalid timezone offset rejected");
    Expect(Cities().size() == 8 && ValidCity("auto") && !ValidCity("fake"), "original city contract");
    const auto city = NamedLocation("beijing");
    Expect(city && city->name == u8"北京" && city->latitude == 39.9042 &&
        city->timezone == "Asia/Shanghai", "named city coordinates and zone");
    Expect(!NamedLocation("auto"), "automatic location requires the real location provider");
    Expect(!ValidLocation({91, 0, "", ""}) &&
        !ValidLocation({0, std::numeric_limits<double>::quiet_NaN(), "", ""}),
        "impossible coordinates rejected");
}
void MetForecast() {
    const auto body = MetBody();
    const auto snapshot = ParseMet(body.dump(), Shanghai, "auto", Now, Now, Local);
    Expect(snapshot.has_value(), "valid original MET fixture decodes");
    const auto& value = *snapshot;
    Expect(value.source == Source::MetNorway && value.remainingDay &&
        value.sourceUpdatedAt == Now - 155 * 60000 && value.validAt == Now - 35 * 60000 &&
        value.updatedAt == Now, "forecast attribution and three distinct times preserved");
    Expect(value.temperature == 21 && value.humidity == 60 &&
        value.windSpeed && std::abs(*value.windSpeed - 14.4) < 1e-8 &&
        !value.apparentTemperature, "unit conversion and missing values remain honest");
    Expect(value.hourly.size() == 6 && value.hourly[0].time == "2026-09-17T11:00" &&
        value.hourly[1].isDay == false && value.isDay, "six actual hours and day/night state");
    Expect(value.forecast.size() == 3 && value.forecast[0].date == "2026-09-17",
        "three local daily forecast ranges");
    const auto advanced = ParseMet(body.dump(), Shanghai, "auto", Now + Hour, Now, Local);
    Expect(advanced && advanced->updatedAt == Now &&
        advanced->hourly[0].time == "2026-09-17T12:00",
        "cache normalization advances hours without inventing a download");
    auto foreign = Shanghai; foreign.timezone = "America/New_York";
    const auto abroad = ParseMet(body.dump(), foreign, "auto", Now, Now, Local);
    Expect(abroad && abroad->hourly[0].time == "2026-09-16T23:00" &&
        abroad->hourly[1].time == "2026-09-17T00:00",
        "forecast groups by requested timezone instead of the user's computer");
    auto absentZone = Shanghai; absentZone.timezone.clear();
    const auto utc = ParseMet(body.dump(), absentZone, "auto", Now, Now, Local);
    Expect(utc && utc->timezone == "UTC", "absent automatic timezone explicitly uses UTC");
    auto reject = [&](Json invalid) {
        Expect(!ParseMet(invalid.dump(), Shanghai, "auto", Now, Now, Local),
            "malformed or stale MET data rejected");
    };
    auto invalid = body; invalid["properties"]["meta"]["updated_at"] = "2026-09-15T01:00:00Z"; reject(invalid);
    invalid = body; invalid["properties"]["meta"]["updated_at"] = "2026-09-18T01:00:00Z"; reject(invalid);
    invalid = body; invalid["properties"]["timeseries"][1]["data"]["instant"]["details"].erase("air_temperature"); reject(invalid);
    invalid = body; invalid["properties"]["timeseries"][1]["data"]["instant"]["details"]["air_temperature"] = "21"; reject(invalid);
    invalid = body; invalid["properties"]["timeseries"][1]["data"]["instant"]["details"]["relative_humidity"] = 101; reject(invalid);
    invalid = body; invalid["properties"]["timeseries"][1]["data"]["next_1_hours"]["summary"]["symbol_code"] = "made-up"; reject(invalid);
    invalid = body; invalid["properties"]["timeseries"][3]["time"] = invalid["properties"]["timeseries"][2]["time"]; reject(invalid);
    invalid = body; invalid["properties"]["timeseries"].erase(invalid["properties"]["timeseries"].begin() + 4); reject(invalid);
    invalid = body; invalid["properties"]["timeseries"] = Json::array(); reject(invalid);
    Expect(!ParseMet(std::string(ProviderMaxBytes + 1, ' '), Shanghai, "auto", Now, Now, Local),
        "provider input size bounded");
    Expect(!ParseMet(std::string(80, '[') + "0" + std::string(80, ']'), Shanghai, "auto", Now, Now, Local),
        "nested JSON bounded");
    Expect(!ParseMet(body.dump(), Shanghai, "auto", Now, Now + 1, Local), "future fetched timestamp rejected");
    Expect(!ParseMet(body.dump(), Shanghai, "auto", Now, Now, {}), "no implicit timezone fallback");
    invalid = body;
    invalid["properties"]["timeseries"][1]["data"]["next_1_hours"]["summary"]["symbol_code"] = "lightsleetandthunder_polartwilight";
    auto thunder = ParseMet(invalid.dump(), Shanghai, "auto", Now, Now, Local);
    Expect(thunder && thunder->weatherCode == 95 && thunder->condition == u8"雷阵雨",
        "compound MET symbols preserve thunder meaning");
}
void Cache() {
    auto value = *ParseMet(MetBody().dump(), Shanghai, "auto", Now, Now, Local);
    auto encoded = EncodeCache(value);
    Expect(encoded.has_value(), "valid snapshot cache encodes");
    auto decoded = DecodeCache(*encoded, "auto", Now + Hour);
    Expect(decoded && decoded->hourly.size() == 6 && decoded->source == value.source &&
        decoded->updatedAt == Now && decoded->sourceUpdatedAt == value.sourceUpdatedAt &&
        decoded->validAt == value.validAt && decoded->remainingDay &&
        decoded->windSpeed == value.windSpeed && !decoded->apparentTemperature,
        "cache roundtrip preserves provenance, units and optional measurements");
    Expect(DecodeCache(*encoded, "auto", Now + Day).has_value(), "24 hour display cache boundary");
    Expect(!DecodeCache(*encoded, "auto", Now + Day + 1) &&
        !DecodeCache(*encoded, "auto", Now - 1) && !DecodeCache(*encoded, "beijing", Now),
        "stale, future and wrong-city caches rejected");
    auto bad = Json::parse(*encoded); bad["version"] = 99;
    Expect(!DecodeCache(bad.dump(), "auto", Now), "unknown cache version rejected");
    bad = Json::parse(*encoded); bad["snapshot"]["temperature"] = 999;
    Expect(!DecodeCache(bad.dump(), "auto", Now), "cache measurements validated again");
    bad = Json::parse(*encoded); bad["snapshot"]["source"] = "made-up";
    Expect(!DecodeCache(bad.dump(), "auto", Now), "cache cannot forge a source");
    bad = Json::parse(*encoded); bad["snapshot"]["hourly"][0]["time"] = "2026-02-30T25:00";
    Expect(!DecodeCache(bad.dump(), "auto", Now), "cache validates actual calendar times");
    Expect(!DecodeCache(std::string(SnapshotMaxBytes + 1, ' '), "auto", Now), "display cache bound");
    value.temperature = std::numeric_limits<double>::infinity();
    Expect(!EncodeCache(value), "invalid values never persisted");
}
void Replace(std::string& text, std::string_view before, std::string_view after) {
    const auto at = text.find(before);
    Expect(at != std::string::npos, "test mutation has a real target");
    text.replace(at, before.size(), after);
}
void PublicObservation() {
    const auto now = *ParseUtc("2026-09-21T18:35:00Z");
    const auto body = PublicPage();
    auto result = ParseQweather(body + "<script>throw new Error('never execute');</script>", Shanghai, "auto", now, now);
    Expect(result.has_value(), "valid original public observation page decodes");
    Expect(result->source == Source::Qweather && !result->remainingDay &&
        result->cityName == u8"昆山市" && result->temperature == 22 && result->humidity == 90 &&
        result->apparentTemperature == 24 && !result->windSpeed && !result->isDay &&
        result->timezone == "Asia/Shanghai", "public observation attribution and measurements");
    Expect(result->validAt == now - 5 * 60000 && result->sourceUpdatedAt == result->validAt &&
        result->updatedAt == now && result->forecast.size() == 3 &&
        result->forecast[0].date == "2026-09-22" && result->forecast[0].high == 29,
        "public timestamps and local daily dates");
    {
        // District pages name their city: a nearby-city link with the city's ID,
        // or the administrative area for the four municipalities.
        const std::string cityLink = u8"<a href='https://www.qweather.com/weather/suzhou-101190401.html' class='tabs__row'>"
            u8"<div class='city'><p>苏州市</p></div><span>29°~21°</span></a>";
        auto district = ParseQweather(body + cityLink, Shanghai, "auto", now, now, "kunshan-101190404");
        Expect(district && district->cityName == u8"苏州 · 昆山市", "district shown with its city");
        Expect(ParseQweather(body + cityLink, Shanghai, "auto", now, now, "suzhou-101190401")->cityName == u8"昆山市",
            "a city's own page gets no prefix");
        Expect(ParseQweather(body, Shanghai, "auto", now, now, "kunshan-101190404")->cityName == u8"昆山市",
            "no city link leaves the district alone");
        Expect(ParseQweather(body + cityLink, Shanghai, "auto", now, now)->cityName == u8"昆山市" &&
            ParseQweather(body + cityLink, Shanghai, "auto", now, now, "kunshan-1011904")->cityName == u8"昆山市",
            "missing or malformed IDs leave the name alone");
        auto municipal = body;
        Replace(municipal, u8"<h1 class='c-submenu__location'>昆山市</h1>",
            u8"<h1 class='c-submenu__location'>朝阳区</h1><span class='c-submenu__location-adm'>北京市&nbsp;-&nbsp;中国</span>");
        Expect(ParseQweather(municipal, Shanghai, "auto", now, now, "chaoyang-101010300")->cityName == u8"北京 · 朝阳区",
            "municipality districts use the administrative area");
    }
    Expect(result->hourly.size() == 6 && result->hourly.front().time == "2026-09-22T03:00" &&
        result->hourly.back().time == "2026-09-22T08:00", "six actual upcoming public hours");
    const auto cache = EncodeCache(*result);
    Expect(cache && DecodeCache(*cache, "auto", now)->source == Source::Qweather,
        "observations survive cache without becoming forecasts");
    for (const auto& variant : std::vector<std::array<std::string, 3>>{
        {"11", "2026-09-22", "10:50"}, {"23", "2026-12-31", "22:50"}, {"0", "2026-09-22", "23:50"}}) {
        const auto time = *ParseLocalMinute(variant[1] + "T" + variant[2], 480);
        const auto page = PublicPage(std::stoi(variant[0]), variant[1], variant[2]);
        const auto weather = ParseQweather(page, Shanghai, "auto", time, time);
        Expect(weather && ParseLocalMinute(weather->hourly[0].time, 480) ==
            (time / Hour + 1) * Hour, "public noon/midnight labels cross dates and years");
    }
    for (const auto& mutation : std::vector<std::array<std::string, 2>>{
        {"2026-09-22 02:30", "2026-09-21 02:30"}, {"2026-09-22 02:30", "2026-09-23 02:30"},
        {u8"22°", u8"999°"}, {"90%", "200%"}, {"window.hour24 = [", "window.hour24 = execute(["},
        {"\"4am\"", "\"7am\""}, {u8"09月22日", u8"09月21日"}, {"151.png", "missing.png"}}) {
        auto invalid = body; Replace(invalid, mutation[0], mutation[1]);
        Expect(!ParseQweather(invalid, Shanghai, "auto", now, now),
            "stale, malformed and changed public page contracts rejected");
    }
    Expect(!ParseQweather("<html>captcha</html>", Shanghai, "auto", now, now),
        "challenge pages do not turn into weather");
    auto encoded = body; Replace(encoded, u8"22°", "22&#176;");
    Replace(encoded, u8"昆山市", u8"&#x6606;山市");
    auto entities = ParseQweather(encoded, Shanghai, "auto", now, now);
    Expect(entities && entities->temperature == 22 && entities->cityName == u8"昆山市",
        "HTML entities decoded as text");
    auto injected = "<!-- " + body + " --><style>" + body + "</style>" + body;
    Expect(ParseQweather(injected, Shanghai, "auto", now, now).has_value(),
        "comments and raw style contents cannot manufacture fields");
    auto sand = body;
    while (sand.find("\"151\"") != std::string::npos) Replace(sand, "\"151\"", "\"503\"");
    auto sandResult = ParseQweather(sand, Shanghai, "auto", now, now);
    Expect(sandResult && sandResult->hourly[0].condition == u8"扬沙" && sandResult->hourly[0].weatherCode == 999,
        "sand is not falsely converted into fog");
    auto late = PublicPage(3, "2026-09-22", "04:30");
    auto shifted = ParseQweather(late, Shanghai, "auto", now + 2 * Hour, now);
    Expect(shifted && shifted->hourly[0].time == "2026-09-22T04:00" && shifted->updatedAt == now,
        "cached public page advances time without altering fetched timestamp");
    Expect(!ParseQweather(std::string(ProviderMaxBytes + 1, ' '), Shanghai, "auto", now, now),
        "public HTML size is bounded");
}
}
int main() {
    TimeAndCities();
    MetForecast();
    Cache();
    PublicObservation();
    std::cout << "weather time, cities, MET, public observation and cache contracts passed\n";
}
