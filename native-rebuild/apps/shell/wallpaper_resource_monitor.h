#pragma once
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <memory>

namespace lume::shell {
// Owns a low-frequency worker. Updates contain only actor-owned identities and
// redacted state; no player IPC, PDH query or disk write runs on the actor.
class WallpaperResourceMonitor final {
public:
    explicit WallpaperResourceMonitor(std::filesystem::path directory,
                                      unsigned intervalMilliseconds = 60000);
    ~WallpaperResourceMonitor();
    WallpaperResourceMonitor(const WallpaperResourceMonitor&) = delete;
    WallpaperResourceMonitor& operator=(const WallpaperResourceMonitor&) = delete;
    void Update(nlohmann::json processes) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
