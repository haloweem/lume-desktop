#include "platform/windows/dynamic_island/notification_audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

namespace lume::platform::windows {
struct NotificationAudio::Impl {
    std::vector<std::uint8_t> bytes;
    std::optional<NotificationPcmWave> wave;
    HWAVEOUT device = nullptr;
    WAVEHDR header{};
    bool attempted = false, prepared = false, submitted = false;

    bool Load() {
        if (attempted) return wave.has_value();
        attempted = true;
        try {
            std::array<wchar_t, 32768> executable{};
            const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            if (!length || length >= executable.size()) return false;
            const auto path = std::filesystem::path(executable.data()).parent_path() /
                L"ui/assets/notifications/off-work-squidward.wav";
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) return false;
            const auto size = file.tellg();
            if (size <= 0 || size > static_cast<std::streamoff>(NotificationAudioMaxBytes)) return false;
            bytes.resize(static_cast<std::size_t>(size));
            file.seekg(0, std::ios::beg);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
                bytes.clear(); return false;
            }
            wave = DecodeNotificationPcm(bytes.data(), bytes.size());
            if (!wave) bytes.clear();
            return wave.has_value();
        } catch (...) { bytes.clear(); wave.reset(); return false; }
    }
    void Stop() {
        submitted = false;
        if (!device) return;
        // waveOutReset returns queued buffers before waveOutUnprepareHeader/waveOutClose.
        waveOutReset(device);
        if (prepared) waveOutUnprepareHeader(device, &header, static_cast<UINT>(sizeof(header)));
        waveOutClose(device);
        device = nullptr; prepared = false; header = {};
    }
    bool Play() {
        Stop();
        if (!Load()) return false;
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = wave->channels;
        format.nSamplesPerSec = wave->sampleRate;
        format.nAvgBytesPerSec = wave->bytesPerSecond;
        format.nBlockAlign = wave->blockAlign;
        format.wBitsPerSample = wave->bitsPerSample;
        if (waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            device = nullptr; return false;
        }
        header = {};
        header.lpData = reinterpret_cast<LPSTR>(bytes.data() + wave->dataOffset);
        header.dwBufferLength = static_cast<DWORD>(wave->dataBytes);
        if (waveOutPrepareHeader(device, &header, static_cast<UINT>(sizeof(header))) != MMSYSERR_NOERROR) { Stop(); return false; }
        prepared = true;
        if (waveOutWrite(device, &header, static_cast<UINT>(sizeof(header))) != MMSYSERR_NOERROR) { Stop(); return false; }
        submitted = true;
        return true;
    }
    bool Playing() const {
        return submitted && device && prepared && !(header.dwFlags & WHDR_DONE);
    }
    std::optional<std::size_t> PositionBytes() const {
        if (!submitted || !device || !prepared || !wave) return {};
        if (header.dwFlags & WHDR_DONE) return wave->dataBytes;
        MMTIME position{};
        position.wType = TIME_SAMPLES;
        if (waveOutGetPosition(device, &position, static_cast<UINT>(sizeof(position))) != MMSYSERR_NOERROR) return {};
        std::uint64_t offset = 0;
        if (position.wType == TIME_BYTES) offset = position.u.cb;
        else if (position.wType == TIME_SAMPLES) offset = static_cast<std::uint64_t>(position.u.sample) * wave->blockAlign;
        else if (position.wType == TIME_MS) offset = static_cast<std::uint64_t>(position.u.ms) * wave->bytesPerSecond / 1000;
        else return {};
        return static_cast<std::size_t>(std::min<std::uint64_t>(offset, wave->dataBytes));
    }
    std::optional<unsigned> PositionMilliseconds() const {
        const auto offset = PositionBytes();
        if (!offset || !wave || !wave->bytesPerSecond) return {};
        return static_cast<unsigned>(*offset * 1000ull / wave->bytesPerSecond);
    }
    float Level() const {
        if (!Playing() || !wave) return 0;
        const auto offset = PositionBytes();
        return offset ? NotificationPcmLevel(bytes.data(), bytes.size(), *wave, *offset) : 0.f;
    }
};
NotificationAudio::NotificationAudio() : impl_(std::make_unique<Impl>()) {}
NotificationAudio::~NotificationAudio() { impl_->Stop(); }
bool NotificationAudio::PlayOffWork() { return impl_->Play(); }
void NotificationAudio::Stop() { impl_->Stop(); }
bool NotificationAudio::Playing() const { return impl_->Playing(); }
float NotificationAudio::Level() const { return impl_->Level(); }
std::optional<unsigned> NotificationAudio::PositionMilliseconds() const { return impl_->PositionMilliseconds(); }
} // namespace lume::platform::windows
