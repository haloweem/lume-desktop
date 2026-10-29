#include "core/settings/config_migrator.h"

namespace lume::settings {

SettingsSnapshot ConfigMigrator::Migrate(SettingsSnapshot value) {
    if (value.schemaVersion == 0) value.schemaVersion = 1;
    if (value.clock.style != "welfare" && value.clock.style != "mond" && value.clock.style != "insideDream") {
        value.clock.style = "welfare";
    }
    const auto defaults = clock::DefaultProfiles();
    for (std::size_t i = 0; i < value.clock.profiles.size(); ++i) {
        if (!clock::ValidAppearance(value.clock.profiles[i])) value.clock.profiles[i] = defaults[i];
    }
    for (auto& widget : value.widgets) {
        if (widget.width < 1) widget.width = 320;
        if (widget.height < 1) widget.height = 152;
    }
    value.schemaVersion = kCurrentSchemaVersion;
    return value;
}

}  // namespace lume::settings
