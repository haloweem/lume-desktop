#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "core/settings/clock_settings.h"

namespace lume::settings {

constexpr std::uint32_t kCurrentSchemaVersion = 3;

struct WidgetLayout {
    std::string id;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool visible = true;
    bool locked = false;
};

struct SettingsSnapshot {
    std::uint32_t schemaVersion = kCurrentSchemaVersion;
    ClockSettings clock{};
    std::vector<WidgetLayout> widgets;
};

}  // namespace lume::settings
