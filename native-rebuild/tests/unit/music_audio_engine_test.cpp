#include "platform/windows/music_audio_engine.h"

#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <thread>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool Eventually(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

void WriteWave(const std::filesystem::path& path, bool eightBit) {
    std::ofstream file(path, std::ios::binary);
    const auto word = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) file.put(static_cast<char>((value >> (i * 8)) & 255));
    };
    constexpr std::uint32_t rate = 8000, samples = 3 * rate;
    const std::uint32_t stride = eightBit ? 1 : 2;
    file.write("RIFF", 4); word(36 + samples * stride, 4); file.write("WAVEfmt ", 8);
    word(16, 4); word(1, 2); word(1, 2); word(rate, 4); word(rate * stride, 4);
    word(stride, 2); word(stride * 8, 2); file.write("data", 4); word(samples * stride, 4);
    for (std::uint32_t i = 0; i < samples; ++i) {
        const double hz = i < rate ? 1000 : i < 2 * rate ? 0 : 2000;
        const double sample = 0.5 * std::sin(2 * 3.14159265358979323846 * hz * i / rate);
        if (eightBit) word(static_cast<std::uint32_t>(128 + sample * 127), 1);
        else word(static_cast<std::uint16_t>(static_cast<std::int16_t>(sample * 32767)), 2);
    }
}

// 5.1 PCM (WAVE_FORMAT_EXTENSIBLE, FL FR FC LFE BL BR) with a 1 kHz tone in
// front left/right only, like immersive surround levels deliver.
void WriteSurroundWave(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    const auto word = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) file.put(static_cast<char>((value >> (i * 8)) & 255));
    };
    constexpr std::uint32_t rate = 8000, frames = 2 * rate, channels = 6, block = channels * 2;
    file.write("RIFF", 4); word(60 + frames * block, 4); file.write("WAVEfmt ", 8);
    word(40, 4); word(0xFFFE, 2); word(channels, 2); word(rate, 4); word(rate * block, 4);
    word(block, 2); word(16, 2); word(22, 2); word(16, 2); word(0x3F, 4);
    // KSDATAFORMAT_SUBTYPE_PCM {00000001-0000-0010-8000-00AA00389B71}
    word(1, 4); word(0, 2); word(0x10, 2);
    for (const int byte : {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}) file.put(static_cast<char>(byte));
    file.write("data", 4); word(frames * block, 4);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const auto tone = static_cast<std::int16_t>(0.5 * std::sin(2 * 3.14159265358979323846 * 1000 * i / rate) * 32767);
        for (std::uint32_t c = 0; c < channels; ++c) word(static_cast<std::uint16_t>(c < 2 ? tone : 0), 2);
    }
}

DWORD LargestSample(const std::filesystem::path& path) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType> type;
    Expect(SUCCEEDED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader)), "fixture MF reader opens");
    constexpr DWORD allStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
    constexpr DWORD audioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    Expect(SUCCEEDED(reader->SetStreamSelection(allStreams, FALSE)) &&
        SUCCEEDED(reader->SetStreamSelection(audioStream, TRUE)) &&
        SUCCEEDED(MFCreateMediaType(&type)) &&
        SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) &&
        SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) &&
        SUCCEEDED(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) &&
        SUCCEEDED(reader->SetCurrentMediaType(audioStream, nullptr, type.Get())),
        "fixture negotiates the same PCM as the engine");
    DWORD largest = 0;
    for (unsigned i = 0; i < 10000; ++i) {
        ComPtr<IMFSample> sample;
        DWORD flags = 0;
        Expect(SUCCEEDED(reader->ReadSample(audioStream, 0,
            nullptr, &flags, nullptr, &sample)), "fixture decodes bounded samples");
        if (sample) {
            DWORD length = 0;
            Expect(SUCCEEDED(sample->GetTotalLength(&length)), "measure real decoded sample");
            largest = std::max(largest, length);
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return largest;
    }
    Expect(false, "generated fixture must reach EOF within the probe limit");
    return 0;
}
}

int main(int argc, char** argv) {
    lume::music::WindowsAudioEngine audio;
    Expect(!audio.Load(""), "an empty source is rejected");
    Expect(!audio.Load("bad\"path.mp3"), "a quoted source is rejected");
    Expect(!audio.Play(), "play before load is rejected");
    Expect(!audio.Pause(), "pause before load is rejected");
    Expect(!audio.Seek(1), "seek before load is rejected");
    Expect(audio.Duration() == 0.0 && audio.Position() == 0.0 && !audio.Playing(),
        "an unloaded engine exposes an empty state");
    audio.Stop();
    Expect(audio.SetVolume(0.5) && audio.SetMuted(true), "controls before load are accepted");
    Expect(!audio.SetVolume(-1) && !audio.SetVolume(2) &&
        !audio.SetVolume(std::numeric_limits<double>::quiet_NaN()), "invalid volume is rejected");
    const auto directory = std::filesystem::temp_directory_path() /
        ("lume-decoded-audio-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto source = directory / "tone.wav";
    WriteWave(source, false);
    Expect(audio.Load(source.string()), "Media Foundation decodes a real PCM WAV");
    Expect(audio.Duration() > 2.99 && audio.Duration() < 3.01, "decoded duration is accurate");
    Expect(audio.Analysis().energy == 0 && audio.Play(), "ready analysis is empty and real output starts");
    Expect(Eventually([&] { return audio.Position() > 0.02; }), "output device advances playback position");
    Expect(audio.Analysis().energy == 0, "preload mute survives load");
    Expect(audio.SetMuted(false) && audio.SetVolume(1), "native unmute and volume reach output");
    Expect(Eventually([&] {
        const auto analysis = audio.Analysis();
        const auto peak = std::max_element(analysis.bins.begin(), analysis.bins.end()) - analysis.bins.begin();
        return analysis.energy > 0.3 && analysis.energy < 0.4 && peak == 7;
    }), "decoded 1 kHz PCM produces measured RMS and its real frequency bin");
    Expect(audio.SetVolume(0.25), "native gain changes");
    Expect(Eventually([&] { const auto energy = audio.Analysis().energy; return energy > 0.07 && energy < 0.10; }),
        "analysis follows actual output gain");
    Expect(audio.Pause() && audio.Analysis().energy == 0, "pause drops stale analysis");
    const auto paused = audio.Position();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    Expect(std::abs(audio.Position() - paused) < 0.02, "pause stops the output clock");
    Expect(audio.Seek(1.2) && audio.Analysis().energy == 0 && audio.Play(), "seek drops old buffers");
    Expect(Eventually([&] { return audio.Position() > 1.25; }), "seeked silence actually plays");
    Expect(audio.Analysis().energy == 0, "decoded silence never produces fabricated energy");
    Expect(audio.Seek(2.2) && audio.SetVolume(1) && audio.Play(), "seek to second frequency succeeds");
    Expect(Eventually([&] {
        const auto analysis = audio.Analysis();
        const auto peak = std::max_element(analysis.bins.begin(), analysis.bins.end()) - analysis.bins.begin();
        return analysis.energy > 0.3 && peak == 15;
    }), "seeking to 2 kHz replaces the old frequency bins");
    Expect(Eventually([&] { return !audio.Playing(); }), "decoded EOF drains real output");
    Expect(audio.Position() > 2.99 && audio.Analysis().energy == 0, "EOF clears analysis at the real duration");
    WriteWave(source, true);
    Expect(audio.Load(source.string()) && audio.Play(), "8-bit WAV is converted into decoded 16-bit output");
    Expect(Eventually([&] { return audio.Analysis().energy > 0.3; }), "8-bit input yields real analysis");
    const auto surround = directory / "surround.wav";
    WriteSurroundWave(surround);
    Expect(audio.Load(surround.string()) && audio.SetVolume(1) && audio.SetMuted(false) && audio.Play(),
        "5.1 PCM loads and is folded to stereo instead of being rejected");
    Expect(Eventually([&] { return audio.Position() > 0.1 && audio.Analysis().energy > 0.1; }),
        "downmixed surround produces real advancing stereo output");
    Expect(audio.Seek(1.0) && audio.Play() && Eventually([&] { return audio.Position() > 1.05; }),
        "surround seek resumes after the decode thread applies the target");
    Expect(!audio.Load("bad\"path.mp3") && !audio.Playing() && audio.Analysis().energy == 0,
        "failed replacement cannot retain playback or analysis");
    audio.Stop();
    bool largeSample = false;
    const bool seekFixtures = argc > 1 && std::string(argv[1]) == "--seek-fixtures";
    for (int i = seekFixtures ? 2 : 1; i < argc; ++i) {
        const auto input = std::filesystem::path(argv[i]);
        if (seekFixtures) {
            Expect(audio.Load(input.string()) && audio.SetMuted(true) && audio.Prepare(1.2, false),
                "fresh paused candidate begins preroll without playing");
            Expect(Eventually([&] { return audio.Ready(); }) && !audio.Playing(),
                "candidate reports ready only after paused audio is decoded and queued");
            const auto buffered = audio.Position();
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            Expect(std::abs(audio.Position() - buffered) < 0.002,
                "preparation cannot move a paused song or output audio");
            Expect(audio.SetMuted(false) && audio.Play() && Eventually([&] { return audio.Position() > 1.25; }) &&
                audio.Analysis().energy < 0.002, "prepared candidate starts with target silence");
            Expect(audio.Load(input.string()) && audio.SetMuted(false) && audio.Play(), "seek fixture loads");
            Expect(Eventually([&] { return audio.Position() > 0.05 && audio.Analysis().energy > 0.01; }), "fixture starts with a tone");
            Expect(audio.Load(input.string()), "reload for quality candidate seek");
            Expect(audio.Seek(1.2) && audio.Play(), "compressed fixture seeks into silence");
            Expect(Eventually([&] { return audio.Position() > 1.25; }), "compressed seek advances");
            std::cout << input.filename().string() << " seekPosition=" << audio.Position()
                      << " energy=" << audio.Analysis().energy << std::endl;
            Expect(audio.Analysis().energy < 0.002, "compressed seek must play target silence, not the opening tone");
            Expect(audio.Load(input.string()) && audio.Seek(2.2), "fresh quality candidate keeps a paused seek target");
            Expect(!audio.Playing() && audio.Analysis().energy == 0, "paused quality candidate stays silent");
            Expect(audio.Play(), "compressed candidate resumes at second tone");
            Expect(Eventually([&] {
                const auto analysis = audio.Analysis();
                const auto peak = std::max_element(analysis.bins.begin(), analysis.bins.end()) - analysis.bins.begin();
                return audio.Position() > 2.25 && analysis.energy > 0.01 && peak == 2;
            }), "compressed candidate plays the 2 kHz target, not the 1 kHz opening tone");
            audio.Stop();
            continue;
        }
        const auto largest = LargestSample(input);
        largeSample |= largest > 64 * 1024;
        std::cout << input.filename().string() << " decodedMaxSampleBytes=" << largest << '\n';
        Expect(largest > 0 && largest <= 4 * 1024 * 1024, "real format stays within bounded sample budget");
        Expect(audio.Load(input.string()) && audio.Play(), "generated format loads and plays");
        Expect(Eventually([&] { return audio.Position() > 0.10 && audio.Analysis().energy > 0.01; }),
            "generated format produces real advancing decoded audio");
        Expect(audio.Pause() && audio.Analysis().energy == 0, "format pause clears analysis");
        Expect(audio.Seek(0.2) && audio.Play(), "format seeks with fresh sliced buffers");
        Expect(Eventually([&] { return audio.Position() > 0.3 && audio.Analysis().energy > 0.01; }),
            "format seeks to real measured samples");
        audio.Stop();
        Expect(audio.Analysis().energy == 0, "format teardown clears samples");
    }
    if (argc > 1 && !seekFixtures) Expect(largeSample, "multiformat fixtures reproduce a legitimate sample over 64KiB");
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::cout << "music audio engine validation passed\n";
    return 0;
}
