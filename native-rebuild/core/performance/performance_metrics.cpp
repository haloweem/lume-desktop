#include "core/performance/performance_metrics.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace lume::performance {
// Port of original widgets/performance-metrics.js. Group by LUID + physical
// engine, summing process instances; adding separate engines overstates usage.
std::optional<double> GpuUsage(const std::optional<std::vector<GpuEngine>>& engines) {
    if (!engines) return {};
    std::map<std::wstring, double> totals;
    double busiest = 0;
    for (const auto& engine : *engines) {
        if (!std::isfinite(engine.usage) || engine.usage < 0) continue;
        const auto start = engine.name.find(L"_luid_");
        if (start == std::wstring::npos) continue;
        auto& total = totals[engine.name.substr(start)];
        total = std::min(100.0, total + engine.usage);
        busiest = std::max(busiest, total);
    }
    return busiest;
}
Snapshot CreateSnapshot(const RawSample& current, const RawSample* previous) {
    Snapshot result;
    result.monotonicMillis = current.monotonicMillis;
    result.totalMemory = current.totalMemory;
    result.usedMemory = current.totalMemory - std::min(current.totalMemory, current.freeMemory);
    if (current.totalMemory)
        result.memory = static_cast<double>(result.usedMemory) * 100.0 / current.totalMemory;
    result.gpu = GpuUsage(current.engines);
    if (!previous || current.monotonicMillis <= previous->monotonicMillis) return result;
    if (current.cpu && previous->cpu && current.cpu->processors != 0 &&
        current.cpu->processors == previous->cpu->processors &&
        current.cpu->total > previous->cpu->total && current.cpu->idle >= previous->cpu->idle) {
        const auto total = current.cpu->total - previous->cpu->total;
        const auto idle = current.cpu->idle - previous->cpu->idle;
        if (idle <= total) result.cpu = static_cast<double>(total - idle) * 100.0 / total;
    }
    if (current.interfaces && previous->interfaces) {
        const double seconds = static_cast<double>(current.monotonicMillis - previous->monotonicMillis) / 1000.0;
        result.download = 0;
        result.upload = 0;
        for (const auto& adapter : *current.interfaces) {
            const auto found = std::find_if(previous->interfaces->begin(), previous->interfaces->end(),
                [&](const auto& old) { return old.id == adapter.id; });
            if (found == previous->interfaces->end()) continue;
            if (adapter.received >= found->received)
                *result.download += static_cast<double>(adapter.received - found->received) / seconds;
            if (adapter.sent >= found->sent)
                *result.upload += static_cast<double>(adapter.sent - found->sent) / seconds;
        }
    }
    return result;
}
}
