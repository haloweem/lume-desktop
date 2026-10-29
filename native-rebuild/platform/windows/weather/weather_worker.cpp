#include "platform/windows/weather/weather_worker.h"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>
namespace lume::platform::windows {
using namespace weather;
using Steady = std::chrono::steady_clock;
struct WeatherWorker::State {
    State(WeatherBackendFactory f, std::function<void()> n, Clock c, std::chrono::milliseconds i)
        : factory(std::move(f)), notify(std::move(n)), clock(std::move(c)), interval(i) {}
    bool Usable(const Snapshot& value, std::string_view expected) const {
        const auto now = clock();
        return value.cityId == expected && ValidSnapshot(value) &&
            now >= value.updatedAt && now - value.updatedAt <= Day;
    }
    void Notify() const noexcept {
        try { if (notify) notify(); } catch (...) {}
    }
    void Run() noexcept {
        try {
            auto backend = factory();
            if (!backend) throw std::runtime_error("Weather backend unavailable");
            // Original uses a fixed interval per enabled instance with up to
            // one minute of jitter. No periodic wake/poll while it is idle.
            const auto delay = interval.count() > 0 ? interval :
                std::chrono::milliseconds(600000 + std::random_device{}() % 60000);
            auto deadline = Steady::now();
            std::uint64_t loadedGeneration = 0;
            for (;;) {
                std::string requested;
                std::uint64_t requestGeneration = 0;
                std::shared_ptr<Cancellation> token;
                {
                    std::unique_lock lock(mutex);
                    wake.wait_until(lock, deadline, [&] { return stop || pending; });
                    if (stop) break;
                    requested = city;
                    requestGeneration = generation;
                    token = cancel;
                    pending = false;
                    busy = true;
                    latest.loading = true;
                    if (latest.snapshot && !Usable(*latest.snapshot, requested)) latest.snapshot.reset();
                }
                if (loadedGeneration != requestGeneration) {
                    std::optional<Snapshot> cached;
                    try { cached = backend->Load(requested); } catch (...) {}
                    {
                        std::lock_guard lock(mutex);
                        if (stop) break;
                        if (generation != requestGeneration) continue;
                        if (cached && Usable(*cached, requested)) latest.snapshot = std::move(cached);
                    }
                    loadedGeneration = requestGeneration;
                }
                Notify();
                bool invalidateLocation = false;
                {
                    std::lock_guard lock(mutex);
                    invalidateLocation = refreshLocation;
                    refreshLocation = false;
                }
                if (invalidateLocation) backend->InvalidateLocation();
                WeatherFetch fetched;
                try { fetched = backend->Fetch(requested, *token); }
                catch (...) { fetched.result.error = ProviderError::Unavailable; }
                {
                    std::lock_guard lock(mutex);
                    if (stop) break;
                    if (generation != requestGeneration) continue;
                    auto& result = fetched.result;
                    if (result.snapshot && Usable(*result.snapshot, requested)) {
                        latest.snapshot = std::move(result.snapshot);
                        latest.error = ProviderError::None;
                    } else {
                        latest.error = result.error == ProviderError::None ? ProviderError::Unavailable : result.error;
                        if (latest.snapshot && !Usable(*latest.snapshot, requested)) latest.snapshot.reset();
                    }
                    latest.loading = false;
                    latest.cacheWriteFailed = fetched.cacheWriteFailed;
                    busy = false;
                    deadline = Steady::now() + delay;
                    const auto now = clock();
                    if (result.retryAt > now)
                        deadline = std::max(deadline, Steady::now() +
                            std::chrono::milliseconds(std::min(result.retryAt - now, Day)));
                }
                Notify();
            }
        } catch (...) {
            bool publish = false;
            {
                std::lock_guard lock(mutex);
                publish = !stop;
                if (publish) {
                    latest.loading = false;
                    latest.error = ProviderError::Unavailable;
                }
            }
            if (publish) Notify();
        }
        std::lock_guard lock(mutex);
        running = false;
        busy = false;
    }
    WeatherBackendFactory factory;
    std::function<void()> notify;
    Clock clock;
    std::chrono::milliseconds interval;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread thread;
    std::shared_ptr<Cancellation> cancel;
    WeatherReading latest;
    std::string city;
    std::uint64_t generation = 0;
    bool stop = true, pending = false, busy = false, running = false, refreshLocation = false;
};
WeatherWorker::WeatherWorker(WeatherBackendFactory factory, std::function<void()> notify, Clock clock,
    std::chrono::milliseconds interval)
    : state_(std::make_unique<State>(std::move(factory), std::move(notify), std::move(clock), interval)) {}
WeatherWorker::~WeatherWorker() { Stop(); }
bool WeatherWorker::Start(std::string city) {
    if (!ValidCity(city)) return false;
    if (Running()) return SetCity(std::move(city));
    Stop();
    std::lock_guard lock(state_->mutex);
    try {
        state_->city = std::move(city);
        ++state_->generation;
        state_->cancel = std::make_shared<Cancellation>();
        state_->stop = false;
        state_->pending = state_->running = state_->latest.loading = true;
        state_->thread = std::thread([state = state_.get()] { state->Run(); });
        return true;
    } catch (...) {
        state_->stop = true;
        state_->running = state_->pending = state_->latest.loading = false;
        state_->latest.error = ProviderError::Unavailable;
        state_->cancel.reset();
        return false;
    }
}
bool WeatherWorker::SetCity(std::string city) {
    if (!ValidCity(city)) return false;
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->running) return false;
        if (state_->city == city) return true;
        auto next = std::make_shared<Cancellation>();
        state_->cancel->Cancel();
        state_->cancel = std::move(next);
        state_->city = std::move(city);
        ++state_->generation;
        state_->latest = {};
        state_->latest.loading = true;
        state_->pending = true;
    }
    state_->wake.notify_one();
    return true;
}
void WeatherWorker::Refresh() {
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->running || state_->stop || state_->busy) return;
        state_->pending = true;
        state_->latest.loading = true;
        state_->refreshLocation = true;
    }
    state_->wake.notify_one();
}
void WeatherWorker::Stop() {
    {
        std::lock_guard lock(state_->mutex);
        state_->stop = true;
        if (state_->cancel) state_->cancel->Cancel();
    }
    state_->wake.notify_one();
    if (state_->thread.joinable()) state_->thread.join();
    std::lock_guard lock(state_->mutex);
    state_->running = state_->pending = state_->busy = state_->refreshLocation = false;
    state_->latest = {};
    state_->cancel.reset();
    state_->city.clear();
}
bool WeatherWorker::Running() const {
    std::lock_guard lock(state_->mutex);
    return state_->running;
}
WeatherReading WeatherWorker::Latest() const {
    std::lock_guard lock(state_->mutex);
    return state_->latest;
}
}
