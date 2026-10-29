#pragma once

#include "core/clock/clock_types.h"

#include <string>

namespace lume::settings {

struct ClockSettings {
    bool enabled = false;
    std::string style = "welfare";
    clock::Profiles profiles = clock::DefaultProfiles();
    clock::Appearance& Active() { return profiles[clock::Index(clock::ParseStyle(style))]; }
    const clock::Appearance& Active() const { return profiles[clock::Index(clock::ParseStyle(style))]; }
};

}  // namespace lume::settings
