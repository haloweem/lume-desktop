#include "platform/windows/wallpaper_resource_sampler.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace lume::platform::windows;
using namespace lume::platform::windows::wallpaper_resource_detail;
namespace {
void Check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
bool Near(std::optional<double> value, double expected) {
    return value && std::abs(*value - expected) < 0.000001;
}
std::uint64_t Ticks(FILETIME value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
void PureBoundaries() {
    const auto engine = ParseGpuInstance(L"pid_123_luid_0x00000000_0xABCD_phys_0_eng_2_engtype_VideoDecode");
    Check(engine && engine->pid == 123 && engine->type == L"VideoDecode" &&
          engine->engine == L"luid_0x0_0xabcd_phys_0_eng_2", "engine identity includes adapter physical node and engine");
    const auto memory = ParseGpuInstance(L"pid_123_luid_0x0_0xabcd_phys_1");
    Check(memory && memory->engine.empty() && memory->adapter == L"luid_0x0_0xabcd_phys_1", "memory instance parsing");
    Check(ParseGpuInstance(L"pid_123_luid_0x0_0xabcd_phys_0_eng_2_engtype_Copy#1").has_value(), "PDH duplicate suffix is bounded and accepted");
    for (const auto* invalid : {L"pid_12x_luid_0x0_0x1_phys_0", L"pid_0_luid_0x0_0x1_phys_0",
             L"pid_4294967296_luid_0x0_0x1_phys_0", L"pid_123_luid_x_0x1_phys_0",
             L"pid_123_luid_0x100000000_0x1_phys_0",
             L"pid_123_luid_0x0_0x1_phys_0_extra", L"pid_123_luid_0x0_0x1_phys_0_eng_1_engtype_",
             L"pid_123_luid_0x0_0x1_phys_0_eng_1_engtype_Copy#x"})
        Check(!ParseGpuInstance(invalid), "malformed PID or hardware identity is rejected");
    std::vector<GpuEngineValue> values{
        {L"pid_12_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 20},
        {L"pid_13_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 35},
        {L"pid_12_luid_0x0_0x1_phys_0_eng_1_engtype_Copy", 40},
        {L"pid_12_luid_0x0_0x2_phys_0_eng_0_engtype_3D", 45},
        {L"pid_120_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 99},
        {L"pid_12_luid_0x0_0x1_phys_0_eng_0_engtype_Alias#1", 19},
        {L"pid_12_luid_0x0_0x1_phys_0_eng_2_engtype_Compute", std::numeric_limits<double>::quiet_NaN()},
        {L"pid_12_luid_0x0_0x1_phys_0_eng_3_engtype_Compute", -1}};
    Check(Near(BusiestGpuEngine(values, {12, 13}), 55), "sum target PIDs on each engine then take busiest engine");
    Check(Near(BusiestGpuEngine(values, {12}), 45), "PID matching uses full numeric boundary and adapters stay separate");
    Check(!BusiestGpuEngine(values, {14}) && !BusiestGpuEngine({}, {12}), "missing GPU instances stay unavailable not idle zero");
    Check(Near(BusiestGpuEngine({{L"pid_12_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 0}}, {12}), 0),
          "valid zero GPU engine observation remains a measured idle value");
    values.push_back({L"pid_13_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 95});
    Check(Near(BusiestGpuEngine(values, {12, 13}), 100), "engine aggregate saturates without adding unrelated engines");
    Check(Near(CpuMachinePercent(100, 300000100, 60000, 8), 6.25), "minute CPU ticks normalized across whole machine");
    Check(Near(CpuMachinePercent(100, 100, 60000, 8), 0), "observed idle CPU is a real zero");
    Check(!CpuMachinePercent(100, 99, 60000, 8) && !CpuMachinePercent(100, 101, 0, 8) &&
          !CpuMachinePercent(100, 101, 60000, 0), "CPU resets zero intervals and missing topology are unavailable");
}
void RealIdentity() {
    FILETIME created{}, exited{}, kernel{}, user{};
    Check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE, "read test process identity");
    const WallpaperProcessIdentity identity{GetCurrentProcessId(), Ticks(created)};
    WallpaperResourceSampler sampler;
    const auto unavailable = sampler.Read({{identity.pid, identity.createdFileTime + 1}});
    const auto& rejected = unavailable.at("processes").at(0);
    Check(rejected.at("processStatus") == "identity-mismatch-or-exited", "creation time mismatch is rejected");
    for (const auto* metric : {"privateBytes", "workingSetBytes", "cpuMachinePercent", "cpuIntervalMs",
                               "gpuDedicatedBytes", "gpuSharedBytes", "gpuEnginePercent"})
        Check(rejected.at(metric).is_null(), "unverified identities never acquire resource data");
    Check(unavailable.at("gpuStatus") == "unavailable" && unavailable.at("wallpaperGpuEnginePercent").is_null(),
          "no valid whitelist process leaves GPU unavailable");
    const auto first = sampler.Read({identity, identity});
    Check(first.at("processes").size() == 1, "duplicate controller identities are sampled once");
    const auto& initial = first.at("processes").at(0);
    Check(initial.at("createdFileTime") == std::to_string(identity.createdFileTime), "FILETIME identity preserves exact JSON precision as a string");
    Check(initial.at("cpuMachinePercent").is_null() && initial.at("cpuIntervalMs").is_null(), "first CPU sample only establishes baseline");
    Check(initial.at("privateBytes").get<std::uint64_t>() > 0 && initial.at("workingSetBytes").get<std::uint64_t>() > 0,
          "real whitelisted process supplies commit and working set bytes");
    Check(first.at("atUnixMs").get<std::uint64_t>() > 1700000000000ull &&
          first.at("logicalProcessors").get<DWORD>() > 0, "time and processor metadata available");
    Sleep(40);
    const auto second = sampler.Read({identity});
    const auto& measured = second.at("processes").at(0);
    Check(measured.at("cpuIntervalMs").get<std::uint64_t>() > 0, "second CPU sample records its actual elapsed interval");
    const auto cpu = measured.at("cpuMachinePercent").get<double>();
    Check(std::isfinite(cpu) && cpu >= 0 && cpu <= 100, "real CPU differential is finite and normalized");
    for (const auto* sample : {&first, &second}) {
        const auto& process = sample->at("processes").at(0);
        const auto status = process.at("gpuStatus").get<std::string>();
        Check(status == "warming" || status == "available" || status == "no-matching" || status == "unavailable",
              "GPU availability is explicit even on machines without GPU counters");
        if (status != "available") Check(process.at("gpuEnginePercent").is_null(), "unavailable GPU never becomes fabricated idle zero");
        Check(process.at("gpuEngines").size() <= 8, "GPU breakdown stays bounded");
    }
    const auto empty = sampler.Read({});
    Check(empty.at("processes").empty() && empty.at("gpuStatus") == "unavailable" &&
          empty.at("wallpaperGpuEnginePercent").is_null(), "no wallpaper releases query and clears GPU coverage");
    const auto restarted = sampler.Read({identity});
    Check(restarted.at("processes").at(0).at("cpuMachinePercent").is_null(), "empty whitelist retires the CPU baseline");
    const auto invalid = sampler.Read({{0, 0}});
    Check(invalid.at("processes").at(0).at("processStatus") == "invalid-identity", "zero identity is never opened");
}
}
int main() {
    PureBoundaries();
    RealIdentity();
    std::puts("wallpaper resource sampler passed: exact identities, interval CPU and bounded GPU counters");
}
