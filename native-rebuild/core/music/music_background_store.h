#pragma once

#include "core/music/music_background.h"
#include "core/settings/file_store.h"

#include <filesystem>

namespace lume::music {

class MusicBackgroundStore final {
public:
    MusicBackgroundStore(std::filesystem::path, settings::IFileStore&);
    MusicBackgroundResult Load();
    MusicBackgroundResult Save(const std::optional<MusicBackgroundMedia>&, std::uint64_t expectedRevision);
    MusicBackgroundResult SaveAsset(const std::optional<MusicBackgroundMedia>&,
        MusicBackgroundStorage, std::string sourceWallpaperId, std::uint64_t expectedRevision);
    const std::optional<MusicBackgroundMedia>& media() const { return media_; }
    const std::optional<MusicBackgroundMedia>& previous() const { return previous_; }
    MusicBackgroundStorage storage() const { return storage_; }
    MusicBackgroundStorage previousStorage() const { return previousStorage_; }
    const std::string& sourceWallpaperId() const { return sourceWallpaperId_; }
    const std::string& previousSourceWallpaperId() const { return previousSourceWallpaperId_; }
    std::uint64_t revision() const { return revision_; }

private:
    std::string path_;
    settings::IFileStore& files_;
    std::optional<MusicBackgroundMedia> media_;
    std::optional<MusicBackgroundMedia> previous_;
    MusicBackgroundStorage storage_ = MusicBackgroundStorage::Plain;
    MusicBackgroundStorage previousStorage_ = MusicBackgroundStorage::Plain;
    std::string sourceWallpaperId_;
    std::string previousSourceWallpaperId_;
    std::uint64_t revision_ = 0;
    bool writable_ = false;
};

}  // namespace lume::music
