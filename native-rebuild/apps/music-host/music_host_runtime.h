#pragma once

#include "core/music/audio_engine.h"
#include "core/music/music_host_protocol.h"

#include <optional>
#include <string>
#include <string_view>

namespace lume::music {

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
