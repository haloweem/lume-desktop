#pragma once

#include "core/music/music_lyrics.h"
#include "core/music/music_types.h"

#include <functional>

namespace lume::music {

// The caller must authorize this native track against the current import set.
// No page-supplied path is accepted by the session that calls this reader.
LocalLyricsResult LoadLocalLyrics(const Track& track,
    const std::function<bool()>& cancelled = {}) noexcept;

}  // namespace lume::music
