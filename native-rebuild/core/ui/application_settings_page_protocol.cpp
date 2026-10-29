#include "core/ui/application_settings_page_protocol.h"
#include "vendor/nlohmann/json.hpp"
#include <set>
#include <stdexcept>

namespace lume::ui {
std::optional<ApplicationSettingsPageCommand> ParseApplicationSettingsPageJson(std::wstring_view input) noexcept {
    if (input.empty() || input.size() > 1024) return {};
    try {
        std::string text;
        for (const auto c : input) {
            if (static_cast<unsigned int>(c) > 127) return {};
            text.push_back(static_cast<char>(c));
        }
        using Json = nlohmann::json;
        std::set<std::string> keys;
        const auto json = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 1 || (event == Json::parse_event_t::key && !keys.insert(value.get<std::string>()).second))
                throw std::runtime_error("invalid command");
            return true;
        });
        if (!json.is_object() || !json.contains("version") || !json["version"].is_number_integer() ||
            json["version"] != 1 || json.value("scope", std::string{}) != "app-settings") return {};
        const auto action = json.at("action").get<std::string>();
        ApplicationSettingsPageCommand command;
        bool hasValue = false;
        if (action == "snapshot") command.action = ApplicationSettingsAction::Snapshot;
        else if (action == "checkUpdate") command.action = ApplicationSettingsAction::CheckUpdate;
        else if (action == "downloadUpdate") command.action = ApplicationSettingsAction::DownloadUpdate;
        else if (action == "installUpdate") command.action = ApplicationSettingsAction::InstallUpdate;
        else if (action == "chooseWallpaperDirectory") command.action = ApplicationSettingsAction::ChooseWallpaperDirectory;
        else if (action == "chooseDataDirectory") command.action = ApplicationSettingsAction::ChooseDataDirectory;
        else if (action == "language") { command.action = ApplicationSettingsAction::Language; hasValue = true; }
        else if (action == "balancedMode") { command.action = ApplicationSettingsAction::BalancedMode; hasValue = true; }
        else if (action == "autostart") { command.action = ApplicationSettingsAction::Autostart; hasValue = true; }
        else return {};
        if (json.size() != (hasValue ? 4u : 3u)) return {};
        if (hasValue) {
            const auto& value = json.at("value");
            if (command.action == ApplicationSettingsAction::Language) {
                if (!value.is_string()) return {};
                command.language = value.get<std::string>();
                if (command.language != "zh-CN" && command.language != "en") return {};
            } else {
                if (!value.is_boolean()) return {};
                command.flag = value.get<bool>();
            }
        }
        return command;
    } catch (...) { return {}; }
}
}
