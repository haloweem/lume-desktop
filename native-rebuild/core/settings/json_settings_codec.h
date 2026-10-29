#pragma once

#include "core/settings/json_codec.h"

namespace lume::settings {

class JsonSettingsCodec final : public ISettingsCodec {
public:
    bool Decode(const std::string& text, SettingsSnapshot& out) const override;
    bool Encode(const SettingsSnapshot& value, std::string& out) const override;
};

}  // namespace lume::settings
