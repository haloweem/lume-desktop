#include "core/settings/settings_store.h"
#include "core/settings/config_migrator.h"

namespace lume::settings {

SettingsSnapshot SettingsStore::Defaults() { return {}; }

SettingsSnapshot SettingsStore::Load(const std::string& path) const {
    std::string text;
    SettingsSnapshot value;
    if (!files_.Read(path, text) || !codec_.Decode(text, value)) {
        if (files_.Exists(path)) {
            files_.Rename(path, path + ".corrupt");
        }
        return Defaults();
    }
    return ConfigMigrator::Migrate(value);
}

bool SettingsStore::Save(const std::string& path, const SettingsSnapshot& value) {
    std::string encoded;
    if (!codec_.Encode(value, encoded)) return false;
    const std::string temporary = path + ".tmp";
    if (!files_.Write(temporary, encoded)) return false;
    if (files_.Exists(path) && !files_.Rename(path, path + ".bak")) {
        files_.Remove(temporary); return false;
    }
    if (!files_.Rename(temporary, path)) {
        files_.Remove(temporary);
        if (files_.Exists(path + ".bak")) files_.Rename(path + ".bak", path);
        return false;
    }
    return true;
}

}  // namespace lume::settings
