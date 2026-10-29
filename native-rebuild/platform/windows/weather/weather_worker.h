#pragma once
#include "core/weather/weather_provider.h"
#include <chrono>
#include <memory>
namespace lume::platform::windows {
struct WeatherReading {
    std::optional<weather::Snapshot> snapshot;
    weather::ProviderError error = weather::ProviderError::None;
    bool loading = false;
    bool cacheWriteFailed = false;
};
struct WeatherFetch {
    weather::ProviderResult result;
    bool cacheWriteFailed = false;
};
// Created, used and destroyed on the single weather worker. Implementations
// own their provider/location caches, HTTP and timezone resources.
class IWeatherBackend {
public:
    virtual ~IWeatherBackend() = default;
    virtual std::optional<weather::Snapshot> Load(std::string_view city) = 0;
    virtual WeatherFetch Fetch(std::string_view city, const weather::Cancellation&) = 0;
    virtual void InvalidateLocation() {}
};
using WeatherBackendFactory = std::function<std::unique_ptr<IWeatherBackend>()>;
// Lifecycle belongs to the UI thread. Notify runs on the worker and must only
// post to its owner, never manipulate UI or call Stop. Stop joins and clears
// all data: no notification/resource survives its return.
class WeatherWorker final {
public:
    WeatherWorker(WeatherBackendFactory, std::function<void()> notify, weather::Clock,
        std::chrono::milliseconds interval = std::chrono::milliseconds{0});
    ~WeatherWorker();
    WeatherWorker(const WeatherWorker&) = delete;
    WeatherWorker& operator=(const WeatherWorker&) = delete;
    bool Start(std::string city);
    bool SetCity(std::string city);
    void Refresh();
    void Stop();
    bool Running() const;
    WeatherReading Latest() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
