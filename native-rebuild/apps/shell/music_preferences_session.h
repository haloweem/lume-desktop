#pragma once

#include "core/music/music_preferences_store.h"
#include "core/ui/music_preferences_protocol.h"

namespace lume::shell {

// UI-thread owned, independent of playback and MusicHost lifetime.
class MusicPreferencesSession final {
public:
    MusicPreferencesSession(std::filesystem::path, settings::IFileStore&);
    std::wstring State() const;
    std::wstring HandlePage(const ui::MusicPreferencesCommand&);

private:
    std::wstring Message(std::uint64_t request) const;
    music::MusicPreferencesStore store_;
    music::MusicPreferencesResult result_;
};

}  // namespace lume::shell
