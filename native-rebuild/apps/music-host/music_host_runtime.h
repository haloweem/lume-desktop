#pragma once

#include "core/music/music_host_protocol.h"

#include <optional>
#include <string>
#include <string_view>

namespace lume::music {

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;
    virtual bool Load(const std::string& source) = 0;
    virtual bool Play() = 0;
    virtual bool Pause() = 0;
    virtual bool Seek(double position) = 0;
    // Decode a paused candidate before it replaces a live source. Follow keeps
    // the target moving with that source while network preroll is discarded.
    virtual bool Prepare(double position, bool follow) = 0;
    virtual bool Ready() const = 0;
    virtual void Stop() = 0;
    virtual double Position() const = 0;
    virtual double Duration() const = 0;
    virtual bool Playing() const = 0;
    virtual bool SetVolume(double volume) = 0;
    virtual bool SetMuted(bool muted) = 0;
    virtual AudioAnalysis Analysis() const = 0;
};

class MusicHostRuntime {
public:
    explicit MusicHostRuntime(IAudioEngine& audio) : audio_(audio) {}

    std::optional<std::string> Handle(std::string_view frame);
    bool Closed() const noexcept { return closed_; }

private:
    std::string Event(std::uint64_t sequence, const char* status, const char* error = "", bool analysis = false) const;
    std::string Error(std::uint64_t sequence, const char* error) const;

    IAudioEngine& audio_;
    std::string session_;
    std::string source_;
    std::uint64_t lastSequence_ = 0;
    bool loaded_ = false;
    bool closed_ = false;
    double volume_ = 1.0;
    bool muted_ = false;
};

}  // namespace lume::music
