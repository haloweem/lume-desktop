#include "core/ui/application_settings_page_protocol.h"
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
}
int main() {
    using namespace lume::ui;
    const auto language = ParseApplicationSettingsPageJson(
        LR"({"version":1,"scope":"app-settings","action":"language","value":"en"})");
    Check(language && language->action == ApplicationSettingsAction::Language && language->language == "en",
          "language command reaches the typed backend");
    const auto balanced = ParseApplicationSettingsPageJson(
        LR"({"version":1,"scope":"app-settings","action":"balancedMode","value":false})");
    Check(balanced && !balanced->flag, "false is not treated as a missing value");
    for (const auto* action : {L"snapshot", L"checkUpdate", L"downloadUpdate", L"installUpdate"}) {
        Check(ParseApplicationSettingsPageJson(std::wstring(L"{\"version\":1,\"scope\":\"app-settings\",\"action\":\"") +
            action + L"\"}").has_value(), "read-only actions contain no value");
    }
    Check(ParseApplicationSettingsPageJson(
        LR"({"version":1,"scope":"app-settings","action":"autostart","value":true})").has_value(), "autostart is boolean");
    for (const auto* input : {
        LR"({"version":1,"scope":"app-settings","action":"language","value":"fr"})",
        LR"({"version":1,"scope":"app-settings","action":"language","value":true})",
        LR"({"version":1,"scope":"app-settings","action":"balancedMode","value":"false"})",
        LR"({"version":1,"scope":"app-settings","action":"autostart","value":1})",
        LR"({"version":1,"scope":"app-settings","action":"autostart","value":null})",
        LR"({"version":1,"scope":"app-settings","action":"language"})",
        LR"({"version":1,"scope":"app-settings","action":"snapshot","value":false})",
        LR"({"version":1,"scope":"app-settings","action":"snapshot","extra":0})",
        LR"({"version":1,"scope":"app-settings","action":"checkUpdate","url":"https://evil.test"})",
        LR"({"version":1,"scope":"app-settings","action":"snapshot","action":"installUpdate"})",
        LR"({"version":1,"version":1,"scope":"app-settings","action":"snapshot"})",
        LR"({"version":1,"scope":"app-settings","action":"balancedMode","value":true,"val\u0075e":false})",
        LR"({"version":1.0,"scope":"app-settings","action":"snapshot"})",
        LR"({"version":true,"scope":"app-settings","action":"snapshot"})",
        LR"({"version":2,"scope":"app-settings","action":"snapshot"})",
        LR"({"version":1,"scope":"wallpaper","action":"snapshot"})",
        LR"({"version":1,"scope":"app-settings","action":"unknown"})",
        LR"({"version":1,"scope":"app-settings","action":"snapshot","extra":{}})",
        LR"([])", LR"({})", LR"({"version":1,})"
    }) Check(!ParseApplicationSettingsPageJson(input), "reject malformed, duplicated or over-capable commands");
    Check(!ParseApplicationSettingsPageJson(std::wstring(2048, L' ')), "bounded protocol input");
    std::cout << "application settings protocol passed\n";
}
