#pragma once

#include "core/settings/json_codec.h"
#include "core/settings/file_store.h"

#include <string>

namespace lume::settings {

class SettingsStore final {
public:
    SettingsStore(IFileStore& files, const ISettingsCodec& codec) : files_(files), codec_(codec) {}
    SettingsSnapshot Load(const std::string& path) const;
    bool Save(const std::string& path, const SettingsSnapshot& value);

private:
    static SettingsSnapshot Defaults();
    IFileStore& files_;
    const ISettingsCodec& codec_;
};

}  // namespace lume::settings
