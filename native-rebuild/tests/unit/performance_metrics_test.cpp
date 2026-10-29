#include "core/performance/performance_metrics.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace lume::performance;
namespace {
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool Near(std::optional<double> value, double expected) {
    return value && std::abs(*value - expected) < 0.0001;
}
RawSample Sample(std::uint64_t time, std::uint64_t idle, std::uint64_t total) {
    RawSample sample;
    sample.monotonicMillis = time;
    sample.cpu = CpuCounters{idle, total, 8};
    sample.totalMemory = 16ull * 1024 * 1024 * 1024;
    sample.freeMemory = 4ull * 1024 * 1024 * 1024;
    return sample;
}
}
int main() {
    auto first = Sample(1000, 100, 200);
    auto initial = CreateSnapshot(first);
    Expect(!initial.cpu && !initial.download && !initial.upload && !initial.gpu,
        "first sample is unavailable rather than fabricated zero");
    Expect(Near(initial.memory, 75) && initial.usedMemory == 12ull * 1024 * 1024 * 1024,
        "memory available on first sample with exact byte totals");
    auto next = Sample(2500, 130, 300);
    auto snapshot = CreateSnapshot(next, &first);
    Expect(Near(snapshot.cpu, 70), "CPU includes kernel idle exactly once");
    Expect(snapshot.monotonicMillis == 2500, "monotonic sample time preserved");
    next.cpu->processors = 16;
    Expect(!CreateSnapshot(next, &first).cpu, "processor topology change resets baseline");
    next.cpu->processors = 8;
    next.cpu->idle = 99;
    Expect(!CreateSnapshot(next, &first).cpu, "counter reset cannot underflow");
    next.cpu->idle = 250;
    Expect(!CreateSnapshot(next, &first).cpu, "invalid idle larger than total delta rejected");
    next.cpu.reset();
    Expect(!CreateSnapshot(next, &first).cpu, "unavailable CPU never reuses stale percentage");
    next = Sample(1000, 130, 300);
    Expect(!CreateSnapshot(next, &first).cpu, "zero elapsed time cannot produce rate");
    next.monotonicMillis = 900;
    Expect(!CreateSnapshot(next, &first).cpu, "backward timestamp rejected");
    next = Sample(2500, 130, 300);
    first.interfaces = std::vector<NetworkCounters>{{7, 1000, 100}, {9, 500, 100}};
    next.interfaces = std::vector<NetworkCounters>{{7, 4000, 1600}, {11, 1000000, 50000}};
    snapshot = CreateSnapshot(next, &first);
    Expect(Near(snapshot.download, 2000) && Near(snapshot.upload, 1000),
        "rates use elapsed seconds and stable interface identity, ignore new/removed adapters");
    next.interfaces = std::vector<NetworkCounters>{{7, 10, 50}};
    snapshot = CreateSnapshot(next, &first);
    Expect(Near(snapshot.download, 0) && Near(snapshot.upload, 0), "network counter resets do not spike");
    next.interfaces.reset();
    Expect(!CreateSnapshot(next, &first).download, "network failure not reported as zero traffic");
    Expect(!GpuUsage(std::nullopt), "GPU not supported stays unavailable");
    std::vector<GpuEngine> engines{
        {L"pid_1_luid_0x0_0x10_phys_0_eng_0_engtype_3D", 20},
        {L"pid_2_luid_0x0_0x10_phys_0_eng_0_engtype_3D", 35},
        {L"pid_1_luid_0x0_0x10_phys_0_eng_1_engtype_Copy", 40},
        {L"pid_1_luid_0x0_0x20_phys_0_eng_0_engtype_3D", 45},
        {L"not_an_engine", 100},
        {L"pid_3_luid_0x0_0x10_phys_0_eng_0_engtype_3D", -4},
        {L"pid_4_luid_0x0_0x10_phys_0_eng_0_engtype_3D", std::numeric_limits<double>::quiet_NaN()}
    };
    Expect(Near(GpuUsage(engines), 55), "GPU aggregates processes per hardware engine then takes busiest");
    engines.push_back({engines[0].name, 80});
    Expect(Near(GpuUsage(engines), 100), "GPU percentage saturates at 100");
    Expect(Near(GpuUsage(std::vector<GpuEngine>{}), 0), "successful empty GPU sample is idle");
    next.totalMemory = 0;
    Expect(!CreateSnapshot(next).memory, "memory API failure stays unavailable");
    std::cout << "performance metrics passed\n";
}
