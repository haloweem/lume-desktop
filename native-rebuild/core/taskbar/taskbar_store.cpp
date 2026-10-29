#include "core/taskbar/taskbar_store.h"
#include "vendor/nlohmann/json.hpp"
#include <set>

namespace lume::taskbar {
using Json = nlohmann::json;
const char* ErrorName(Error error) {
    switch (error) {
        case Error::None: return "";
        case Error::Load: return "load";
        case Error::Save: return "save";
        case Error::Busy: return "busy";
        case Error::Unsupported: return "unsupported";
        case Error::MissingPayload: return "missing-payload";
        case Error::PayloadIntegrity: return "payload-integrity";
        case Error::Conflict: return "conflict";
        case Error::Discover: return "discover";
        case Error::ReadAppearance: return "read-appearance";
        case Error::ApplyAppearance: return "apply-appearance";
        case Error::ReadAutoHide: return "read-auto-hide";
        case Error::ApplyAutoHide: return "apply-auto-hide";
        case Error::CoreStart: return "core-start";
        case Error::CoreExited: return "core-exited";
        case Error::CoreStop: return "core-stop";
        case Error::Cancelled: return "cancelled";
        case Error::Recovery: return "recovery";
        case Error::Watch: return "watch";
        case Error::SystemEffectsDisabled: return "system-effects-disabled";
        case Error::ReadVisibility: return "read-visibility";
        case Error::ApplyVisibility: return "apply-visibility";
    }
    return "unknown";
}
const char* ModeName(Mode mode) {
    switch (mode) {
        case Mode::Unknown: return "unavailable";
        case Mode::Classic: return "classic";
        case Mode::Windows11X64: return "windows11-x64";
        case Mode::Windows11Arm64: return "windows11-arm64";
    }
    return "unavailable";
}
namespace {
bool Valid(const Record& r) {
    if (r.appearances.size() > MaxTaskbars || r.hiddenTaskbars.size() > MaxTaskbars ||
        (r.preferences.hideBehavior != HideBehavior::AutoHide && r.preferences.hideBehavior != HideBehavior::FullyHidden)) return false;
    for (std::size_t i = 0; i < r.hiddenTaskbars.size(); ++i) {
        const auto& a = r.hiddenTaskbars[i];
        if (!a.window || !a.process || !a.processCreated || a.layeredFlags > 3) return false;
        for (std::size_t j = 0; j < i; ++j) if (SameTarget(a, r.hiddenTaskbars[j])) return false;
    }
    for (std::size_t i = 0; i < r.appearances.size(); ++i) {
        const auto& a = r.appearances[i];
        if (!a.window || !a.process || !a.processCreated) return false;
        for (std::size_t j = 0; j < i; ++j) if (SameTarget(a, r.appearances[j])) return false;
    }
    return true;
}
bool Unsigned(const Json& o, const char* key, std::uint64_t limit, std::uint64_t& out) {
    const auto it = o.find(key);
    if (it == o.end() || !it->is_number_unsigned()) return false;
    out = it->get<std::uint64_t>(); return out <= limit;
}
}
bool Encode(const Record& r, std::string& out) {
    if (!Valid(r)) return false;
    Json appearances = Json::array();
    for (const auto& a : r.appearances) appearances.push_back({
        {"window", a.window}, {"process", a.process}, {"processCreated", a.processCreated},
        {"accent", a.accent}, {"flags", a.flags}, {"color", a.color}, {"animation", a.animation}});
    Json hidden = Json::array();
    for (const auto& a : r.hiddenTaskbars) hidden.push_back({
        {"window", a.window}, {"process", a.process}, {"processCreated", a.processCreated},
        {"extendedStyle", a.extendedStyle}, {"colorKey", a.colorKey}, {"alpha", a.alpha},
        {"layeredFlags", a.layeredFlags}, {"visible", a.visible}});
    const auto text = Json{{"version", 2}, {"enabled", r.preferences.enabled},
        {"transparentTaskbar", r.preferences.transparentTaskbar}, {"autoHideTaskbar", r.preferences.autoHideTaskbar},
        {"hideBehavior", r.preferences.hideBehavior == HideBehavior::FullyHidden ? "fully-hidden" : "auto-hide"},
        {"hiddenTaskbars", std::move(hidden)},
        {"ownsAutoHide", r.ownsAutoHide}, {"originalAutoHide", r.originalAutoHide},
        {"ownsCore", r.ownsCore}, {"appearances", std::move(appearances)}}.dump();
    if (text.size() > MaxRecordBytes) return false;
    out = text; return true;
}
bool Decode(std::string_view text, Record& out) {
    if (text.empty() || text.size() > MaxRecordBytes) return false;
    bool duplicate = false;
    std::vector<std::set<std::string>> objects;
    auto callback = [&](int, Json::parse_event_t event, Json& value) {
        if (event == Json::parse_event_t::object_start) objects.emplace_back();
        if (event == Json::parse_event_t::key &&
            (objects.empty() || !objects.back().insert(value.get<std::string>()).second)) duplicate = true;
        if (event == Json::parse_event_t::object_end && !objects.empty()) objects.pop_back();
        return true;
    };
    const auto data = Json::parse(text.begin(), text.end(), callback, false);
    if (duplicate || data.is_discarded() || !data.is_object() ||
        !data.contains("version") || !data["version"].is_number_unsigned()) return false;
    const bool legacy = data["version"] == 1;
    if (!(legacy ? data.size() == 8 : data["version"] == 2 && data.size() == 10)) return false;
    Record r;
    if (!legacy) {
        const auto behavior = data.find("hideBehavior");
        if (behavior == data.end() || !behavior->is_string() ||
            (*behavior != "auto-hide" && *behavior != "fully-hidden")) return false;
        r.preferences.hideBehavior = *behavior == "fully-hidden" ? HideBehavior::FullyHidden : HideBehavior::AutoHide;
        const auto hidden = data.find("hiddenTaskbars");
        if (hidden == data.end() || !hidden->is_array() || hidden->size() > MaxTaskbars) return false;
        for (const auto& row : *hidden) {
            if (!row.is_object() || row.size() != 8 || !row.contains("visible") || !row["visible"].is_boolean()) return false;
            Visibility a; std::uint64_t value = 0;
            if (!Unsigned(row, "window", UINT64_MAX, a.window) ||
                !Unsigned(row, "processCreated", UINT64_MAX, a.processCreated) ||
                !Unsigned(row, "process", UINT32_MAX, value)) return false;
            a.process = static_cast<std::uint32_t>(value);
            if (!Unsigned(row, "extendedStyle", UINT32_MAX, value)) return false;
            a.extendedStyle = static_cast<std::uint32_t>(value);
            if (!Unsigned(row, "colorKey", UINT32_MAX, value)) return false;
            a.colorKey = static_cast<std::uint32_t>(value);
            if (!Unsigned(row, "alpha", UINT8_MAX, value)) return false;
            a.alpha = static_cast<std::uint8_t>(value);
            if (!Unsigned(row, "layeredFlags", 3, value)) return false;
            a.layeredFlags = static_cast<std::uint32_t>(value);
            a.visible = row["visible"].get<bool>();
            r.hiddenTaskbars.push_back(a);
        }
    }
    const auto boolean = [&](const char* key, bool& target) {
        const auto it = data.find(key);
        if (it == data.end() || !it->is_boolean()) return false;
        target = it->get<bool>(); return true;
    };
    if (!boolean("enabled", r.preferences.enabled) ||
        !boolean("transparentTaskbar", r.preferences.transparentTaskbar) ||
        !boolean("autoHideTaskbar", r.preferences.autoHideTaskbar) ||
        !boolean("ownsAutoHide", r.ownsAutoHide) ||
        !boolean("originalAutoHide", r.originalAutoHide) ||
        !boolean("ownsCore", r.ownsCore)) return false;
    const auto list = data.find("appearances");
    if (list == data.end() || !list->is_array() || list->size() > MaxTaskbars) return false;
    for (const auto& row : *list) {
        if (!row.is_object() || row.size() != 7) return false;
        Appearance a; std::uint64_t value = 0;
        if (!Unsigned(row, "window", UINT64_MAX, a.window) ||
            !Unsigned(row, "processCreated", UINT64_MAX, a.processCreated) ||
            !Unsigned(row, "process", UINT32_MAX, value)) return false;
        a.process = static_cast<std::uint32_t>(value);
        if (!Unsigned(row, "accent", UINT32_MAX, value)) return false;
        a.accent = static_cast<std::uint32_t>(value);
        if (!Unsigned(row, "flags", UINT32_MAX, value)) return false;
        a.flags = static_cast<std::uint32_t>(value);
        if (!Unsigned(row, "color", UINT32_MAX, value)) return false;
        a.color = static_cast<std::uint32_t>(value);
        if (!Unsigned(row, "animation", UINT32_MAX, value)) return false;
        a.animation = static_cast<std::uint32_t>(value);
        r.appearances.push_back(a);
    }
    if (!Valid(r)) return false;
    out = std::move(r); return true;
}
LoadedRecord RecordStore::Load(const std::string& path) const {
    LoadedRecord result;
    if (path.empty()) { result.state = LoadState::Failed; return result; }
    if (!files_.Exists(path)) return result;
    std::string text;
    result.state = files_.ReadBounded(path, MaxRecordBytes, text) && Decode(text, result.record)
                       ? LoadState::Loaded : LoadState::Failed;
    return result;
}
bool RecordStore::Save(const std::string& path, const Record& value) {
    std::string text;
    if (path.empty() || !Encode(value, text)) return false;
    const auto temporary = path + ".tmp";
    if (!files_.Write(temporary, text) || !files_.Rename(temporary, path)) {
        files_.Remove(temporary); return false;
    }
    return true;
}
}  // namespace lume::taskbar
