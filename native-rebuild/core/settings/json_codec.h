#pragma once

#include "core/settings/settings_types.h"

#include <string>

namespace lume::settings {

class ISettingsCodec {
public:
    virtual ~ISettingsCodec() = default;
    virtual bool Decode(const std::string& text, SettingsSnapshot& out) const = 0;
    virtual bool Encode(const SettingsSnapshot& value, std::string& out) const = 0;
};

}  // namespace lume::settings
