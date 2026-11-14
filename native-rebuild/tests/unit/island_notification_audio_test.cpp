#include "platform/windows/dynamic_island/notification_audio.h"
#include "platform/windows/dynamic_island/off_work_voice_captions.h"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace lume::platform::windows;
namespace {
unsigned checks = 0;
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    ++checks;
}
void U16(std::vector<std::uint8_t>& bytes, unsigned value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
void U32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
void Put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned part = 0; part < 4; ++part) bytes[at + part] = static_cast<std::uint8_t>(value >> (part * 8));
}
void Tag(std::vector<std::uint8_t>& bytes, const char* name) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(name[i]));
}
void Chunk(std::vector<std::uint8_t>& bytes, const char* name, const std::vector<std::uint8_t>& body) {
    Tag(bytes, name); U32(bytes, static_cast<std::uint32_t>(body.size()));
    bytes.insert(bytes.end(), body.begin(), body.end());
    if (body.size() & 1u) bytes.push_back(0);
}
std::vector<std::uint8_t> Format(unsigned bits = 16, unsigned channels = 1) {
    std::vector<std::uint8_t> fmt;
    U16(fmt, 1); U16(fmt, channels); U32(fmt, 8000); U32(fmt, 8000 * channels * bits / 8);
    U16(fmt, channels * bits / 8); U16(fmt, bits);
    return fmt;
}
std::vector<std::uint8_t> Wave(const std::vector<std::uint8_t>& pcm, unsigned bits = 16, unsigned channels = 1) {
    std::vector<std::uint8_t> result;
    Tag(result, "RIFF"); U32(result, 0); Tag(result, "WAVE");
    Chunk(result, "fmt ", Format(bits, channels));
    Chunk(result, "data", pcm);
    Put32(result, 4, static_cast<std::uint32_t>(result.size() - 8));
    return result;
}
auto Decode(const std::vector<std::uint8_t>& bytes) { return DecodeNotificationPcm(bytes.data(), bytes.size()); }
bool Near(float actual, float expected) { return std::abs(actual - expected) < 0.0001f; }
}

int main(int argc, char** argv) {
    const std::array<std::pair<unsigned, std::wstring_view>, 12> captions{{
        {0,L"5"},{479,L"5"},{480,L"4"},{1366,L"4"},{1367,L"3"},
        {2220,L"2"},{2824,L"1"},{3719,L"1"},{3720,L"我下班了，蟹老板"},
        {5966,L"我要说的是"},{13440,L"站在这油污的地板上"},{17601,L"站在这油污的地板上"}
    }};
    for (const auto& expected : captions)
        Check(OffWorkVoiceCaption(expected.first) == expected.second, "captions follow the recorded nonuniform counts and confirmed dialogue");
    Check(OffWorkVoiceCaption(3720,true) == L"I'm off work, Mr. Krabs.", "English caption uses the same recorded cue boundary");
    for (std::size_t i = 1; i < OffWorkVoiceCues.size(); ++i)
        Check(OffWorkVoiceCues[i].startMs > OffWorkVoiceCues[i-1].startMs &&
            OffWorkVoiceCues[i].startMs < NotificationAudio::DurationMilliseconds,
            "every voice caption stays ordered inside the unchanged recording");
    std::vector<std::uint8_t> pcm;
    for (unsigned sample = 0; sample < 480; ++sample) U16(pcm, sample < 240 ? 16384 : 0);
    const auto valid = Wave(pcm);
    const auto wave = Decode(valid);
    Check(wave && wave->channels == 1 && wave->sampleRate == 8000 && wave->bitsPerSample == 16, "strict mono PCM decodes");
    Check(wave->DurationMilliseconds() == 60, "PCM frame duration is exact");
    Check(Near(NotificationPcmLevel(valid.data(), valid.size(), *wave, 0), .5f), "RMS reads actual non-silent PCM");
    Check(Near(NotificationPcmLevel(valid.data(), valid.size(), *wave, 480), 0), "RMS is zero in a silent segment");
    Check(Near(NotificationPcmLevel(valid.data(), valid.size(), *wave, 1), .5f), "device byte position aligns to a complete frame");
    Check(NotificationPcmLevel(valid.data(), valid.size(), *wave, pcm.size()) == 0, "RMS is zero at playback end");
    Check(NotificationPcmLevel(valid.data(), valid.size(), *wave, pcm.size() + 1) == 0, "RMS rejects a position beyond the buffer");
    Check(NotificationPcmLevel(valid.data(), wave->dataOffset, *wave, 0) == 0, "RMS rejects truncated source storage");
    Check(!DecodeNotificationPcm(nullptr, 0), "empty PCM is rejected");
    Check(!DecodeNotificationPcm(valid.data(), NotificationAudioMaxBytes + 1), "over-limit bytes are rejected before reading");
    auto bad = valid; bad[0] = 'X'; Check(!Decode(bad), "non-RIFF is rejected");
    bad = valid; bad[8] = 'X'; Check(!Decode(bad), "non-WAVE is rejected");
    bad = valid; bad[4] ^= 1; Check(!Decode(bad), "RIFF length mismatch is rejected");
    bad = valid; bad[20] = 3; Check(!Decode(bad), "float WAV is rejected");
    bad = valid; bad[22] = 0; Check(!Decode(bad), "zero channel count is rejected");
    bad = valid; bad[22] = 3; Check(!Decode(bad), "unsupported channel count is rejected");
    bad = valid; Put32(bad, 24, 0); Check(!Decode(bad), "zero sample rate is rejected");
    bad = valid; Put32(bad, 24, 384000); Check(!Decode(bad), "unsupported sample rate is rejected");
    bad = valid; Put32(bad, 28, 7); Check(!Decode(bad), "inconsistent bytes per second are rejected");
    bad = valid; bad[32] = 0; Check(!Decode(bad), "inconsistent block alignment is rejected");
    bad = valid; bad[34] = 12; Check(!Decode(bad), "non-byte PCM sample width is rejected");
    bad = valid; Put32(bad, 40, 0xffffffffu); Check(!Decode(bad), "oversized data chunk cannot overflow bounds");
    bad = valid; bad.pop_back(); Put32(bad, 4, static_cast<std::uint32_t>(bad.size() - 8));
    Check(!Decode(bad), "truncated PCM data is rejected");
    Check(!Decode(Wave({1})), "partial PCM frame is rejected");
    Check(!Decode(Wave({})), "empty data chunk is rejected");
    bad = valid; Chunk(bad, "fmt ", Format()); Put32(bad, 4, static_cast<std::uint32_t>(bad.size() - 8));
    Check(!Decode(bad), "duplicate fmt chunk is rejected");
    bad = valid; Chunk(bad, "data", {0, 0}); Put32(bad, 4, static_cast<std::uint32_t>(bad.size() - 8));
    Check(!Decode(bad), "duplicate data chunk is rejected");
    bad.clear(); Tag(bad, "RIFF"); U32(bad, 0); Tag(bad, "WAVE");
    Chunk(bad, "data", {0, 0}); Chunk(bad, "fmt ", Format()); Put32(bad, 4, static_cast<std::uint32_t>(bad.size() - 8));
    Check(!Decode(bad), "data before format is rejected");
    auto metadata = valid; Chunk(metadata, "LIST", {1, 2, 3});
    Put32(metadata, 4, static_cast<std::uint32_t>(metadata.size() - 8));
    Check(Decode(metadata).has_value(), "odd LIST chunk padding is skipped");
    metadata.pop_back(); Put32(metadata, 4, static_cast<std::uint32_t>(metadata.size() - 8));
    Check(!Decode(metadata), "missing odd LIST padding is rejected");
    bad = valid; bad.push_back(0); Put32(bad, 4, static_cast<std::uint32_t>(bad.size() - 8));
    Check(!Decode(bad), "partial trailing chunk header is rejected");

    for (const unsigned bits : {8u, 16u, 24u, 32u}) {
        std::vector<std::uint8_t> samples;
        for (unsigned index = 0; index < 240; ++index) {
            const std::uint32_t value = bits == 8 ? 192u : std::uint32_t{1} << (bits - 2);
            for (unsigned part = 0; part < bits / 8; ++part) samples.push_back(static_cast<std::uint8_t>(value >> (part * 8)));
        }
        const auto fixture = Wave(samples, bits);
        const auto decoded = Decode(fixture);
        Check(decoded && Near(NotificationPcmLevel(fixture.data(), fixture.size(), *decoded, 0), .5f), "8/16/24/32-bit PCM RMS uses actual amplitude");
    }
    std::vector<std::uint8_t> stereo;
    for (unsigned frame = 0; frame < 240; ++frame) { U16(stereo, 16384); U16(stereo, 49152); }
    const auto opposite = Wave(stereo, 16, 2);
    const auto oppositeWave = Decode(opposite);
    Check(oppositeWave && Near(NotificationPcmLevel(opposite.data(), opposite.size(), *oppositeWave, 0), .5f), "stereo RMS retains opposite-polarity channels");

    Check(argc == 2, "a bundled WAV resource is supplied");
    std::ifstream resource(std::filesystem::path(argv[1]), std::ios::binary);
    Check(resource.is_open(), "bundled WAV exists");
    const std::vector<std::uint8_t> recording{std::istreambuf_iterator<char>(resource), std::istreambuf_iterator<char>()};
    const auto actual = Decode(recording);
    Check(actual && actual->channels == 2 && actual->sampleRate == 44100 && actual->bitsPerSample == 16, "bundled recording is standard 44.1 kHz stereo 16-bit PCM");
    Check(actual->dataBytes == 3104768 && actual->DurationMilliseconds() == NotificationAudio::DurationMilliseconds,
          "bundled recording retains its 17.6007256 second duration");
    bool voice = false;
    for (std::size_t position = 0; position < actual->dataBytes; position += actual->bytesPerSecond / 10) {
        const float level = NotificationPcmLevel(recording.data(), recording.size(), *actual, position);
        Check(std::isfinite(level) && level >= 0 && level <= 1, "bundled voice RMS remains normalized");
        voice = voice || level > .01f;
    }
    Check(voice, "bundled recording contains actual non-silent audio");
    std::cout << "notification audio: " << checks << " silent PCM checks passed\n";
}
