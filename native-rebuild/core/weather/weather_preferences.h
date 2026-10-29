#pragma once
#include "core/settings/file_store.h"
#include "core/widget/widget_preferences_store.h"
#include "core/weather/weather.h"
namespace lume::weather {
inline constexpr widget::Limits WindowLimits{320,152,320,152,720,720};
struct Preferences {
    widget::Preferences window=widget::Defaults(WindowLimits);
    std::string city="auto";
};
bool DecodePreferences(const std::string&,Preferences&);
bool EncodePreferences(const Preferences&,std::string&);
struct LoadedPreferences {
    Preferences value;
    widget::LoadState state=widget::LoadState::Fresh;
};
class PreferencesStore final {
public:
    explicit PreferencesStore(settings::IFileStore& files):files_(files){}
    LoadedPreferences Load(const std::string&) const;
    bool Save(const std::string&,const Preferences&);
private:
    settings::IFileStore& files_;
};
}
