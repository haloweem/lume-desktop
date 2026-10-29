#pragma once
#include "core/diagnostics/diagnostics.h"
#include "core/ui/page_bridge.h"
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace lume::shell {

// Which diagnostics module a page command configures, if any.
std::optional<std::string> DiagnosticModule(ui::PageCommand command);
// Which module a host -> page state message reports on.
std::string DiagnosticModuleForMessage(const nlohmann::json& message);
// Identifies the newest event, so callers can tell whether a message added one.
std::string LatestEventIdentity(const nlohmann::json& snapshot);

// Pairs a page command with the state message that completes it: while a
// module has a pending "configure" operation, its next completing state
// message is recorded under that operation and closes it.
class DiagnosticOperations final {
public:
    explicit DiagnosticOperations(diagnostics::Collector& collector) : collector_(collector) {}
    // Starts an operation for the command's module, cancelling an older one.
    // taskbarRevision is read only for taskbar commands.
    void Begin(ui::PageCommand command, const std::function<std::uint64_t()>& taskbarRevision);
    void Record(std::string_view bytes);
    void Clear();
private:
    diagnostics::Collector& collector_;
    std::mutex mutex_;
    std::map<std::string, std::string> pending_;
    std::uint64_t pendingTaskbarRevision_ = 0;
};

}  // namespace lume::shell
