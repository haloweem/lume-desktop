#include "core/weather/weather_location.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace lume::weather {
namespace {
using Json = nlohmann::json;
constexpr Millis Minute = 60000;
// A system fix is refreshed every 30 minutes; a desktop rarely moves, so an
// IP fix lasts 6 hours. Failed or unconfirmed lookups retry after 10 minutes.
constexpr Millis DeviceTtl = 30 * Minute, IpTtl = 6 * Hour, RetryTtl = 10 * Minute;
// For a day after a system fix, IP results cannot replace it.
constexpr Millis DeviceHold = Day;
// Results this close count as the same place.
constexpr double NearKm = 50;
constexpr std::size_t LocationMaxBytes = 64 * 1024, StoreMaxBytes = 4096;
struct Cancelled {};
void Check(const Cancellation& cancel) { if (cancel.Cancelled()) throw Cancelled{}; }
bool Space(std::uint32_t c) {
    return (c >= 9 && c <= 13) || c == 32 || c == 0xa0 || c == 0x1680 ||
        (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 ||
        c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
}
// JSON has already validated UTF-8. Bound names by the original 60 UTF-16
// units without leaving a split surrogate/UTF-8 sequence at the boundary.
std::string Name(std::string_view text, bool chineseOnly = false) {
    std::size_t first = text.size(), last = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto start = i;
        auto c = static_cast<std::uint32_t>(static_cast<unsigned char>(text[i++]));
        unsigned extra = 0;
        if (c >= 0xf0) { c &= 7; extra = 3; }
        else if (c >= 0xe0) { c &= 15; extra = 2; }
        else if (c >= 0xc0) { c &= 31; extra = 1; }
        while (extra-- && i < text.size())
            c = (c << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        if (!Space(c)) { first = std::min(first, start); last = i; }
    }
    if (first >= last) return {};
    text = text.substr(first, last - first);
    std::size_t end = 0, units = 0;
    bool chinese = false;
    while (end < text.size()) {
        const auto start = end;
        auto c = static_cast<std::uint32_t>(static_cast<unsigned char>(text[end++]));
        unsigned extra = 0;
        if (c >= 0xf0) { c &= 7; extra = 3; }
        else if (c >= 0xe0) { c &= 15; extra = 2; }
        else if (c >= 0xc0) { c &= 31; extra = 1; }
        while (extra-- && end < text.size())
            c = (c << 6) | (static_cast<unsigned char>(text[end++]) & 63);
        units += c > 0xffff ? 2 : 1;
        if (units > 60) { end = start; break; }
        chinese = chinese || (c >= 0x3400 && c <= 0x9fff);
    }
    if (chineseOnly && !chinese) return {};
    return std::string(text.substr(0, end));
}
std::string Field(const Json& value, const char* key) {
    const auto found = value.find(key);
    return found != value.end() && found->is_string() ? found->get<std::string>() : std::string{};
}
bool Truthy(const Json& value) {
    if (value.is_null()) return false;
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number()) return value.get<double>() != 0;
    if (value.is_string()) return !value.get_ref<const std::string&>().empty();
    return true;
}
std::optional<double> Decimal(std::string_view text) {
    if (text.empty()) return {};
    std::size_t i = text[0] == '-' ? 1 : 0, digits = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
    if (digits == i) return {};
    if (i < text.size() && text[i] == '.') {
        digits = ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
        if (digits == i) return {};
    }
    if (i != text.size()) return {};
    double result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || !std::isfinite(result)) return {};
    return result;
}
std::string Coordinate(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(15) << value;
    return stream.str();
}
double DistanceKm(const Location& a, const Location& b) {
    constexpr double Radians = 3.14159265358979323846 / 180, EarthKm = 6371;
    const double dLat = (b.latitude - a.latitude) * Radians, dLon = (b.longitude - a.longitude) * Radians;
    const double h = std::sin(dLat / 2) * std::sin(dLat / 2) +
        std::cos(a.latitude * Radians) * std::cos(b.latitude * Radians) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2 * EarthKm * std::asin(std::sqrt(std::min(1.0, h)));
}
std::string Upper(std::string text) {
    for (auto& c : text) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return text;
}
// Direct requests bypass the system proxy, so a proxy in system mode does not
// replace the user's address with its exit's. Reverse geocoding only names
// coordinates and keeps the normal route.
Json Request(const HttpTransport& transport, std::string url, const Cancellation& cancel, bool direct = false) {
    Check(cancel);
    HttpRequest request{std::move(url),
        {{"Accept", "application/json"}, {"User-Agent", "Lume/1.0.8 (desktop weather)"}},
        12000, LocationMaxBytes};
    request.direct = direct;
    auto reply = transport(request, cancel);
    Check(cancel);
    if (reply.status != 200 || reply.body.empty() || reply.body.size() > LocationMaxBytes)
        throw std::runtime_error("Location response unavailable");
    auto result = Json::parse(reply.body, [](int depth, Json::parse_event_t, Json&) {
        if (depth > 16) throw std::runtime_error("Location response nesting");
        return true;
    });
    if (!result.is_object()) throw std::runtime_error("Location response object");
    return result;
}
}
WeatherLocation::WeatherLocation(HttpTransport t, Clock c, LocalTime l, PlaceNormalizer n, DeviceLocation d,
                                 LocationEnvironment e, LocationStore s)
    : transport_(std::move(t)), clock_(std::move(c)), local_(std::move(l)),
      normalize_(std::move(n)), device_(std::move(d)), environment_(std::move(e)), store_(std::move(s)) {}
void WeatherLocation::Invalidate() {
    validUntil_ = 0;
}
void WeatherLocation::Restore() {
    if (restored_) return;
    restored_ = true;
    try {
        if (!store_.load) return;
        const auto text = store_.load();
        if (!text || text->empty() || text->size() > StoreMaxBytes) return;
        const auto value = Json::parse(*text);
        if (!value.is_object() || value.value("version", 0) != 1) return;
        Trusted trusted;
        trusted.location = {value.at("latitude").get<double>(), value.at("longitude").get<double>(),
                            Name(Field(value, "name")), Field(value, "timezone")};
        const auto origin = Field(value, "origin");
        if (origin != "device" && origin != "ip") return;
        trusted.origin = origin == "device" ? Origin::Device : Origin::Ip;
        trusted.at = value.at("at").get<Millis>();
        if (trusted.location.name.empty() || trusted.location.timezone.empty() || !ValidLocation(trusted.location))
            return;
        // Restored only as the anchor and fallback; the next Resolve still looks again.
        trusted_ = std::move(trusted);
    } catch (...) {}
}
void WeatherLocation::Accept(Location location, Origin origin, Millis now) {
    trusted_ = Trusted{std::move(location), origin, now};
    pending_.reset();
    checkedAt_ = now;
    validUntil_ = now + (origin == Origin::Device ? DeviceTtl : IpTtl);
    if (!store_.save) return;
    try {
        const Json value{{"version", 1}, {"latitude", trusted_->location.latitude},
                         {"longitude", trusted_->location.longitude}, {"name", trusted_->location.name},
                         {"timezone", trusted_->location.timezone},
                         {"origin", origin == Origin::Device ? "device" : "ip"}, {"at", now}};
        store_.save(value.dump());
    } catch (...) {}
}
LocationResult WeatherLocation::Resolve(std::string_view city, const Cancellation& cancel) {
    if (cancel.Cancelled()) return {{}, ProviderError::Cancelled};
    if (!ValidCity(city)) return {{}, ProviderError::InvalidLocation};
    if (city != "auto") return {NamedLocation(city), ProviderError::None};
    Restore();
    const auto now = clock_();
    if (trusted_ && now >= checkedAt_ && now < validUntil_) return {trusted_->location, ProviderError::None};
    // Keeps the trusted location for now and looks again after a short wait.
    const auto keep = [&]() -> LocationResult {
        checkedAt_ = now;
        validUntil_ = now + RetryTtl;
        if (trusted_) return {trusted_->location, ProviderError::None};
        return {{}, ProviderError::Unavailable};
    };
    try {
        const auto normalize = [&](const std::string& text) {
            auto result = normalize_ ? normalize_(text) : text;
            // The port must not produce an unbounded or invalid display name.
            const Location probe{0, 0, result, "UTC"};
            return result.empty() || !ValidLocation(probe) ? std::string(u8"当前位置") : result;
        };
        const auto finish = [&](Location location) -> std::optional<Location> {
            if (!ValidLocation(location)) return {};
            if (location.timezone.empty() || !local_ || !local_(now, location.timezone))
                location.timezone = "UTC";
            auto chinese = Name(location.name, true);
            if (chinese.empty()) {
                const auto latitudeText = Coordinate(location.latitude);
                const auto longitudeText = Coordinate(location.longitude);
                try {
                    const auto translated = Request(transport_,
                        "https://api-bdc.io/data/reverse-geocode-client?latitude=" + latitudeText +
                        "&longitude=" + longitudeText + "&localityLanguage=zh", cancel);
                    for (const auto* key : {"locality", "city", "principalSubdivision"}) {
                        chinese = Name(Field(translated, key), true);
                        if (!chinese.empty()) break;
                    }
                } catch (const Cancelled&) { throw; } catch (const std::exception&) {}
                if (chinese.empty()) {
                    try {
                        const auto translated = Request(transport_,
                            "https://nominatim.openstreetmap.org/reverse?format=jsonv2&lat=" + latitudeText +
                            "&lon=" + longitudeText + "&zoom=10&accept-language=zh-CN", cancel);
                        const auto address = translated.find("address");
                        if (address != translated.end() && address->is_object()) {
                            for (const auto* key : {"city", "town", "county", "municipality", "village", "state"}) {
                                chinese = Name(Field(*address, key), true);
                                if (!chinese.empty()) break;
                            }
                        }
                    } catch (const Cancelled&) { throw; } catch (const std::exception&) {}
                }
            }
            location.name = normalize(chinese.empty() ? std::string(u8"当前位置") : chinese);
            return location;
        };
        // The system fix uses nearby Wi-Fi, which a VPN cannot move: accept it at once.
        if (device_) {
            try {
                if (const auto location = device_(now)) {
                    if (const auto resolved = finish(*location)) {
                        Check(cancel);
                        Accept(*resolved, Origin::Device, clock_());
                        return {trusted_->location, ProviderError::None};
                    }
                }
            } catch (const Cancelled&) { throw; } catch (const std::exception&) {}
            Check(cancel);
        }
        // A recent system fix outranks any IP result.
        if (trusted_ && trusted_->origin == Origin::Device && now >= trusted_->at && now - trusted_->at < DeviceHold)
            return keep();
        // An IP result is a proxy exit when its country or UTC offset disagrees with the computer's.
        const auto plausible = [&](const Json& value, const std::string& timezone) {
            auto country = Upper(Field(value, "country_code"));
            if (country.empty()) country = Upper(Field(value, "country"));
            if (country.size() == 2 && !environment_.countries.empty() &&
                std::find(environment_.countries.begin(), environment_.countries.end(), country) ==
                    environment_.countries.end())
                return false;
            if (!timezone.empty() && local_ && environment_.localTime) {
                const auto there = local_(now, timezone), here = environment_.localTime(now);
                if (there && here) {
                    const auto a = ParseLocalMinute(*there, 0), b = ParseLocalMinute(*here, 0);
                    if (a && b && std::llabs(*a - *b) >= 30 * Minute) return false;
                }
            }
            return true;
        };
        for (const auto* url : {"https://api.ip.sb/geoip", "https://ipinfo.io/json"}) {
            try {
                const auto value = Request(transport_, url, cancel, true);
                const auto error = value.find("error"), success = value.find("success");
                if ((error != value.end() && Truthy(*error)) ||
                    (success != value.end() && *success == false)) continue;
                const auto name = Name(Field(value, "city"));
                if (name.empty()) continue;
                std::optional<double> latitude, longitude;
                const auto coordinates = Field(value, "loc");
                const auto comma = coordinates.find(',');
                if (comma != std::string::npos) {
                    latitude = Decimal(std::string_view(coordinates).substr(0, comma));
                    longitude = Decimal(std::string_view(coordinates).substr(comma + 1));
                    if (!latitude || !longitude) { latitude.reset(); longitude.reset(); }
                }
                const auto lat = value.find("latitude"), lon = value.find("longitude");
                if (lat != value.end() && !lat->is_null()) {
                    if (!lat->is_number()) continue;
                    latitude = lat->get<double>();
                }
                if (lon != value.end() && !lon->is_null()) {
                    if (!lon->is_number()) continue;
                    longitude = lon->get<double>();
                }
                if (!latitude || !longitude) continue;
                const Location candidate{*latitude, *longitude, name, Field(value, "timezone")};
                if (!ValidLocation(candidate) || !plausible(value, candidate.timezone)) continue;
                // A far move needs the same answer twice in a row.
                if (trusted_ && DistanceKm(trusted_->location, candidate) > NearKm) {
                    if (!pending_ || DistanceKm(*pending_, candidate) > NearKm) {
                        pending_ = candidate;
                        return keep();
                    }
                }
                const auto resolved = finish(candidate);
                if (!resolved) continue;
                Check(cancel);
                Accept(*resolved, Origin::Ip, clock_());
                return {trusted_->location, ProviderError::None};
            } catch (const Cancelled&) { throw; } catch (const std::exception&) {}
            Check(cancel);
        }
    } catch (const Cancelled&) { return {{}, ProviderError::Cancelled}; }
    return keep();
}
}
