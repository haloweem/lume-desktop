#include "core/music/melody_analysis.h"
#include "platform/windows/music_score_source.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>

// Manual probe: decode a real source and analyze it.
// lume-music-score-probe <path-or-url>           summary and first beats
// lume-music-score-probe <path-or-url> --track   plus the pitch track every 100 ms
// lume-music-score-probe <path-or-url> --json    a music-score page message, for UI harnesses
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string_view mode = argc > 2 ? argv[2] : "";
    const auto start = std::chrono::steady_clock::now();
    const auto audio = lume::music::DecodeScoreAudio(argv[1], {});
    const auto decoded = std::chrono::steady_clock::now();
    if (audio.mono.empty()) { std::fputs("decode failed\n", stderr); return 1; }
    const auto result = lume::music::AnalyzeMelody(audio.mono, audio.rate);
    const auto done = std::chrono::steady_clock::now();
    if (mode == "--json") {
        std::printf("{\"version\":1,\"type\":\"music-score\",\"session\":1,\"trackId\":\"probe\",\"hop\":%g,\"tempo\":%.2f,\"pitch\":[",
            result.hop, result.tempo);
        for (std::size_t t = 0; t < result.pitch.size(); ++t) std::printf("%s%u", t ? "," : "", result.pitch[t]);
        std::printf("],\"beats\":[");
        for (std::size_t i = 0; i < result.beats.size(); ++i) std::printf("%s%.3f", i ? "," : "", result.beats[i]);
        std::printf("],\"start\":%.3f,\"end\":%.3f}\n", result.start, result.end);
        return 0;
    }
    std::size_t voiced = 0;
    for (auto value : result.pitch) voiced += value != 0;
    std::printf("rate %.0f, %.1f s, decode %.2f s, analyze %.2f s, voiced %.0f%%, tempo %.1f, beats %zu\n",
        audio.rate, audio.mono.size() / audio.rate,
        std::chrono::duration<double>(decoded - start).count(), std::chrono::duration<double>(done - decoded).count(),
        100.0 * voiced / std::max<std::size_t>(1, result.pitch.size()), result.tempo, result.beats.size());
    if (mode == "--track") for (std::size_t t = 0; t < result.pitch.size(); t += 5)
        std::printf("%.2f %.1f\n", t * result.hop, result.pitch[t] / 10.0);
    for (std::size_t i = 0; i < result.beats.size() && i < 12; ++i) std::printf("beat %.3f\n", result.beats[i]);
    return 0;
}
