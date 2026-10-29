#include "core/music/music_background_store.h"

#include <utility>

namespace lume::music {
namespace {
using Json = nlohmann::json;
enum class FileState { Missing, Valid, Invalid, Unsupported, Unreadable };
struct Record {
    FileState state = FileState::Missing;
    std::optional<MusicBackgroundMedia> media;
    MusicBackgroundStorage storage = MusicBackgroundStorage::Plain;
    std::string sourceWallpaperId;
    std::uint64_t revision = 0;
    std::string bytes;
};

bool Unsupported(const Json& value) {
    return value.is_object() && value.contains("version") &&
        (!value.at("version").is_number_integer() ||
            (value.at("version") != 1 && value.at("version") != 2));
}

Record Read(settings::IFileStore& files, const std::string& path) {
    Record record;
    if (!files.Exists(path)) return record;
    if (!files.ReadBounded(path, kMusicBackgroundMaxRecordBytes, record.bytes)) {
        record.state = FileState::Unreadable;
        return record;
    }
    record.state = FileState::Invalid;
    try {
        const auto probe = Json::parse(record.bytes, nullptr, false);
        if (Unsupported(probe)) { record.state = FileState::Unsupported; return record; }
        const auto json = background_detail::Parse(record.bytes);
        const bool version2 = json.at("version") == 2;
        if (!(version2
                ? background_detail::Keys(json, {"version", "revision", "media", "storage", "sourceWallpaperId"})
                : background_detail::Keys(json, {"version", "revision", "media"})) ||
            !json.at("version").is_number_integer() ||
            !background_detail::Integer(json.at("revision"), record.revision)) return record;
        if (version2) {
            const auto storage = json.at("storage").get<std::string>();
            if (storage != "plain" && storage != "protected") return record;
            record.storage = storage == "protected" ? MusicBackgroundStorage::Protected : MusicBackgroundStorage::Plain;
            record.sourceWallpaperId = json.at("sourceWallpaperId").get<std::string>();
            if (!record.sourceWallpaperId.empty() && !ValidMusicBackgroundSourceId(record.sourceWallpaperId)) return record;
            if (record.storage == MusicBackgroundStorage::Protected && record.sourceWallpaperId.empty()) return record;
        }
        if (json.at("media").is_null()) {
            if (record.storage != MusicBackgroundStorage::Plain || !record.sourceWallpaperId.empty()) return record;
            record.media.reset(); record.state = FileState::Valid; return record;
        }
        MusicBackgroundMedia media;
        if (!DecodeMusicBackgroundMedia(json.at("media"), media)) return record;
        record.media = std::move(media); record.state = FileState::Valid;
    } catch (...) {}
    return record;
}

const Record* Select(const Record& main, const Record& backup) {
    if (main.state == FileState::Unsupported) return nullptr;
    if (main.state == FileState::Valid) return &main;
    if (backup.state == FileState::Valid) return &backup;
    if (main.state == FileState::Missing && backup.state == FileState::Missing) return &main;
    return nullptr;
}
}

MusicBackgroundStore::MusicBackgroundStore(std::filesystem::path path, settings::IFileStore& files)
    : path_(path.u8string()), files_(files) {}

MusicBackgroundResult MusicBackgroundStore::Load() {
    writable_ = false;
    if (path_.empty()) return MusicBackgroundResult::Load;
    try {
        const auto main = Read(files_, path_);
        if (main.state == FileState::Unsupported) return MusicBackgroundResult::Load;
        const auto backup = Read(files_, path_ + ".bak");
        if (backup.state == FileState::Unsupported) return MusicBackgroundResult::Load;
        const auto* record = Select(main, backup);
        if (!record) return MusicBackgroundResult::Load;
        media_ = record->media;
        storage_ = record->storage;
        sourceWallpaperId_ = record->sourceWallpaperId;
        previous_ = main.state == FileState::Valid && backup.state == FileState::Valid ? backup.media : std::nullopt;
        previousStorage_ = previous_ ? backup.storage : MusicBackgroundStorage::Plain;
        previousSourceWallpaperId_ = previous_ ? backup.sourceWallpaperId : std::string{};
        revision_ = record->revision;
        writable_ = true;
        return MusicBackgroundResult::Ready;
    } catch (...) { return MusicBackgroundResult::Load; }
}

MusicBackgroundResult MusicBackgroundStore::Save(const std::optional<MusicBackgroundMedia>& input,
                                                 std::uint64_t expectedRevision) {
    return SaveAsset(input, MusicBackgroundStorage::Plain, {}, expectedRevision);
}

MusicBackgroundResult MusicBackgroundStore::SaveAsset(const std::optional<MusicBackgroundMedia>& input,
    MusicBackgroundStorage storage, std::string sourceWallpaperId, std::uint64_t expectedRevision) {
    if ((storage != MusicBackgroundStorage::Plain && storage != MusicBackgroundStorage::Protected) ||
        (!sourceWallpaperId.empty() && !ValidMusicBackgroundSourceId(sourceWallpaperId)) ||
        (storage == MusicBackgroundStorage::Protected && sourceWallpaperId.empty()) ||
        (!input && (storage != MusicBackgroundStorage::Plain || !sourceWallpaperId.empty())))
        return MusicBackgroundResult::Invalid;
    std::optional<MusicBackgroundMedia> next;
    if (input) {
        MusicBackgroundMedia normalized;
        if (!NormalizeMusicBackgroundMedia(*input, normalized)) return MusicBackgroundResult::Invalid;
        next = std::move(normalized);
    }
    if (expectedRevision > kMusicBackgroundMaxSafeInteger) return MusicBackgroundResult::Invalid;
    if (expectedRevision != revision_) return MusicBackgroundResult::Conflict;
    if (revision_ == kMusicBackgroundMaxSafeInteger) return MusicBackgroundResult::Invalid;
    if (!writable_ || path_.empty()) return MusicBackgroundResult::Load;

    const auto temporary = path_ + ".tmp", backupTemporary = path_ + ".bak.tmp";
    const auto cleanup = [&] {
        try { files_.Remove(temporary); } catch (...) {}
        try { files_.Remove(backupTemporary); } catch (...) {}
    };
    try {
        const auto main = Read(files_, path_);
        const auto backup = Read(files_, path_ + ".bak");
        if (main.state == FileState::Unsupported || backup.state == FileState::Unsupported)
            return MusicBackgroundResult::Load;
        if (main.state == FileState::Unreadable || backup.state == FileState::Unreadable)
            return MusicBackgroundResult::Save;
        const auto* current = Select(main, backup);
        if (!current) return MusicBackgroundResult::Load;
        if (current->revision != revision_ || current->media != media_ ||
            current->storage != storage_ || current->sourceWallpaperId != sourceWallpaperId_)
            return MusicBackgroundResult::Conflict;
        const Json encoded{{"version", 2}, {"revision", revision_ + 1},
            {"media", next ? EncodeMusicBackgroundMedia(*next) : Json(nullptr)},
            {"storage", storage == MusicBackgroundStorage::Protected ? "protected" : "plain"},
            {"sourceWallpaperId", sourceWallpaperId}};
        const auto bytes = encoded.dump();
        if (bytes.size() > kMusicBackgroundMaxRecordBytes) return MusicBackgroundResult::Invalid;
        if (!files_.Write(temporary, bytes)) { cleanup(); return MusicBackgroundResult::Save; }
        if (main.state == FileState::Valid &&
            (!files_.Write(backupTemporary, main.bytes) || !files_.Rename(backupTemporary, path_ + ".bak"))) {
            cleanup(); return MusicBackgroundResult::Save;
        }
        // A failed primary replacement may already have rotated the backup.
        // Publish the retained backup identity even on that failure.
        if (main.state == FileState::Valid) {
            previous_ = main.media;
            previousStorage_ = main.storage;
            previousSourceWallpaperId_ = main.sourceWallpaperId;
        } else {
            previous_ = backup.media;
            previousStorage_ = backup.storage;
            previousSourceWallpaperId_ = backup.sourceWallpaperId;
        }
        if (!files_.Rename(temporary, path_)) { cleanup(); return MusicBackgroundResult::Save; }
        storage_ = storage;
        sourceWallpaperId_ = std::move(sourceWallpaperId);
        media_ = std::move(next); ++revision_;
        return MusicBackgroundResult::Ready;
    } catch (...) { cleanup(); return MusicBackgroundResult::Save; }
}
}  // namespace lume::music
