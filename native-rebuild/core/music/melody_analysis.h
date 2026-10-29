#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace lume::music {

// Lead-melody pitch track and beat grid of a whole song, for the 音符律动
// lyric style. Pitch uses harmonic-summation salience (Salamon & Gómez style)
// with a continuity Viterbi; beats use an onset envelope, autocorrelation
// tempo and Ellis' dynamic-programming tracker. Pure computation, no I/O.
struct MelodyAnalysis {
    double hop = 0.02;                   // seconds per pitch frame
    std::vector<std::uint16_t> pitch;    // MIDI × 10 per frame, 0 = unvoiced
    double tempo = 0;                    // beats per minute, 0 if unknown
    std::vector<double> beats;           // beat times in seconds
    double start = 0, end = 0;           // seconds the song is audible from and to; equal if silent
};

// `mono` is one channel at `rate` Hz (8000–16000 is plenty; callers should
// decimate). Returns an empty analysis when cancelled or the input is unusable.
MelodyAnalysis AnalyzeMelody(const std::vector<float>& mono, double rate,
                             const std::function<bool()>& cancelled = {});

}  // namespace lume::music
