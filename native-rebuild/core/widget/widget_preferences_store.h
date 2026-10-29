#pragma once
#include "core/widget/widget_preferences.h"
#include "core/settings/file_store.h"
namespace lume::widget {
enum class LoadState { Fresh,Loaded,Recovered,Failed };
struct LoadedPreferences {Preferences value;LoadState state=LoadState::Fresh;};
class PreferencesStore final {
public:
    PreferencesStore(settings::IFileStore& files,Limits limits):files_(files),limits_(limits){}
    LoadedPreferences Load(const std::string& path)const;
    bool Save(const std::string& path,const Preferences&);
private:
    settings::IFileStore& files_;Limits limits_;
};
}
