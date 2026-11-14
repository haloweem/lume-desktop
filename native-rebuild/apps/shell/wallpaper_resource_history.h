#pragma once

#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <string>

namespace lume::shell {
// Best-effort, local diagnostic history. Only documented scalar fields are
// persisted; caller payloads, paths, command lines and media URLs are discarded.
// Four fixed files, 2 MiB each. Instances and export readers share one lock.
class WallpaperResourceHistory final {
public:
    explicit WallpaperResourceHistory(std::filesystem::path directory);
    bool Append(const nlohmann::json& record) noexcept;
private:
    std::filesystem::path directory_;
};

struct WallpaperResourceHistoryExport {
    std::string records; // Valid, redacted JSONL in chronological order.
    std::string csv;     // UTF-8 BOM, one row per sampled process; no events.
    std::string summary; // Human-readable Chinese, including missing coverage.
    nlohmann::json summaryState;
};

// Reads at most 8 MiB / 12,000 records from the four fixed files and exports
// only the last 72 hours. Corrupt and incomplete lines never escape as JSONL.
WallpaperResourceHistoryExport ReadWallpaperResourceHistory(const std::filesystem::path& directory) noexcept;
} // namespace lume::shell
