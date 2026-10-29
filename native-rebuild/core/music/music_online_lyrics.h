#pragma once

#include "core/music/music_lyrics.h"
#include "core/music/music_provider_account.h"

namespace lume::music {

enum class LyricFormat { Lrc, Yrc, Qrc, Krc };

// Text is decoded UTF-8. No inferred word timing. duration bounds plain LRC only;
// provider-supplied line/word intervals are preserved, with the one-day ceiling.
LocalLyricsResult ParseTimedLyrics(LyricFormat format, std::string_view text,
    std::string_view translation = {}, double duration = 0) noexcept;

// Pure data adapter: accepts the historical provider response, never performs IO.
// crypt=1 QRC and krc1 compressed bytes require an upstream decoder; errors are
// "unsupported-qrc-encryption"/"unsupported-krc-compression", never fake LRC.
LocalLyricsResult ParseOnlineLyrics(auth::Provider provider, const auth::Json& response,
    double duration = 0) noexcept;
LocalLyricsResult ParseOnlineLyrics(std::string_view provider, const auth::Json& response,
    double duration = 0) noexcept;

}  // namespace lume::music
