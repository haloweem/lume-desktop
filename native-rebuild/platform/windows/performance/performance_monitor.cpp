#include "platform/windows/performance/performance_monitor.h"
#include "platform/windows/performance/performance_sampler.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace lume::platform::windows {
struct PerformanceMonitor::State {
    explicit State(std::function<void()> notify) : notify(std::move(notify)) {}
    void Run() {
        try {
            PerformanceSampler sampler;
            std::optional<performance::RawSample> previous;
            for (;;) {
                {
                    std::lock_guard lock(mutex);
                    if (stop) break;
                }
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                auto sample = sampler.Read();
                auto next = performance::CreateSnapshot(sample, previous ? &*previous : nullptr);
                previous = std::move(sample);
                {
                    std::lock_guard lock(mutex);
                    if (stop) break;
                    latest = next;
                }
                if (notify) notify();
                std::unique_lock lock(mutex);
                if (wake.wait_until(lock, deadline, [this] { return stop; })) break;
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
    bool stop = false, running = false;
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
bool PerformanceMonitor::Running() const {
    std::lock_guard lock(state_->mutex);
    return state_->running;
}
std::optional<performance::Snapshot> PerformanceMonitor::Latest() const {
    std::lock_guard lock(state_->mutex);
    return state_->latest;
}
}
