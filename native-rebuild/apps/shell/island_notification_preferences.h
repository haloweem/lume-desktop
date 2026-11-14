#pragma once
#include "core/settings/file_store.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lume::shell {
struct IslandNotificationSettings {
    bool waterEnabled = false;
    int waterIntervalMinutes = 60;
    int waterGoal = 8;
    bool standEnabled = false;
    int standIntervalMinutes = 60;
    bool sleepEnabled = false;
    std::string sleepTime = "23:30";
    int sleepLeadMinutes = 30;
    bool offWorkEnabled = false;
    std::string offWorkTime = "18:00";
    std::string offWorkStyle = "default";
    bool weatherSevere = false;
    bool weatherRain = false;
    bool weatherTemperatureDrop = false;
    int temperatureDropDegrees = 5;
    bool messagesQQ = false;
    bool messagesWeChat = false;
    bool messagePreview = false;
};

// Page preferences only. Reading or saving these settings never grants access,
// starts a scheduler, or subscribes to notifications from another application.
class IslandNotificationPreferences final {
public:
    IslandNotificationPreferences(settings::IFileStore&, const std::string& islandPreferencesPath);
    std::optional<std::wstring> Handle(std::wstring_view);
    std::wstring State() const;
    const IslandNotificationSettings& Snapshot() const { return preferences_; }
private:
    void Load();
    bool Commit(const IslandNotificationSettings&);
    std::wstring State(bool applied, bool saved) const;
    settings::IFileStore& files_;
    std::string path_;
    IslandNotificationSettings preferences_;
    std::uint64_t revision_ = 0;
    std::string loaded_ = "fresh";
    std::string error_;
};
}  // namespace lume::shell
