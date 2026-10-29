#include "apps/shell/diagnostic_operations.h"
#include <iostream>

using namespace lume;
namespace {
int failures = 0;
void Check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; } }
}

int main() {
    using ui::PageCommand;
    const auto module = [](PageCommand c) { return shell::DiagnosticModule(c).value_or("-"); };
    Check(module(PageCommand::ClockEnable) == "clock" && module(PageCommand::ClockReset) == "clock", "clock range");
    Check(module(PageCommand::CalendarEnable) == "calendar", "calendar range");
    Check(module(PageCommand::CalendarTextColor) == "-" && module(PageCommand::CalendarHighlightColor) == "-", "calendar colours excluded");
    Check(module(PageCommand::WeatherCityNanjing) == "weather" && module(PageCommand::WeatherLocationSettings) == "-", "weather");
    Check(module(PageCommand::RadialCaptureBegin) == "-" && module(PageCommand::RadialShortcutLast) == "radial", "radial");
    Check(module(PageCommand::DockLaunchpadIconScaleLast) == "dock" && module(PageCommand::TaskbarReset) == "taskbar", "dock, taskbar");
    Check(module(PageCommand::Ready) == "-" && module(PageCommand::Invalid) == "-", "non-module commands");
    Check(shell::DiagnosticModuleForMessage({{"type", "settings"}}) == "clock", "clock settings message");
    Check(shell::DiagnosticModuleForMessage({{"type", "calendar-settings"}}) == "calendar", "calendar settings message");
    Check(shell::DiagnosticModuleForMessage({{"type", "dock-state"}}) == "dock", "state suffix");
    Check(shell::DiagnosticModuleForMessage({{"type", "-state"}}) == "-state", "bare suffix kept");
    Check(shell::LatestEventIdentity(nlohmann::json::object()).empty(), "no events");
    Check(shell::LatestEventIdentity({{"events", {{{"module", "dock"}, {"kind", "error"}, {"firstAt", 5}, {"error", "x"}}}}}) ==
          "dock|error|5|x|1", "event identity");
    if (!failures) std::cout << "diagnostic operations passed\n";
    return failures ? 1 : 0;
}
