#pragma once
#include "core/performance/performance_metrics.h"
#include <functional>
#include <memory>
#include <optional>

namespace lume::platform::windows {
// UI-thread-owned lifecycle. The worker only publishes data and a notification;
// callbacks must not touch UI or stop the monitor. Post a message to the owner.
// Stop joins the worker: no callback and no PDH lease survives its return.
class PerformanceMonitor final {
public:
    explicit PerformanceMonitor(std::function<void()> notify);
    ~PerformanceMonitor();
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;
    bool Start();
    void Stop();
    bool Running() const;
    std::optional<performance::Snapshot> Latest() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
