#include "apps/shell/island_notification_preferences.h"
#include "vendor/nlohmann/json.hpp"
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
using lume::shell::IslandNotificationPreferences;
void Check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct Files final : lume::settings::IFileStore {
    std::map<std::string, std::string> values;
    bool failWrite = false, failRename = false;
    unsigned writes = 0;
    mutable std::size_t lastLimit = 0;
    bool Read(const std::string& path, std::string& out) const override {
        const auto found = values.find(path);
        if (found == values.end()) return false;
        out = found->second; return true;
    }
    bool ReadBounded(const std::string& path, std::size_t limit, std::string& out) const override {
        lastLimit = limit;
        const auto found = values.find(path);
        if (found == values.end() || found->second.size() > limit) return false;
        out = found->second; return true;
    }
    bool Exists(const std::string& path) const override { return values.count(path) != 0; }
    bool Write(const std::string& path, const std::string& value) override {
        ++writes; if (failWrite) return false; values[path] = value; return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        if (failRename || !Exists(from)) return false;
        values[to] = values[from]; values.erase(from); return true;
    }
    bool Remove(const std::string& path) override { values.erase(path); return true; }
};
Json State(const IslandNotificationPreferences& preferences) { return Json::parse(preferences.State()); }
Json Send(IslandNotificationPreferences& preferences, const Json& command) {
    const auto text = command.dump();
    const auto reply = preferences.Handle(std::wstring(text.begin(), text.end()));
    Check(reply.has_value(), "notification command has a native reply");
    return Json::parse(*reply);
}
Json Set(IslandNotificationPreferences& preferences, const char* key, Json value) {
    return Send(preferences, {{"version", 1}, {"scope", "island-notifications"}, {"action", "set"},
        {"key", key}, {"value", std::move(value)}});
}
void NoReceiving(const Json& state) {
    Check(state["receiving"] == false && state["availability"] == "not-connected",
        "page preferences never imply an active notification receiver or granted permission");
}
}  // namespace
int main() {
    try {
        Files files;
        // Legacy display data is deliberately unrelated to the opt-in file.
        files.values["island-display.json"] = R"({"version":1,"chatNotifications":true,"messagesEnabled":true})";
        IslandNotificationPreferences preferences(files, "island.json");
        auto state = State(preferences);
        Check(state["loaded"] == "fresh" && state["revision"] == 0 && files.writes == 0,
            "opening the page does not create or mutate settings");
        const auto defaults = state["preferences"];
        Check(defaults.size() == 18 && !defaults.contains("careEnabled") && !defaults.contains("weatherEnabled") &&
            !defaults.contains("messagesEnabled") && defaults["offWorkTime"] == "18:00" && defaults["offWorkStyle"] == "default",
            "current shape has direct child settings, an 18:00 work time and the unchanged default style");
        for (const auto* key : {"waterEnabled", "standEnabled", "sleepEnabled", "offWorkEnabled", "weatherSevere",
                "weatherRain", "weatherTemperatureDrop", "messagesQQ", "messagesWeChat", "messagePreview"})
            Check(defaults[key] == false, "all reminder and message choices start off independently of legacy chat values");
        NoReceiving(state);
        const Json snapshot{{"version", 1}, {"scope", "island-notifications"}, {"action", "snapshot"}};
        const auto badShape = [&](const Json& value) {
            Files invalidFiles;
            const auto original = Json{{"version", 1}, {"preferences", value}}.dump();
            invalidFiles.values["invalid-notifications.json"] = original;
            IslandNotificationPreferences invalid(invalidFiles, "invalid.json");
            Check(State(invalid)["loaded"] == "failed" && State(invalid)["preferences"] == defaults &&
                invalidFiles.writes == 0 && invalidFiles.values["invalid-notifications.json"] == original,
                "partial, hybrid, unknown and malformed shapes fail closed without replacing the source file");
        };
        for (bool withWork : {false, true}) for (unsigned mask = 0; mask < 8; ++mask) {
            Files legacyFiles;
            auto legacy = defaults;
            legacy.erase("offWorkStyle");
            for (const auto* key : {"waterEnabled", "standEnabled", "sleepEnabled", "weatherSevere",
                    "weatherRain", "weatherTemperatureDrop", "messagesQQ", "messagesWeChat", "messagePreview"}) legacy[key] = true;
            legacy["careEnabled"] = (mask & 1) != 0;
            legacy["weatherEnabled"] = (mask & 2) != 0;
            legacy["messagesEnabled"] = (mask & 4) != 0;
            legacy["standEnabled"] = legacy["weatherRain"] = legacy["messagesWeChat"] = false;
            legacy["sleepTime"] = "00:15"; legacy["waterGoal"] = 13;
            if (withWork) { legacy["offWorkEnabled"] = true; legacy["offWorkTime"] = "17:45"; }
            else { legacy.erase("offWorkEnabled"); legacy.erase("offWorkTime"); }
            Check(legacy.size() == (withWork ? 20 : 18), "migration fixture uses an exact historical field set");
            auto expected = defaults;
            expected["sleepTime"] = "00:15"; expected["waterGoal"] = 13; expected["messagePreview"] = true;
            for (const auto* key : {"waterEnabled", "standEnabled", "sleepEnabled"}) expected[key] = (mask & 1) != 0;
            for (const auto* key : {"weatherSevere", "weatherRain", "weatherTemperatureDrop"}) expected[key] = (mask & 2) != 0;
            for (const auto* key : {"messagesQQ", "messagesWeChat"}) expected[key] = (mask & 4) != 0;
            expected["standEnabled"] = expected["weatherRain"] = expected["messagesWeChat"] = false;
            expected["offWorkEnabled"] = withWork && (mask & 1) != 0;
            expected["offWorkTime"] = withWork ? "17:45" : "18:00";
            legacyFiles.values["legacy-notifications.json"] = Json{{"version", 1}, {"preferences", legacy}}.dump();
            IslandNotificationPreferences migrated(legacyFiles, "legacy.json");
            Check(State(migrated)["loaded"] == "loaded" && State(migrated)["preferences"] == expected && legacyFiles.writes == 0,
                "both old schemas fold all master combinations into effective child opt-ins and preserve other settings");
            Check(Set(migrated, "offWorkStyle", "squidward")["saved"] == true,
                "migrated choices accept the new work reminder style");
            const auto persistedMigration = Json::parse(legacyFiles.values["legacy-notifications.json"]);
            expected["offWorkStyle"] = "squidward";
            Check(persistedMigration["preferences"] == expected && persistedMigration["version"] == 1,
                "first confirmed save writes only the current fields without changing version one");
            IslandNotificationPreferences migratedReopened(legacyFiles, "legacy.json");
            Check(State(migratedReopened)["preferences"] == expected, "migrated effective choices and style survive reopening");
            legacyFiles.failRename = true;
            Check(Set(migratedReopened, "offWorkStyle", "default")["saved"] == false &&
                migratedReopened.Snapshot().offWorkStyle == "squidward" &&
                Json::parse(legacyFiles.values["legacy-notifications.json"])["preferences"] == expected,
                "failed style save preserves confirmed settings and the original preferences file");
            for (const auto& item : legacy.items()) { auto partial = legacy; partial.erase(item.key()); badShape(partial); }
            auto unknown = legacy; unknown["extra"] = false; badShape(unknown);
            auto malformedMaster = legacy; malformedMaster["careEnabled"] = 0; badShape(malformedMaster);
            auto malformedChild = legacy; malformedChild["waterEnabled"] = "true"; badShape(malformedChild);
        }
        for (const auto& item : defaults.items()) { auto partial = defaults; partial.erase(item.key()); badShape(partial); }
        for (const auto* key : {"careEnabled", "weatherEnabled", "messagesEnabled"}) {
            auto hybrid = defaults; hybrid[key] = true; badShape(hybrid);
        }
        auto invalidStyle = defaults; invalidStyle["offWorkStyle"] = "other"; badShape(invalidStyle);
        Check(Send(preferences, snapshot)["preferences"] == defaults && files.writes == 0,
            "snapshot does not enable any module");
        Check(!preferences.Handle(L"{\"version\":1,\"scope\":\"music\",\"action\":\"snapshot\"}"),
            "unrelated messages remain with their owning session");
        state = Set(preferences, "messagesQQ", true);
        Check(state["saved"] == true && state["result"] == "applied" && state["preferences"]["messagesQQ"] == true &&
            state["preferences"]["messagesWeChat"] == false,
            "each message source is independently acknowledged only after storage succeeds");
        NoReceiving(state);
        const auto revision = state["revision"].get<unsigned>();
        IslandNotificationPreferences reopened(files, "island.json");
        state = State(reopened);
        Check(state["loaded"] == "loaded" && state["preferences"]["messagesQQ"] == true && state["preferences"]["messagesWeChat"] == false,
            "manual choices persist independently across reopen");
        NoReceiving(state);
        const auto persisted = files.values.at("island-notifications.json");
        files.failRename = true;
        state = Set(preferences, "messagesQQ", false);
        Check(state["saved"] == false && state["error"] == "save" && state["preferences"]["messagesQQ"] == true &&
            files.values.at("island-notifications.json") == persisted && !files.Exists("island-notifications.json.tmp"),
            "failed atomic replacement keeps confirmed settings and removes its temporary file");
        Check(state["revision"].get<unsigned>() > revision, "save failures carry a newer response revision");
        files.failRename = false; files.failWrite = true;
        state = Set(preferences, "waterGoal", 12);
        Check(state["saved"] == false && state["preferences"]["waterGoal"] == 8, "failed writes never publish requested values");
        files.failWrite = false;
        const auto beforeInvalid = State(preferences)["preferences"];
        for (const auto& input : Json::array({
            {{"key", "careEnabled"}, {"value", true}}, {{"key", "weatherEnabled"}, {"value", true}},
            {{"key", "messagesEnabled"}, {"value", true}}, {{"key", "waterEnabled"}, {"value", 1}},
            {{"key", "waterIntervalMinutes"}, {"value", 14}}, {{"key", "standIntervalMinutes"}, {"value", 241}},
            {{"key", "waterGoal"}, {"value", 0}}, {{"key", "sleepLeadMinutes"}, {"value", 121}},
            {{"key", "temperatureDropDegrees"}, {"value", 16}}, {{"key", "waterGoal"}, {"value", 3.5}},
            {{"key", "sleepTime"}, {"value", "24:00"}}, {{"key", "sleepTime"}, {"value", "8:00"}},
            {{"key", "offWorkEnabled"}, {"value", 1}}, {{"key", "offWorkTime"}, {"value", "24:00"}},
            {{"key", "offWorkTime"}, {"value", "8:00"}}, {{"key", "offWorkTime"}, {"value", "18:60"}},
            {{"key", "offWorkTime"}, {"value", 1800}},
            {{"key", "offWorkStyle"}, {"value", ""}}, {{"key", "offWorkStyle"}, {"value", "Squidward"}},
            {{"key", "offWorkStyle"}, {"value", "other"}}, {{"key", "offWorkStyle"}, {"value", true}},
            {{"key", "sleepTime"}, {"value", "23:60"}}, {{"key", "receiving"}, {"value", true}}})) {
            const auto writes = files.writes;
            state = Set(preferences, input["key"].get<std::string>().c_str(), input["value"]);
            Check(state["saved"] == false && state["error"] == "invalid" && state["preferences"] == beforeInvalid && files.writes == writes,
                "invalid fields, time and range never mutate confirmed preferences");
        }
        auto extra = snapshot; extra["receiving"] = true;
        Check(Send(preferences, extra)["error"] == "invalid", "command envelopes cannot inject host receiver state");
        auto wrongVersion = snapshot; wrongVersion["version"] = 1.0;
        Check(Send(preferences, wrongVersion)["error"] == "invalid", "protocol version must be an integer");
        Check(!preferences.Handle(std::wstring(1025, L'x')), "oversized bridge input is rejected before parse");
        for (const auto& input : Json::array({
            {{"key", "waterIntervalMinutes"}, {"value", 15}}, {{"key", "standIntervalMinutes"}, {"value", 240}},
            {{"key", "waterGoal"}, {"value", 20}}, {{"key", "sleepLeadMinutes"}, {"value", 0}},
            {{"key", "temperatureDropDegrees"}, {"value", 15}}, {{"key", "sleepTime"}, {"value", "00:00"}},
            {{"key", "offWorkEnabled"}, {"value", true}}, {{"key", "offWorkTime"}, {"value", "00:00"}},
            {{"key", "offWorkTime"}, {"value", "23:59"}}, {{"key", "offWorkStyle"}, {"value", "squidward"}},
            {{"key", "offWorkStyle"}, {"value", "default"}}}))
            Check(Set(preferences, input["key"].get<std::string>().c_str(), input["value"])["saved"] == true,
                "valid boundary values save without rounding away the user's setting");
        state = Send(preferences, {{"version", 1}, {"scope", "island-notifications"}, {"action", "reset"}});
        Check(state["saved"] == true && state["preferences"] == defaults, "reset turns all direct child switches off and restores the default style");
        IslandNotificationPreferences resetReloaded(files, "island.json");
        Check(State(resetReloaded)["preferences"] == defaults, "reset survives reopening");
        const auto validFile = files.values.at("island-notifications.json");
        for (const auto& corrupt : {std::string(4097, 'x'), std::string("{invalid"),
                std::string(R"({"version":1,"preferences":{"messagesEnabled":true}})")}) {
            files.values["island-notifications.json"] = corrupt;
            IslandNotificationPreferences broken(files, "island.json");
            state = State(broken);
            Check(files.lastLimit == 4096 && state["loaded"] == "failed" && state["error"] == "load" &&
                state["preferences"] == defaults && files.values.at("island-notifications.json") == corrupt,
                "bounded or corrupt files fail closed without overwriting the user's file");
            NoReceiving(state);
        }
        files.values["island-notifications.json"] = validFile;
        auto malformed = Json::parse(validFile); malformed["preferences"]["messagesQQ"] = true;
        malformed["preferences"]["sleepTime"] = "invalid";
        files.values["island-notifications.json"] = malformed.dump();
        IslandNotificationPreferences partial(files, "island.json");
        Check(!partial.Snapshot().messagesQQ, "a late invalid field cannot leave a partially loaded opt-in");
        IslandNotificationPreferences noPath(files, "");
        state = Set(noPath, "messagesQQ", true);
        Check(state["saved"] == false && state["preferences"]["messagesQQ"] == false,
            "missing storage never pretends opt-in was persisted");
        std::cout << "ISLAND_NOTIFICATION_PREFERENCES_PASS: direct opt-ins, strict old/current migration, styles, persistence, rollback, bounded load\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
