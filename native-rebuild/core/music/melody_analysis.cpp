#include "core/music/melody_analysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>

namespace lume::music {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kLowMidi = 38, kHighMidi = 90, kBinStep = 0.2;   // D2..F#6 in 20-cent bins
constexpr int kBins = static_cast<int>((kHighMidi - kLowMidi) / kBinStep) + 1;
constexpr int kCandidates = 4;
constexpr double kPitchHop = 0.02, kBeatHop = 0.01;

using Complex = std::complex<double>;
void Fft(std::vector<Complex>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const Complex step = std::polar(1.0, -2 * kPi / static_cast<double>(length));
        for (std::size_t i = 0; i < n; i += length) {
            Complex w = 1;
            for (std::size_t k = 0; k < length / 2; ++k) {
                const Complex u = a[i + k], v = a[i + k + length / 2] * w;
                a[i + k] = u + v;
                a[i + k + length / 2] = u - v;
                w *= step;
            }
        }
    }
}
// Magnitudes of the frame centred on `center`, Hann-windowed and zero-padded.
struct Spectrum {
    explicit Spectrum(std::size_t size) : size(size), window(size), data(size), magnitude(size / 2) {
        for (std::size_t i = 0; i < size; ++i) window[i] = 0.5 - 0.5 * std::cos(2 * kPi * i / (size - 1));
    }
    double Compute(const std::vector<float>& mono, std::ptrdiff_t center) {
        double squares = 0;
        const auto start = center - static_cast<std::ptrdiff_t>(size / 2);
        for (std::size_t i = 0; i < size; ++i) {
            const auto index = start + static_cast<std::ptrdiff_t>(i);
            const double sample = index >= 0 && index < static_cast<std::ptrdiff_t>(mono.size()) ? mono[index] : 0.0;
            squares += sample * sample;
            data[i] = sample * window[i];
        }
        Fft(data);
        for (std::size_t k = 0; k < size / 2; ++k) magnitude[k] = std::abs(data[k]);
        return std::sqrt(squares / size);
    }
    std::size_t size;
    std::vector<double> window;
    std::vector<Complex> data;
    std::vector<double> magnitude;
};
double Percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0;
    const auto index = static_cast<std::size_t>(std::clamp(fraction, 0.0, 1.0) * (values.size() - 1));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

struct Candidate { double midi = 0, salience = 0; };
struct PitchFrame { std::array<Candidate, kCandidates> candidates{}; int count = 0; double top = 0, rms = 0; };

// Harmonic summation over spectral peaks: every peak votes for the pitches of
// which it could be the 1st..10th harmonic, weighted 0.8^(h-1) and by
// distance in a ±1 semitone cos² kernel. A broad prior around F4 favours the
// singing voice over bass and cymbals.
PitchFrame Salience(const std::vector<double>& magnitude, double rate, std::size_t size) {
    PitchFrame frame;
    struct Peak { double frequency, amplitude; };
    std::vector<Peak> peaks;
    double loudest = 0;
    for (double value : magnitude) loudest = std::max(loudest, value);
    if (loudest <= 0) return frame;
    const double floor = loudest * 0.01;   // -40 dB
    for (std::size_t k = 2; k + 2 < magnitude.size(); ++k) {
        const double b = magnitude[k];
        if (b < floor || b <= magnitude[k - 1] || b < magnitude[k + 1]) continue;
        const double a = std::log(magnitude[k - 1] + 1e-12), c = std::log(magnitude[k + 1] + 1e-12), l = std::log(b);
        const double denominator = a - 2 * l + c;
        const double offset = denominator != 0 ? std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5) : 0;
        const double frequency = (k + offset) * rate / static_cast<double>(size);
        if (frequency < 70 || frequency > 5000) continue;
        peaks.push_back({frequency, std::exp(l - 0.25 * (a - c) * offset)});
    }
    if (peaks.size() > 30) {
        std::partial_sort(peaks.begin(), peaks.begin() + 30, peaks.end(),
            [](const Peak& x, const Peak& y) { return x.amplitude > y.amplitude; });
        peaks.resize(30);
    }
    std::array<double, kBins> salience{};
    for (const auto& peak : peaks) {
        double weight = 1;
        for (int harmonic = 1; harmonic <= 10; ++harmonic, weight *= 0.8) {
            const double midi = 69 + 12 * std::log2(peak.frequency / harmonic / 440.0);
            if (midi < kLowMidi - 1) break;
            if (midi > kHighMidi + 1) continue;
            const double center = (midi - kLowMidi) / kBinStep;
            const int first = std::max(0, static_cast<int>(std::ceil(center - 5)));
            const int last = std::min(kBins - 1, static_cast<int>(std::floor(center + 5)));
            for (int bin = first; bin <= last; ++bin) {
                const double distance = std::abs(bin - center) / 5;
                const double kernel = std::cos(distance * kPi / 2);
                salience[bin] += kernel * kernel * weight * peak.amplitude;
            }
        }
    }
    for (int bin = 0; bin < kBins; ++bin) {
        const double midi = kLowMidi + bin * kBinStep;
        salience[bin] *= std::exp(-0.5 * std::pow((midi - 65) / 13, 2));
    }
    for (int bin = 1; bin + 1 < kBins; ++bin) {
        const double value = salience[bin];
        if (value <= 0 || value < salience[bin - 1] || value < salience[bin + 1]) continue;
        const double a = salience[bin - 1], c = salience[bin + 1];
        const double denominator = a - 2 * value + c;
        const double offset = denominator != 0 ? std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5) : 0;
        Candidate candidate{kLowMidi + (bin + offset) * kBinStep, value};
        // Keep the strongest few, sorted descending.
        int slot = frame.count;
        if (slot == kCandidates) {
            if (candidate.salience <= frame.candidates[kCandidates - 1].salience) continue;
            slot = kCandidates - 1;
        } else ++frame.count;
        while (slot > 0 && frame.candidates[slot - 1].salience < candidate.salience) {
            frame.candidates[slot] = frame.candidates[slot - 1];
            --slot;
        }
        frame.candidates[slot] = candidate;
    }
    frame.top = frame.count ? frame.candidates[0].salience : 0;
    return frame;
}

// Viterbi over each frame's candidates plus an unvoiced state: strong
// salience pays, pitch jumps and voicing flips cost, so octave errors and
// one-frame blips lose to a continuous line.
std::vector<std::uint16_t> TrackPitch(const std::vector<PitchFrame>& frames) {
    std::vector<double> tops, levels;
    for (const auto& frame : frames) { levels.push_back(frame.rms); if (frame.top > 0) tops.push_back(frame.top); }
    const double norm = Percentile(tops, 0.8);
    const double silence = Percentile(levels, 0.9) * 0.03;
    std::vector<std::uint16_t> pitch(frames.size(), 0);
    if (frames.empty() || norm <= 0) return pitch;
    constexpr int kStates = kCandidates + 1;   // last state = unvoiced
    constexpr double kUnvoiced = 0.45, kFlip = 0.2, kJump = 0.04;
    std::vector<std::array<double, kStates>> score(frames.size());
    std::vector<std::array<int, kStates>> back(frames.size());
    const auto emission = [&](std::size_t t, int state) {
        const auto& frame = frames[t];
        if (state == kCandidates) return kUnvoiced;
        if (state >= frame.count || frame.rms < silence) return -1e9;
        return frame.candidates[state].salience / norm;
    };
    for (int state = 0; state < kStates; ++state) score[0][state] = emission(0, state);
    for (std::size_t t = 1; t < frames.size(); ++t) {
        for (int state = 0; state < kStates; ++state) {
            double best = -1e18; int from = kCandidates;
            for (int previous = 0; previous < kStates; ++previous) {
                double cost = 0;
                const bool voicedNow = state < kCandidates, voicedBefore = previous < kCandidates;
                if (voicedNow != voicedBefore) cost = kFlip;
                else if (voicedNow) cost = kJump * std::min(24.0, std::abs(
                    frames[t].candidates[state].midi - frames[t - 1].candidates[previous].midi));
                const double value = score[t - 1][previous] - cost;
                if (value > best) { best = value; from = previous; }
            }
            score[t][state] = best + emission(t, state);
            back[t][state] = from;
        }
    }
    int state = static_cast<int>(std::max_element(score.back().begin(), score.back().end()) - score.back().begin());
    for (std::size_t t = frames.size(); t-- > 0;) {
        if (state < kCandidates && state < frames[t].count)
            pitch[t] = static_cast<std::uint16_t>(std::lround(frames[t].candidates[state].midi * 10));
        if (t) state = back[t][state];
    }
    // Drop voiced runs shorter than 60 ms and smooth the rest with a 5-frame median.
    for (std::size_t t = 0; t < pitch.size();) {
        if (!pitch[t]) { ++t; continue; }
        std::size_t end = t;
        while (end < pitch.size() && pitch[end]) ++end;
        if (end - t < 3) std::fill(pitch.begin() + t, pitch.begin() + end, std::uint16_t{0});
        else {
            std::vector<std::uint16_t> run(pitch.begin() + t, pitch.begin() + end);
            for (std::size_t i = 0; i < run.size(); ++i) {
                std::array<std::uint16_t, 5> window{};
                std::size_t count = 0;
                for (std::size_t j = i >= 2 ? i - 2 : 0; j <= std::min(run.size() - 1, i + 2); ++j) window[count++] = run[j];
                std::nth_element(window.begin(), window.begin() + count / 2, window.begin() + count);
                pitch[t + i] = window[count / 2];
            }
        }
        t = end;
    }
    return pitch;
}

// Ellis (2007): tempo from the autocorrelation of the onset envelope with a
// log-Gaussian preference around 120 BPM, then a DP that trades onset
// strength against deviation from that period.
void TrackBeats(std::vector<double> onset, MelodyAnalysis& result) {
    const double rate = 1 / kBeatHop;
    if (onset.size() < static_cast<std::size_t>(rate * 6)) return;
    const std::size_t average = static_cast<std::size_t>(rate * 0.4);
    std::vector<double> prefix(onset.size() + 1, 0);
    for (std::size_t i = 0; i < onset.size(); ++i) prefix[i + 1] = prefix[i] + onset[i];
    std::vector<double> envelope(onset.size());
    for (std::size_t i = 0; i < onset.size(); ++i) {
        const auto lo = i > average ? i - average : 0, hi = std::min(onset.size(), i + average + 1);
        envelope[i] = std::max(0.0, onset[i] - (prefix[hi] - prefix[lo]) / (hi - lo));
    }
    const double mean = std::accumulate(envelope.begin(), envelope.end(), 0.0) / envelope.size();
    double variance = 0;
    for (double value : envelope) variance += (value - mean) * (value - mean);
    const double deviation = std::sqrt(variance / envelope.size());
    if (deviation <= 0) return;
    for (double& value : envelope) value /= deviation;
    const int shortest = static_cast<int>(60 * rate / 200), longest = static_cast<int>(60 * rate / 50);
    std::vector<double> correlation(longest * 2 + 2, 0);
    for (int lag = shortest; lag <= longest * 2 + 1 && lag < static_cast<int>(envelope.size()); ++lag) {
        double sum = 0;
        for (std::size_t t = lag; t < envelope.size(); ++t) sum += envelope[t] * envelope[t - lag];
        correlation[lag] = sum / (envelope.size() - lag);
    }
    std::vector<double> weighted(longest + 2, 0);
    int bestLag = 0;
    for (int lag = shortest; lag <= longest; ++lag) {
        const double bpm = 60 * rate / lag;
        weighted[lag] = std::exp(-0.5 * std::pow(std::log2(bpm / 120), 2)) *
            (correlation[lag] + 0.5 * correlation[std::min<int>(lag * 2, static_cast<int>(correlation.size()) - 1)]);
        if (!bestLag || weighted[lag] > weighted[bestLag]) bestLag = lag;
    }
    if (bestLag <= shortest || bestLag >= longest || weighted[bestLag] <= 0) return;
    const double a = weighted[bestLag - 1], b = weighted[bestLag], c = weighted[bestLag + 1];
    const double denominator = a - 2 * b + c;
    const double period = bestLag + (denominator != 0 ? std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5) : 0);
    result.tempo = 60 * rate / period;
    constexpr double kTightness = 100;
    std::vector<double> cumulative(envelope.size());
    std::vector<int> link(envelope.size(), -1);
    for (std::size_t t = 0; t < envelope.size(); ++t) {
        double best = 0; int from = -1;
        const auto lo = static_cast<std::ptrdiff_t>(t) - static_cast<std::ptrdiff_t>(std::lround(2 * period));
        const auto hi = static_cast<std::ptrdiff_t>(t) - static_cast<std::ptrdiff_t>(std::lround(period / 2));
        for (auto previous = std::max<std::ptrdiff_t>(0, lo); previous <= hi; ++previous) {
            const double gap = static_cast<double>(t - previous) / period;
            const double value = cumulative[previous] - kTightness * std::pow(std::log(gap), 2);
            if (from < 0 || value > best) { best = value; from = static_cast<int>(previous); }
        }
        cumulative[t] = envelope[t] + (from >= 0 ? best : 0);
        link[t] = from;
    }
    const auto tailStart = envelope.size() - std::min(envelope.size(), static_cast<std::size_t>(period));
    auto beat = static_cast<int>(std::max_element(cumulative.begin() + tailStart, cumulative.end()) - cumulative.begin());
    std::vector<double> beats;
    for (; beat >= 0; beat = link[beat]) beats.push_back(beat * kBeatHop);
    std::reverse(beats.begin(), beats.end());
    result.beats = std::move(beats);
}
}  // namespace

MelodyAnalysis AnalyzeMelody(const std::vector<float>& mono, double rate, const std::function<bool()>& cancelled) {
    MelodyAnalysis result;
    result.hop = kPitchHop;
    if (!(rate >= 4000 && rate <= 48000) || mono.size() < static_cast<std::size_t>(rate) ||
        mono.size() > static_cast<std::size_t>(rate * 1800)) return result;
    const auto stop = [&] { return cancelled && cancelled(); };
    std::size_t pitchSize = 1;
    while (pitchSize < rate * 0.12) pitchSize <<= 1;
    Spectrum pitchSpectrum(pitchSize);
    const auto pitchFrames = static_cast<std::size_t>(mono.size() / (rate * kPitchHop)) + 1;
    std::vector<PitchFrame> frames(pitchFrames);
    for (std::size_t t = 0; t < pitchFrames; ++t) {
        if (t % 256 == 0 && stop()) return {};
        const double rms = pitchSpectrum.Compute(mono, static_cast<std::ptrdiff_t>(std::lround(t * rate * kPitchHop)));
        frames[t] = Salience(pitchSpectrum.magnitude, rate, pitchSize);
        frames[t].rms = rms;
    }
    if (stop()) return {};
    result.pitch = TrackPitch(frames);
    // Audible span: first and last frame above the silence floor the pitch
    // tracker uses (30 dB under the loud passages).
    std::vector<double> levels;
    for (const auto& frame : frames) levels.push_back(frame.rms);
    const double quiet = Percentile(levels, 0.9) * 0.03;
    const auto audible = [&](const PitchFrame& frame) { return frame.rms > quiet; };
    const auto first = std::find_if(frames.begin(), frames.end(), audible);
    if (first != frames.end()) {
        const auto last = std::find_if(frames.rbegin(), frames.rend(), audible);
        result.start = (first - frames.begin()) * kPitchHop;
        result.end = (frames.rend() - last) * kPitchHop;
    }
    // Onset strength: log-magnitude spectral flux on short frames.
    std::size_t beatSize = 1;
    while (beatSize < rate * 0.046) beatSize <<= 1;
    Spectrum beatSpectrum(beatSize);
    const auto beatFrames = static_cast<std::size_t>(mono.size() / (rate * kBeatHop)) + 1;
    std::vector<double> onset(beatFrames, 0), previous(beatSize / 2, 0);
    for (std::size_t t = 0; t < beatFrames; ++t) {
        if (t % 1024 == 0 && stop()) return {};
        beatSpectrum.Compute(mono, static_cast<std::ptrdiff_t>(std::lround(t * rate * kBeatHop)));
        double flux = 0;
        for (std::size_t k = 1; k < beatSize / 2; ++k) {
            const double value = std::log1p(1000 * beatSpectrum.magnitude[k]);
            if (t) flux += std::max(0.0, value - previous[k]);
            previous[k] = value;
        }
        onset[t] = flux;
    }
    TrackBeats(std::move(onset), result);
    return stop() ? MelodyAnalysis{} : result;
}

}  // namespace lume::music
