#include "core/dock/dock_model.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace lume::dock {
namespace {
using Json = nlohmann::json;
bool ValidPaths(const std::vector<std::string>& paths, std::size_t limit, const TextKey& pathKey) {
    if (paths.size() > limit || !pathKey) return false;
    std::set<std::string> keys;
    for (const auto& path : paths) {
        const auto normalized = organizer::NormalizeLocalPath(path);
        if (!normalized || *normalized != path) return false;
        const auto key = pathKey(path);
        if (key.empty() || !keys.insert(key).second) return false;
    }
    return true;
}
bool ReadPaths(const Json& value, std::vector<std::string>& output) {
    if (!value.is_array()) return false;
    output.clear();
    for (const auto& item : value) {
        if (!item.is_string()) return false;
        output.push_back(item.get<std::string>());
    }
    return true;
}
bool ValidCollectionId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (const char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    return true;
}
bool ValidCollectionTitle(std::string_view title) {
    if (title.empty() || title.size() > 80 || title.find('\0') != std::string::npos) return false;
    return title.find_first_not_of(" \t\r\n") != std::string_view::npos;
}
bool ValidOrder(const std::vector<std::string>& order, const std::vector<Collection>& collections,
                const TextKey& pathKey) {
    if (order.size() > MaxOrder || !pathKey) return false;
    std::set<std::string> keys;
    for (const auto& value : order) {
        std::string key;
        if (value.rfind("collection:", 0) == 0) {
            const auto id = value.substr(11);
            if (!ValidCollectionId(id) || !std::any_of(collections.begin(), collections.end(),
                                                       [&](const auto& c) { return c.id == id; })) return false;
            key = value;
        } else {
            const auto normalized = organizer::NormalizeLocalPath(value);
            if (!normalized || *normalized != value || pathKey(value).empty()) return false;
            key = pathKey(value);
        }
        if (!keys.insert(key).second) return false;
    }
    return true;
}
bool ValidCollections(const std::vector<Collection>& collections, const TextKey& pathKey) {
    if (collections.size() > MaxCollections || !pathKey) return false;
    std::set<std::string> ids, paths;
    for (const auto& collection : collections) {
        if (!ValidCollectionId(collection.id) || !ids.insert(collection.id).second ||
            !ValidCollectionTitle(collection.title) || collection.paths.size() > MaxAdded) return false;
        for (const auto& path : collection.paths) {
            const auto normalized = organizer::NormalizeLocalPath(path);
            if (!normalized || *normalized != path || pathKey(path).empty()) return false;
            if (!paths.insert(pathKey(path)).second) return false;
        }
    }
    return true;
}
bool ReadCollections(const Json& value, std::vector<Collection>& output) {
    if (!value.is_array() || value.size() > MaxCollections) return false;
    output.clear();
    for (const auto& item : value) {
        if (!item.is_object() || item.size() != 3 || !item.contains("id") || !item.contains("title") ||
            !item.contains("paths") || !item.at("id").is_string() || !item.at("title").is_string() ||
            !item.at("paths").is_array()) return false;
        Collection collection;
        collection.id = item.at("id").get<std::string>();
        collection.title = item.at("title").get<std::string>();
        if (!ReadPaths(item.at("paths"), collection.paths)) return false;
        for (auto& path : collection.paths) {
            const auto normalized = organizer::NormalizeLocalPath(path);
            if (!normalized || *normalized != path) return false;
            path = *normalized;
        }
        output.push_back(std::move(collection));
    }
    return true;
}
bool EndsWith(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char c = text[text.size() - suffix.size() + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != suffix[i]) return false;
    }
    return true;
}
std::vector<std::string> Terms(const std::string& query) {
    std::vector<std::string> terms;
    std::size_t start = 0;
    while (start < query.size()) {
        const auto space = query.find_first_of(" \t\r\n", start);
        const auto end = space == std::string::npos ? query.size() : space;
        if (end > start) terms.push_back(query.substr(start, end - start));
        start = end + 1;
    }
    return terms;
}
// The folder of a path key, without the trailing separator; empty for a bare name.
std::string FolderKey(const std::string& key) {
    const auto slash = key.find_last_of("\\/");
    return slash == std::string::npos ? std::string{} : key.substr(0, slash);
}
// A launcher's UI often runs from a subfolder of its install folder under a
// different executable name. Only a strictly deeper folder counts, and only
// for a dedicated install folder: shared folders (a drive root, Program
// Files, Windows) hold unrelated programs.
bool InstallSubfolder(const std::string& target, const std::string& running) {
    const auto folder = FolderKey(target);
    if (folder.empty() || running.size() <= folder.size() + 1 || running.compare(0, folder.size(), folder) != 0 ||
        (running[folder.size()] != '\\' && running[folder.size()] != '/')) return false;
    if (FolderKey(running) == folder) return false;
    std::size_t parts = 0;
    for (const char c : folder) parts += c == '\\' || c == '/';
    if (parts < 2) return false;  // "c:\\x" or a root
    const auto second = folder.find_first_of("\\/");
    const auto top = folder.substr(second + 1, folder.find_first_of("\\/", second + 1) - second - 1);
    return top != "windows";
}
std::vector<std::string>::iterator FindPath(std::vector<std::string>& paths, std::string_view path,
                                             const TextKey& pathKey) {
    const auto key = pathKey(path);
    return std::find_if(paths.begin(), paths.end(), [&](const auto& item) { return pathKey(item) == key; });
}
}  // namespace

bool ValidPosition(int value) { return value >= MinPosition && value <= MaxPosition; }
bool ValidPositionMode(int value) { return value >= PositionCenter && value <= PositionRight; }
bool ValidPositionOffset(int value) { return value >= MinPositionOffset && value <= MaxPositionOffset; }
bool ValidBottomOffset(int value) { return value >= MinBottomOffset && value <= MaxBottomOffset; }
bool ValidIconSpacing(int value) { return value >= MinIconSpacing && value <= MaxIconSpacing; }
bool ValidSummonShortcut(unsigned code) noexcept {
    if (code == 0) return true;
    if (code > 4095) return false;
    const unsigned vk = code % 256, mods = code / 256;
    const bool key = vk == 8 || vk == 9 || vk == 13 || vk == 19 || vk == 20 || vk == 32 ||
        vk == 44 || vk == 45 || vk == 46 || vk == 144 || vk == 145 || (vk >= 33 && vk <= 40) ||
        (vk >= 48 && vk <= 57) || (vk >= 65 && vk <= 90) || (vk >= 96 && vk <= 135) ||
        (vk >= 186 && vk <= 192) || (vk >= 219 && vk <= 222) || vk == 226;
    const bool control = (mods & 2) != 0, alt = (mods & 1) != 0, shift = (mods & 4) != 0, win = (mods & 8) != 0;
    if (!key || vk == 123 || !(control || alt || win)) return false;
    return !((vk == 9 && (mods & 9)) || (vk == 46 && (mods & 3) == 3) ||
             (vk == 76 && win) || (vk == 27 && control && shift) ||
             (vk == 115 && alt) || (win && (vk == 68 || vk == 69 || vk == 82)));
}
bool ValidLumePosition(int value, std::size_t pinnedCount) {
    return value >= 0 && static_cast<std::size_t>(value) <= std::min(pinnedCount, MaxPinned);
}
bool Valid(const Preferences& value, const TextKey& pathKey) {
    std::set<std::string> dockIds;
    if (value.dockCollections.size() > MaxCollections) return false;
    for (const auto& id : value.dockCollections)
        if (!FindCollection(value, id) || !dockIds.insert(id).second) return false;
    return value.screen >= -1 && value.screen <= 64 && value.iconScale >= MinIconScale && value.iconScale <= MaxIconScale &&
           value.reflectionRatio >= 0 && value.reflectionRatio <= 100 &&
           value.reflectionTransparency >= 0 && value.reflectionTransparency <= 100 &&
           value.animationDuration>=50 && value.animationDuration<=2000 &&
           value.launchpadScale>=MinLaunchpadScale&&value.launchpadScale<=MaxLaunchpadScale&&
           value.launchpadIconScale>=MinLaunchpadIconScale&&value.launchpadIconScale<=MaxLaunchpadIconScale&&
           ValidSummonShortcut(value.summonShortcutCode)&&
           ValidEdge(value.edge) && ValidPosition(value.position) && ValidPositionMode(value.positionMode) && ValidPositionOffset(value.positionOffset) &&
           ValidBottomOffset(value.bottomOffset) && ValidIconSpacing(value.iconSpacing) &&
           ValidLumePosition(value.lumePosition, value.pinned.size()) &&
           ValidPaths(value.pinned, MaxPinned, pathKey) && ValidPaths(value.added, MaxAdded, pathKey) &&
           ValidPaths(value.removed, MaxOrder, pathKey) &&
           ValidOrder(value.order, value.collections, pathKey) && ValidCollections(value.collections, pathKey);
}

bool Encode(const Preferences& value, std::string& output) {
    try {
        Json collections = Json::array();
        for (const auto& collection : value.collections)
            collections.push_back({{"id", collection.id}, {"title", collection.title}, {"paths", collection.paths}});
        auto text = Json{{"version", 1}, {"enabled", value.enabled}, {"frosted", value.frosted},
                         {"transparent", value.transparent}, {"reflection", value.reflection},
                         {"reflectionRatio", value.reflectionRatio}, {"reflectionTransparency", value.reflectionTransparency},
                         {"systemTray", value.systemTray}, {"controlCenter", value.controlCenter}, {"showRunning", value.showRunning}, {"runningPreview", value.runningPreview},
                         {"animation",value.animation},{"animateRestore",value.animateRestore},{"animateLaunch",value.animateLaunch},
                         {"animateClose",value.animateClose},{"animationDuration",value.animationDuration},
                         {"screen", value.screen}, {"summonEnabled", value.summonEnabled}, {"summonShortcutCode", value.summonShortcutCode},
                         {"iconScale", value.iconScale},
                         {"launchpadScale",value.launchpadScale},{"launchpadIconScale",value.launchpadIconScale},
                         {"launchpadMaximized", value.launchpadMaximized}, {"removed", value.removed},
                         {"position", value.position}, {"positionMode", value.positionMode}, {"positionOffset", value.positionOffset},
                         {"edge", static_cast<int>(value.edge)}, {"showStartMenu", value.showStartMenu}, {"launchpadEnabled", value.launchpadEnabled},
                         {"bottomOffset", value.bottomOffset}, {"iconSpacing", value.iconSpacing},
                         {"lumeVisible", value.lumeVisible}, {"lumePosition", value.lumePosition},
                         {"pinned", value.pinned}, {"added", value.added}, {"order", value.order},
                         {"collections", collections}, {"dockCollections", value.dockCollections}}.dump();
        if (text.size() > MaxPreferencesBytes) return false;
        output = std::move(text);
        return true;
    } catch (...) { return false; }
}

bool Decode(const std::string& input, Preferences& output, const TextKey& pathKey) {
    // The JSON lexer treats raw NUL as EOF, so reject it before strict parsing.
    if (input.empty() || input.size() > MaxPreferencesBytes || input.find('\0') != std::string::npos) return false;
    try {
        const auto json = Json::parse(input);
        // "order" and the appearance keys were added later; files without them remain valid.
        if (!json.is_object()) return false;
        const bool ordered = json.contains("order");
        const bool hasCollections = json.contains("collections");
        std::size_t optional = ordered ? 1 : 0;
        optional += hasCollections ? 1 : 0;
        optional += json.contains("dockCollections") ? 1 : 0;
        optional += json.contains("removed") ? 1 : 0;
        optional += json.contains("launchpadMaximized") ? 1 : 0;
        optional += json.contains("showStartMenu") ? 1 : 0;
        optional += json.contains("animationDuration") ? 1 : 0;
        optional += json.contains("edge") ? 1 : 0;
        for (const auto* key : {"transparent", "reflection", "systemTray", "controlCenter", "showRunning", "runningPreview", "animation", "animateRestore", "animateLaunch", "animateClose", "launchpadEnabled", "summonEnabled", "summonShortcutCode", "iconScale", "position", "positionMode", "positionOffset", "bottomOffset", "iconSpacing","launchpadScale","launchpadIconScale",
                                "screen", "lumeVisible", "lumePosition", "reflectionRatio", "reflectionTransparency"})
            optional += json.contains(key) ? 1 : 0;
        if (json.size() != 5u + optional || !json.contains("version") || json.at("version") != 1 ||
            !json.at("enabled").is_boolean() || !json.at("frosted").is_boolean()) return false;
        Preferences value;
        value.enabled = json.at("enabled").get<bool>();
        value.frosted = json.at("frosted").get<bool>();
        for (const auto* key : {"transparent", "reflection", "systemTray", "controlCenter", "showRunning", "runningPreview", "animation", "animateRestore", "animateLaunch", "animateClose", "launchpadEnabled", "summonEnabled"})
            if (json.contains(key) && !json.at(key).is_boolean()) return false;
        if (json.contains("transparent")) value.transparent = json.at("transparent").get<bool>();
        if (json.contains("reflection")) value.reflection = json.at("reflection").get<bool>();
        if (json.contains("systemTray")) value.systemTray = json.at("systemTray").get<bool>();
        if (json.contains("controlCenter")) value.controlCenter = json.at("controlCenter").get<bool>();
        if (json.contains("animation")) value.animation = json.at("animation").get<bool>();
        if (json.contains("animateRestore")) value.animateRestore = json.at("animateRestore").get<bool>();
        if (json.contains("animateLaunch")) value.animateLaunch = json.at("animateLaunch").get<bool>();
        if (json.contains("animateClose")) value.animateClose = json.at("animateClose").get<bool>();
        if (json.contains("animationDuration")) {
            const auto& ms=json.at("animationDuration");
            if (!ms.is_number_integer() || ms<50 || ms>2000) return false;
            value.animationDuration=ms.get<int>();
        }
        if (json.contains("runningPreview")) value.runningPreview = json.at("runningPreview").get<bool>();
        if (json.contains("showRunning")) value.showRunning = json.at("showRunning").get<bool>();
        if (json.contains("launchpadEnabled")) value.launchpadEnabled = json.at("launchpadEnabled").get<bool>();
        if (json.contains("showStartMenu")) {
            if (!json.at("showStartMenu").is_boolean()) return false;
            value.showStartMenu = json.at("showStartMenu").get<bool>();
        }
        if (json.contains("edge")) {
            if (!json.at("edge").is_number_integer() || json.at("edge") < 0 || json.at("edge") > 3) return false;
            value.edge = static_cast<Edge>(json.at("edge").get<int>());
        }
        if (json.contains("screen")) {
            const auto& screen = json.at("screen");
            if (!screen.is_number_integer() || screen < -1 || screen > 64) return false;
            value.screen = screen.get<int>();
        }
        if (json.contains("summonEnabled")) value.summonEnabled = json.at("summonEnabled").get<bool>();
        if (json.contains("summonShortcutCode")) {
            if (!json.at("summonShortcutCode").is_number_integer() || json.at("summonShortcutCode") < 0 ||
                json.at("summonShortcutCode") > 4095) return false;
            value.summonShortcutCode = json.at("summonShortcutCode").get<unsigned>();
        }
        for (const auto& field : {std::pair{"positionMode", &value.positionMode},
                                  std::pair{"positionOffset", &value.positionOffset}}) {
            if (!json.contains(field.first)) continue;
            if (!json.at(field.first).is_number_integer()) return false;
            if (json.at(field.first) < 0 || json.at(field.first) > 10000) return false;
            *field.second = json.at(field.first).get<int>();
        }
        if (json.contains("iconScale")) {
            if (!json.at("iconScale").is_number_integer()||json.at("iconScale")<50||json.at("iconScale")>140) return false;
            value.iconScale = std::min(120,json.at("iconScale").get<int>());
        }
        for (const auto& field : {std::pair{"position", &value.position},
                                  std::pair{"reflectionRatio", &value.reflectionRatio},
                                  std::pair{"reflectionTransparency", &value.reflectionTransparency},
                                  std::pair{"launchpadScale", &value.launchpadScale},
                                  std::pair{"launchpadIconScale", &value.launchpadIconScale},
                                  std::pair{"bottomOffset", &value.bottomOffset},
                                  std::pair{"iconSpacing", &value.iconSpacing}}) {
            if (!json.contains(field.first)) continue;
            if (!json.at(field.first).is_number_integer()) return false;
            if(json.at(field.first)<0||json.at(field.first)>10000)return false;
            *field.second = json.at(field.first).get<int>();
        }
        if (json.contains("launchpadMaximized")) {
            if (!json.at("launchpadMaximized").is_boolean()) return false;
            value.launchpadMaximized = json.at("launchpadMaximized").get<bool>();
        }
        if (json.contains("removed") && !ReadPaths(json.at("removed"), value.removed)) return false;
        if (json.contains("lumeVisible")) {
            if (!json.at("lumeVisible").is_boolean()) return false;
            value.lumeVisible = json.at("lumeVisible").get<bool>();
        }
        if (json.contains("lumePosition")) {
            if (!json.at("lumePosition").is_number_integer()) return false;
            value.lumePosition = json.at("lumePosition").get<int>();
        }
        if (!ReadPaths(json.at("pinned"), value.pinned) || !ReadPaths(json.at("added"), value.added) ||
            (json.contains("dockCollections") && !ReadPaths(json.at("dockCollections"), value.dockCollections)) ||
            (ordered && !ReadPaths(json.at("order"), value.order)) ||
            (hasCollections && !ReadCollections(json.at("collections"), value.collections)) ||
            !Valid(value, pathKey)) return false;
        output = std::move(value);
        return true;
    } catch (...) { return false; }
}

std::string EntryName(std::string_view path) {
    const bool shortcut = EndsWith(path, ".lnk") || EndsWith(path, ".url");
    auto name = organizer::DisplayName(path, organizer::ItemKind::File);
    if (shortcut && name.size() > 4) name.resize(name.size() - 4);
    return name;
}

std::string CollectionKey(std::string_view id) { return "collection:" + std::string(id); }
bool IsCollectionKey(std::string_view value) {
    return value.rfind("collection:", 0) == 0 && ValidCollectionId(value.substr(11));
}
const Collection* FindCollection(const Preferences& value, std::string_view id) {
    const auto found = std::find_if(value.collections.begin(), value.collections.end(),
                                    [&](const auto& collection) { return collection.id == id; });
    return found == value.collections.end() ? nullptr : &*found;
}
std::vector<Entry> CollectionItems(const Collection& collection, const TextKey& pathKey) {
    std::vector<Entry> items;
    if (!pathKey) return items;
    for (const auto& path : collection.paths) {
        if (pathKey(path).empty()) continue;
        items.push_back({path, EntryName(path), true});
    }
    return items;
}

std::vector<Entry> LaunchpadItems(const std::vector<std::string>& desktop, const std::vector<std::string>& added,
                                  const std::vector<std::string>& order, const TextKey& pathKey, const TextKey& searchKey,
                                  const std::vector<Collection>& collections,
                                  const std::vector<std::string>& removed) {
    std::vector<Entry> items;
    if (!pathKey) return items;
    std::map<std::string, std::size_t> seen;
    std::set<std::string> hidden, dismissed;
    for (const auto& path : removed) dismissed.insert(pathKey(path));
    for (const auto& collection : collections)
        for (const auto& path : collection.paths) hidden.insert(pathKey(path));
    const auto append = [&](const std::string& path, bool isAdded) {
        const auto key = pathKey(path);
        if (key.empty() || hidden.count(key)) return;
        const auto found = seen.find(key);
        if (found != seen.end()) { items[found->second].added |= isAdded; return; }
        seen.emplace(key, items.size());
        items.push_back({path, EntryName(path), isAdded});
    };
    // Desktop entries first, so a shared path keeps the file's actual casing.
    for (const auto& path : desktop)
        if (!dismissed.count(pathKey(path))) append(path, false);
    for (const auto& path : added) append(path, true);
    for (const auto& collection : collections) {
        Entry entry{CollectionKey(collection.id), collection.title, true, true, collection.id};
        for (const auto& member : CollectionItems(collection, pathKey)) {
            entry.previewPaths.push_back(member.path);
            if (entry.previewPaths.size() == 4) break;
        }
        items.push_back(std::move(entry));
    }
    const auto itemKey = [&](const Entry& entry) {
        return entry.collection ? CollectionKey(entry.collectionId) : pathKey(entry.path);
    };
    std::map<std::string, std::size_t> rank;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const std::string key = IsCollectionKey(order[i]) ? order[i] : pathKey(order[i]);
        if (!key.empty()) rank.emplace(key, i);
    }
    std::vector<std::pair<std::size_t, std::string>> keys;
    for (const auto& item : items) {
        const auto found = rank.find(itemKey(item));
        keys.emplace_back(found == rank.end() ? order.size() : found->second, searchKey(item.name));
    }
    std::vector<std::size_t> indices(items.size());
    for (std::size_t i = 0; i < indices.size(); ++i) indices[i] = i;
    // Arranged items keep their saved order; new ones follow by name.
    std::stable_sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
        if (keys[a].first != keys[b].first) return keys[a].first < keys[b].first;
        return keys[a].second != keys[b].second ? keys[a].second < keys[b].second : items[a].name < items[b].name;
    });
    std::vector<Entry> sorted;
    for (const auto index : indices) sorted.push_back(items[index]);
    return sorted;
}

std::vector<std::string> Moved(const std::vector<Entry>& items, std::size_t from, std::size_t to) {
    std::vector<std::string> paths;
    for (const auto& item : items) paths.push_back(item.path);
    if (from < paths.size()) {
        auto moving = paths[from];
        paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(from));
        paths.insert(paths.begin() + static_cast<std::ptrdiff_t>(std::min(to, paths.size())), std::move(moving));
    }
    if (paths.size() > MaxOrder) paths.resize(MaxOrder);
    return paths;
}

std::vector<Entry> Filter(const std::vector<Entry>& items, std::string_view query, const TextKey& searchKey) {
    if (!searchKey) return items;
    const auto terms = Terms(searchKey(query));
    if (terms.empty()) return items;
    std::vector<Entry> result;
    for (const auto& item : items) {
        const auto name = searchKey(item.name);
        if (std::all_of(terms.begin(), terms.end(), [&](const auto& term) { return name.find(term) != std::string::npos; }))
            result.push_back(item);
    }
    return result;
}

int PageCount(std::size_t items) {
    return std::max(1, static_cast<int>((items + PageSize - 1) / PageSize));
}

std::vector<Entry> PageItems(const std::vector<Entry>& items, int page) {
    const int clamped = std::clamp(page, 0, PageCount(items.size()) - 1);
    const auto begin = std::min(items.size(), static_cast<std::size_t>(clamped) * PageSize);
    const auto end = std::min(items.size(), begin + PageSize);
    return {items.begin() + static_cast<std::ptrdiff_t>(begin), items.begin() + static_cast<std::ptrdiff_t>(end)};
}

std::vector<Item> DockItems(const std::vector<std::string>& pinned, const std::vector<std::string>& targets,
                            const std::vector<RunningApp>& running, const TextKey& pathKey,
                            bool lumeVisible, int lumePosition, std::string_view lumePath, bool systemTray, bool showRunning, bool runningPreview) {
    std::vector<Item> items{{Kind::Launchpad, {}, "\xe5\x90\xaf\xe5\x8a\xa8\xe5\x8f\xb0", false}};
    const auto lumeSlot = std::clamp(lumePosition, 0, static_cast<int>(pinned.size()));
    const auto addLume = [&] {
        if (lumeVisible) items.push_back({Kind::Lume, std::string(lumePath), "Lume", false, static_cast<std::size_t>(lumeSlot)});
    };
    std::set<std::string> runningKeys, claimed;
    for (const auto& app : running) runningKeys.insert(pathKey(app.executable));
    for (std::size_t i = 0; i < pinned.size(); ++i) {
        if (static_cast<int>(i) == lumeSlot) addLume();
        const auto target = pathKey(i < targets.size() && !targets[i].empty() ? targets[i] : pinned[i]);
        const bool live = !target.empty() && runningKeys.count(target) != 0;
        if (live) claimed.insert(target);
        items.push_back({Kind::Pinned, pinned[i], EntryName(pinned[i]), live, i});
        if (i < targets.size()) items.back().target = targets[i];
    }
    // Match launchers to their UI process for activation and the running dot.
    // This match does not remove the app from the separate running section.
    for (auto& item : items) {
        if (item.kind != Kind::Pinned || item.running) continue;
        const auto target = pathKey(item.target.empty() ? item.path : item.target);
        for (const auto& app : running) {
            const auto key = pathKey(app.executable);
            if (key.empty() || claimed.count(key) || !InstallSubfolder(target, key)) continue;
            claimed.insert(key);
            item.running = true;
            item.runningProgram = app.executable;
            break;
        }
    }
    if (lumeSlot == static_cast<int>(pinned.size())) addLume();
    if (showRunning) {
        std::vector<Item> extra;
        std::set<std::string> shown;
        for (const auto& app : running) {
            const auto key = pathKey(app.executable);
            if (key.empty() || !shown.insert(key).second) continue;
            if (app.asWindows) continue;  // its window thumbnails stand for it
            extra.push_back({Kind::Running, app.executable, app.name.empty() ? EntryName(app.executable) : app.name, true});
            if (runningPreview) extra.back().window = app.frontWindow;
        }
        if (!extra.empty()) {
            items.push_back({Kind::Separator, {}, {}, false});
            items.insert(items.end(), extra.begin(), extra.end());
        }
    }
    if (systemTray) {
        if (!items.empty() && items.back().kind != Kind::Separator) items.push_back({Kind::Separator, {}, {}, false});
        items.push_back({Kind::SystemTray, {}, "控制中心", false});
    }
    return items;
}

Change Pin(Preferences& value, std::string_view path, const TextKey& pathKey) {
    const auto normalized = organizer::NormalizeLocalPath(path);
    if (!normalized || !pathKey || pathKey(*normalized).empty()) return Change::Invalid;
    if (FindPath(value.pinned, *normalized, pathKey) != value.pinned.end()) return Change::Unchanged;
    if (value.pinned.size() >= MaxPinned) return Change::Limit;
    value.pinned.push_back(*normalized);
    return Change::Changed;
}

void AppendWindows(std::vector<Item>& items, const std::vector<ProgramWindow>& windows) {
    if (windows.empty()) return;
    const bool separated = std::any_of(items.begin(), items.end(), [](const Item& item) { return item.kind == Kind::Separator; });
    if (!separated) items.push_back({Kind::Separator, {}, {}, false});
    for (const auto& window : windows) {
        if (!window.handle) continue;
        Item item{Kind::Window, window.executable, window.title.empty() ? EntryName(window.executable) : window.title, false};
        item.window = window.handle;
        items.push_back(std::move(item));
    }
}

std::vector<Item> DockItemsFor(const Preferences& prefs, const std::vector<std::string>& targets,
                            const std::vector<RunningApp>& running, const TextKey& pathKey,
                            std::string_view lumePath, bool managerOpen,
                            const std::map<std::string, std::vector<std::string>>& collectionTargets) {
    auto openApps = running;
    // Window enumeration excludes our own process so resident widgets never
    // become running apps. Add only the open manager, even if its pin is hidden.
    if (managerOpen && !lumePath.empty() && pathKey && !pathKey(lumePath).empty() &&
        std::none_of(openApps.begin(), openApps.end(), [&](const auto& app) {
            return pathKey(app.executable) == pathKey(lumePath);
        })) openApps.push_back({std::string(lumePath), "Lume"});
    auto items = DockItems(prefs.pinned, targets, openApps, pathKey, prefs.lumeVisible, prefs.lumePosition, lumePath,
                           SystemTrayAvailable && prefs.controlCenter, prefs.showRunning, prefs.runningPreview);
    if (prefs.animation) {
        for (auto& item : items) {
            if (item.kind != Kind::Pinned && item.kind != Kind::Running && item.kind != Kind::Lume) continue;
            const auto key = pathKey(!item.runningProgram.empty() ? item.runningProgram :
                                     !item.target.empty() ? item.target : item.path);
            const auto found = std::find_if(openApps.begin(), openApps.end(),
                [&](const auto& app) { return pathKey(app.executable) == key; });
            if (found == openApps.end()) continue;
            item.animationWindows = found->windows;
            if (item.animationWindows.empty() && found->frontWindow)
                item.animationWindows.push_back(found->frontWindow);
        }
    }
    std::vector<Item> groups;
    for (const auto& id : prefs.dockCollections) {
        const auto* collection = FindCollection(prefs, id);
        if (!collection) continue;
        Item item{Kind::Collection, CollectionKey(id), collection->title};
        const auto members = CollectionItems(*collection, pathKey);
        for (std::size_t i = 0; i < std::min<std::size_t>(4, members.size()); ++i)
            item.previewPaths.push_back(members[i].path);
        const auto targetsForGroup = collectionTargets.find(id);
        if (prefs.animation && targetsForGroup != collectionTargets.end()) {
            item.collectionPrograms = targetsForGroup->second;
            std::vector<std::string> keys;
            for (const auto& program : item.collectionPrograms) keys.push_back(pathKey(program));
            for (const auto& app : openApps) {
                const auto program = pathKey(app.executable);
                if (std::none_of(keys.begin(), keys.end(), [&](const auto& key) {
                    return key == program || InstallSubfolder(key, program);
                })) continue;
                item.animationWindows.insert(item.animationWindows.end(), app.windows.begin(), app.windows.end());
                if (app.windows.empty() && app.frontWindow) item.animationWindows.push_back(app.frontWindow);
            }
        }
        groups.push_back(std::move(item));
    }
    auto afterLaunchpad = prefs.launchpadEnabled ? items.begin() + 1 : items.erase(items.begin());
    if (prefs.showStartMenu)
        afterLaunchpad = items.insert(afterLaunchpad, {Kind::StartMenu, {}, "开始菜单"}) + 1;
    items.insert(afterLaunchpad, groups.begin(), groups.end());
    if (!items.empty() && items.front().kind == Kind::Separator) items.erase(items.begin());
    return items;
}

void KeepControlCenterLast(std::vector<Item>& items) {
    const auto found = std::find_if(items.begin(), items.end(), [](const Item& item) { return item.kind == Kind::SystemTray; });
    if (found == items.end() || found + 1 == items.end()) return;
    const auto center = *found;
    items.erase(found);
    if (!items.empty() && items.back().kind != Kind::Separator) items.push_back({Kind::Separator, {}, {}, false});
    items.push_back(center);
}

Change Unpin(Preferences& value, std::string_view path, const TextKey& pathKey) {
    if (!pathKey) return Change::Invalid;
    const auto found = FindPath(value.pinned, path, pathKey);
    if (found == value.pinned.end()) return Change::Unchanged;
    const auto index = static_cast<std::size_t>(found - value.pinned.begin());
    value.pinned.erase(found);
    if (index < static_cast<std::size_t>(value.lumePosition))
        --value.lumePosition;
    value.lumePosition = std::min(value.lumePosition, static_cast<int>(value.pinned.size()));
    return Change::Changed;
}

bool IsPinned(const Preferences& value, std::string_view path, const TextKey& pathKey) {
    auto copy = value.pinned;
    return pathKey && FindPath(copy, path, pathKey) != copy.end();
}

Change Add(Preferences& value, const std::vector<std::string>& paths, const TextKey& pathKey) {
    if (!pathKey) return Change::Invalid;
    auto next = value.added;
    auto removed = value.removed;
    bool changed = false;
    for (const auto& path : paths) {
        const auto normalized = organizer::NormalizeLocalPath(path);
        if (!normalized || pathKey(*normalized).empty()) return Change::Invalid;
        // Adding a path the user removed from the launchpad shows it again.
        for (auto found = FindPath(removed, *normalized, pathKey); found != removed.end();
             found = FindPath(removed, *normalized, pathKey)) { removed.erase(found); changed = true; }
        if (FindPath(next, *normalized, pathKey) != next.end()) continue;
        if (next.size() >= MaxAdded) return Change::Limit;
        next.push_back(*normalized);
        changed = true;
    }
    if (!changed) return Change::Unchanged;
    value.added = std::move(next);
    value.removed = std::move(removed);
    return Change::Changed;
}

Change MovePinned(Preferences& value, std::size_t from, std::size_t to) {
    if (from >= value.pinned.size() || to >= value.pinned.size()) return Change::Invalid;
    if (from == to) return Change::Unchanged;
    const auto lume = static_cast<std::size_t>(std::max(0, value.lumePosition));
    auto moving = value.pinned[from];
    value.pinned.erase(value.pinned.begin() + static_cast<std::ptrdiff_t>(from));
    value.pinned.insert(value.pinned.begin() + static_cast<std::ptrdiff_t>(to), std::move(moving));
    if (from < lume && to >= lume)
        value.lumePosition = static_cast<int>(lume - 1);
    else if (from >= lume && to < lume)
        value.lumePosition = static_cast<int>(lume + 1);
    value.lumePosition = std::min(value.lumePosition, static_cast<int>(value.pinned.size()));
    return Change::Changed;
}

Change PinAt(Preferences& value, std::string_view path, std::size_t index, const TextKey& pathKey) {
    const auto normalized = organizer::NormalizeLocalPath(path);
    if (!normalized || !pathKey || pathKey(*normalized).empty()) return Change::Invalid;
    const auto found = FindPath(value.pinned, *normalized, pathKey);
    if (found != value.pinned.end()) {
        const auto from = static_cast<std::size_t>(found - value.pinned.begin());
        return MovePinned(value, from, std::min(index, value.pinned.size() - 1));
    }
    if (value.pinned.size() >= MaxPinned) return Change::Limit;
    const auto insertion = std::min(index, value.pinned.size());
    value.pinned.insert(value.pinned.begin() + static_cast<std::ptrdiff_t>(insertion), *normalized);
    if (insertion <= static_cast<std::size_t>(std::max(0, value.lumePosition)))
        ++value.lumePosition;
    return Change::Changed;
}

Change Arrange(Preferences& value, std::vector<std::string> order, const TextKey& pathKey) {
    if (!ValidOrder(order, value.collections, pathKey)) return Change::Invalid;
    if (order == value.order) return Change::Unchanged;
    value.order = std::move(order);
    return Change::Changed;
}

Change CreateCollection(Preferences& value, std::string id, std::string title) {
    if (!ValidCollectionId(id) || !ValidCollectionTitle(title) || value.collections.size() >= MaxCollections)
        return Change::Limit;
    if (std::any_of(value.collections.begin(), value.collections.end(),
                    [&](const auto& collection) { return collection.id == id; })) return Change::Invalid;
    value.collections.push_back({std::move(id), std::move(title), {}});
    return Change::Changed;
}

Change RenameCollection(Preferences& value, std::string_view id, std::string_view title) {
    if (!ValidCollectionTitle(title)) return Change::Invalid;
    const auto found = std::find_if(value.collections.begin(), value.collections.end(),
                                    [&](const auto& collection) { return collection.id == id; });
    if (found == value.collections.end()) return Change::Invalid;
    if (found->title == title) return Change::Unchanged;
    found->title = std::string(title);
    return Change::Changed;
}

Change DeleteCollection(Preferences& value, std::string_view id) {
    const auto found = std::find_if(value.collections.begin(), value.collections.end(),
                                    [&](const auto& collection) { return collection.id == id; });
    if (found == value.collections.end()) return Change::Unchanged;
    for (const auto& path : found->paths) {
        if (std::none_of(value.added.begin(), value.added.end(), [&](const auto& item) { return item == path; })) {
            if (value.added.size() >= MaxAdded) return Change::Limit;
            value.added.push_back(path);
        }
    }
    value.order.erase(std::remove(value.order.begin(), value.order.end(), CollectionKey(id)), value.order.end());
    value.dockCollections.erase(std::remove(value.dockCollections.begin(), value.dockCollections.end(), id), value.dockCollections.end());
    value.collections.erase(found);
    return Change::Changed;
}

Change AddToCollection(Preferences& value, std::string_view id, const std::vector<std::string>& paths,
                       const TextKey& pathKey) {
    const auto found = std::find_if(value.collections.begin(), value.collections.end(),
                                    [&](const auto& collection) { return collection.id == id; });
    if (found == value.collections.end() || !pathKey) return Change::Invalid;
    auto added = value.added;
    std::set<std::string> present;
    for (const auto& path : found->paths) present.insert(pathKey(path));
    bool changed = false;
    for (const auto& raw : paths) {
        const auto normalized = organizer::NormalizeLocalPath(raw);
        if (!normalized || pathKey(*normalized).empty()) return Change::Invalid;
        const auto key = pathKey(*normalized);
        if (std::none_of(added.begin(), added.end(), [&](const auto& item) { return pathKey(item) == key; })) {
            if (added.size() >= MaxAdded) return Change::Limit;
            added.push_back(*normalized);
            changed = true;
        }
        if (!present.insert(key).second) continue;
        if (found->paths.size() >= MaxAdded) return Change::Limit;
        found->paths.push_back(*normalized);
        changed = true;
    }
    if (!changed) return Change::Unchanged;
    value.added = std::move(added);
    // A path may only live in one collection; remove it from the others.
    for (auto& collection : value.collections) {
        if (collection.id == id) continue;
        collection.paths.erase(std::remove_if(collection.paths.begin(), collection.paths.end(),
            [&](const auto& path) { return std::any_of(paths.begin(), paths.end(),
                [&](const auto& raw) {
                    const auto normalized = organizer::NormalizeLocalPath(raw);
                    return normalized && pathKey(*normalized) == pathKey(path);
                }); }), collection.paths.end());
    }
    return Change::Changed;
}

Change RemoveFromCollection(Preferences& value, std::string_view id, std::string_view path,
                            const TextKey& pathKey) {
    if (!pathKey) return Change::Invalid;
    const auto found = std::find_if(value.collections.begin(), value.collections.end(),
                                    [&](const auto& collection) { return collection.id == id; });
    if (found == value.collections.end()) return Change::Invalid;
    const auto key = pathKey(path);
    const auto before = found->paths.size();
    found->paths.erase(std::remove_if(found->paths.begin(), found->paths.end(),
                                      [&](const auto& item) { return pathKey(item) == key; }),
                       found->paths.end());
    return found->paths.size() == before ? Change::Unchanged : Change::Changed;
}

Change ArrangeCollection(Preferences& value, std::string_view id, std::vector<std::string> paths,
                         const TextKey& pathKey) {
    if (!pathKey) return Change::Invalid;
    const auto found = std::find_if(value.collections.begin(), value.collections.end(),
                                    [&](const auto& collection) { return collection.id == id; });
    if (found == value.collections.end()) return Change::Invalid;
    std::set<std::string> keys;
    for (const auto& path : paths) {
        const auto normalized = organizer::NormalizeLocalPath(path);
        if (!normalized || *normalized != path || !keys.insert(pathKey(path)).second) return Change::Invalid;
    }
    if (paths.size() > MaxAdded) return Change::Invalid;
    if (found->paths == paths) return Change::Unchanged;
    found->paths = std::move(paths);
    return Change::Changed;
}

Change RemoveAdded(Preferences& value, std::string_view path, const TextKey& pathKey) {
    if (!pathKey) return Change::Invalid;
    const auto found = FindPath(value.added, path, pathKey);
    if (found == value.added.end()) return Change::Unchanged;
    value.added.erase(found);
    return Change::Changed;
}
Change RemoveFromLaunchpad(Preferences& value, std::string_view path, const std::vector<std::string>& desktop,
                           const TextKey& pathKey) {
    if (!pathKey || path.empty() || pathKey(path).empty()) return Change::Invalid;
    auto next = value;
    bool changed = false;
    if (const auto found = FindPath(next.added, path, pathKey); found != next.added.end()) {
        next.added.erase(found);
        changed = true;
    }
    auto onDesktop = desktop;
    if (FindPath(onDesktop, path, pathKey) != onDesktop.end() &&
        FindPath(next.removed, path, pathKey) == next.removed.end()) {
        if (next.removed.size() >= MaxOrder) return Change::Limit;
        next.removed.emplace_back(path);
        changed = true;
    }
    if (!changed) return Change::Unchanged;
    const auto key = pathKey(path);
    next.order.erase(std::remove_if(next.order.begin(), next.order.end(),
                                    [&](const std::string& item) { return !IsCollectionKey(item) && pathKey(item) == key; }),
                     next.order.end());
    value = std::move(next);
    return Change::Changed;
}
Change PinCollection(Preferences& value, std::string_view id, const TextKey& pathKey) {
    const auto* collection = FindCollection(value, id);
    if (!collection || !pathKey) return Change::Invalid;
    if (std::find(value.dockCollections.begin(), value.dockCollections.end(), id) != value.dockCollections.end())
        return Change::Unchanged;
    if (value.dockCollections.size() >= MaxCollections) return Change::Limit;
    const auto paths = collection->paths;
    auto next = value;
    next.dockCollections.insert(next.dockCollections.begin(), std::string(id));
    const auto moved = MoveToDockCollection(next, id, paths, pathKey);
    if (moved == Change::Invalid || moved == Change::Limit) return moved;
    value = std::move(next);
    return Change::Changed;
}
Change MoveToDockCollection(Preferences& value, std::string_view id, const std::vector<std::string>& paths,
                             const TextKey& pathKey) {
    auto next = value;
    const auto change = AddToCollection(next, id, paths, pathKey);
    if (change == Change::Invalid || change == Change::Limit) return change;
    bool changed = change == Change::Changed;
    for (const auto& path : paths) changed |= Unpin(next, path, pathKey) == Change::Changed;
    if (!changed) return Change::Unchanged;
    value = std::move(next);
    return Change::Changed;
}
Change MoveFromDockCollection(Preferences& value, std::string_view id, std::string_view path,
                               std::size_t slot, const TextKey& pathKey) {
    const auto* collection = FindCollection(value, id);
    if (!collection || !pathKey || std::none_of(collection->paths.begin(), collection->paths.end(),
        [&](const auto& member) { return pathKey(member) == pathKey(path); })) return Change::Invalid;
    auto next = value;
    const auto pinned = PinAt(next, path, slot, pathKey);
    if (pinned == Change::Invalid || pinned == Change::Limit) return pinned;
    if (RemoveFromCollection(next, id, path, pathKey) != Change::Changed) return Change::Invalid;
    value = std::move(next);
    return Change::Changed;
}
bool Movable(const Item& item) {
    return item.kind == Kind::StartMenu || item.kind == Kind::Lume || item.kind == Kind::Pinned || item.kind == Kind::Running;
}
bool Removable(const Item& item) { return Movable(item); }
std::string MotionKey(const Item& item) {
    return std::to_string(static_cast<int>(item.kind)) + "|" + item.path +
           (item.kind == Kind::Window && item.window ? "#" + std::to_string(item.window) : std::string{});
}
}  // namespace lume::dock
