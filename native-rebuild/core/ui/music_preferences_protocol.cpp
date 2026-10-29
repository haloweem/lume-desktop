#include "core/ui/music_preferences_protocol.h"

#include <stdexcept>
#include <utility>

namespace lume::ui {
using Json = nlohmann::json;

std::optional<MusicPreferencesCommand> DecodeMusicPreferencesCommand(const Json& json, std::string* error) {
    if (error) *error = "invalid";
    try {
        if (!json.is_object() || !json.at("version").is_number_integer() || json.at("version") != 1 ||
            json.at("scope") != "music-preferences" || !json.at("action").is_string()) return {};
        MusicPreferencesCommand command;
        const auto& action = json.at("action").get_ref<const std::string&>();
        if (action == "snapshot") {
            if (!music::detail::MusicPreferencesKeys(json, {"version", "scope", "action"})) return {};
        } else {
            if (action == "apply") {
                if (!music::detail::MusicPreferencesKeys(json, {"version", "scope", "action", "request", "revision", "preferences"})) return {};
                command.action = MusicPreferencesAction::Apply;
                music::MusicPreferences next;
                if (!music::DecodeMusicPreferences(json.at("preferences"), next)) return {};
                command.preferences = std::move(next);
            } else if (action == "reset") {
                if (!music::detail::MusicPreferencesKeys(json, {"version", "scope", "action", "request", "revision"})) return {};
                command.action = MusicPreferencesAction::Reset;
            } else return {};
            if (!music::detail::MusicPreferencesInteger(json.at("request"), command.request, true) ||
                !music::detail::MusicPreferencesInteger(json.at("revision"), command.revision)) return {};
        }
        if (error) error->clear();
        return command;
    } catch (...) { return {}; }
}

std::optional<MusicPreferencesCommand> ParseMusicPreferencesPageJson(std::wstring_view input) noexcept {
    try { return DecodeMusicPreferencesCommand(music::detail::ParseMusicPreferencesJson(input)); }
    catch (...) { return {}; }
}

Json EncodeMusicPreferencesMessage(const MusicPreferencesMessage& message) {
    if (message.revision > music::kMusicPreferencesMaxSafeInteger || message.request > music::kMusicPreferencesMaxSafeInteger)
        throw std::invalid_argument("music preferences counter");
    const char* error = "";
    switch (message.result) {
    case music::MusicPreferencesResult::Ready: break;
    case music::MusicPreferencesResult::Invalid: error = "invalid"; break;
    case music::MusicPreferencesResult::Save: error = "save"; break;
    case music::MusicPreferencesResult::Conflict: error = "conflict"; break;
    case music::MusicPreferencesResult::Load: error = "load"; break;
    default: throw std::invalid_argument("music preferences result");
    }
    return Json{{"version", 1}, {"type", "music-preferences-state"}, {"revision", message.revision},
        {"request", message.request}, {"status", *error ? "error" : "ready"}, {"error", error},
        {"preferences", music::EncodeMusicPreferences(message.preferences)}};
}

std::wstring EncodeMusicPreferencesPageState(const MusicPreferencesMessage& message) {
    // ASCII JSON escapes preserve Unicode families when widened for WebView delivery.
    const auto text = EncodeMusicPreferencesMessage(message).dump(-1, ' ', true);
    return std::wstring(text.begin(), text.end());
}
}  // namespace lume::ui
