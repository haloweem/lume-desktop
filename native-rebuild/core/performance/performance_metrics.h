#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lume::performance {
struct CpuCounters {
    std::uint64_t idle = 0;
    std::uint64_t total = 0; // Includes idle time, as GetSystemTimes kernel does.
    unsigned processors = 0;
};
struct NetworkCounters {
    std::uint64_t id = 0;
    std::uint64_t received = 0;
    std::uint64_t sent = 0;
};
struct GpuEngine {
    std::wstring name;
    double usage = 0;
};
struct RawSample {
    std::uint64_t monotonicMillis = 0;
    std::optional<CpuCounters> cpu;
    std::uint64_t totalMemory = 0;
    std::uint64_t freeMemory = 0;
    std::optional<std::vector<NetworkCounters>> interfaces;
    std::optional<std::vector<GpuEngine>> engines;
};
struct Snapshot {
    std::optional<double> cpu, memory, gpu, download, upload;
    std::uint64_t usedMemory = 0, totalMemory = 0;
    std::uint64_t monotonicMillis = 0;
};
// Pure original-source statistics; missing or first-sample deltas remain absent.
std::optional<double> GpuUsage(const std::optional<std::vector<GpuEngine>>&);
Snapshot CreateSnapshot(const RawSample&, const RawSample* previous = nullptr);
}
