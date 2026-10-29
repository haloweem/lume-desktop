#pragma once
#include "core/weather/weather.h"
#include <atomic>
#include <memory>
#include <utility>

namespace lume::weather {
class Cancellation final {
public:
    void Cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
    bool Cancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }
private:
    std::atomic<bool> cancelled_{false};
};
struct HttpRequest {
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    unsigned timeoutMs = 10000;
    std::size_t maxBytes = ProviderMaxBytes;
    // Bypass the system proxy (automatic location must see the user's own address).
    bool direct = false;
};
struct HttpResponse {
    int status = 0;
    std::string body, retryAfter, expires, lastModified;
};
using HttpTransport = std::function<HttpResponse(const HttpRequest&, const Cancellation&)>;
using Clock = std::function<Millis()>;
enum class ProviderError { None, InvalidLocation, Unavailable, RetryDelayed, Cancelled };
struct ProviderResult {
    std::optional<Snapshot> snapshot;
    ProviderError error = ProviderError::Unavailable;
    Millis retryAt = 0;
};
std::optional<Millis> ParseHttpDate(std::string_view);

// Owned by one weather worker. Calls are serialized by that worker; no UI, disk,
// threads or global cache is retained here. One resolved location is used by both
// providers, so fallback does not repeat IP geolocation.
class WeatherProvider final {
public:
    WeatherProvider(HttpTransport, Clock, LocalTime);
    ~WeatherProvider();
    WeatherProvider(const WeatherProvider&) = delete;
    WeatherProvider& operator=(const WeatherProvider&) = delete;
    ProviderResult Fetch(std::string_view cityId, const Location&, const Cancellation&);
    // Bounded raw provider caches; reparsed on use. Display cache is separate.
    std::optional<std::string> SaveCache() const;
    void RestoreCache(std::string_view);
    std::size_t CachedEntries() const;
    std::size_t CachedBodyBytes() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace lume::weather
