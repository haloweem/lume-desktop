#pragma once

#include "core/music/music_types.h"

#include <string>

namespace lume::music {

// Playback contract shared by the host runtime and platform implementations.
// It carries no host process, wire protocol or Windows ownership.
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

}  // namespace lume::music
