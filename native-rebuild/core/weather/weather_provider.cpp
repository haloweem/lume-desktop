#include "core/weather/weather_provider.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace lume::weather {
namespace {
using Json = nlohmann::json;
constexpr Millis Ttl = 10 * 60000;
constexpr Millis MaxTime = 253402300799999LL;
constexpr std::size_t CacheLimit = 8 * ProviderMaxBytes + 16384;
constexpr auto PublicOrigin = "https://www.qweather.com";
constexpr auto MetUrl = "https://api.met.no/weatherapi/locationforecast/2.0/compact";
struct Cancelled {};
void Check(const Cancellation& cancel) { if (cancel.Cancelled()) throw Cancelled{}; }
bool Time(Millis time) { return time >= 0 && time <= MaxTime; }
Millis Add(Millis time, Millis duration) { return time + std::clamp(duration, Millis{0}, MaxTime - time); }
bool SafeHeader(std::string_view value) {
    return value.size() <= 128 && std::none_of(value.begin(), value.end(),
        [](unsigned char c) { return c < 32 || c == 127; });
}
bool Slug(std::string_view value) {
    const auto last = value.rfind('-');
    if (value.size() > 160 || last == std::string_view::npos || last == 0 ||
        value.size() - last != 10 || value.substr(last + 1, 3) != "101") return false;
    for (std::size_t i = last + 4; i < value.size(); ++i)
        if (value[i] < '0' || value[i] > '9') return false;
    for (std::size_t i = 0; i < last; ++i) {
        const auto c = value[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) continue;
        if (c != '-' || i == 0 || value[i - 1] == '-') return false;
    }
    return value[last - 1] != '-';
}
std::string Coordinate(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(4) << value;
    return stream.str();
}
Json Parse(std::string_view text, std::size_t bound) {
    if (text.empty() || text.size() > bound) throw std::runtime_error("Weather cache size");
    return Json::parse(text.begin(), text.end(), [](int depth, Json::parse_event_t, Json&) {
        if (depth > 32) throw std::runtime_error("Weather cache nesting");
        return true;
    });
}
Millis RetryDelay(std::string_view value, Millis now, Millis fallback) {
    if (!value.empty() && value.size() <= 16 &&
        std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        Millis seconds = 0;
        for (char c : value) {
            if (seconds > MaxTime / 10000) return MaxTime;
            seconds = seconds * 10 + c - '0';
        }
        return std::min(seconds, MaxTime / 1000) * 1000;
    }
    const auto date = ParseHttpDate(value);
    return date ? std::max<Millis>(0, *date - now) : fallback;
}
template<class T> auto Find(std::vector<T>& entries, const std::string& key) {
    return std::find_if(entries.begin(), entries.end(), [&](const auto& item) { return item.key == key; });
}
template<class T> void Store(std::vector<T>& entries, T entry) {
    const auto found = Find(entries, entry.key);
    if (found != entries.end()) *found = std::move(entry);
    else {
        if (entries.size() >= 4) entries.erase(entries.begin());
        entries.push_back(std::move(entry));
    }
}
Millis SavedTime(const Json& value) {
    if (!value.is_number_integer() || (value.is_number_unsigned() &&
        value.get<std::uint64_t>() > static_cast<std::uint64_t>(MaxTime)))
        throw std::runtime_error("Weather cache timestamp");
    auto result = value.get<Millis>();
    if (!Time(result)) throw std::runtime_error("Weather cache timestamp");
    return result;
}
bool Key(std::string_view key) {
    return !key.empty() && key.size() <= 32 && std::all_of(key.begin(), key.end(),
        [](char c) { return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ','; });
}
}
std::optional<Millis> ParseHttpDate(std::string_view text) {
    // IMF-fixdate from HTTPS response headers. Never use the machine's locale.
    if (text.size() != 29 || text.substr(3, 2) != ", " || text[7] != ' ' ||
        text[11] != ' ' || text[16] != ' ' || text.substr(25) != " GMT") return {};
    constexpr std::string_view months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const auto month = months.find(text.substr(8, 3));
    if (month == std::string_view::npos || month % 3) return {};
    const auto n = month / 3 + 1;
    const std::string iso = std::string(text.substr(12, 4)) + "-" +
        (n < 10 ? "0" : "") + std::to_string(n) + "-" + std::string(text.substr(5, 2)) +
        "T" + std::string(text.substr(17, 8)) + "Z";
    return ParseUtc(iso);
}
struct WeatherProvider::State {
    struct PublicCache { std::string key, body, slug; Millis fetchedAt; };
    struct MetCache { std::string key, body, modified; Millis fetchedAt, expires; };
    HttpTransport transport;
    Clock clock;
    LocalTime localTime;
    std::vector<PublicCache> publicCache;
    std::vector<MetCache> metCache;
    Millis publicRetryAt = 0, metRetryAt = 0, previousTime = 0;
    State(HttpTransport t, Clock c, LocalTime l)
        : transport(std::move(t)), clock(std::move(c)), localTime(std::move(l)) {}
    Millis Now() const {
        const auto value = clock();
        if (!Time(value)) throw std::runtime_error("Invalid weather clock");
        return value;
    }
    HttpResponse Request(std::string url, bool met, const Cancellation& cancel,
        std::string modified = {}) {
        Check(cancel);
        HttpRequest request;
        request.url = std::move(url);
        request.timeoutMs = met ? 12000 : 10000;
        request.headers = {{"User-Agent", "Lume/1.0.8 (Windows desktop weather)"},
            {"Accept", met ? "application/json" : "text/html, application/json"},
            {"Accept-Encoding", "gzip, deflate"}};
        if (!modified.empty() && SafeHeader(modified))
            request.headers.emplace_back("If-Modified-Since", std::move(modified));
        HttpResponse response;
        try { response = transport(request, cancel); }
        catch (...) { Check(cancel); return {}; }
        Check(cancel);
        if (response.body.size() > request.maxBytes) return {};
        return response;
    }
    std::optional<Snapshot> Public(std::string_view city, const Location& location,
        const std::string& key, const Cancellation& cancel) {
        auto now = Now();
        if (now < publicRetryAt) return {};
        Millis delay = Ttl;
        try {
            auto existing = Find(publicCache, key);
            if (existing != publicCache.end() && now < existing->fetchedAt) {
                publicCache.erase(existing); existing = publicCache.end();
            }
            if (existing != publicCache.end() && now - existing->fetchedAt < Ttl) {
                auto snapshot = ParseQweather(existing->body, location, city, now, existing->fetchedAt, existing->slug);
                if (!snapshot) throw std::runtime_error("Invalid cached public weather");
                snapshot->sourceUrl = std::string(PublicOrigin) + "/weather/" + existing->slug + ".html";
                return snapshot;
            }
            std::string slug = existing != publicCache.end() ? existing->slug : "";
            const auto get = [&](std::string url) {
                auto response = Request(std::move(url), false, cancel);
                if (response.status != 200) {
                    delay = std::max(response.status == 403 || response.status == 429 ? Hour : Ttl,
                        RetryDelay(response.retryAfter, Now(), Ttl));
                    throw std::runtime_error("Public weather request failed");
                }
                return response.body;
            };
            if (slug.empty()) {
                const auto result = Parse(get(std::string(PublicOrigin) + "/v2/search?lng=" +
                    Coordinate(location.longitude) + "&lat=" + Coordinate(location.latitude)), ProviderMaxBytes);
                if (!result.is_object() || result.value("success", false) != true ||
                    !result.contains("data") || !result["data"].is_string() ||
                    !Slug(slug = result["data"].get<std::string>()))
                    throw std::runtime_error("Invalid public weather search");
            }
            const auto url = std::string(PublicOrigin) + "/weather/" + slug + ".html";
            auto body = get(url);
            now = Now();
            auto snapshot = ParseQweather(body, location, city, now, now, slug);
            if (!snapshot) throw std::runtime_error("Invalid public weather page");
            Check(cancel);
            snapshot->sourceUrl = url;
            Store(publicCache, PublicCache{key, std::move(body), std::move(slug), now});
            publicRetryAt = 0;
            return snapshot;
        } catch (const Cancelled&) { throw; }
        catch (...) { publicRetryAt = Add(Now(), delay); return {}; }
    }
    ProviderResult Met(std::string_view city, const Location& location,
        const std::string& key, const Cancellation& cancel) {
        auto now = Now();
        auto existing = Find(metCache, key);
        if (existing != metCache.end() && now < existing->fetchedAt) {
            metCache.erase(existing); existing = metCache.end();
        }
        if (existing != metCache.end() && existing->expires > now) {
            auto snapshot = ParseMet(existing->body, location, city, now, existing->fetchedAt, localTime);
            if (snapshot) {
                snapshot->sourceUrl = MetUrl;
                return {std::move(snapshot), ProviderError::None, 0};
            }
            metCache.erase(existing); existing = metCache.end();
        }
        if (now < metRetryAt) return {{}, ProviderError::RetryDelayed, metRetryAt};
        Millis delay = 60000;
        try {
            auto response = Request(std::string(MetUrl) + "?lat=" + Coordinate(location.latitude) +
                "&lon=" + Coordinate(location.longitude), true, cancel,
                existing != metCache.end() ? existing->modified : "");
            now = Now();
            if (response.status != 200 && response.status != 203 && response.status != 304) {
                if (response.status != 0) delay = std::max<Millis>(60000, RetryDelay(response.retryAfter, now, Ttl));
                throw std::runtime_error("MET weather request failed");
            }
            if (response.status == 304 && existing == metCache.end())
                throw std::runtime_error("MET weather 304 without body");
            auto body = response.status == 304 ? existing->body : std::move(response.body);
            auto snapshot = ParseMet(body, location, city, now, now, localTime);
            if (!snapshot) throw std::runtime_error("Invalid MET weather data");
            Check(cancel);
            const auto expires = ParseHttpDate(response.expires);
            auto modified = SafeHeader(response.lastModified) ? response.lastModified : "";
            if (modified.empty() && existing != metCache.end()) modified = existing->modified;
            Store(metCache, MetCache{key, std::move(body), std::move(modified), now,
                expires && *expires > now ? std::min(*expires, Add(now, Day)) : Add(now, Ttl)});
            metRetryAt = 0;
            snapshot->sourceUrl = MetUrl;
            return {std::move(snapshot), ProviderError::None, 0};
        } catch (const Cancelled&) { throw; }
        catch (...) {
            metRetryAt = Add(Now(), delay);
            return {{}, ProviderError::Unavailable, metRetryAt};
        }
    }
};
WeatherProvider::WeatherProvider(HttpTransport transport, Clock clock, LocalTime local)
    : state_(std::make_unique<State>(std::move(transport), std::move(clock), std::move(local))) {}
WeatherProvider::~WeatherProvider() = default;
ProviderResult WeatherProvider::Fetch(std::string_view city, const Location& location, const Cancellation& cancel) {
    try {
        Check(cancel);
        if (!ValidCity(city) || !ValidLocation(location)) return {{}, ProviderError::InvalidLocation, 0};
        const auto now = state_->Now();
        if (now < state_->previousTime) state_->publicRetryAt = state_->metRetryAt = 0;
        state_->previousTime = now;
        const auto key = Coordinate(location.latitude) + "," + Coordinate(location.longitude);
        auto snapshot = state_->Public(city, location, key, cancel);
        Check(cancel);
        if (snapshot) return {std::move(snapshot), ProviderError::None, 0};
        auto result = state_->Met(city, location, key, cancel);
        Check(cancel);
        if (result.retryAt && state_->publicRetryAt > now)
            result.retryAt = std::min(result.retryAt, state_->publicRetryAt);
        return result;
    } catch (const Cancelled&) { return {{}, ProviderError::Cancelled, 0}; }
    catch (...) { return {{}, ProviderError::Unavailable, 0}; }
}
std::optional<std::string> WeatherProvider::SaveCache() const {
    try {
        Json saved = {{"version", 1}, {"public", Json::array()}, {"met", Json::array()}};
        for (const auto& c : state_->publicCache)
            saved["public"].push_back({{"key", c.key}, {"body", c.body}, {"slug", c.slug}, {"fetchedAt", c.fetchedAt}});
        for (const auto& c : state_->metCache)
            saved["met"].push_back({{"key", c.key}, {"body", c.body}, {"modified", c.modified},
                {"fetchedAt", c.fetchedAt}, {"expires", c.expires}});
        auto text = saved.dump();
        if (text.size() <= CacheLimit) return text;
    } catch (...) {}
    return {};
}
void WeatherProvider::RestoreCache(std::string_view text) {
    try {
        const auto now = state_->Now();
        const auto saved = Parse(text, CacheLimit);
        if (saved.at("version") != 1 || !saved.at("public").is_array() || !saved.at("met").is_array()) return;
        std::vector<State::PublicCache> pages;
        std::vector<State::MetCache> forecasts;
        const auto restore = [&](const char* name, const auto& read) {
            const auto& entries = saved.at(name);
            const std::size_t first = entries.size() > 4 ? entries.size() - 4 : 0;
            for (std::size_t i = first; i < entries.size(); ++i) {
                try { read(entries[i]); } catch (...) {}
            }
        };
        restore("public", [&](const Json& c) {
            State::PublicCache entry{c.at("key"), c.at("body"), c.at("slug"), SavedTime(c.at("fetchedAt"))};
            if (Key(entry.key) && Slug(entry.slug) && !entry.body.empty() && entry.body.size() <= ProviderMaxBytes &&
                entry.fetchedAt <= now && now - entry.fetchedAt < Ttl) Store(pages, std::move(entry));
        });
        restore("met", [&](const Json& c) {
            State::MetCache entry{c.at("key"), c.at("body"), c.at("modified"),
                SavedTime(c.at("fetchedAt")), SavedTime(c.at("expires"))};
            if (Key(entry.key) && SafeHeader(entry.modified) && !entry.body.empty() &&
                entry.body.size() <= ProviderMaxBytes && entry.fetchedAt <= now &&
                now - entry.fetchedAt <= Day && entry.expires <= Add(now, Day)) Store(forecasts, std::move(entry));
        });
        state_->publicCache = std::move(pages);
        state_->metCache = std::move(forecasts);
    } catch (...) {}
}
std::size_t WeatherProvider::CachedEntries() const { return state_->publicCache.size() + state_->metCache.size(); }
std::size_t WeatherProvider::CachedBodyBytes() const {
    std::size_t size = 0;
    for (const auto& entry : state_->publicCache) size += entry.body.size();
    for (const auto& entry : state_->metCache) size += entry.body.size();
    return size;
}
}
