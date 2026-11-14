#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "vendor/nlohmann/json.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lume::platform::windows {

struct WallpaperProcessIdentity {
    DWORD pid = 0;
    std::uint64_t createdFileTime = 0;
};

// A synchronous sampler owned by one sampling thread. Read only the exact process identities
// supplied by the wallpaper controller; never discover processes by name.
// CPU and GPU utilization need consecutive observations. gpuStatus describes
// utilization coverage; dedicated/shared memory fields are independently nullable.
class WallpaperResourceSampler final {
public:
    WallpaperResourceSampler();
    ~WallpaperResourceSampler();
    WallpaperResourceSampler(const WallpaperResourceSampler&) = delete;
    WallpaperResourceSampler& operator=(const WallpaperResourceSampler&) = delete;
    nlohmann::json Read(const std::vector<WallpaperProcessIdentity>& identities);

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Narrow parsing/arithmetic boundaries shared with the resource sampler test.
namespace wallpaper_resource_detail {
struct GpuInstance {
    DWORD pid = 0;
    std::wstring adapter;
    std::wstring engine;
    std::wstring type;
};
struct GpuEngineValue {
    std::wstring name;
    double percent = 0;
};
std::optional<GpuInstance> ParseGpuInstance(std::wstring_view name);
std::optional<double> BusiestGpuEngine(const std::vector<GpuEngineValue>& values,
                                     const std::vector<DWORD>& pids);
std::optional<double> CpuMachinePercent(std::uint64_t previousTicks,
                                       std::uint64_t currentTicks,
                                       std::uint64_t intervalMs, DWORD processors);
}

}  // namespace lume::platform::windows
