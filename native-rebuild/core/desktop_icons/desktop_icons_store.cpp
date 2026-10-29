#include "core/desktop_icons/desktop_icons_store.h"
#include "core/settings/fixed_json_reader.h"

namespace lume::desktop_icons {
bool Encode(const Preferences& p, std::string& output) {
    if (!Valid(p)) return false;
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    output = std::string("{\"version\":1,\"enabled\":") + boolean(p.enabled) +
        ",\"enableAutoHide\":" + boolean(p.enableAutoHide) +
        ",\"autoHideDelay\":" + std::to_string(p.autoHideDelay) +
        ",\"showIconsOnAnyInput\":" + boolean(p.showIconsOnAnyInput) +
        ",\"manuallyHidden\":" + boolean(p.manuallyHidden) + "}";
    return true;
}
bool Decode(std::string_view text, Preferences& output) {
    if (text.empty() || text.size() > MaxPreferencesBytes) return false;
    settings::Reader reader(text);
    if (!reader.Take('{')) return false;
    unsigned seen = 0;
    Preferences p;
    do {
        std::string key;
        if (!reader.String(key) || !reader.Take(':')) return false;
        unsigned flag = 0;
        if (key == "version") {
            int version = 0;
            flag = 1;
            if (!reader.Integer(version) || version != 1) return false;
        } else if (key == "enabled") {
            flag = 2; if (!reader.Boolean(p.enabled)) return false;
        } else if (key == "enableAutoHide") {
            flag = 4; if (!reader.Boolean(p.enableAutoHide)) return false;
        } else if (key == "autoHideDelay") {
            flag = 8; if (!reader.Integer(p.autoHideDelay)) return false;
        } else if (key == "showIconsOnAnyInput") {
            flag = 16; if (!reader.Boolean(p.showIconsOnAnyInput)) return false;
        } else if (key == "manuallyHidden") {
            flag = 32; if (!reader.Boolean(p.manuallyHidden)) return false;
        } else return false;
        if (seen & flag) return false;
        seen |= flag;
    } while (reader.Take(','));
    if (!reader.Take('}') || !reader.End() || seen != 63 || !Valid(p)) return false;
    output = p;
    return true;
}
LoadedPreferences PreferencesStore::Load(const std::string& path) const {
    LoadedPreferences result;
    if (path.empty()) { result.state = LoadState::Failed; return result; }
    std::string text;
    if (files_.Exists(path)) {
        if (files_.ReadBounded(path, MaxPreferencesBytes, text) && Decode(text, result.value)) {
            result.state = LoadState::Loaded; return result;
        }
        result.state = LoadState::Failed;
    }
    if (files_.Exists(path + ".bak")) {
        result.state = files_.ReadBounded(path + ".bak", MaxPreferencesBytes, text) &&
                       Decode(text, result.value) ? LoadState::Recovered : LoadState::Failed;
    }
    return result;
}
bool PreferencesStore::Save(const std::string& path, const Preferences& value) {
    std::string encoded;
    if (path.empty() || !Encode(value, encoded)) return false;
    const bool exists = files_.Exists(path);
    std::string original;
    Preferences old;
    if (exists && !files_.ReadBounded(path, MaxPreferencesBytes, original)) return false;
    const bool validOriginal = exists && Decode(original, old);
    const auto temporary = path + ".tmp", backupTemporary = path + ".bak.tmp";
    const auto fail = [&] {
        files_.Remove(temporary); files_.Remove(backupTemporary);
        return false;
    };
    if (!files_.Write(temporary, encoded)) return fail();
    if (validOriginal && (!files_.Write(backupTemporary, original) ||
                          !files_.Rename(backupTemporary, path + ".bak"))) return fail();
    std::string quarantine;
    if (exists && !validOriginal) {
        for (unsigned i = 0; i < 1024; ++i) {
            const auto target = path + ".corrupt" + (i ? "." + std::to_string(i) : "");
            if (!files_.Exists(target)) { quarantine = target; break; }
        }
        if (quarantine.empty() || !files_.Rename(path, quarantine)) return fail();
    }
    if (!files_.Rename(temporary, path)) {
        if (!quarantine.empty()) files_.Rename(quarantine, path);
        return fail();
    }
    return true;
}
}  // namespace lume::desktop_icons
