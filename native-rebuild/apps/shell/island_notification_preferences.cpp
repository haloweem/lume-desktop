#include "apps/shell/island_notification_preferences.h"
#include "vendor/nlohmann/json.hpp"
#include <array>
#include <utility>

namespace lume::shell {
namespace {
using Json = nlohmann::json;
using Preferences = IslandNotificationSettings;
constexpr std::size_t MaxFileBytes = 4096, MaxCommandCharacters = 1024;
struct Flag { const char* key; bool Preferences::* member; };
constexpr std::array Flags{
    Flag{"waterEnabled", &Preferences::waterEnabled},
    Flag{"standEnabled", &Preferences::standEnabled}, Flag{"sleepEnabled", &Preferences::sleepEnabled},
    Flag{"offWorkEnabled", &Preferences::offWorkEnabled},
    Flag{"weatherSevere", &Preferences::weatherSevere},
    Flag{"weatherRain", &Preferences::weatherRain}, Flag{"weatherTemperatureDrop", &Preferences::weatherTemperatureDrop},
    Flag{"messagesQQ", &Preferences::messagesQQ},
    Flag{"messagesWeChat", &Preferences::messagesWeChat}, Flag{"messagePreview", &Preferences::messagePreview}};
struct Number { const char* key; int Preferences::* member; int minimum, maximum; };
constexpr std::array Numbers{
    Number{"waterIntervalMinutes", &Preferences::waterIntervalMinutes, 15, 240},
    Number{"waterGoal", &Preferences::waterGoal, 1, 20},
    Number{"standIntervalMinutes", &Preferences::standIntervalMinutes, 15, 240},
    Number{"sleepLeadMinutes", &Preferences::sleepLeadMinutes, 0, 120},
    Number{"temperatureDropDegrees", &Preferences::temperatureDropDegrees, 1, 15}};
bool Time(const std::string& value) {
    if (value.size() != 5 || value[2] != ':') return false;
    for (const auto index : {0, 1, 3, 4}) if (value[index] < '0' || value[index] > '9') return false;
    return (value[0] - '0') * 10 + value[1] - '0' < 24 &&
        (value[3] - '0') * 10 + value[4] - '0' < 60;
}
Json Encode(const Preferences& value) {
    Json result = Json::object();
    for (const auto& field : Flags) result[field.key] = value.*field.member;
    for (const auto& field : Numbers) result[field.key] = value.*field.member;
    result["sleepTime"] = value.sleepTime;
    result["offWorkTime"] = value.offWorkTime;
    result["offWorkStyle"] = value.offWorkStyle;
    return result;
}
bool Set(Preferences& next, const std::string& key, const Json& value) {
    for (const auto& field : Flags) if (key == field.key) {
        if (!value.is_boolean()) return false;
        next.*field.member = value.get<bool>(); return true;
    }
    for (const auto& field : Numbers) if (key == field.key) {
        if (!value.is_number_integer() || value < field.minimum || value > field.maximum) return false;
        next.*field.member = value.get<int>(); return true;
    }
    if (key == "offWorkStyle") {
        if (!value.is_string() || (value != "default" && value != "squidward")) return false;
        next.offWorkStyle = value.get<std::string>(); return true;
    }
    if ((key != "sleepTime" && key != "offWorkTime") || !value.is_string() || !Time(value.get<std::string>())) return false;
    if (key == "sleepTime") next.sleepTime = value.get<std::string>();
    else next.offWorkTime = value.get<std::string>();
    return true;
}
bool DecodeCurrent(const Json& json, Preferences& result) {
    if (!json.is_object()) return false;
    if (json.size() != Flags.size() + Numbers.size() + 3) return false;
    for (const auto& field : Flags) if (!json.contains(field.key)) return false;
    for (const auto& field : Numbers) if (!json.contains(field.key)) return false;
    if (!json.contains("sleepTime") || !json.contains("offWorkTime") || !json.contains("offWorkStyle")) return false;
    Preferences next;
    for (const auto& item : json.items()) if (!Set(next, item.key(), item.value())) return false;
    result = std::move(next); return true;
}
bool Decode(const Json& json, Preferences& result) {
    if (!json.is_object()) return false;
    if (json.contains("offWorkStyle")) return DecodeCurrent(json, result);
    // Version 1 had two exact historical shapes. Validate every old field
    // before folding its master switch into the effective child choices.
    for (const auto* key : {"careEnabled", "weatherEnabled", "messagesEnabled"})
        if (!json.contains(key) || !json[key].is_boolean()) return false;
    const bool hasWork = json.contains("offWorkEnabled");
    if (hasWork != json.contains("offWorkTime") || json.size() != (hasWork ? 20 : 18)) return false;
    auto normalized = json;
    for (const auto* key : {"careEnabled", "weatherEnabled", "messagesEnabled"}) normalized.erase(key);
    if (!hasWork) { normalized["offWorkEnabled"] = false; normalized["offWorkTime"] = "18:00"; }
    normalized["offWorkStyle"] = "default";
    Preferences next;
    if (!DecodeCurrent(normalized, next)) return false;
    if (!json["careEnabled"].get<bool>())
        next.waterEnabled = next.standEnabled = next.sleepEnabled = next.offWorkEnabled = false;
    if (!json["weatherEnabled"].get<bool>())
        next.weatherSevere = next.weatherRain = next.weatherTemperatureDrop = false;
    if (!json["messagesEnabled"].get<bool>()) next.messagesQQ = next.messagesWeChat = false;
    result = std::move(next); return true;
}
bool VersionOne(const Json& value) { return value.is_number_integer() && value == 1; }
std::wstring WideAscii(const Json& value) {
    const auto encoded = value.dump(-1, ' ', true);
    return {encoded.begin(), encoded.end()};
}
std::string SettingsPath(const std::string& islandPath) {
    if (islandPath.empty()) return {};
    const auto dot = islandPath.rfind(".json");
    return dot != std::string::npos && dot + 5 == islandPath.size()
        ? islandPath.substr(0, dot) + "-notifications.json" : islandPath + ".notifications.json";
}
}  // namespace
IslandNotificationPreferences::IslandNotificationPreferences(settings::IFileStore& files, const std::string& islandPath)
    : files_(files), path_(SettingsPath(islandPath)) { Load(); }
void IslandNotificationPreferences::Load() {
    if (path_.empty() || !files_.Exists(path_)) return;
    std::string data;
    if (files_.ReadBounded(path_, MaxFileBytes, data)) {
        const auto value = Json::parse(data, nullptr, false);
        if (value.is_object() && value.size() == 2 && value.contains("version") && value.contains("preferences") &&
            VersionOne(value["version"]) && Decode(value["preferences"], preferences_)) {
            loaded_ = "loaded"; return;
        }
    }
    // Reject the entire payload on malformed fields; a partial migration must
    // never opt the user into receiving notifications.
    preferences_ = Preferences{}; loaded_ = "failed"; error_ = "load";
}
bool IslandNotificationPreferences::Commit(const Preferences& next) {
    if (path_.empty()) { error_ = "save"; return false; }
    const auto data = Json{{"version", 1}, {"preferences", Encode(next)}}.dump();
    const auto temporary = path_ + ".tmp";
    if (data.size() <= MaxFileBytes && files_.Write(temporary, data) && files_.Rename(temporary, path_)) {
        preferences_ = next; loaded_ = "loaded"; error_.clear(); return true;
    }
    files_.Remove(temporary); error_ = "save"; return false;
}
std::wstring IslandNotificationPreferences::State(bool applied, bool saved) const {
    return WideAscii(Json{{"version", 1}, {"type", "island-notifications-state"},
        {"preferences", Encode(preferences_)}, {"revision", revision_}, {"loaded", loaded_},
        {"result", applied ? "applied" : "snapshot"}, {"saved", saved}, {"error", error_},
        {"availability", "not-connected"}, {"receiving", false}});
}
std::wstring IslandNotificationPreferences::State() const { return State(false, false); }
std::optional<std::wstring> IslandNotificationPreferences::Handle(std::wstring_view raw) {
    if (raw.size() > MaxCommandCharacters || raw.find(L"island-notifications") == std::wstring_view::npos) return {};
    const auto message = Json::parse(raw, nullptr, false);
    if (!message.is_object() || !message.contains("scope") || message["scope"] != "island-notifications") return {};
    const bool common = message.contains("version") && VersionOne(message["version"]) &&
        message.contains("action") && message["action"].is_string();
    if (common && message["action"] == "snapshot" && message.size() == 3) return State();
    ++revision_;
    const auto invalid = [&]() { error_ = "invalid"; return State(true, false); };
    if (!common) return invalid();
    Preferences next = preferences_;
    if (message["action"] == "reset" && message.size() == 3) next = Preferences{};
    else if (message["action"] == "set" && message.size() == 5 && message.contains("key") && message["key"].is_string() &&
             message.contains("value")) {
        if (!Set(next, message["key"].get<std::string>(), message["value"])) return invalid();
    } else return invalid();
    const bool saved = Commit(next);
    return State(true, saved);
}
}  // namespace lume::shell
