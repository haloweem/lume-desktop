#pragma once

#include "apps/music-host/music_host_runtime.h"

#include <string>
#include <memory>

namespace lume::music {

class WindowsAudioEngine final : public IAudioEngine {
public:
    WindowsAudioEngine();
    ~WindowsAudioEngine() override;

    bool Load(const std::string& source) override;
    bool Play() override;
    bool Pause() override;
    bool Seek(double position) override;
    bool Prepare(double position, bool follow) override;
    bool Ready() const override;
    void Stop() override;
    double Position() const override;
    double Duration() const override;
    bool Playing() const override;
    bool SetVolume(double volume) override;
    bool SetMuted(bool muted) override;
    AudioAnalysis Analysis() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lume::music
