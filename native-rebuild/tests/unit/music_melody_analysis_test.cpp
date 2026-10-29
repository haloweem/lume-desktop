#include "core/music/melody_analysis.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

using lume::music::AnalyzeMelody;
namespace {
bool passed = true;
void Check(bool value, const char* message) {
    if (!value) { passed = false; std::cerr << "FAIL: " << message << '\n'; }
}
constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 11025, kBeat = 0.5;   // 120 BPM
const int kMelody[] = {60, 62, 64, 65, 67, 65, 64, 62, 60, 64, 67, 72, 71, 69, 67, 65,
                       64, 62, 60, 59, 60, 64, 67, 64, 62, 60, 57, 59, 60, 62, 64, 60};

// A sung-like line (8 harmonics, light vibrato) over a bass drone and a kick
// on every beat, with a little noise: the shape of a pop mix, minus words.
std::vector<float> Song(double seconds) {
    std::vector<float> mono(static_cast<std::size_t>(seconds * kRate));
    std::mt19937 random(7);
    std::normal_distribution<double> noise(0, 0.004);
    double phase = 0, bassPhase = 0;
    for (std::size_t i = 0; i < mono.size(); ++i) {
        const double t = i / kRate;
        const auto beat = static_cast<std::size_t>(t / kBeat);
        const double local = t - beat * kBeat;
        const int midi = kMelody[beat % std::size(kMelody)];
        const double frequency = 440 * std::pow(2, (midi - 69) / 12.0) * (1 + 0.004 * std::sin(2 * kPi * 5.5 * t));
        phase += 2 * kPi * frequency / kRate;
        const double envelope = std::min(1.0, local / 0.03) * std::min(1.0, (kBeat - local) / 0.04);
        double voice = 0;
        for (int h = 1; h <= 8; ++h) voice += std::sin(phase * h) * std::pow(0.7, h - 1);
        bassPhase += 2 * kPi * 87.31 / kRate;
        const double bass = 0.12 * (std::sin(bassPhase) + 0.5 * std::sin(2 * bassPhase));
        const double kick = local < 0.08 ? 0.6 * std::exp(-local * 40) * std::sin(2 * kPi * 60 * local) : 0;
        const double click = local < 0.01 ? noise(random) * 25 : 0;
        mono[i] = static_cast<float>(0.18 * voice * envelope + bass + kick + click + noise(random));
    }
    return mono;
}
}  // namespace

int main() {
    const auto song = Song(24);
    const auto result = AnalyzeMelody(song, kRate);
    Check(std::abs(result.hop - 0.02) < 1e-9 && result.pitch.size() >= 1190, "one pitch frame per 20 ms");
    // Melody: the median voiced pitch inside each note must be that note.
    int correct = 0, notes = 0;
    for (std::size_t beat = 0; beat < 46; ++beat) {
        std::vector<int> voiced;
        const auto first = static_cast<std::size_t>((beat * kBeat + 0.12) / result.hop);
        const auto last = static_cast<std::size_t>((beat * kBeat + 0.42) / result.hop);
        for (auto t = first; t <= last && t < result.pitch.size(); ++t) if (result.pitch[t]) voiced.push_back(result.pitch[t]);
        ++notes;
        if (voiced.size() < 5) continue;
        std::nth_element(voiced.begin(), voiced.begin() + voiced.size() / 2, voiced.end());
        const double midi = voiced[voiced.size() / 2] / 10.0;
        if (std::abs(midi - kMelody[beat % std::size(kMelody)]) <= 0.5) ++correct;
    }
    std::cout << "melody notes correct: " << correct << "/" << notes << ", tempo " << result.tempo
              << ", beats " << result.beats.size() << '\n';
    Check(correct >= notes * 9 / 10, "at least 90% of melody notes have the right pitch");
    Check(std::abs(result.tempo - 120) < 3, "tempo is 120 BPM");
    // Beats: close to the true grid.
    std::vector<double> errors;
    for (double beat : result.beats) {
        const double nearest = std::round(beat / kBeat) * kBeat;
        errors.push_back(std::abs(beat - nearest));
    }
    std::sort(errors.begin(), errors.end());
    Check(result.beats.size() >= 44 && result.beats.size() <= 50, "about one beat per half second");
    Check(!errors.empty() && errors[errors.size() / 2] < 0.04, "beats land on the kicks");
    // The audible span skips silence before and after the song.
    std::vector<float> padded(static_cast<std::size_t>(kRate * 3), 0.0f);
    padded.insert(padded.end(), song.begin(), song.end());
    padded.resize(padded.size() + static_cast<std::size_t>(kRate * 2), 0.0f);
    const auto span = AnalyzeMelody(padded, kRate);
    Check(std::abs(span.start - 3) < 0.15 && std::abs(span.end - 27) < 0.15, "audible span is where the song plays");
    // Silence is unvoiced and beatless; cancellation and bad input give nothing.
    const auto silent = AnalyzeMelody(std::vector<float>(static_cast<std::size_t>(kRate * 8), 0.0f), kRate);
    Check(std::all_of(silent.pitch.begin(), silent.pitch.end(), [](std::uint16_t value) { return value == 0; }) &&
        silent.beats.empty() && silent.start == silent.end, "silence has no melody, no beats and no audible span");
    Check(AnalyzeMelody(song, kRate, [] { return true; }).pitch.empty(), "cancelled analysis is empty");
    Check(AnalyzeMelody(std::vector<float>(100, 0.1f), kRate).pitch.empty() &&
        AnalyzeMelody(song, 100).pitch.empty(), "unusable input is rejected");
    if (passed) std::cout << "melody analysis passed\n";
    return passed ? 0 : 1;
}
