#include "core/settings/settings_store.h"
#include "core/settings/config_migrator.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

namespace {
class FakeCodec final : public lume::settings::ISettingsCodec {
public:
    bool Decode(const std::string& text, lume::settings::SettingsSnapshot& out) const override {
        if (text == "corrupt") return false;
        out.schemaVersion = text == "legacy" ? 1u : lume::settings::kCurrentSchemaVersion;
        out.clock.enabled = text != "disabled";
        return true;
    }
    bool Encode(const lume::settings::SettingsSnapshot& value, std::string& out) const override {
        out = "schema=" + std::to_string(value.schemaVersion);
        return true;
    }
};

class FakeFiles final : public lume::settings::IFileStore {
public:
    bool Read(const std::string& path, std::string& out) const override {
        auto it = files.find(path); if (it == files.end()) return false; out = it->second; return true;
    }
    bool Exists(const std::string& path) const override { return files.count(path) != 0; }
    bool Write(const std::string& path, const std::string& value) override {
        if (failWrite) return false;
        files[path] = value;
        return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        if (failRename) return false;
        auto it = files.find(from);
        if (it == files.end()) return false;
        files[to] = it->second;
        files.erase(it);
        return true;
    }
    bool Remove(const std::string& path) override { files.erase(path); return true; }
    mutable std::map<std::string, std::string> files;
    bool failWrite = false;
    bool failRename = false;
};

void Expect(bool value, const char* message) { if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }

void TestDefaultsAndCorruptIsolation() {
    FakeFiles files; FakeCodec codec; lume::settings::SettingsStore store(files, codec);
    const auto defaults = store.Load("settings.json");
    Expect(!defaults.clock.enabled, "time widget defaults disabled");
    files.files["settings.json"] = "corrupt";
    const auto recovered = store.Load("settings.json");
    Expect(!recovered.clock.enabled, "corrupt file falls back to disabled defaults");
    Expect(files.Exists("settings.json.corrupt"), "corrupt file isolated");
}

void TestBackupAndAtomicWrite() {
    FakeFiles files; FakeCodec codec; lume::settings::SettingsStore store(files, codec);
    files.files["settings.json"] = "legacy";
    auto current = store.Load("settings.json");
    Expect(store.Save("settings.json", current), "save settings");
    Expect(files.Exists("settings.json.bak"), "backup created");
    Expect(!files.Exists("settings.json.tmp"), "temporary removed");
    files.failRename = true;
    Expect(!store.Save("settings.json", current), "rename failure reported");
}

void TestMigrationNormalizesLegacyValues() {
    lume::settings::SettingsSnapshot value;
    value.schemaVersion = 1;
    value.clock.profiles[0].alpha = 300;
    value.clock.style = "unknown";
    value.widgets.push_back({"weather", 0, 0, 0, 0, true, false});
    const auto migrated = lume::settings::ConfigMigrator::Migrate(value);
    Expect(migrated.schemaVersion == lume::settings::kCurrentSchemaVersion, "migration schema");
    Expect(migrated.clock.Active().alpha == 255 && migrated.clock.style == "welfare", "migration clock values");
    Expect(migrated.widgets.front().width == 320 && migrated.widgets.front().height == 152, "migration widget size");
}
}

int main() { TestDefaultsAndCorruptIsolation(); TestBackupAndAtomicWrite(); TestMigrationNormalizesLegacyValues(); return 0; }
