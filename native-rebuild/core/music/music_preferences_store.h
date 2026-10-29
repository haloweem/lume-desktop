#pragma once

#include "core/music/music_preferences.h"
#include "core/settings/file_store.h"

#include <filesystem>

namespace lume::music {

// Single-owner store; callers serialize operations. Paths are supplied by composition.
// On disk: {version:1, revision:<safe integer>, preferences:<canonical v2 DTO>}.
class MusicPreferencesStore final {
public:
    MusicPreferencesStore(std::filesystem::path, settings::IFileStore&);
    MusicPreferencesResult Load();
    MusicPreferencesResult Save(const MusicPreferences&, std::uint64_t expectedRevision);
    MusicPreferencesResult Reset(std::uint64_t expectedRevision);
    const MusicPreferences& preferences() const { return preferences_; }
    std::uint64_t revision() const { return revision_; }

private:
    std::string path_;
    settings::IFileStore& files_;
    MusicPreferences preferences_;
    std::uint64_t revision_ = 0;
    bool writable_ = false;
};

}  // namespace lume::music
