#include "core/ui/music_page_protocol.h"

#include "vendor/nlohmann/json.hpp"

#include <algorithm>
#include <cmath>

namespace lume::ui {
namespace {

using Json = nlohmann::json;

bool ValidText(const Json& value, std::size_t max) {
    return value.is_string() && value.get_ref<const std::string&>().size() <= max;
}

bool IsSimple(MusicAction action) {
    switch (action) {
    case MusicAction::Snapshot:
    case MusicAction::Open:
    case MusicAction::Close:
    case MusicAction::Login:
    case MusicAction::Logout:
    case MusicAction::LoadHome:
    case MusicAction::Play:
    case MusicAction::Pause:
    case MusicAction::Toggle:
    case MusicAction::Previous:
    case MusicAction::Next:
    case MusicAction::ClearQueue:
        return true;
    default:
        return false;
    }
}

std::string Status(music::PlaybackStatus value) {
    switch (value) {
    case music::PlaybackStatus::Ready: return "ready";
    case music::PlaybackStatus::Playing: return "playing";
    case music::PlaybackStatus::Paused: return "paused";
    case music::PlaybackStatus::Error: return "error";
    case music::PlaybackStatus::Empty: return "empty";
    }
    return "empty";
}

std::string Repeat(music::RepeatMode value) {
    switch (value) {
    case music::RepeatMode::One: return "one";
    case music::RepeatMode::All: return "all";
    case music::RepeatMode::None: return "none";
    }
    return "none";
}

Json Track(const music::Track& value) {
    auto result = Json{
        {"id", value.id}, {"title", value.title}, {"artist", value.artist},
        {"album", value.album}, {"artwork", value.artwork},
        {"duration", value.duration}, {"vipRequired", value.vipRequired},
    };
    if (!value.membershipBadge.empty()) result["membershipBadge"] = value.membershipBadge;
    return result;
}

Json Snapshot(const music::MusicSnapshot& value) {
    Json queue = Json::array();
    for (const auto& track : value.queue) queue.push_back(Track(track));
    return Json{
        {"status", Status(value.status)},
        {"queue", std::move(queue)},
        {"current", value.current ? Track(*value.current) : Json(nullptr)},
        {"currentIndex", value.currentIndex},
        {"timeline", {{"position", value.position},
            {"duration", value.current ? value.current->duration : 0.0}}},
        {"playing", value.playing},
        {"repeat", Repeat(value.repeat)},
        {"shuffle", value.shuffle},
        {"volume", value.volume}, {"muted", value.muted},
        {"analysis", {{"energy", value.analysis.energy}, {"bins", value.analysis.bins}}},
        {"session", value.session},
        {"revision", value.revision},
        {"capabilities", {
            {"canPrevious", !value.queue.empty()},
            {"canNext", !value.queue.empty()},
            {"canToggle", value.current.has_value()},
            {"canSeek", value.current.has_value()},
        }},
    };
}

std::string Utf8(std::wstring_view value) {
    std::string result;
    for (std::size_t index = 0; index < value.size(); ++index) {
        std::uint32_t codepoint = value[index];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && index + 1 < value.size()) {
            const auto low = static_cast<std::uint32_t>(value[index + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                ++index;
            }
        }
        if (codepoint <= 0x7F) result.push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7FF) {
            result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0x10FFFF) {
            result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }
    return result;
}

std::wstring Wide(std::string_view value) {
    std::wstring result;
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index++]);
        std::uint32_t codepoint = 0;
        std::size_t continuation = 0;
        if (first <= 0x7F) codepoint = first;
        else if ((first & 0xE0) == 0xC0) { codepoint = first & 0x1F; continuation = 1; }
        else if ((first & 0xF0) == 0xE0) { codepoint = first & 0x0F; continuation = 2; }
        else if ((first & 0xF8) == 0xF0) { codepoint = first & 0x07; continuation = 3; }
        else { result.push_back(L'\uFFFD'); continue; }
        bool valid = index + continuation <= value.size();
        for (std::size_t part = 0; valid && part < continuation; ++part) {
            const auto next = static_cast<unsigned char>(value[index++]);
            if ((next & 0xC0) != 0x80) valid = false;
            else codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if (!valid || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            result.push_back(L'\uFFFD');
        } else if (codepoint <= 0xFFFF) {
            result.push_back(static_cast<wchar_t>(codepoint));
        } else {
            codepoint -= 0x10000;
            result.push_back(static_cast<wchar_t>(0xD800 | (codepoint >> 10)));
            result.push_back(static_cast<wchar_t>(0xDC00 | (codepoint & 0x3FF)));
        }
    }
    return result;
}

bool AddSession(const Json& json, MusicPageCommand& command) {
    if (!json.contains("session")) return true;
    if (!json.at("session").is_number_unsigned()) return false;
    command.session = json.at("session").get<std::uint64_t>();
    return command.session != 0;
}

std::size_t ExpectedSize(std::size_t base, const Json& json) {
    return base + (json.contains("session") ? 1u : 0u);
}

}  // namespace

std::optional<MusicPageCommand> ParseMusicPageJson(std::wstring_view input) noexcept {
    try {
        if (input.empty() || input.size() > 4096) return {};
        const auto json = Json::parse(Utf8(input), nullptr, false);
        if (json.is_discarded() || json.dump().size() > 8192) return {};
        if (!json.is_object() || json.value("version", 0) != 1 ||
            json.value("scope", std::string{}) != "music" ||
            !json.contains("action") || !json.at("action").is_string()) return {};
        const auto action = json.at("action").get<std::string>();
        MusicPageCommand command;
        if (action == "snapshot") command.action = MusicAction::Snapshot;
        else if (action == "open") command.action = MusicAction::Open;
        else if (action == "close") command.action = MusicAction::Close;
        else if (action == "visibility") command.action = MusicAction::Visibility;
        else if (action == "login") command.action = MusicAction::Login;
        else if (action == "logout") command.action = MusicAction::Logout;
        else if (action == "loadHome") command.action = MusicAction::LoadHome;
        else if (action == "loadLyrics") command.action = MusicAction::LoadLyrics;
        else if (action == "loadScore") command.action = MusicAction::LoadScore;
        else if (action == "play") command.action = MusicAction::Play;
        else if (action == "pause") command.action = MusicAction::Pause;
        else if (action == "toggle") command.action = MusicAction::Toggle;
        else if (action == "previous") command.action = MusicAction::Previous;
        else if (action == "next") command.action = MusicAction::Next;
        else if (action == "seek") command.action = MusicAction::Seek;
        else if (action == "playNow") command.action = MusicAction::PlayNow;
        else if (action == "enqueue") command.action = MusicAction::Enqueue;
        else if (action == "removeFromQueue") command.action = MusicAction::RemoveFromQueue;
        else if (action == "clearQueue") command.action = MusicAction::ClearQueue;
        else if (action == "setRepeat") command.action = MusicAction::SetRepeat;
        else if (action == "setShuffle") command.action = MusicAction::SetShuffle;
        else if (action == "insertNext") command.action = MusicAction::InsertNext;
        else if (action == "setVolume") command.action = MusicAction::SetVolume;
        else if (action == "setMuted") command.action = MusicAction::SetMuted;
        else if (action == "setQuality") command.action = MusicAction::SetQuality;
        else return {};
        if (!AddSession(json, command)) return {};

        if (IsSimple(command.action)) {
            if (json.size() != ExpectedSize(3, json)) return {};
        } else if (command.action == MusicAction::Visibility) {
            if (json.size() != ExpectedSize(4, json) || !json.contains("visible") || !json.at("visible").is_boolean()) return {};
            command.flag = json.at("visible").get<bool>();
        } else if (command.action == MusicAction::Seek) {
            if (json.size() != ExpectedSize(4, json) || !json.contains("position") || !json.at("position").is_number()) return {};
            command.position = json.at("position").get<double>();
            if (!std::isfinite(command.position) || command.position < 0.0 || command.position > 1e9) return {};
        } else if (command.action == MusicAction::PlayNow) {
            if (json.size() != ExpectedSize(5, json) || !json.contains("index") || !json.at("index").is_number_unsigned() ||
                !ValidText(json.value("id", Json{}), 128)) return {};
            command.index = json.at("index").get<std::size_t>();
            command.id = json.at("id").get<std::string>();
            if (command.index > 100000 || command.id.empty() || command.id.find('\0') != std::string::npos) return {};
        } else if (command.action == MusicAction::LoadLyrics || command.action == MusicAction::LoadScore ||
                   command.action == MusicAction::RemoveFromQueue) {
            if (json.size() != ExpectedSize(4, json) || !ValidText(json.value("id", Json{}), 128)) return {};
            command.id = json.at("id").get<std::string>();
            if ((command.action == MusicAction::LoadLyrics || command.action == MusicAction::LoadScore) && (command.session == 0 ||
                command.session > 9007199254740991ull || command.id.empty())) return {};
        } else if (command.action == MusicAction::Enqueue || command.action == MusicAction::InsertNext) {
            if (json.size() != ExpectedSize(4, json) || !ValidText(json.value("id", Json{}), 128)) return {};
            command.id = json.at("id").get<std::string>();
            if (command.id.empty() || command.id.find('\0') != std::string::npos) return {};
        } else if (command.action == MusicAction::SetQuality) {
            if (json.size() != ExpectedSize(4, json) || !ValidText(json.value("value", Json{}), 8)) return {};
            command.value = json.at("value").get<std::string>();
            if (!music::ValidAudioQuality(command.value)) return {};
        } else if (command.action == MusicAction::SetRepeat) {
            if (json.size() != ExpectedSize(4, json) || !ValidText(json.value("value", Json{}), 8)) return {};
            command.value = json.at("value").get<std::string>();
            if (command.value != "none" && command.value != "one" && command.value != "all") return {};
        } else if (command.action == MusicAction::SetVolume) {
            if (json.size() != ExpectedSize(4, json) || !json.contains("value") || !json.at("value").is_number()) return {};
            command.volume = json.at("value").get<double>();
            if (!std::isfinite(command.volume) || command.volume < 0.0 || command.volume > 1.0) return {};
        } else if (command.action == MusicAction::SetShuffle || command.action == MusicAction::SetMuted) {
            if (json.size() != ExpectedSize(4, json) || !json.contains("value") || !json.at("value").is_boolean()) return {};
            command.flag = json.at("value").get<bool>();
        }
        return command;
    } catch (...) {
        return {};
    }
}

std::wstring EncodeMusicPageState(const MusicPageState& state) {
    auto json = Snapshot(state.snapshot);
    json["version"] = 1;
    json["type"] = "music-state";
    json["pageOpen"] = state.pageOpen;
    json["pageVisible"] = state.pageVisible;
    json["account"] = {{"signedIn", state.accountSignedIn}};
    json["error"] = state.error;
    if (state.audio) json["audio"] = {{"requestedQuality", state.audio->requestedQuality},
        {"quality", state.audio->quality}, {"bitrate", state.audio->bitrate}, {"switching", state.audio->switching}};
    return Wide(json.dump());
}

std::wstring EncodeMusicLyrics(const MusicLyricsMessage& lyrics) {
    Json lines = Json::array();
    const bool valid = lyrics.trackId.size() <= 128 && lyrics.trackId.find('\0') == std::string::npos &&
        lyrics.session <= 9007199254740991ull && (!lyrics.trackId.empty() || lyrics.lines.empty()) &&
        music::ValidateLyrics(lyrics.lines);
    if (valid) for (const auto& line : lyrics.lines) {
        Json words = Json::array();
        for (const auto& word : line.words)
            words.push_back({{"startTime", word.startTime}, {"endTime", word.endTime}, {"text", word.text}});
        lines.push_back({{"startTime", line.startTime}, {"endTime", line.endTime},
            {"fullText", line.fullText}, {"words", std::move(words)}, {"translation", line.translation}});
    }
    return Wide(Json{{"version", 1}, {"type", "music-lyrics"}, {"session", lyrics.session},
        {"trackId", lyrics.trackId}, {"lines", std::move(lines)}}.dump(-1, ' ', false, Json::error_handler_t::replace));
}

std::wstring EncodeMusicScore(const MusicScoreMessage& score) {
    // Bounded like lyrics: 30 minutes of 20 ms frames, at most 4000 beats.
    const bool valid = score.trackId.size() <= 128 && score.trackId.find('\0') == std::string::npos &&
        !score.trackId.empty() && score.session <= 9007199254740991ull &&
        score.hop >= 0.005 && score.hop <= 0.1 && score.pitch.size() <= 90000 && score.beats.size() <= 4000 &&
        std::isfinite(score.tempo) && score.tempo >= 0 && score.tempo <= 400 &&
        std::all_of(score.pitch.begin(), score.pitch.end(), [](std::uint16_t value) { return value == 0 || (value >= 200 && value <= 1100); }) &&
        std::all_of(score.beats.begin(), score.beats.end(), [](double value) { return std::isfinite(value) && value >= 0 && value <= 86400; });
    const bool span = valid && std::isfinite(score.start) && std::isfinite(score.end) &&
        score.start >= 0 && score.start <= score.end && score.end <= 86400;
    Json json = {{"version", 1}, {"type", "music-score"}, {"session", score.session}, {"trackId", score.trackId},
        {"hop", valid ? score.hop : 0.02}, {"tempo", valid ? std::round(score.tempo * 100) / 100 : 0},
        {"pitch", valid ? Json(score.pitch) : Json::array()}, {"beats", Json::array()},
        {"start", span ? std::round(score.start * 1000) / 1000 : 0}, {"end", span ? std::round(score.end * 1000) / 1000 : 0}};
    if (valid) for (double beat : score.beats) json["beats"].push_back(std::round(beat * 1000) / 1000);
    return Wide(json.dump());
}

std::wstring EncodeMusicWidgetState(const music::MusicSnapshot& snapshot) {
    const auto current = snapshot.current;
    const auto json = Json{
        {"version", 1}, {"type", "music-widget-state"},
        {"session", snapshot.session}, {"revision", snapshot.revision},
        {"title", current ? current->title : ""},
        {"artist", current ? current->artist : ""},
        {"artwork", current ? current->artwork : ""},
        {"status", Status(snapshot.status)}, {"playing", snapshot.playing},
        {"volume", snapshot.volume}, {"muted", snapshot.muted},
        {"position", snapshot.position},
        {"duration", current ? current->duration : 0.0},
        {"capabilities", {
            {"canPrevious", !snapshot.queue.empty()},
            {"canNext", !snapshot.queue.empty()},
            {"canToggle", current.has_value()}, {"canSeek", current.has_value()},
        }},
    };
    return Wide(json.dump());
}

}  // namespace lume::ui
