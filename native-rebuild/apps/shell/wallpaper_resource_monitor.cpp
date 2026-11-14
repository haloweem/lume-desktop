#include "apps/shell/wallpaper_resource_monitor.h"
#include "apps/shell/wallpaper_resource_history.h"
#include "platform/windows/wallpaper_resource_sampler.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace lume::shell {
namespace {
using Json = nlohmann::json;
std::uint64_t UnixMillis() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
}
struct WallpaperResourceMonitor::Impl {
    std::mutex mutex;
    std::condition_variable changed;
    Json current = Json::array();
    std::deque<Json> events;
    bool stopping = false, initialized = false;
    unsigned interval;
    WallpaperResourceHistory history;
    const std::string runId = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(UnixMillis());
    std::thread worker;
    Impl(std::filesystem::path directory, unsigned milliseconds)
        : interval(std::max(100u, milliseconds)), history(std::move(directory)), worker([this] { Run(); }) {}
    ~Impl() {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
        changed.notify_one();
        if (worker.joinable()) worker.join();
    }
    Json Record(const char* type, Json processes) const {
        return {{"schemaVersion", 1}, {"type", type}, {"runId", runId}, {"atUnixMs", UnixMillis()},
                {"uptimeMs", GetTickCount64()}, {"processes", std::move(processes)}};
    }
    void Update(Json processes) {
        if (!processes.is_array()) return;
        // The number of displays is already bounded by the playback model.
        if (processes.size() > 32) processes.erase(processes.begin() + 32, processes.end());
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping || (initialized && current == processes)) return;
        initialized = true;
        current = std::move(processes);
        if (events.size() == 64) events.pop_front();
        events.push_back(Record("event", current));
        changed.notify_one();
    }
    void Run() noexcept {
        try {
            platform::windows::WallpaperResourceSampler sampler;
            auto next = std::chrono::steady_clock::now();
            for (;;) {
                Json context;
                std::deque<Json> pending;
                bool finish = false, sample = false;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    if (!initialized) changed.wait(lock, [&] { return stopping || initialized; });
                    else if (current.empty()) changed.wait(lock, [&] { return stopping || !events.empty(); });
                    else changed.wait_until(lock, next, [&] { return stopping || !events.empty(); });
                    finish = stopping;
                    pending.swap(events);
                    context = current;
                    const auto now = std::chrono::steady_clock::now();
                    sample = initialized && now >= next && !finish;
                    if (sample) next = now + std::chrono::milliseconds(interval);
                }
                for (const auto& event : pending) history.Append(event);
                if (finish) break;
                if (context.empty()) { sampler.Read({}); next = std::chrono::steady_clock::now(); continue; }
                if (!sample) continue;
                std::vector<platform::windows::WallpaperProcessIdentity> identities;
                for (const auto& process : context) {
                    const auto pid = process.value("pid", DWORD{0});
                    if (!pid) continue;
                    try {
                        identities.push_back({pid, std::stoull(process.value("createdFileTime", std::string{"0"}))});
                    } catch (...) {}
                }
                auto sampled = sampler.Read(identities);
                // Merge onto the captured policy. Identity matching prevents a
                // replacement process from inheriting old resource counters.
                for (auto& process : context) {
                    process["cpuMachinePercent"] = nullptr;
                    process["privateBytes"] = nullptr;
                    process["workingSetBytes"] = nullptr;
                    process["gpuDedicatedBytes"] = nullptr;
                    process["gpuSharedBytes"] = nullptr;
                    process["gpuEnginePercent"] = nullptr;
                    process["gpuStatus"] = process.value("pid", DWORD{0}) ? "unavailable" : "no-player";
                    process["processStatus"] = process.value("pid", DWORD{0}) ? "unavailable" : "no-player";
                    if (!sampled.contains("processes") || !sampled["processes"].is_array()) continue;
                    for (const auto& row : sampled["processes"]) {
                        if (row.value("pid", DWORD{0}) != process.value("pid", DWORD{0}) ||
                            row.value("createdFileTime", std::string{}) != process.value("createdFileTime", std::string{})) continue;
                        for (const auto* key : {"cpuMachinePercent", "cpuIntervalMs", "privateBytes", "workingSetBytes",
                             "gpuDedicatedBytes", "gpuSharedBytes", "gpuEnginePercent", "gpuStatus", "processStatus"})
                            if (row.contains(key)) process[key] = row[key];
                        break;
                    }
                }
                auto record = Record("sample", std::move(context));
                for (const auto* key : {"atUnixMs", "uptimeMs", "logicalProcessors", "gpuStatus", "wallpaperGpuEnginePercent"})
                    if (sampled.contains(key)) record[key] = sampled[key];
                // Empty configuration stops GPU collection and writes only the
                // final state event. A parked display still records its policy.
                if (!record["processes"].empty()) history.Append(record);
            }
        } catch (...) {
            try { history.Append(Record("event", Json::array({{{"displayId", "0"}, {"pid", 0},
                {"state", "failed"}, {"error", "WALLPAPER_RESOURCE_MONITOR_FAILED"}}}))); } catch (...) {}
        }
    }
};
WallpaperResourceMonitor::WallpaperResourceMonitor(std::filesystem::path directory, unsigned intervalMilliseconds)
    : impl_(std::make_unique<Impl>(std::move(directory), intervalMilliseconds)) {}
WallpaperResourceMonitor::~WallpaperResourceMonitor() = default;
void WallpaperResourceMonitor::Update(nlohmann::json processes) noexcept {
    try { impl_->Update(std::move(processes)); } catch (...) {}
}
}
