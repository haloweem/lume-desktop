#pragma once
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace lume::shell {
// A single zip the user can send when something goes wrong, so the problem
// can be found from evidence instead of a description: system, GPU, display
// and taskbar environment, Lume's process tree, every module's current state,
// Lume's runtime log and crash dumps, Windows' own crash records for Lume
// processes, and the user's preference files. Account data, music accounts,
// weather location and anything credential-like are never included.
struct DiagnosticReportInput {
    std::filesystem::path dataDirectory;  // Lume's Data folder.
    std::filesystem::path logDirectory;   // diag::Directory().
    std::filesystem::path installDirectory;
    std::filesystem::path outputDirectory;  // Empty: the user's desktop.
    std::string version;
    std::string note;           // What the user saw, optional.
    nlohmann::json modules = nlohmann::json::object();  // type -> state message.
};
struct DiagnosticReportResult {
    bool ok = false;
    std::filesystem::path path;
    std::string error;
};
DiagnosticReportResult WriteDiagnosticReport(const DiagnosticReportInput& input);
// The same facts as a short text a user can paste into a comment or chat:
// version, Windows, GPU driver, self-check, failing components, recent
// crashes and where the logs are. No files are written.
std::string DiagnosticText(const DiagnosticReportInput& input);
// Page command {"version":1,"scope":"diagnostic-report","action":"create"|"open-logs","note":"..."}.
struct DiagnosticReportCommand {
    std::string action;  // create | open-logs | copy | self-check
    std::string note;
};
std::optional<DiagnosticReportCommand> ParseDiagnosticReportCommand(std::wstring_view message);
// The environment section alone, also logged once at startup.
nlohmann::json DescribeSystem();
}  // namespace lume::shell
