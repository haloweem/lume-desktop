#include "core/calendar/calendar_preferences_store.h"
namespace lume::calendar {
PreferencesLoadResult CalendarPreferencesStore::Load(const std::string& path) const {
    PreferencesLoadResult result;
    if (path.empty()) {
        result.status = LoadStatus::Failed;
        result.error = LoadError::InvalidPath;
        return result;
    }
    if (files_.Exists(path)) {
        std::string text;
        if (!files_.Read(path,text)) result.error = LoadError::MainRead;
        else if (codec_.Decode(text,result.value)) {
            result.source = PreferencesSource::Main;
            result.status = LoadStatus::Loaded;
            return result;
        } else {
            result.error = Isolate(path) ? LoadError::MainInvalid : LoadError::Isolation;
        }
    }
    const auto backup = path + ".bak";
    if (files_.Exists(backup)) {
        std::string text;
        LoadError backupError = LoadError::None;
        if (!files_.Read(backup,text)) backupError = LoadError::BackupRead;
        else if (!codec_.Decode(text,result.value)) backupError = LoadError::BackupInvalid;
        else {
            result.source = PreferencesSource::Backup;
            result.status = LoadStatus::RecoveredBackup;
            return result;
        }
        if (result.error == LoadError::None) result.error = backupError;
    }
    result.status = result.error == LoadError::None ? LoadStatus::Fresh : LoadStatus::Failed;
    return result;
}
bool CalendarPreferencesStore::Save(const std::string& path, const Preferences& value) {
    std::string encoded;
    if (path.empty() || !codec_.Encode(value,encoded)) return false;
    std::string original;
    bool validOriginal = false;
    if (files_.Exists(path)) {
        if (!files_.Read(path,original)) return false;
        Preferences previous;
        validOriginal = codec_.Decode(original,previous);
        if (!validOriginal && !Isolate(path)) return false;
    }
    const auto temporary = path + ".tmp";
    const auto backupTemporary = path + ".bak.tmp";
    bool ownsBackupTemporary = false;
    const auto fail = [&]() {
        files_.Remove(temporary);
        if (ownsBackupTemporary) files_.Remove(backupTemporary);
        return false;
    };
    if (!files_.Write(temporary,encoded)) return fail();
    if (validOriginal) {
        ownsBackupTemporary = true;
        if (!files_.Write(backupTemporary,original)) return fail();
        if (!files_.Rename(backupTemporary,path + ".bak")) return fail();
        ownsBackupTemporary = false;
    }
    // The main file stays in place until the final replacing rename succeeds.
    if (!files_.Rename(temporary,path)) return fail();
    return true;
}
bool CalendarPreferencesStore::Isolate(const std::string& path) const {
    constexpr unsigned maxAttempts = 1024;
    for (unsigned suffix = 0; suffix < maxAttempts; ++suffix) {
        const auto candidate = path + ".corrupt" + (suffix == 0 ? "" : "." + std::to_string(suffix));
        if (!files_.Exists(candidate)) return files_.Rename(path,candidate);
    }
    return false;
}
}
