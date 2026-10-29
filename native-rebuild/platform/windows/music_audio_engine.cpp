#include "platform/windows/music_audio_engine.h"
#include "platform/windows/music_http_flac_decoder.h"

#include <windows.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <optional>
#include <vector>

namespace lume::music {
namespace {
using Microsoft::WRL::ComPtr;
constexpr std::size_t kBufferBytes = 64 * 1024;
constexpr std::size_t kMaxSampleBytes = 4 * 1024 * 1024;
constexpr std::size_t kBuffers = 4;
constexpr std::size_t kWindowFrames = 256;
constexpr double kPi = 3.14159265358979323846;
constexpr DWORD kAudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

std::wstring WideSource(const std::string& source) {
    if (source.empty() || source.size() > 4096 || source.find('\0') != std::string::npos ||
        source.find_first_of("\"\r\n") != std::string::npos) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
        static_cast<int>(source.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
        static_cast<int>(source.size()), wide.data(), size);
    return wide;
}

AudioAnalysis Analyze(const std::int16_t* pcm, std::size_t frames, std::size_t channels, double gain) {
    AudioAnalysis result;
    frames = std::min(frames, kWindowFrames);
    if (!frames || !channels || gain <= 0) return result;
    std::array<double, kWindowFrames> mono{};
    double squares = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t c = 0; c < channels; ++c) {
            const double sample = static_cast<double>(pcm[i * channels + c]) / 32768.0 * gain;
            squares += sample * sample;
            mono[i] += sample / static_cast<double>(channels);
        }
    }
    result.energy = std::clamp(std::sqrt(squares / static_cast<double>(frames * channels)), 0.0, 1.0);
    // ponytail: 256-point direct DFT at the 4 Hz host poll ceiling. Use FFT
    // only if the analysis window or publication rate grows.
    for (std::size_t frequency = 1; frequency <= kWindowFrames / 2; ++frequency) {
        double real = 0, imaginary = 0;
        for (std::size_t i = 0; i < frames; ++i) {
            const double window = 0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / (kWindowFrames - 1));
            const double phase = 2 * kPi * static_cast<double>(frequency * i) / kWindowFrames;
            real += mono[i] * window * std::cos(phase);
            imaginary -= mono[i] * window * std::sin(phase);
        }
        const auto bin = (frequency - 1) / (kWindowFrames / 2 / kMusicAnalysisBins);
        result.bins[bin] = std::max(result.bins[bin],
            std::clamp(4 * std::hypot(real, imaginary) / kWindowFrames, 0.0, 1.0));
    }
    return result;
}
}  // namespace

struct WindowsAudioEngine::Impl {
    struct Buffer {
        WAVEHDR header{};
        std::array<std::int16_t, kBufferBytes / sizeof(std::int16_t)> pcm{};
        std::uint64_t firstFrame = 0;
        std::size_t frames = 0;
        bool queued = false;
    };

    ~Impl() {
        Stop();
        if (mfStarted) MFShutdown();
        if (comStarted) CoUninitialize();
    }

    bool Initialize() {
        if (mfStarted) return true;
        if (!comStarted) {
            const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            comStarted = SUCCEEDED(hr);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
        }
        mfStarted = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
        return mfStarted;
    }

    bool Load(const std::string& source) {
        Stop();
        const auto wide = WideSource(source);
        if (wide.empty() || !Initialize()) return false;
        flac = std::make_unique<HttpFlacDecoder>();
        if (!flac->Open(wide)) flac.reset();
        if (!flac && FAILED(MFCreateSourceReaderFromURL(wide.c_str(), nullptr, &reader))) return false;
        ComPtr<IMFMediaType> actual;
        bool ready = flac || (SUCCEEDED(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE)) &&
            SUCCEEDED(reader->SetStreamSelection(kAudioStream, TRUE)));
        // Prefer 16-bit PCM. Hi-Res FLAC (24-bit, 96/192 kHz) decoders refuse
        // to narrow, so fall back to the decoder's native PCM width and
        // convert in Pump.
        const auto negotiate = [&](bool sixteenBit) {
            ComPtr<IMFMediaType> requested;
            return SUCCEEDED(MFCreateMediaType(&requested)) &&
                SUCCEEDED(requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) &&
                SUCCEEDED(requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) &&
                (!sixteenBit || SUCCEEDED(requested->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16))) &&
                SUCCEEDED(reader->SetCurrentMediaType(kAudioStream, nullptr, requested.Get()));
        };
        if (!flac) ready = ready && (negotiate(true) || negotiate(false)) &&
            SUCCEEDED(reader->GetCurrentMediaType(kAudioStream, &actual));
        UINT32 channels = flac ? flac->Channels() : 0, rate = flac ? flac->Rate() : 0, bits = flac ? 16 : 0;
        if (ready && !flac) ready = SUCCEEDED(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) &&
            SUCCEEDED(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate)) &&
            SUCCEEDED(actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)) &&
            channels > 0 && channels <= 8 && rate >= 8000 && rate <= 192000 &&
            (bits == 16 || bits == 24 || bits == 32);
        if (!ready) { Stop(); return false; }
        sourceBits = bits;
        // Surround sources (5.1/7.1 immersive levels) are folded to stereo in
        // Pump; the waveOut device always receives at most two channels.
        sourceChannels = channels;
        if (channels > 2) channels = 2;
        format = {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = static_cast<WORD>(channels);
        format.nSamplesPerSec = rate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(channels * 2);
        format.nAvgBytesPerSec = rate * format.nBlockAlign;
        if (waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            output = nullptr;
            Stop();
            return false;
        }
        if (waveOutPause(output) != MMSYSERR_NOERROR || !ApplyGain()) { Stop(); return false; }
        PROPVARIANT value{};
        if (flac) duration = static_cast<double>(flac->Frames()) / flac->Rate();
        else if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value)) &&
            value.vt == VT_UI8) duration = static_cast<double>(value.uhVal.QuadPart) / 10000000.0;
        PropVariantClear(&value);
        if (!std::isfinite(duration) || duration < 0 || duration > 1e9) { Stop(); return false; }
        loaded = true;
        worker = std::thread([this] { Pump(); });
        return true;
    }

    bool ApplyGain() const {
        if (!output) return true;
        const auto level = static_cast<DWORD>(std::lround((muted ? 0.0 : volume) * 65535));
        return waveOutSetVolume(output, level | (level << 16)) == MMSYSERR_NOERROR;
    }

    std::uint64_t OutputFrames() const {
        MMTIME time{};
        time.wType = TIME_SAMPLES;
        if (!output || waveOutGetPosition(output, &time, sizeof(time)) != MMSYSERR_NOERROR) return completed;
        if (time.wType == TIME_SAMPLES) return time.u.sample;
        if (time.wType == TIME_BYTES) return time.u.cb / format.nBlockAlign;
        if (time.wType == TIME_MS) return static_cast<std::uint64_t>(time.u.ms) * format.nSamplesPerSec / 1000;
        return completed;
    }

    void ResetBuffers() {
        if (output) waveOutReset(output);
        for (auto& buffer : buffers) {
            if (output && (buffer.header.dwFlags & WHDR_PREPARED))
                waveOutUnprepareHeader(output, &buffer.header, sizeof(buffer.header));
            buffer.header = {};
            buffer.queued = false;
            buffer.frames = 0;
        }
        submitted = completed = 0;
        end = failed = false;
        pending.clear();
        pendingOffset = 0;
        prepared = preparing = followSeek = false;
        discardBefore.reset();
    }

    // One little-endian source sample at 16/24/32 bits, in 16-bit scale.
    double Sample(const BYTE* at) const {
        if (sourceBits == 16) return static_cast<std::int16_t>(at[0] | (at[1] << 8));
        if (sourceBits == 24)
            return static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(at[0]) << 8 |
                static_cast<std::uint32_t>(at[1]) << 16 | static_cast<std::uint32_t>(at[2]) << 24)) / 65536.0;
        return static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(at[0]) |
            static_cast<std::uint32_t>(at[1]) << 8 | static_cast<std::uint32_t>(at[2]) << 16 |
            static_cast<std::uint32_t>(at[3]) << 24)) / 65536.0;
    }
    // Narrows Hi-Res samples to 16 bits and folds surround to stereo. WAVE
    // channel order FL FR FC LFE BL BR SL SR: LFE is dropped, centre and
    // surrounds are folded in at -3 dB and normalised against clipping.
    void Convert(const BYTE* bytes, std::size_t count) {
        const auto step = sourceBits / 8;
        const auto clamp16 = [](double value) {
            return static_cast<std::int16_t>(std::clamp(std::lround(value), -32768L, 32767L));
        };
        if (sourceChannels <= 2) {
            pending.resize(count * sourceChannels);
            for (std::size_t i = 0; i < pending.size(); ++i) pending[i] = clamp16(Sample(bytes + i * step));
            return;
        }
        constexpr double side = 0.7071;
        static constexpr int left[8] = {1, 0, 2, 0, 3, 0, 3, 0}, right[8] = {0, 1, 2, 0, 0, 3, 0, 3};
        double leftWeight = 0, rightWeight = 0;
        for (UINT32 c = 0; c < sourceChannels; ++c) {
            leftWeight += left[c] == 1 ? 1.0 : left[c] ? side : 0.0;
            rightWeight += right[c] == 1 ? 1.0 : right[c] ? side : 0.0;
        }
        pending.resize(count * 2);
        for (std::size_t frame = 0; frame < count; ++frame) {
            double l = 0, r = 0;
            for (UINT32 c = 0; c < sourceChannels; ++c) {
                const double sample = Sample(bytes + (frame * sourceChannels + c) * step);
                l += sample * (left[c] == 1 ? 1.0 : left[c] ? side : 0.0);
                r += sample * (right[c] == 1 ? 1.0 : right[c] ? side : 0.0);
            }
            pending[frame * 2] = clamp16(l / std::max(leftWeight, 1.0));
            pending[frame * 2 + 1] = clamp16(r / std::max(rightWeight, 1.0));
        }
    }

    void Stop() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            playing = false;
            condition.notify_all();
        }
        if (worker.joinable()) worker.join();
        std::lock_guard lock(mutex);
        ResetBuffers();
        if (output) waveOutClose(output);
        output = nullptr;
        reader.Reset();
        flac.reset();
        loaded = false;
        stopping = false;
        duration = basePosition = 0;
        seekTarget.reset();
        discardBefore.reset();
        sourceChannels = 2;
        sourceBits = 16;
        ++generation;
    }

    void Pump() {
        const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::unique_lock lock(mutex);
        for (;;) {
            condition.wait(lock, [this] { return stopping || playing || preparing; });
            if (stopping) break;
            for (auto& buffer : buffers) {
                if (buffer.queued && (buffer.header.dwFlags & WHDR_DONE)) {
                    completed = std::max(completed, buffer.firstFrame + buffer.frames);
                    waveOutUnprepareHeader(output, &buffer.header, sizeof(buffer.header));
                    buffer.header = {};
                    buffer.queued = false;
                }
                if (buffer.queued || failed) continue;
                if (pendingOffset == pending.size() && !end) {
                    if (seekTarget) {
                        // Seek only records the target; the reader is used
                        // by this thread alone, never concurrently.
                        PROPVARIANT value{};
                        value.vt = VT_I8;
                        value.hVal.QuadPart = static_cast<LONGLONG>(*seekTarget * 10000000.0);
                        seekTarget.reset();
                        const auto source = reader;
                        const auto seekGeneration = generation;
                        lock.unlock();
                        const auto hr = flac ? flac->Seek(static_cast<double>(value.hVal.QuadPart) / 10000000.0) :
                            source->SetCurrentPosition(GUID{}, value);
                        lock.lock();
                        if (stopping || seekGeneration != generation) break;
                        if (FAILED(hr)) { failed = true; break; }
                        discardBefore = value.hVal.QuadPart;
                    }
                    ComPtr<IMFSample> sample;
                    DWORD flags = 0;
                    LONGLONG timestamp = 0;
                    // Network reads can stall for seconds on lossless streams.
                    // Release the lock so Position/Pause/Seek and the host's
                    // status poll stay responsive; discard the sample if a
                    // seek or stop happened meanwhile.
                    const auto source = reader;
                    const auto readGeneration = generation;
                    lock.unlock();
                    const auto hr = flac ? flac->Read(&sample, &flags, &timestamp) :
                        source->ReadSample(kAudioStream, 0, nullptr, &flags, &timestamp, &sample);
                    lock.lock();
                    if (stopping || readGeneration != generation) break;
                    if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))) {
                        failed = true;
                        break;
                    }
                    end = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
                    pending.clear();
                    pendingOffset = 0;
                    if (sample) {
                        ComPtr<IMFMediaBuffer> media;
                        DWORD length = 0;
                        BYTE* bytes = nullptr;
                        if (FAILED(sample->GetTotalLength(&length)) || length > kMaxSampleBytes ||
                            FAILED(sample->ConvertToContiguousBuffer(&media)) ||
                            FAILED(media->GetCurrentLength(&length)) || length > kMaxSampleBytes ||
                            length % (sourceChannels * sourceBits / 8) != 0 ||
                            FAILED(media->Lock(&bytes, nullptr, &length))) { failed = true; break; }
                        if (length > kMaxSampleBytes || length % (sourceChannels * sourceBits / 8) != 0) {
                            media->Unlock(); failed = true; break;
                        }
                        if (sourceChannels <= 2 && sourceBits == 16) {
                            pending.resize(length / sizeof(std::int16_t));
                            if (length) std::memcpy(pending.data(), bytes, length);
                        } else Convert(bytes, length / (sourceChannels * sourceBits / 8));
                        media->Unlock();
                        // Compressed sources may seek to an earlier packet (FLAC can
                        // return its opening block). Never label that PCM as the
                        // requested position: discard/trim using the sample clock.
                        if (discardBefore && !pending.empty()) {
                            const auto target = *discardBefore + (followSeek ? static_cast<LONGLONG>(
                                std::chrono::duration<double>(std::chrono::steady_clock::now() - seekStarted).count() *
                                10000000.0) : 0);
                            if (timestamp < target) {
                                const auto skip = std::min<std::uint64_t>(
                                    static_cast<std::uint64_t>(std::ceil(
                                        static_cast<double>(target - timestamp) *
                                        format.nSamplesPerSec / 10000000.0)),
                                    pending.size() / format.nChannels);
                                pendingOffset = static_cast<std::size_t>(skip) * format.nChannels;
                            }
                            if (pendingOffset < pending.size()) {
                                basePosition = static_cast<double>(timestamp) / 10000000.0 +
                                    static_cast<double>(pendingOffset / format.nChannels) / format.nSamplesPerSec;
                                discardBefore.reset();
                                followSeek = false;
                            }
                        }
                    }
                }
                if (pendingOffset == pending.size()) continue;
                // At most 50 ms per output buffer, four buffers total.
                const auto count = std::min(pending.size() - pendingOffset,
                    static_cast<std::size_t>(format.nSamplesPerSec / 20) * format.nChannels);
                std::copy_n(pending.data() + pendingOffset, count, buffer.pcm.data());
                pendingOffset += count;
                buffer.frames = count / format.nChannels;
                buffer.firstFrame = submitted;
                buffer.header = {};
                buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.pcm.data());
                buffer.header.dwBufferLength = static_cast<DWORD>(count * sizeof(std::int16_t));
                if (waveOutPrepareHeader(output, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR ||
                    waveOutWrite(output, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR) {
                    failed = true;
                    break;
                }
                buffer.queued = true;
                submitted += buffer.frames;
            }
            const bool queued = std::any_of(buffers.begin(), buffers.end(), [](const auto& b) { return b.queued; });
            if (queued && !discardBefore && (end || std::all_of(buffers.begin(), buffers.end(),
                    [](const auto& b) { return b.queued; }))) {
                prepared = true;
                preparing = false;
            }
            if (failed) {
                waveOutReset(output);
                playing = false;
                preparing = prepared = false;
            } else if (end && pendingOffset == pending.size() && !queued) {
                playing = false;
                preparing = false;
                duration = basePosition + static_cast<double>(completed) / format.nSamplesPerSec;
            }
            // Preroll has no audible buffers to pace. Continue decoding without
            // adding 10 ms per four discarded packets on a distant seek.
            if (discardBefore && !end && !failed && (playing || preparing)) continue;
            condition.wait_for(lock, std::chrono::milliseconds(10),
                [this] { return stopping || (!playing && !preparing); });
        }
        lock.unlock();
        if (SUCCEEDED(com)) CoUninitialize();
    }

    mutable std::mutex mutex;
    std::condition_variable condition;
    std::thread worker;
    ComPtr<IMFSourceReader> reader;
    std::unique_ptr<HttpFlacDecoder> flac;
    HWAVEOUT output = nullptr;
    WAVEFORMATEX format{};
    std::array<Buffer, kBuffers> buffers{};
    std::vector<std::int16_t> pending;
    std::size_t pendingOffset = 0;
    std::uint64_t submitted = 0, completed = 0;
    // Bumped by Seek/Stop so a read that finished unlocked is discarded.
    std::uint64_t generation = 0;
    std::optional<double> seekTarget;
    std::optional<LONGLONG> discardBefore;
    std::chrono::steady_clock::time_point seekStarted;
    bool preparing = false, prepared = false, followSeek = false;
    UINT32 sourceChannels = 2, sourceBits = 16;
    double duration = 0, basePosition = 0, volume = 1;
    bool muted = false, playing = false, loaded = false;
    bool stopping = false, end = false, failed = false;
    bool comStarted = false, mfStarted = false;
};

WindowsAudioEngine::WindowsAudioEngine() : impl_(std::make_unique<Impl>()) {}
WindowsAudioEngine::~WindowsAudioEngine() = default;
bool WindowsAudioEngine::Load(const std::string& source) { return impl_->Load(source); }
void WindowsAudioEngine::Stop() { impl_->Stop(); }

bool WindowsAudioEngine::Play() {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->loaded || impl_->failed || waveOutRestart(impl_->output) != MMSYSERR_NOERROR) return false;
    impl_->playing = true;
    impl_->condition.notify_one();
    return true;
}

bool WindowsAudioEngine::Pause() {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->loaded || waveOutPause(impl_->output) != MMSYSERR_NOERROR) return false;
    impl_->playing = false;
    impl_->condition.notify_one();
    return true;
}

bool WindowsAudioEngine::Seek(double position) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->loaded || !std::isfinite(position) || position < 0 || position > 1e9) return false;
    const double target = impl_->duration > 0 ? std::min(position, impl_->duration) : position;
    impl_->playing = false;
    impl_->ResetBuffers();
    waveOutPause(impl_->output);
    // Applied by the decode thread before its next read; a failure there
    // surfaces as a stopped engine instead of blocking this caller.
    impl_->seekTarget = target;
    ++impl_->generation;
    impl_->basePosition = target;
    impl_->condition.notify_one();
    return true;
}

bool WindowsAudioEngine::Prepare(double position, bool follow) {
    if (!Seek(position)) return false;
    std::lock_guard lock(impl_->mutex);
    impl_->preparing = true;
    impl_->followSeek = follow;
    impl_->seekStarted = std::chrono::steady_clock::now();
    impl_->condition.notify_one();
    return true;
}

bool WindowsAudioEngine::Ready() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->loaded && impl_->prepared && !impl_->failed;
}

double WindowsAudioEngine::Position() const {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->loaded) return 0;
    const auto frames = std::min(impl_->OutputFrames(), impl_->submitted);
    return impl_->basePosition + static_cast<double>(frames) / impl_->format.nSamplesPerSec;
}

double WindowsAudioEngine::Duration() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->duration;
}

bool WindowsAudioEngine::Playing() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->playing;
}

bool WindowsAudioEngine::SetVolume(double volume) {
    if (!std::isfinite(volume) || volume < 0 || volume > 1) return false;
    std::lock_guard lock(impl_->mutex);
    const double previous = impl_->volume;
    impl_->volume = volume;
    if (impl_->ApplyGain()) return true;
    impl_->volume = previous;
    return false;
}

bool WindowsAudioEngine::SetMuted(bool muted) {
    std::lock_guard lock(impl_->mutex);
    const bool previous = impl_->muted;
    impl_->muted = muted;
    if (impl_->ApplyGain()) return true;
    impl_->muted = previous;
    return false;
}

AudioAnalysis WindowsAudioEngine::Analysis() const {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->playing || impl_->muted || impl_->volume == 0 || impl_->failed) return {};
    const auto cursor = impl_->OutputFrames();
    for (const auto& buffer : impl_->buffers) {
        if (!buffer.queued || cursor < buffer.firstFrame || cursor >= buffer.firstFrame + buffer.frames) continue;
        const auto offset = static_cast<std::size_t>(cursor - buffer.firstFrame);
        return Analyze(buffer.pcm.data() + offset * impl_->format.nChannels,
            buffer.frames - offset, impl_->format.nChannels, impl_->volume);
    }
    return {};
}

}  // namespace lume::music
