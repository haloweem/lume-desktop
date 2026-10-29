#include "apps/music-host/music_host_runtime.h"

#include <cstdlib>
#include <iostream>

namespace {
class FakeAudioEngine final : public lume::music::IAudioEngine {
public:
    bool Load(const std::string& source) override {
        loaded = source;
        duration = 30.0;
        position = 0.0;
        return source != "broken.mp3";
    }
    bool Play() override { ++playCalls; playing = true; return loaded.has_value(); }
    bool Pause() override { ++pauseCalls; playing = false; return loaded.has_value(); }
    bool Seek(double value) override { position = value; playing = false; return loaded.has_value(); }
    bool Prepare(double value, bool advance) override { follow = advance; ready = false; return Seek(value); }
    bool Ready() const override { return ready; }
    void Stop() override { ++stopCalls; playing = false; loaded.reset(); }
    double Position() const override { return position; }
    double Duration() const override { return duration; }
    bool Playing() const override { return playing; }
    bool SetVolume(double value) override { volume = value; return true; }
    bool SetMuted(bool value) override { muted = value; return true; }
    lume::music::AudioAnalysis Analysis() const override {
        ++analysisCalls;
        lume::music::AudioAnalysis result;
        result.energy = muted ? 0 : volume * 0.5;
        result.bins.fill(result.energy);
        return result;
    }

    std::optional<std::string> loaded;
    double position = 0.0;
    double duration = 0.0;
    bool playing = false;
    bool follow = false, ready = true;
    int playCalls = 0;
    int pauseCalls = 0;
    int stopCalls = 0;
    double volume = 1;
    bool muted = false;
    mutable int analysisCalls = 0;
};

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    FakeAudioEngine audio;
    lume::music::MusicHostRuntime runtime(audio);

    const auto hello = runtime.Handle(R"({"version":1,"session":"s1","sequence":1,"action":"hello"})");
    Expect(hello.has_value() && hello->find("\"status\":\"empty\"") != std::string::npos,
        "hello publishes an empty status");

    const auto load = runtime.Handle(R"({"version":1,"session":"s1","sequence":2,"action":"load","source":"song.mp3"})");
    Expect(load.has_value() && audio.loaded == "song.mp3", "load reaches the audio engine");
    Expect(load->find("\"status\":\"ready\"") != std::string::npos, "load publishes ready");

    runtime.Handle(R"({"version":1,"session":"s1","sequence":3,"action":"play"})");
    Expect(audio.playCalls == 1 && audio.playing, "play reaches the audio engine once");
    runtime.Handle(R"({"version":1,"session":"s1","sequence":4,"action":"seek","value":12.5})");
    Expect(audio.position == 12.5, "seek reaches the audio engine");
    Expect(audio.playing, "seek preserves playing intent even when the native engine stops on seek");
    runtime.Handle(R"({"version":1,"session":"s1","sequence":5,"action":"pause"})");
    Expect(audio.pauseCalls == 1 && !audio.playing, "pause reaches the audio engine once");

    const auto stale = runtime.Handle(R"({"version":1,"session":"s1","sequence":5,"action":"play"})");
    Expect(stale.has_value() && stale->find("\"error\":\"sequence\"") != std::string::npos,
        "stale sequence is rejected");
    const auto otherSession = runtime.Handle(R"({"version":1,"session":"s2","sequence":6,"action":"play"})");
    Expect(otherSession.has_value() && otherSession->find("\"error\":\"session\"") != std::string::npos,
        "foreign session is rejected");

    const auto broken = runtime.Handle(R"({"version":1,"session":"s1","sequence":6,"action":"load","source":"broken.mp3"})");
    Expect(broken.has_value() && broken->find("\"error\":\"load-failed\"") != std::string::npos,
        "audio failures become fixed errors");
    Expect(!audio.playing && !audio.loaded, "failed replacement cannot retain previous or partially loaded audio");

    const auto close = runtime.Handle(R"({"version":1,"session":"s1","sequence":7,"action":"close"})");
    Expect(close.has_value() && audio.stopCalls == 4 && !audio.playing && !audio.loaded,
        "replacement, failed load and close release actual audio");
    Expect(!runtime.Handle(R"({"version":1,"session":"s1","sequence":8,"action":"play"})").has_value(),
        "commands after close are ignored");

    FakeAudioEngine longAudio;
    lume::music::MusicHostRuntime longRuntime(longAudio);
    lume::music::HostCommand longLoad;
    longLoad.session = "long-source";
    longLoad.sequence = 1;
    longLoad.action = "load";
    longLoad.source = std::string(180, 'a') + ".wav";
    const auto longReply = longRuntime.Handle(lume::music::EncodeHostCommand(longLoad));
    Expect(longReply && lume::music::DecodeHostEvent(*longReply),
        "authorized source beyond trackId hint limit still has a decodable ACK");
    longLoad.sequence = 2;
    longLoad.action = "seek";
    longLoad.hasValue = true;
    longLoad.value = 12;
    longRuntime.Handle(lume::music::EncodeHostCommand(longLoad));
    Expect(!longAudio.playing && longAudio.position == 12, "seek while paused does not start playback");
    longLoad.sequence = 3;
    longLoad.action = "prepare";
    longLoad.follow = true;
    const auto preparing = longRuntime.Handle(lume::music::EncodeHostCommand(longLoad));
    Expect(preparing && !lume::music::DecodeHostEvent(*preparing)->ready && !longAudio.playing && longAudio.follow,
        "preparation ACK exposes buffering and cannot start audible playback");
    longAudio.ready = true;
    const auto prepared = longRuntime.Handle(R"({"version":1,"session":"long-source","sequence":4,"action":"hello"})");
    Expect(prepared && lume::music::DecodeHostEvent(*prepared)->ready,
        "poll reports readiness only after the decoder is buffered");

    FakeAudioEngine controlled;
    lume::music::MusicHostRuntime controls(controlled);
    controls.Handle(R"({"version":1,"session":"c","sequence":1,"action":"setVolume","value":0.25})");
    controls.Handle(R"({"version":1,"session":"c","sequence":2,"action":"setMuted","value":true})");
    Expect(controlled.volume == 0.25 && controlled.muted, "controls can be set before load");
    controls.Handle(R"({"version":1,"session":"c","sequence":3,"action":"load","source":"tone.wav"})");
    controls.Handle(R"({"version":1,"session":"c","sequence":4,"action":"play"})");
    controls.Handle(R"({"version":1,"session":"c","sequence":5,"action":"hello"})");
    Expect(controlled.analysisCalls == 0, "ordinary host polling does not compute analysis");
    const auto silence = controls.Handle(R"({"version":1,"session":"c","sequence":6,"action":"hello","analysis":true})");
    Expect(silence && lume::music::DecodeHostEvent(*silence)->analysis.energy == 0, "muted analysis stays silent");
    controls.Handle(R"({"version":1,"session":"c","sequence":7,"action":"setMuted","value":false})");
    const auto measured = controls.Handle(R"({"version":1,"session":"c","sequence":8,"action":"hello","analysis":true})");
    Expect(measured && lume::music::DecodeHostEvent(*measured)->analysis.energy == 0.125 &&
        lume::music::DecodeHostEvent(*measured)->volume == 0.25, "analysis and controls come from the engine");
    controls.Handle(R"({"version":1,"session":"c","sequence":9,"action":"pause"})");
    const auto paused = controls.Handle(R"({"version":1,"session":"c","sequence":10,"action":"hello","analysis":true})");
    Expect(paused && lume::music::DecodeHostEvent(*paused)->analysis.energy == 0 && controlled.analysisCalls == 2,
        "pause clears analysis without sampling");

    std::cout << "music host runtime behavior passed\n";
    return 0;
}
