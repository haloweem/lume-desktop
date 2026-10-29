#pragma once
#include "core/settings/file_store.h"
#include <cstdint>
#include <string>
#include <vector>

namespace lume::wallpaper {
struct PlaybackPreference {
    std::uint64_t display=0;
    std::string wallpaperId;
    bool paused=false;
    int volume=0;
    bool cover=true;
};
struct PlaybackPreferences {
    // Fullscreen pausing is opt-in, matching the original non-balanced default.
    bool pauseFullscreen=false;
    std::vector<PlaybackPreference> displays;
};
struct LoadedPlaybackPreferences {
    PlaybackPreferences value;
    std::string status="fresh";
    bool writable=true;
};
bool EncodePlaybackPreferences(const PlaybackPreferences&,std::string&);
bool DecodePlaybackPreferences(const std::string&,PlaybackPreferences&);
class PlaybackPreferencesStore {
public:
    explicit PlaybackPreferencesStore(settings::IFileStore& files):files_(files){}
    LoadedPlaybackPreferences Load(const std::string& path) const;
    bool Save(const std::string& path,const PlaybackPreferences&);
private:
    settings::IFileStore& files_;
};
}
