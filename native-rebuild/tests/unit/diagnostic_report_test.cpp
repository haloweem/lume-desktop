#include "apps/shell/diagnostic_report.h"
#include "apps/shell/self_check.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace lume;
namespace fs = std::filesystem;
namespace {
int failures = 0;
void Check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; } }
void Write(const fs::path& file, const std::string& text) { std::ofstream(file, std::ios::binary) << text; }
std::string Read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
bool Has(const std::string& zip, const std::string& name) { return zip.find(name) != std::string::npos; }
}

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = fs::path(temp) / (L"lume-diagnostic-report-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(root);
    const auto data = root / L"Data", logs = data / L"Diagnostics", out = root / L"out";
    fs::create_directories(logs / L"crashes");
    fs::create_directories(out);
    Write(data / L"dock-preferences.json", R"({"enabled":true})");
    Write(data / L"account-session.json", R"({"token":"SECRET-TOKEN"})");
    Write(data / L"weather-location.json", R"({"city":"PRIVATE-CITY"})");
    Write(logs / L"crashes" / L"LumeShell-1.dmp", "MDMP-fake");
    Write(data / L"huge-preferences.json", std::string(600 * 1024, 'x'));
    Write(logs / L"crashes" / L"LumeShell-0.dmp", std::string(17u << 20, 'd'));

    platform::windows::diag::Initialize(logs);
    platform::windows::diag::Log("test", "wallpaper player exited, code " + platform::windows::diag::ExitCodeText(0xC0000005));
    Check(Read(logs / L"lume.log").find("0xC0000005 (access violation)") != std::string::npos, "runtime log line and exit code text");

    shell::DiagnosticReportInput input;
    input.dataDirectory = data;
    input.logDirectory = logs;
    input.outputDirectory = out;
    input.installDirectory = root;
    input.version = "test";
    input.note = "TASKBAR-NOTE";
    input.modules = {{"taskbar-state", {{"type", "taskbar-state"}, {"error", "core-start"}}},
                     {"dock-state", {{"type", "dock-state"}, {"error", ""}, {"materialStatus", "ready"}}}};
    const auto result = shell::WriteDiagnosticReport(input);
    Check(result.ok && fs::is_regular_file(result.path), "report written");
    const auto zip = result.ok ? Read(result.path) : std::string{};
    Check(zip.size() > 22 && zip.compare(0, 4, "PK\x03\x04") == 0 && zip.find("PK\x05\x06") != std::string::npos, "zip structure");
    for (const auto* name : {"system.json", "processes.json", "modules.json", "windows-events.txt", "logs/lume.log",
                             "settings/dock-preferences.json", "crashes/LumeShell-1.dmp"})
        Check(Has(zip, name), name);
    Check(Has(zip, "README.txt"), "summary entry");
    Check(Has(zip, "TASKBAR-NOTE") && Has(zip, "taskbar-state: error=core-start"), "summary has note and failing module");
    Check(!Has(zip, "dock-state: "), "healthy module not listed as a problem");
    Check(!Has(zip, std::string(4096, 'x')) && Has(zip, "settings/huge-preferences.json (600 KB)"), "oversized settings file left out and named");
    Check(!Has(zip, std::string(4096, 'd')) && Has(zip, "crashes/LumeShell-0.dmp (17408 KB)") && zip.size() < (33u << 20), "oversized dump left out, report under the cap");
    Check(!Has(zip, "SECRET-TOKEN") && !Has(zip, "PRIVATE-CITY"), "account and location excluded");

    Check(shell::ParseDiagnosticReportCommand(LR"({"version":1,"scope":"diagnostic-report","action":"create","note":"任务栏"})")->note == "\xE4\xBB\xBB\xE5\x8A\xA1\xE6\xA0\x8F", "create command");
    Check(shell::ParseDiagnosticReportCommand(LR"({"version":1,"scope":"diagnostic-report","action":"open-logs"})").has_value(), "open logs command");
    for (const auto* action : {L"copy", L"self-check"})
        Check(shell::ParseDiagnosticReportCommand(std::wstring(LR"({"version":1,"scope":"diagnostic-report","action":")") + action + L"\"}").has_value(), "copy and self-check commands");
    const auto text = shell::DiagnosticText(input);
    Check(text.find("Lume 诊断信息") != std::string::npos && text.find("启动自检") != std::string::npos &&
          text.find("日志位置") != std::string::npos && text.find("TASKBAR-NOTE") != std::string::npos, "copied text has version, self-check, log path and note");
    const auto checks = shell::RunSelfChecks(root);
    Check(checks.size() == 5, "five self-checks");
    for (const auto& check : checks)
        Check(check.status != shell::CheckStatus::Ok || check.id == "gpu-decode", "an empty install folder is never reported OK");
    Check(!shell::ParseDiagnosticReportCommand(LR"({"version":1,"scope":"diagnostic-report","action":"delete"})"), "unknown action");
    Check(!shell::ParseDiagnosticReportCommand(LR"({"version":1,"scope":"diagnostics","action":"create"})"), "other scope");

    std::error_code ignored;
    if (GetEnvironmentVariableW(L"LUME_KEEP_REPORT", nullptr, 0)) std::cout << result.path.u8string() << '\n' << text << '\n';
    if (GetEnvironmentVariableW(L"LUME_SELF_CHECK_INSTALL", nullptr, 0)) {
        wchar_t install[MAX_PATH]{};
        GetEnvironmentVariableW(L"LUME_SELF_CHECK_INSTALL", install, MAX_PATH);
        for (const auto& check : shell::RunSelfChecks(install))
            std::cout << shell::CheckStatusName(check.status) << " " << check.id << ": " << check.detail << " | " << check.fix << '\n';
    }
    else fs::remove_all(root, ignored);
    if (!failures) std::cout << "diagnostic report passed\n";
    return failures ? 1 : 0;
}
