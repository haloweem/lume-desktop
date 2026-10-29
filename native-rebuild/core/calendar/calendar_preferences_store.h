#pragma once
#include "core/calendar/calendar_preferences_codec.h"
#include "core/settings/file_store.h"
#include <string>

namespace lume::calendar {
enum class PreferencesSource { Main, Backup, Defaults };
enum class LoadStatus { Loaded, Fresh, RecoveredBackup, Failed };
enum class LoadError { None, InvalidPath, MainRead, MainInvalid, BackupRead, BackupInvalid, Isolation };
struct PreferencesLoadResult {
    Preferences value{};
    PreferencesSource source = PreferencesSource::Defaults;
    LoadStatus status = LoadStatus::Fresh;
    LoadError error = LoadError::None;
};
// A store has one writer per path. Fixed sibling temporary paths are owned by Save.
class CalendarPreferencesStore final {
public:
    explicit CalendarPreferencesStore(settings::IFileStore& files) : files_(files) {}
    PreferencesLoadResult Load(const std::string& path) const;
    bool Save(const std::string& path, const Preferences& value);
private:
    bool Isolate(const std::string& path) const;
    settings::IFileStore& files_;
    CalendarPreferencesCodec codec_;
};
}  // namespace lume::calendar
