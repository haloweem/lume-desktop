#include "platform/windows/music_score_source.h"
#include "platform/windows/music_http_flac_decoder.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace lume::music {
namespace {
using Microsoft::WRL::ComPtr;
constexpr DWORD kAudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
constexpr double kTargetRate = 11025, kMaxSeconds = 900, kPi = 3.14159265358979323846;

std::wstring Wide(const std::string& source) {
    if (source.empty() || source.size() > 4096 || source.find('\0') != std::string::npos ||
        source.find_first_of("\"\r\n") != std::string::npos) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
        static_cast<int>(source.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()), wide.data(), size);
    return wide;
}
struct Platform {
    Platform() {
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com = SUCCEEDED(hr);
        ok = (com || hr == RPC_E_CHANGED_MODE) && SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }
    ~Platform() { if (ok) MFShutdown(); if (com) CoUninitialize(); }
    bool com = false, ok = false;
};
// Windowed-sinc low-pass and integer decimation to about 11 kHz.
class Decimator {
public:
    explicit Decimator(double rate) : factor_(std::max(1, static_cast<int>(std::lround(rate / kTargetRate)))) {
        if (factor_ == 1) { kernel_ = {1.0}; history_ = {0.0}; return; }
        const int taps = factor_ * 8 + 1;
        const double cutoff = 0.45 / factor_;
        double sum = 0;
        for (int i = 0; i < taps; ++i) {
            const double x = i - (taps - 1) / 2.0;
            const double sinc = x == 0 ? 2 * cutoff : std::sin(2 * kPi * cutoff * x) / (kPi * x);
            const double window = 0.54 - 0.46 * std::cos(2 * kPi * i / static_cast<double>(taps - 1));
            kernel_.push_back(sinc * window);
            sum += kernel_.back();
        }
        for (double& value : kernel_) value /= sum;
        history_.assign(kernel_.size(), 0);
    }
    double Rate(double rate) const { return rate / factor_; }
    void Push(float sample, std::vector<float>& out) {
        history_[position_] = sample;
        position_ = (position_ + 1) % history_.size();
        if (++phase_ < factor_) return;
        phase_ = 0;
        double value = 0;
        for (std::size_t i = 0; i < kernel_.size(); ++i) value += kernel_[i] * history_[(position_ + i) % history_.size()];
        out.push_back(static_cast<float>(value));
    }
private:
    int factor_;
    std::vector<double> kernel_, history_;
    std::size_t position_ = 0;
    int phase_ = 0;
};
}  // namespace

ScoreAudio DecodeScoreAudio(const std::string& source, const std::function<bool()>& cancelled) {
    const auto stop = [&] { return cancelled && cancelled(); };
    const auto wide = Wide(source);
    Platform platform;
    if (wide.empty() || !platform.ok || stop()) return {};
    // HTTP FLAC goes through the same decoder playback uses; everything else
    // through Media Foundation, asking for 16-bit PCM.
    auto flac = std::make_unique<HttpFlacDecoder>();
    if (!flac->Open(wide)) flac.reset();
    ComPtr<IMFSourceReader> reader;
    UINT32 channels = 0, rate = 0, bits = 16;
    if (flac) { channels = flac->Channels(); rate = flac->Rate(); }
    else {
        ComPtr<IMFMediaType> requested, actual;
        if (FAILED(MFCreateSourceReaderFromURL(wide.c_str(), nullptr, &reader)) ||
            FAILED(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE)) ||
            FAILED(reader->SetStreamSelection(kAudioStream, TRUE)) ||
            FAILED(MFCreateMediaType(&requested)) ||
            FAILED(requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
            FAILED(requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
            FAILED(requested->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
            FAILED(reader->SetCurrentMediaType(kAudioStream, nullptr, requested.Get())) ||
            FAILED(reader->GetCurrentMediaType(kAudioStream, &actual)) ||
            FAILED(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) ||
            FAILED(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate)) ||
            FAILED(actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits))) return {};
    }
    if (!channels || channels > 8 || rate < 8000 || rate > 192000 || bits != 16) return {};
    Decimator decimator(rate);
    ScoreAudio result;
    result.rate = decimator.Rate(rate);
    const auto limit = static_cast<std::size_t>(kMaxSeconds * rate);
    std::size_t frames = 0;
    for (;;) {
        if (stop()) return {};
        ComPtr<IMFSample> sample;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        const auto hr = flac ? flac->Read(&sample, &flags, &timestamp) :
            reader->ReadSample(kAudioStream, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))) return {};
        if (sample) {
            ComPtr<IMFMediaBuffer> media;
            BYTE* bytes = nullptr;
            DWORD length = 0;
            if (FAILED(sample->ConvertToContiguousBuffer(&media)) || FAILED(media->Lock(&bytes, nullptr, &length))) return {};
            const std::size_t count = length / (channels * sizeof(std::int16_t));
            const auto* pcm = reinterpret_cast<const std::int16_t*>(bytes);
            for (std::size_t i = 0; i < count; ++i) {
                float sum = 0;
                for (UINT32 c = 0; c < channels; ++c) sum += pcm[i * channels + c];
                decimator.Push(sum / (32768.0f * channels), result.mono);
            }
            media->Unlock();
            frames += count;
            if (frames > limit) return {};
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    return result.mono.size() > result.rate ? result : ScoreAudio{};
}

}  // namespace lume::music
