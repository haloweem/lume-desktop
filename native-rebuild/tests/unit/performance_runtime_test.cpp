#include "platform/windows/performance/performance_monitor.h"
#include "platform/windows/performance/performance_sampler.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

using namespace lume::platform::windows;
namespace {
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}
int main() {
    {
        PerformanceSampler sampler;
        const auto sample = sampler.Read();
        Expect(sample.totalMemory > 0 && sample.freeMemory <= sample.totalMemory,
            "reads real physical memory without Node or WMI");
        Expect(sample.cpu && sample.cpu->processors > 0 && sample.cpu->total >= sample.cpu->idle,
            "reads real CPU counters");
        Expect(sample.monotonicMillis > 0, "timestamps are monotonic");
    }
    std::atomic<unsigned> calls{0};
    std::mutex mutex;
    std::condition_variable changed;
    PerformanceMonitor monitor([&] { ++calls; changed.notify_all(); });
    Expect(!monitor.Running() && !monitor.Latest(), "construction has no active sampling");
    auto await = [&](unsigned count) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(10), [&] { return calls >= count; });
    };
    Expect(monitor.Start() && monitor.Start(), "start is idempotent");
    Expect(await(2), "asynchronous sampling publishes first and delta samples");
    const auto second = monitor.Latest();
    Expect(second && second->memory && second->cpu && *second->cpu >= 0 && *second->cpu <= 100,
        "live second sample includes valid CPU and memory");
    monitor.Stop();
    Expect(!monitor.Running() && !monitor.Latest(), "stop clears worker and stale sample");
    const unsigned stopped = calls;
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    Expect(calls == stopped, "no samples or notifications after stop");
    DWORD handlesBefore = 0, handlesAfter = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
    for (unsigned i = 0; i < 8; ++i) {
        const unsigned before = calls;
        Expect(monitor.Start() && await(before + 1), "re-enable restarts sampler");
        monitor.Stop();
    }
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    Expect(handlesAfter <= handlesBefore + 2, "repeated enable/disable does not retain native handles");
    monitor.Stop();
    std::cout << "performance runtime passed; handle delta "
              << static_cast<long>(handlesAfter) - static_cast<long>(handlesBefore) << '\n';
}
