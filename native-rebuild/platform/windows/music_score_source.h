#pragma once

#include <functional>
#include <string>
#include <vector>

namespace lume::music {

struct ScoreAudio {
    std::vector<float> mono;
    double rate = 0;
};

// Decodes a whole playback source (local path or HTTP(S) URL, the same string
// the audio host plays) to mono near 11 kHz for melody analysis. Runs apart
// from playback; empty on failure, cancellation or sources over 15 minutes.
ScoreAudio DecodeScoreAudio(const std::string& source, const std::function<bool()>& cancelled);

}  // namespace lume::music
