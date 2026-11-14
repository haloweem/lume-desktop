#include "platform/windows/performance/performance_monitor.h"
#include "platform/windows/performance/performance_sampler.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace lume::platform::windows {
namespace {
// Mirrored widgets share the expensive system/PDH query while keeping their
// own UI subscriptions. The final hidden/stopped consumer releases the query.
struct SharedSampler {
    std::mutex mutex;
    PerformanceSampler sampler;
    std::optional<performance::RawSample> previous;
    std::optional<performance::Snapshot> latest;
    std::chrono::steady_clock::time_point next{};
    std::optional<performance::Snapshot> Read() {
        std::lock_guard lock(mutex);
        const auto now=std::chrono::steady_clock::now();
        if(now<next)return latest;
        auto raw=sampler.Read();
        latest=performance::CreateSnapshot(raw,previous?&*previous:nullptr);
        previous=std::move(raw);next=now+std::chrono::seconds(1);
        return latest;
    }
};
std::shared_ptr<SharedSampler> AcquireSampler() {
    static std::mutex mutex;
    static std::weak_ptr<SharedSampler> shared;
    std::lock_guard lock(mutex);
    auto source=shared.lock();
    if(!source){source=std::make_shared<SharedSampler>();shared=source;}
    return source;
}
}
struct PerformanceMonitor::State {
    explicit State(std::function<void()> notify) : notify(std::move(notify)) {}
    void Run() {
        try {
            std::shared_ptr<SharedSampler> sampler;
            for (;;) {
                {
                    std::unique_lock lock(mutex);
                    if (!active) {
                        sampler.reset();
                        // Keep the last real snapshot for a stable first frame
                        // on resume; no query or timestamp advances while hidden.
                        wake.wait(lock,[this]{return stop||active;});
                    }
                    if (stop) break;
                }
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                const bool resumed = !sampler;
                if(resumed)sampler=AcquireSampler();
                auto next = sampler->Read();
                {
                    std::lock_guard lock(mutex);
                    if (stop) break;
                    if (!active) continue;
                    // A newly acquired sampler first establishes CPU deltas.
                    // Retain the old snapshot only for that one baseline read.
                    if (!(resumed && latest && next && !next->cpu && latest->cpu))
                        latest = next;
                }
                if (notify) notify();
                std::unique_lock lock(mutex);
                wake.wait_until(lock, deadline, [this] { return stop||!active; });
                if(stop)break;
            }
        } catch (...) {
            // A failed source never crashes the UI or leaks a PDH query. Clear
            // stale statistics and notify the owner to present unavailable.
            {
                std::lock_guard lock(mutex);
                latest.reset();
            }
            try { if (notify) notify(); } catch (...) {}
        }
        std::lock_guard lock(mutex);
        running = false;
    }
    std::function<void()> notify;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::optional<performance::Snapshot> latest;
    bool stop = false, running = false, active = true;
};
PerformanceMonitor::PerformanceMonitor(std::function<void()> notify) : state_(std::make_unique<State>(std::move(notify))) {}
PerformanceMonitor::~PerformanceMonitor() { Stop(); }
bool PerformanceMonitor::Start() {
    if (Running()) return true;
    Stop();
    std::lock_guard lock(state_->mutex);
    state_->stop = false;
    state_->running = true;
    try { state_->worker = std::thread([state = state_.get()] { state->Run(); }); }
    catch (...) { state_->running = false; return false; }
    return true;
}
void PerformanceMonitor::Stop() {
    {
        std::lock_guard lock(state_->mutex);
        state_->stop = true;
    }
    state_->wake.notify_all();
    if (state_->worker.joinable()) state_->worker.join();
    std::lock_guard lock(state_->mutex);
    state_->running = false;
    state_->latest.reset();
}
void PerformanceMonitor::SetActive(bool active) {
    {std::lock_guard lock(state_->mutex);state_->active=active;}
    state_->wake.notify_all();
}
bool PerformanceMonitor::Running() const {
    std::lock_guard lock(state_->mutex);
    return state_->running;
}
std::optional<performance::Snapshot> PerformanceMonitor::Latest() const {
    std::lock_guard lock(state_->mutex);
    return state_->latest;
}
}
