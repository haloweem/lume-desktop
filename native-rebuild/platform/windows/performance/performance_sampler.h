#pragma once
#include "core/performance/performance_metrics.h"
#include <memory>

namespace lume::platform::windows {
// Owns one lazy PDH query. Construct/read/destroy on the sampling thread.
class PerformanceSampler final {
public:
    PerformanceSampler();
    ~PerformanceSampler();
    PerformanceSampler(const PerformanceSampler&) = delete;
    PerformanceSampler& operator=(const PerformanceSampler&) = delete;
    performance::RawSample Read();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
