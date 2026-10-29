#pragma once

#include <filesystem>

// Development-probe helper only. Appends one JSON object and a newline to an
// existing-parent output path. Calls must be serialized by the caller.
//
// Returns whether the record was written and closed successfully. Collection
// failures are still written: inspect diagnosticsCompleteWithinDeclaredCoverage
// and the individual complete/status/error fields before using their counters.
// Heap coverage is ONLY the default process heap, never all process allocators.
// Collection locks that heap briefly and must run outside steady CPU sampling.
// Does not create directories, trim working sets, compact heaps, or read config.
bool WriteProcessMemorySnapshot(const std::filesystem::path& output,
    const char* phase, int round);
