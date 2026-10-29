#pragma once

#include "core/settings/settings_types.h"

namespace lume::settings {

class ConfigMigrator final {
public:
    static SettingsSnapshot Migrate(SettingsSnapshot value);
};

}  // namespace lume::settings
