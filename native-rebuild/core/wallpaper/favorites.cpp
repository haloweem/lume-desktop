#include "core/wallpaper/favorites.h"
#include "core/wallpaper/catalog.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include "vendor/nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <stdexcept>

namespace lume::wallpaper {
namespace {
using Json = nlohmann::json;
constexpr std::size_t MaxBytes = 2u * 1024u * 1024u;

bool ValidPath(const std::string& path) {
    return !path.empty() && path.find('\0') == std::string::npos;
}

bool ValidIds(const std::vector<std::string>& ids) {
    if (ids.size() > kMaxLibraryItems) return false;
    std::set<std::string> seen;
    for (const auto& id : ids) {
        if (!IsSafeWallpaperId(id)) return false;
        auto normalized = id;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (!seen.insert(std::move(normalized)).second) return false;
    }
    return true;
}

enum class DecodeState { Invalid, Unsupported, Valid };
struct Decoded {
    DecodeState state = DecodeState::Invalid;
    std::vector<std::string> ids;
};

Decoded Decode(const std::string& text) {
    Decoded result;
    if (text.empty() || text.size() > MaxBytes) return result;
    try {
        std::array<std::set<std::string>, 4> keys;
        std::size_t values = 0;
        const auto root = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth < 0 || depth >= static_cast<int>(keys.size()))
                throw std::invalid_argument("favorites depth");
            if (event == Json::parse_event_t::object_start)
                keys[static_cast<std::size_t>(depth)].clear();
            if (event == Json::parse_event_t::key && (depth == 0 ||
                !keys[static_cast<std::size_t>(depth - 1)].insert(value.get<std::string>()).second))
                throw std::invalid_argument("favorites duplicate key");
            if (event == Json::parse_event_t::value && depth == 2 && ++values > kMaxLibraryItems)
                throw std::invalid_argument("favorites count");
            return true;
        });
        if (root.is_object() && root.contains("version") &&
            (!root["version"].is_number_integer() || root["version"] != 1)) {
            result.state = DecodeState::Unsupported;
            return result;
        }
        if (!root.is_object() || root.size() != 2 || !root.contains("version") ||
            !root["version"].is_number_integer() || root["version"] != 1 ||
            !root.contains("ids") || !root["ids"].is_array() || root["ids"].size() > kMaxLibraryItems)
            return result;
        std::vector<std::string> ids;
        ids.reserve(root["ids"].size());
        for (const auto& id : root["ids"]) {
            if (!id.is_string()) return result;
            ids.push_back(id.get<std::string>());
        }
        if (!ValidIds(ids)) return result;
        result.ids = std::move(ids);
        result.state = DecodeState::Valid;
    } catch (...) {}
    return result;
}

LoadedFavorites Failed() {
    LoadedFavorites result;
    result.status = "failed";
    result.writable = false;
    return result;
}

struct Snapshot {
    LoadedFavorites loaded;
    std::string primaryText;
    bool primaryValid = false;
};

Snapshot ReadSnapshot(settings::IFileStore& files, const std::string& path) {
    Snapshot result;
    if (!ValidPath(path)) { result.loaded = Failed(); return result; }
    const bool primaryExists = files.Exists(path), backupExists = files.Exists(path + ".bak");
    if (!primaryExists && !backupExists) return result;
    std::string text;
    if (primaryExists && files.ReadBounded(path, MaxBytes, text)) {
        auto parsed = Decode(text);
        if (parsed.state == DecodeState::Unsupported) { result.loaded = Failed(); return result; }
        if (parsed.state == DecodeState::Valid) {
            result.loaded.ids = std::move(parsed.ids);
            result.loaded.status = "loaded";
            result.primaryText = std::move(text);
            result.primaryValid = true;
            return result;
        }
    }
    if (backupExists && files.ReadBounded(path + ".bak", MaxBytes, text)) {
        auto parsed = Decode(text);
        if (parsed.state == DecodeState::Valid) {
            result.loaded.ids = std::move(parsed.ids);
            result.loaded.status = "recovered";
            return result;
        }
    }
    result.loaded = Failed();
    return result;
}
} // namespace

LoadedFavorites FavoritesStore::Load(const std::string& path) const {
    try { return ReadSnapshot(files_, path).loaded; }
    catch (...) { return Failed(); }
}

bool FavoritesStore::Save(const std::string& path, const std::vector<std::string>& ids) {
    std::string temporary, backupTemporary;
    const auto fail = [&] {
        try { if (!temporary.empty()) files_.Remove(temporary); } catch (...) {}
        try { if (!backupTemporary.empty()) files_.Remove(backupTemporary); } catch (...) {}
        return false;
    };
    try {
        if (!ValidPath(path) || !ValidIds(ids)) return false;
        const auto text = Json{{"version", 1}, {"ids", ids}}.dump();
        if (text.size() > MaxBytes) return false;
        const auto previous = ReadSnapshot(files_, path);
        if (!previous.loaded.writable) return false;
        temporary = path + ".tmp";
        backupTemporary = path + ".bak.tmp";
        if (!files_.Write(temporary, text)) return fail();
        if (previous.primaryValid && (!files_.Write(backupTemporary, previous.primaryText) ||
            !files_.Rename(backupTemporary, path + ".bak"))) return fail();
        if (!files_.Rename(temporary, path)) return fail();
        return true;
    } catch (...) { return fail(); }
}
} // namespace lume::wallpaper
