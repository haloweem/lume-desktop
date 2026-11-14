#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace lume::platform::windows {
inline constexpr std::size_t NotificationAudioMaxBytes = 4u * 1024u * 1024u;

// Offsets refer to the caller-owned RIFF bytes. Parsing never opens an audio device.
struct NotificationPcmWave {
    std::uint16_t channels = 0, bitsPerSample = 0, blockAlign = 0;
    std::uint32_t sampleRate = 0, bytesPerSecond = 0;
    std::size_t dataOffset = 0, dataBytes = 0;
    unsigned DurationMilliseconds() const noexcept {
        return bytesPerSecond ? static_cast<unsigned>((dataBytes * 1000ull + bytesPerSecond - 1) / bytesPerSecond) : 0;
    }
};

inline std::optional<NotificationPcmWave> DecodeNotificationPcm(const std::uint8_t* bytes, std::size_t size) noexcept {
    if (!bytes || size < 44 || size > NotificationAudioMaxBytes) return {};
    const auto tag = [&](std::size_t at, const char* value) {
        return bytes[at] == value[0] && bytes[at + 1] == value[1] &&
            bytes[at + 2] == value[2] && bytes[at + 3] == value[3];
    };
    const auto u16 = [&](std::size_t at) {
        return static_cast<std::uint16_t>(bytes[at] | (static_cast<unsigned>(bytes[at + 1]) << 8));
    };
    const auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[at + 2]) << 16) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    };
    if (!tag(0, "RIFF") || !tag(8, "WAVE") || u32(4) != size - 8) return {};
    NotificationPcmWave wave;
    bool haveFormat = false, haveData = false;
    std::size_t at = 12;
    while (at < size) {
        if (size - at < 8) return {};
        const auto chunkSize = static_cast<std::size_t>(u32(at + 4));
        const auto body = at + 8;
        if (chunkSize > size - body) return {};
        const auto padded = chunkSize + (chunkSize & 1u);
        if (padded > size - body) return {};
        if (tag(at, "fmt ")) {
            if (haveFormat || (chunkSize != 16 && chunkSize != 18) || u16(body) != 1 ||
                (chunkSize == 18 && u16(body + 16) != 0)) return {};
            wave.channels = u16(body + 2);
            wave.sampleRate = u32(body + 4);
            wave.bytesPerSecond = u32(body + 8);
            wave.blockAlign = u16(body + 12);
            wave.bitsPerSample = u16(body + 14);
            if ((wave.channels != 1 && wave.channels != 2) || wave.sampleRate < 8000 || wave.sampleRate > 192000 ||
                (wave.bitsPerSample != 8 && wave.bitsPerSample != 16 && wave.bitsPerSample != 24 && wave.bitsPerSample != 32) ||
                wave.blockAlign != wave.channels * (wave.bitsPerSample / 8) ||
                wave.bytesPerSecond != wave.sampleRate * wave.blockAlign) return {};
            haveFormat = true;
        } else if (tag(at, "data")) {
            if (!haveFormat || haveData || !chunkSize || chunkSize % wave.blockAlign) return {};
            wave.dataOffset = body;
            wave.dataBytes = chunkSize;
            haveData = true;
        }
        // LIST/JUNK and other bounded RIFF metadata are skipped, including odd-byte padding.
        at = body + padded;
    }
    return haveFormat && haveData ? std::optional<NotificationPcmWave>(wave) : std::nullopt;
}

// Actual normalized RMS of the next 30 ms from a device-reported PCM byte position.
// Shared with silent tests; no synthetic animation values or wall-clock projection.
inline float NotificationPcmLevel(const std::uint8_t* bytes, std::size_t size,
                                  const NotificationPcmWave& wave, std::size_t positionBytes) noexcept {
    if (!bytes || !wave.blockAlign || !wave.channels || !wave.sampleRate ||
        wave.dataOffset > size || wave.dataBytes > size - wave.dataOffset || positionBytes >= wave.dataBytes ||
        (wave.bitsPerSample != 8 && wave.bitsPerSample != 16 && wave.bitsPerSample != 24 && wave.bitsPerSample != 32) ||
        wave.blockAlign != wave.channels * (wave.bitsPerSample / 8)) return 0;
    const auto sampleBytes = static_cast<std::size_t>(wave.bitsPerSample / 8);
    const auto start = positionBytes - positionBytes % wave.blockAlign;
    const auto windowBytes = static_cast<std::size_t>(wave.sampleRate) * 30u / 1000u * wave.blockAlign;
    const auto count = std::min(windowBytes, wave.dataBytes - start) / sampleBytes;
    if (!count) return 0;
    double energy = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto* sample = bytes + wave.dataOffset + start + index * sampleBytes;
        std::uint32_t raw = 0;
        for (std::size_t part = 0; part < sampleBytes; ++part) raw |= static_cast<std::uint32_t>(sample[part]) << (part * 8);
        double value;
        if (wave.bitsPerSample == 8) value = (static_cast<int>(raw) - 128) / 128.0;
        else {
            const auto sign = std::uint64_t{1} << (wave.bitsPerSample - 1);
            const auto signedValue = raw >= sign ? static_cast<std::int64_t>(raw) - static_cast<std::int64_t>(sign * 2) : static_cast<std::int64_t>(raw);
            value = static_cast<double>(signedValue) / static_cast<double>(sign);
        }
        energy += value * value;
    }
    return static_cast<float>(std::clamp(std::sqrt(energy / static_cast<double>(count)), 0.0, 1.0));
}

// UI-thread owner of one independent waveOut device and one cached bundled recording.
class NotificationAudio final {
public:
    static constexpr unsigned DurationMilliseconds = 17601;
    NotificationAudio();
    ~NotificationAudio();
    NotificationAudio(const NotificationAudio&) = delete;
    NotificationAudio& operator=(const NotificationAudio&) = delete;
    bool PlayOffWork();
    void Stop();
    bool Playing() const;
    float Level() const;
    // nullopt means unavailable; zero is a real device-reported start position.
    std::optional<unsigned> PositionMilliseconds() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace lume::platform::windows
