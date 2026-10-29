#include "core/music/music_library.h"
#include "core/music/music_model.h"
#include "vendor/nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace lume::music {
namespace {
using Json = nlohmann::json;

bool Text(const std::string& value, std::size_t max) {
    return value.size() <= max && value.find('\0') == std::string::npos &&
        Json::accept(Json(value).dump(-1, ' ', false, Json::error_handler_t::strict));
}

bool Id(std::string_view value) {
    return value.size() > 6 && value.size() <= 128 && value.substr(0, 6) == "local-" &&
        value.find_first_not_of("0123456789", 6) == value.npos;
}

bool Valid(const LibraryEntry& entry) {
    const auto& t = entry.track;
    return Id(t.id) && !t.source.empty() &&
        Text(t.source, 4096) && Text(t.title, 512) && Text(t.artist, 512) &&
        Text(t.album, 512) && Text(t.artwork, 2048) &&
        (t.artwork.empty() || t.artwork == "music-artwork:" + t.id) &&
        !entry.identity.empty() && Text(entry.identity, 128) && entry.bytes > 0 &&
        entry.bytes <= 512ull * 1024 * 1024 &&
        std::isfinite(t.duration) && t.duration >= 0 && t.duration <= 1e9;
}

Json EncodeEntry(const LibraryEntry& entry) {
    const auto& t = entry.track;
    return Json{{"id", t.id}, {"source", t.source}, {"title", t.title},
        {"artist", t.artist}, {"album", t.album}, {"artwork", t.artwork},
        {"duration", t.duration}, {"identity", entry.identity},
        {"bytes", entry.bytes}, {"modified", entry.modified}};
}

Json Parse(std::string_view bytes, std::size_t max) {
    if (bytes.empty() || bytes.size() > max) throw std::runtime_error("size");
    std::vector<std::set<std::string>> keys;
    return Json::parse(bytes.begin(), bytes.end(), [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 8) throw std::runtime_error("depth");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key &&
            (keys.empty() || !keys.back().insert(value.get<std::string>()).second))
            throw std::runtime_error("duplicate");
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}
}

MusicLibrary::MusicLibrary(std::size_t capacity)
    : capacity_(std::clamp<std::size_t>(capacity, 1, kMaxLibraryTracks)) {}

const LibraryEntry* MusicLibrary::Find(std::string_view id) const noexcept {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [&](const auto& e) { return e.track.id == id; });
    return found == entries_.end() ? nullptr : &*found;
}

const LibraryEntry* MusicLibrary::FindIdentity(std::string_view identity) const noexcept {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [&](const auto& e) { return e.identity == identity; });
    return found == entries_.end() ? nullptr : &*found;
}

LibraryChange MusicLibrary::Upsert(LibraryEntry entry) {
    try {
        if (!Valid(entry)) return LibraryChange::Invalid;
        // ponytail: linear lookup bounded at 4096 entries; add an index only if this cap grows.
        const auto found = std::find_if(entries_.begin(), entries_.end(),
            [&](const auto& e) { return e.identity == entry.identity || e.track.id == entry.track.id; });
        if (found != entries_.end()) {
            if (found->identity != entry.identity) return LibraryChange::Invalid;
            entry.track.id = found->track.id;
            if (!entry.track.artwork.empty()) entry.track.artwork = "music-artwork:" + entry.track.id;
            if (EncodeEntry(*found) == EncodeEntry(entry) && found->unavailable == entry.unavailable)
                return LibraryChange::Unchanged;
            *found = std::move(entry);
            return LibraryChange::Updated;
        }
        if (entries_.size() >= capacity_) return LibraryChange::Full;
        entries_.push_back(std::move(entry));
        return LibraryChange::Added;
    } catch (...) { return LibraryChange::Invalid; }
}

bool MusicLibrary::Remove(std::string_view id) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [&](const auto& e) { return e.track.id == id; });
    if (found == entries_.end()) return false;
    entries_.erase(found);
    return true;
}

bool MusicLibrary::SetAvailability(std::string_view id, std::string error) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [&](const auto& e) { return e.track.id == id; });
    if (found == entries_.end()) return false;
    found->unavailable = std::move(error);
    return true;
}

std::vector<Track> MusicLibrary::Tracks() const {
    std::vector<Track> result;
    for (const auto& entry : entries_)
        if (entry.unavailable.empty()) result.push_back(entry.track);
    return result;
}

std::string MusicLibrary::Encode() const {
    Json entries = Json::array();
    for (const auto& entry : entries_) entries.push_back(EncodeEntry(entry));
    return Json{{"version", 1}, {"tracks", std::move(entries)}}.dump();
}

bool MusicLibrary::Decode(std::string_view bytes) noexcept {
    try {
        const auto json = Parse(bytes, kMaxLibraryIndexBytes);
        if (!json.is_object() || json.size() != 2 || json.at("version") != 1 ||
            !json.at("version").is_number_integer() || !json.at("tracks").is_array() ||
            json.at("tracks").size() > capacity_) return false;
        MusicLibrary next(capacity_);
        std::set<std::string> ids, identities;
        for (const auto& row : json.at("tracks")) {
            if (!row.is_object() || row.size() != 10 || !row.at("bytes").is_number_unsigned() ||
                !row.at("modified").is_number_unsigned()) return false;
            LibraryEntry entry;
            entry.track = {row.at("id").get<std::string>(), row.at("title").get<std::string>(),
                row.at("artist").get<std::string>(), row.at("album").get<std::string>(),
                row.at("source").get<std::string>(), row.at("artwork").get<std::string>(),
                row.at("duration").get<double>(), false, ""};
            entry.identity = row.at("identity").get<std::string>();
            entry.bytes = row.at("bytes").get<std::uint64_t>();
            entry.modified = row.at("modified").get<std::uint64_t>();
            if (!Valid(entry) || !ids.insert(entry.track.id).second ||
                !identities.insert(entry.identity).second) return false;
            next.entries_.push_back(std::move(entry));
        }
        entries_ = std::move(next.entries_);
        return true;
    } catch (...) { return false; }
}

std::optional<std::string> MusicLibrary::EncodeQueue(const MusicSnapshot& snapshot) const {
    if (snapshot.queue.size() > kMaxMusicTracks || !std::isfinite(snapshot.position) ||
        snapshot.position < 0 || snapshot.position > 1e9 ||
        !std::isfinite(snapshot.volume) || snapshot.volume < 0 || snapshot.volume > 1 ||
        (snapshot.repeat != RepeatMode::None && snapshot.repeat != RepeatMode::One &&
            snapshot.repeat != RepeatMode::All) ||
        (snapshot.queue.empty() ? snapshot.currentIndex != 0 :
            snapshot.currentIndex >= snapshot.queue.size())) return {};
    const auto originalCurrent = snapshot.queue.empty() ? std::string{} : snapshot.queue[snapshot.currentIndex].id;
    if (snapshot.current && (snapshot.queue.empty() || snapshot.current->id != originalCurrent)) return {};
    Json ids = Json::array();
    std::set<std::string> unique;
    std::size_t currentIndex = 0;
    for (std::size_t i = 0; i < snapshot.queue.size(); ++i) {
        const auto& track = snapshot.queue[i];
        if (!Id(track.id) || !unique.insert(track.id).second) return {};
        if (!Find(track.id)) continue;
        if (i < snapshot.currentIndex) ++currentIndex;
        ids.push_back(track.id);
    }
    // A removed current item resumes at its next survivor, or the last predecessor.
    const auto current = ids.empty() ? std::string{} :
        ids[std::min(currentIndex, ids.size() - 1)].get<std::string>();
    return Json{{"version", 1}, {"ids", std::move(ids)}, {"current", current},
        {"position", current.empty() || current != originalCurrent ? 0.0 : snapshot.position},
        {"repeat", static_cast<int>(snapshot.repeat)}, {"shuffle", snapshot.shuffle},
        {"volume", snapshot.volume}, {"muted", snapshot.muted}}.dump();
}

std::optional<MusicSnapshot> MusicLibrary::DecodeQueue(std::string_view bytes) const noexcept {
    try {
        const auto json = Parse(bytes, kMaxLibraryQueueBytes);
        if (!json.is_object() || (json.size() != 6 && json.size() != 8) || json.at("version") != 1 ||
            !json.at("version").is_number_integer() || !json.at("ids").is_array() ||
            json.at("ids").size() > kMaxMusicTracks || !json.at("shuffle").is_boolean() ||
            !json.at("repeat").is_number_integer() ||
            (json.at("repeat") != 0 && json.at("repeat") != 1 && json.at("repeat") != 2)) return {};
        const auto repeat = json.at("repeat").get<int>();
        const auto position = json.at("position").get<double>();
        const auto current = json.at("current").get<std::string>();
        if (repeat < 0 || repeat > 2 || !std::isfinite(position) || position < 0 || position > 1e9)
            return {};
        MusicSnapshot snapshot;
        snapshot.repeat = static_cast<RepeatMode>(repeat);
        snapshot.shuffle = json.at("shuffle").get<bool>();
        if (json.size() == 8) {
            snapshot.volume = json.at("volume").get<double>();
            snapshot.muted = json.at("muted").get<bool>();
            if (!std::isfinite(snapshot.volume) || snapshot.volume < 0 || snapshot.volume > 1) return {};
        }
        std::set<std::string> ids;
        bool foundCurrent = false;
        for (const auto& item : json.at("ids")) {
            const auto id = item.get<std::string>();
            if (!Id(id) || !ids.insert(id).second) return {};
            const auto* entry = Find(id);
            if (!entry || !entry->unavailable.empty()) continue;
            if (id == current) {
                foundCurrent = true;
                snapshot.currentIndex = snapshot.queue.size();
            }
            snapshot.queue.push_back(entry->track);
        }
        if ((!ids.empty() && !ids.count(current)) || (ids.empty() && !current.empty())) return {};
        if (!snapshot.queue.empty()) {
            snapshot.current = snapshot.queue[snapshot.currentIndex];
            snapshot.position = foundCurrent ? position : 0;
            if (snapshot.current->duration > 0)
                snapshot.position = std::min(snapshot.position, snapshot.current->duration);
            snapshot.status = PlaybackStatus::Ready;
        }
        return snapshot;
    } catch (...) { return {}; }
}

}  // namespace lume::music
