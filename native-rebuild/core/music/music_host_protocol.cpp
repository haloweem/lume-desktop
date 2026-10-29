#include "core/music/music_host_protocol.h"

#include "vendor/nlohmann/json.hpp"

#include <cmath>

namespace lume::music {
namespace {

bool ValidText(const nlohmann::json& value, std::size_t max) {
    return value.is_string() && value.get_ref<const std::string&>().size() <= max;
}

bool HasOnlyKnownValue(const nlohmann::json& object, const char* key) {
    return !object.contains(key) || object.at(key).is_number();
}

}  // namespace

std::optional<HostCommand> DecodeHostCommand(std::string_view text) {
    if (text.empty() || text.size() > kMaxMusicHostFrameBytes) return std::nullopt;
    try {
        const auto json = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
        if (json.is_discarded() || !json.is_object() || json.value("version", 0) != 1 ||
            !ValidText(json.value("session", nlohmann::json{}), 128) ||
            !json.contains("sequence") || !json.at("sequence").is_number_unsigned() ||
            json.at("sequence").get<std::uint64_t>() == 0 ||
            !ValidText(json.value("action", nlohmann::json{}), 32)) {
            return std::nullopt;
        }
        HostCommand command;
        command.version = 1;
        command.session = json.at("session").get<std::string>();
        command.sequence = json.at("sequence").get<std::uint64_t>();
        command.action = json.at("action").get<std::string>();
        static constexpr const char* actions[] = {
            "hello", "load", "play", "pause", "toggle", "next", "previous", "seek", "close",
            "setVolume", "setMuted", "prepare"};
        bool supported = false;
        for (const auto* action : actions) supported = supported || command.action == action;
        if (!supported) return std::nullopt;
        if (json.contains("follow")) {
            if (command.action != "prepare" || !json.at("follow").is_boolean()) return std::nullopt;
            command.follow = json.at("follow").get<bool>();
        }
        if (json.contains("analysis")) {
            if (command.action != "hello" || !json.at("analysis").is_boolean()) return std::nullopt;
            command.analysis = json.at("analysis").get<bool>();
        }
        if (command.action == "setMuted") {
            if (!json.contains("value") || !json.at("value").is_boolean()) return std::nullopt;
            command.flag = json.at("value").get<bool>();
        } else if (!HasOnlyKnownValue(json, "value")) return std::nullopt;
        if (json.contains("value") && command.action != "setMuted") {
            command.value = json.at("value").get<double>();
            command.hasValue = true;
            if (!std::isfinite(command.value) || command.value < 0.0 || command.value > 1e9) return std::nullopt;
        }
        if (((command.action == "seek" || command.action == "prepare" || command.action == "setVolume") && !command.hasValue) ||
            (command.action != "seek" && command.action != "prepare" && command.action != "setVolume" && command.action != "load" && command.hasValue) ||
            (command.action == "setVolume" && command.value > 1.0)) {
            return std::nullopt;
        }
        if (command.action == "load") {
            if (!ValidText(json.value("source", nlohmann::json{}), 4096)) return std::nullopt;
            command.source = json.at("source").get<std::string>();
        } else if (json.contains("source")) {
            return std::nullopt;
        }
        return command;
    } catch (...) {
        return std::nullopt;
    }
}

std::string EncodeHostCommand(const HostCommand& command) {
    nlohmann::json json{
        {"version", command.version}, {"session", command.session},
        {"sequence", command.sequence}, {"action", command.action},
    };
    if (command.action == "load") json["source"] = command.source;
    if (command.action == "seek" || command.action == "prepare" || command.action == "setVolume") json["value"] = command.value;
    if (command.action == "prepare") json["follow"] = command.follow;
    if (command.action == "setMuted") json["value"] = command.flag;
    if (command.action == "hello" && command.analysis) json["analysis"] = true;
    return json.dump();
}

std::optional<HostEvent> DecodeHostEvent(std::string_view text) {
    if (text.empty() || text.size() > kMaxMusicHostFrameBytes) return std::nullopt;
    try {
        const auto json = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
        if (json.is_discarded() || !json.is_object() || json.value("version", 0) != 1 ||
            !ValidText(json.value("session", nlohmann::json{}), 128) ||
            !json.contains("sequence") || !json.at("sequence").is_number_unsigned() ||
            !ValidText(json.value("status", nlohmann::json{}), 16) ||
            !ValidText(json.value("trackId", nlohmann::json{}), 128) ||
            !ValidText(json.value("error", nlohmann::json{}), 128)) return std::nullopt;
        HostEvent event;
        event.session = json.at("session").get<std::string>();
        event.sequence = json.at("sequence").get<std::uint64_t>();
        event.status = json.at("status").get<std::string>();
        event.trackId = json.at("trackId").get<std::string>();
        event.error = json.at("error").get<std::string>();
        event.position = json.value("position", 0.0);
        event.duration = json.value("duration", 0.0);
        event.playing = json.value("playing", false);
        event.ready = json.value("ready", true);
        event.canPrevious = json.value("canPrevious", false);
        event.canNext = json.value("canNext", false);
        event.canToggle = json.value("canToggle", false);
        event.canSeek = json.value("canSeek", false);
        event.volume = json.value("volume", 1.0);
        event.muted = json.value("muted", false);
        if (!std::isfinite(event.volume) || event.volume < 0.0 || event.volume > 1.0) return std::nullopt;
        if (json.contains("analysis")) {
            const auto& analysis = json.at("analysis");
            if (!analysis.is_object() || !analysis.contains("energy") || !analysis.at("energy").is_number() ||
                !analysis.contains("bins") || !analysis.at("bins").is_array() ||
                analysis.at("bins").size() != kMusicAnalysisBins) return std::nullopt;
            event.analysis.energy = analysis.at("energy").get<double>();
            if (!std::isfinite(event.analysis.energy) || event.analysis.energy < 0 || event.analysis.energy > 1) return std::nullopt;
            for (std::size_t i = 0; i < kMusicAnalysisBins; ++i) {
                if (!analysis.at("bins")[i].is_number()) return std::nullopt;
                const double value = analysis.at("bins")[i].get<double>();
                if (!std::isfinite(value) || value < 0 || value > 1) return std::nullopt;
                event.analysis.bins[i] = value;
            }
        }
        if (!std::isfinite(event.position) || !std::isfinite(event.duration) ||
            event.position < 0.0 || event.duration < 0.0 || event.position > 1e9 || event.duration > 1e9)
            return std::nullopt;
        return event;
    } catch (...) {
        return std::nullopt;
    }
}

std::string EncodeHostEvent(const HostEvent& event) {
    const nlohmann::json json = {
        {"version", 1},
        {"session", event.session},
        {"sequence", event.sequence},
        {"status", event.status},
        {"trackId", event.trackId},
        {"position", event.position},
        {"duration", event.duration},
        {"playing", event.playing},
        {"ready", event.ready},
        {"canPrevious", event.canPrevious},
        {"canNext", event.canNext},
        {"canToggle", event.canToggle},
        {"canSeek", event.canSeek},
        {"error", event.error},
        {"volume", event.volume}, {"muted", event.muted},
        {"analysis", {{"energy", event.analysis.energy}, {"bins", event.analysis.bins}}},
    };
    return json.dump();
}

}  // namespace lume::music
