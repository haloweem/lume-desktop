#include "core/music/music_preferences_store.h"

#include <utility>

namespace lume::music {
namespace {
using Json = nlohmann::json;
enum class FileState { Missing, Valid, Invalid, Unsupported, Unreadable };
struct Record {
    FileState state = FileState::Missing;
    MusicPreferences preferences;
    std::uint64_t revision = 0;
    std::string bytes;
};

bool UnsupportedEnvelopeVersion(const Json& value) {
    return value.is_object() && value.contains("version") &&
        (!value.at("version").is_number_integer() || value.at("version") != 1);
}

bool UnsupportedPreferenceVersion(const Json& value) {
    return value.is_object() && value.contains("version") &&
        (!value.at("version").is_number_integer() ||
            (value.at("version") != 1 && value.at("version") != 2 && value.at("version") != 3));
}

Record Read(settings::IFileStore& files, const std::string& path) {
    Record record;
    if (!files.Exists(path)) return record;
    if (!files.ReadBounded(path, kMusicPreferencesMaxBytes, record.bytes)) {
        record.state = FileState::Unreadable;
        return record;
    }
    record.state = FileState::Invalid;
    try {
        // A bounded future schema may have a deeper shape than our strict parser allows.
        const auto versionProbe = Json::parse(record.bytes, nullptr, false);
        if (UnsupportedEnvelopeVersion(versionProbe) || (versionProbe.is_object() && versionProbe.contains("preferences") &&
                UnsupportedPreferenceVersion(versionProbe.at("preferences")))) {
            record.state = FileState::Unsupported;
            return record;
        }
        const auto json = detail::ParseMusicPreferencesJson(std::string_view(record.bytes));
        if (detail::MusicPreferencesKeys(json, {"version", "revision", "preferences"}) &&
            json.at("version").is_number_integer() && json.at("version") == 1 &&
            detail::MusicPreferencesInteger(json.at("revision"), record.revision) &&
            DecodeMusicPreferences(json.at("preferences"), record.preferences)) record.state = FileState::Valid;
    } catch (...) { /* A malformed main may be recovered from the independent backup. */ }
    return record;
}

// Read-only recovery: the sole good backup stays untouched until a successful user commit.
const Record* Select(const Record& main, const Record& backup) {
    if (main.state == FileState::Unsupported) return nullptr;
    if (main.state == FileState::Valid) return &main;
    if (backup.state == FileState::Valid) return &backup;
    if (main.state == FileState::Missing && backup.state == FileState::Missing) return &main;
    return nullptr;
}
}

MusicPreferencesStore::MusicPreferencesStore(std::filesystem::path path, settings::IFileStore& files)
    : path_(path.u8string()), files_(files) {}

MusicPreferencesResult MusicPreferencesStore::Load() {
    writable_ = false;
    if (path_.empty()) return MusicPreferencesResult::Load;
    try {
        const auto main = Read(files_, path_);
        if (main.state == FileState::Unsupported) return MusicPreferencesResult::Load;
        const auto backup = main.state == FileState::Valid ? Record{} : Read(files_, path_ + ".bak");
        const auto* record = Select(main, backup);
        if (!record) return MusicPreferencesResult::Load;
        preferences_ = record->preferences;
        revision_ = record->revision;
        writable_ = true;
        return MusicPreferencesResult::Ready;
    } catch (...) { return MusicPreferencesResult::Load; }
}

MusicPreferencesResult MusicPreferencesStore::Save(const MusicPreferences& input, std::uint64_t expectedRevision) {
    MusicPreferences next;
    if (!NormalizeMusicPreferences(input, next) || expectedRevision > kMusicPreferencesMaxSafeInteger)
        return MusicPreferencesResult::Invalid;
    if (expectedRevision != revision_) return MusicPreferencesResult::Conflict;
    if (revision_ == kMusicPreferencesMaxSafeInteger) return MusicPreferencesResult::Invalid;
    if (!writable_ || path_.empty()) return MusicPreferencesResult::Load;

    const auto temporary = path_ + ".tmp", backupTemporary = path_ + ".bak.tmp";
    const auto cleanup = [&] {
        try { files_.Remove(temporary); } catch (...) {}
        try { files_.Remove(backupTemporary); } catch (...) {}
    };
    try {
        // Recheck disk immediately before writing so an unsupported schema installed after Load stays intact.
        const auto main = Read(files_, path_);
        const auto backup = Read(files_, path_ + ".bak");
        if (main.state == FileState::Unsupported || backup.state == FileState::Unsupported)
            return MusicPreferencesResult::Load;
        if (main.state == FileState::Unreadable || backup.state == FileState::Unreadable)
            return MusicPreferencesResult::Save;
        const auto* current = Select(main, backup);
        if (!current) return MusicPreferencesResult::Load;
        if (current->revision != revision_ || !(current->preferences == preferences_))
            return MusicPreferencesResult::Conflict;
        const auto encoded = Json{{"version", 1}, {"revision", revision_ + 1},
            {"preferences", EncodeMusicPreferences(next)}}.dump();
        if (encoded.size() > kMusicPreferencesMaxBytes) return MusicPreferencesResult::Invalid;
        if (!files_.Write(temporary, encoded)) { cleanup(); return MusicPreferencesResult::Save; }
        if (main.state == FileState::Valid &&
            (!files_.Write(backupTemporary, main.bytes) || !files_.Rename(backupTemporary, path_ + ".bak"))) {
            cleanup();
            return MusicPreferencesResult::Save;
        }
        if (!files_.Rename(temporary, path_)) { cleanup(); return MusicPreferencesResult::Save; }
        // No fallible file operation follows the commit point.
        preferences_ = std::move(next);
        ++revision_;
        return MusicPreferencesResult::Ready;
    } catch (...) {
        cleanup();
        return MusicPreferencesResult::Save;
    }
}

MusicPreferencesResult MusicPreferencesStore::Reset(std::uint64_t expectedRevision) {
    return Save(MusicPreferences{}, expectedRevision);
}
}  // namespace lume::music
