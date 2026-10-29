#include "core/notes/notes_store.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <set>

namespace lume::notes {
namespace {
using Json = nlohmann::json;
// Decodes one UTF-8 code point at text[at]; false for malformed input.
bool Next(const std::string &text, std::size_t &at, std::uint32_t &cp) noexcept {
    const auto first = static_cast<unsigned char>(text[at++]);
    if (first < 0x80) {
        cp = first;
        return true;
    }
    int count = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
        count = 1;
        minimum = 0x80;
        cp = first & 0x1f;
    } else if (first >= 0xe0 && first <= 0xef) {
        count = 2;
        minimum = 0x800;
        cp = first & 0x0f;
    } else if (first >= 0xf0 && first <= 0xf4) {
        count = 3;
        minimum = 0x10000;
        cp = first & 7;
    } else
        return false;
    if (text.size() - at < static_cast<std::size_t>(count))
        return false;
    while (count--) {
        const auto c = static_cast<unsigned char>(text[at++]);
        if ((c & 0xc0) != 0x80)
            return false;
        cp = (cp << 6) | (c & 0x3f);
    }
    return cp >= minimum && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
}
bool Space(std::uint32_t cp) noexcept {
    return (cp >= 9 && cp <= 13) || cp == 32 || cp == 0xa0 || cp == 0x3000 || cp == 0xfeff ||
           (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029;
}
bool Stamp(const Json &value, Millis &out) {
    if (!value.is_number_integer())
        return false;
    out = value.get<Millis>();
    return out >= 0 && out <= 253402300799999LL;
}
} // namespace

bool ValidUtf8(const std::string &text) noexcept {
    std::size_t at = 0;
    std::uint32_t cp = 0;
    while (at < text.size()) {
        if (!Next(text, at, cp) || cp == 0)
            return false;
    }
    return true;
}
bool ValidId(const std::string &id) noexcept {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}
bool ValidNote(const Note &note) noexcept {
    return ValidId(note.id) && note.title.size() <= MaxTitleBytes && note.body.size() <= MaxBodyBytes &&
           note.title.find_first_of("\r\n") == std::string::npos && ValidUtf8(note.title) && ValidUtf8(note.body) &&
           note.createdAt >= 0 && note.updatedAt >= note.createdAt;
}
std::size_t CharacterCount(const std::string &text) noexcept {
    std::size_t at = 0, count = 0;
    std::uint32_t cp = 0;
    while (at < text.size()) {
        if (!Next(text, at, cp))
            return count;
        if (!Space(cp))
            ++count;
    }
    return count;
}
std::string Preview(const std::string &text, std::size_t maxCodePoints) {
    std::string out;
    std::size_t at = 0, count = 0;
    std::uint32_t cp = 0;
    bool space = false;
    while (at < text.size() && count < maxCodePoints) {
        const auto start = at;
        if (!Next(text, at, cp))
            break;
        if (Space(cp)) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += ' ';
            ++count;
            space = false;
            if (count >= maxCodePoints)
                break;
        }
        out.append(text, start, at - start);
        ++count;
    }
    return out;
}
std::string Encode(const NotesState &state) {
    Json notes = Json::array();
    for (const auto &note : state.notes)
        notes.push_back({{"id", note.id},
                         {"title", note.title},
                         {"body", note.body},
                         {"createdAt", note.createdAt},
                         {"updatedAt", note.updatedAt}});
    Json root{{"version", 1}, {"pinnedId", state.pinnedId}, {"notes", std::move(notes)}};
    if (state.desktopLayoutKnown) {
        root["desktopNotes"] = Json::array();
        for (const auto &desktop : state.desktopNotes) {
            std::string encoded;
            if (!widget::Encode(desktop.preferences, encoded, DesktopLimits)) return {};
            root["desktopNotes"].push_back({{"id", desktop.id}, {"preferences", Json::parse(encoded)}});
        }
    }
    return root.dump();
}
bool Decode(const std::string &text, NotesState &out) {
    if (text.size() > MaxFileBytes)
        return false;
    try {
        const auto root = Json::parse(text);
        if (!root.is_object() || root.value("version", 0) != 1 || !root.contains("notes") ||
            !root["notes"].is_array() || root["notes"].size() > MaxNotes)
            return false;
        NotesState state;
        std::set<std::string> ids;
        for (const auto &value : root["notes"]) {
            if (!value.is_object() || !value.contains("id") || !value["id"].is_string() || !value.contains("title") ||
                !value["title"].is_string() || !value.contains("body") || !value["body"].is_string() ||
                !value.contains("createdAt") || !value.contains("updatedAt"))
                return false;
            Note note;
            note.id = value["id"].get<std::string>();
            note.title = value["title"].get<std::string>();
            note.body = value["body"].get<std::string>();
            if (!Stamp(value["createdAt"], note.createdAt) || !Stamp(value["updatedAt"], note.updatedAt) ||
                !ValidNote(note) || !ids.insert(note.id).second)
                return false;
            state.notes.push_back(std::move(note));
        }
        if (root.contains("pinnedId") && root["pinnedId"].is_string()) {
            const auto pinned = root["pinnedId"].get<std::string>();
            if (ids.count(pinned))
                state.pinnedId = pinned;
        }
        if (root.contains("desktopNotes")) {
            const auto &desktops = root["desktopNotes"];
            if (!desktops.is_array() || desktops.size() > MaxNotes) return false;
            std::set<std::string> seen;
            for (const auto &desktop : desktops) {
                if (!desktop.is_object() || !desktop.contains("id") || !desktop["id"].is_string() || !desktop.contains("preferences")) return false;
                NotesState::DesktopNote entry;
                entry.id = desktop["id"].get<std::string>();
                if (!ids.count(entry.id) || !seen.insert(entry.id).second ||
                    !widget::Decode(desktop["preferences"].dump(), entry.preferences, DesktopLimits)) return false;
                state.desktopNotes.push_back(std::move(entry));
            }
            state.desktopLayoutKnown = true;
        }
        SortByRecent(state);
        out = std::move(state);
        return true;
    } catch (const Json::exception &) {
        return false;
    }
}
const Note *Find(const NotesState &state, const std::string &id) noexcept {
    for (const auto &note : state.notes)
        if (note.id == id)
            return &note;
    return nullptr;
}
const Note *Shown(const NotesState &state) noexcept {
    if (const auto pinned = Find(state, state.pinnedId))
        return pinned;
    return state.notes.empty() ? nullptr : &state.notes.front();
}
void SortByRecent(NotesState &state) {
    std::stable_sort(state.notes.begin(), state.notes.end(),
                     [](const Note &a, const Note &b) { return a.updatedAt > b.updatedAt; });
}
LoadStatus NotesStore::Load(NotesState &out) const {
    if (path_.empty())
        return LoadStatus::Failed;
    if (!files_.Exists(path_)) {
        out = {};
        return LoadStatus::Fresh;
    }
    std::string text;
    if (!files_.ReadBounded(path_, MaxFileBytes, text) || !Decode(text, out))
        return LoadStatus::Failed;
    return LoadStatus::Loaded;
}
bool NotesStore::Save(const NotesState &state) {
    if (path_.empty() || state.notes.size() > MaxNotes)
        return false;
    const auto text = Encode(state);
    if (text.empty() || text.size() > MaxFileBytes)
        return false;
    const auto temporary = path_ + ".tmp";
    if (!files_.Write(temporary, text) || !files_.Rename(temporary, path_)) {
        files_.Remove(temporary);
        return false;
    }
    return true;
}
} // namespace lume::notes
