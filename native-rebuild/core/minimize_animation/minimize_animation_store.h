#pragma once
#include "core/minimize_animation/minimize_animation.h"
#include "core/settings/file_store.h"
#include <string_view>

namespace lume::minimize_animation {
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
