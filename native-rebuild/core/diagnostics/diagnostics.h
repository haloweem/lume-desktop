#pragma once

#include "vendor/nlohmann/json.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lume::diagnostics {

inline constexpr bool FeatureEnabled = false; // Hidden and disabled in this release; collector retained for later use.

using Json = nlohmann::json;

enum class Action {
    Snapshot,
    Clear,
};

// Strict, versioned command boundary used by the management WebView.
std::optional<Action> ParseCommand(std::wstring_view json) noexcept;

struct Event final {
    std::uint64_t at = 0;
    std::uint64_t firstAt = 0;
    std::uint32_t repeatCount = 1;
    std::string operationId;
    std::string module;
    std::string kind;
    std::string action;
    std::string status;
    std::string error;
    std::string expected;
    std::string actual;
};

// Native diagnostic state is deliberately small and redacted. It stores only
// the latest bounded event history and one compact health record per module.
class Collector final {
public:
    static constexpr std::uint32_t kSchemaVersion = 1;
    static constexpr std::size_t kMaxEvents = 80;
    static constexpr std::size_t kMaxHealthEntries = 64;
    static constexpr std::size_t kMaxFieldLength = 240;
    static constexpr std::size_t kMaxMessageBytes = 64 * 1024;
    static constexpr std::size_t kMaxPersistedBytes = 256 * 1024;

    explicit Collector(std::string path = {});
    ~Collector() = default;

    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;

    // Changes the persistence target and loads its bounded state. An empty
    // path keeps the collector in memory-only mode.
    void SetPath(std::string path);

    // Returns a process-local operation id and tracks its module/action pair.
    // The id can be included in later structured state messages.
    std::string StartOperation(std::string_view module, std::string_view action);

    // Records a terminal operation. Successful operations are retained only
    // in the in-memory active-operation table; failures and mismatches become
    // bounded events.
    bool CompleteOperation(std::string_view operationId, std::string_view status,
                           std::string_view module = {}, std::string_view action = {},
                           std::string_view expected = {}, std::string_view actual = {},
                           std::string_view error = {});

    // Consumes a structured native state message. Only the documented
    // top-level fields are retained; arbitrary payload is discarded.
    bool RecordMessage(std::string_view message);
    bool RecordMessage(const Json& message);

    // Adds an already-redacted event. Empty/unknown event kinds are rejected.
    bool RecordEvent(Event event);

    // Returns a JSON object suitable for the versioned diagnostics-state page
    // message. Snapshot() never exposes active operation internals.
    Json Snapshot() const;
    std::string SnapshotText() const;
    std::string StorageStatus() const;

    // Clears persisted and in-memory events/health. Active operations are
    // cancelled because their identifiers must not survive a user clear.
    bool Clear();

    std::uint64_t Revision() const;

    // Shared by protocol adapters when they need the same bounded text rule.
    static std::string BoundedText(std::string_view value);

private:
    struct Operation {
        std::string module;
        std::string action;
    };

    static std::uint64_t NowMillis();
    static std::string JsonValueText(const Json& value);
    static std::string OperationId();
    static bool IsTerminalStatus(std::string_view status);
    static bool IsFailureStatus(std::string_view status);
    static bool IsMeaningfulError(std::string_view error);

    bool LoadLocked();
    bool PersistLocked() const;
    bool RecordEventLocked(Event event);
    bool UpdateHealthLocked(const Json& message);
    Json SnapshotLocked() const;

    mutable std::mutex mutex_;
    std::string path_;
    std::uint64_t revision_ = 0;
    std::vector<Event> events_;
    Json health_ = Json::object();
    std::unordered_map<std::string, Event> activeMismatches_;
    std::unordered_map<std::string, Operation> operations_;
    bool storageAvailable_ = true;
};

}  // namespace lume::diagnostics
