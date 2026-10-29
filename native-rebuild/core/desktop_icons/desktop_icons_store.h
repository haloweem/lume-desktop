#pragma once
#include "core/desktop_icons/desktop_icons_policy.h"
#include "core/settings/file_store.h"
#include <string>
#include <string_view>

namespace lume::desktop_icons {
constexpr std::size_t MaxPreferencesBytes = 4096;
bool Encode(const Preferences&, std::string&);
bool Decode(std::string_view, Preferences&);
enum class LoadState { Fresh, Loaded, Recovered, Failed };
struct LoadedPreferences { Preferences value; LoadState state = LoadState::Fresh; };
class PreferencesStore final {
public:
    explicit PreferencesStore(settings::IFileStore& files) : files_(files) {}
    LoadedPreferences Load(const std::string&) const;
    bool Save(const std::string&, const Preferences&);
private:
    settings::IFileStore& files_;
};
}
