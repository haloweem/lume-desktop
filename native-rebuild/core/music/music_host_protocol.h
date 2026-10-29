#pragma once

#include "core/music/music_types.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lume::music {

constexpr std::size_t kMaxMusicHostFrameBytes = 64 * 1024;

struct HostCommand {
    int version = 1;
    std::string session;
    std::uint64_t sequence = 0;
    std::string action;
    std::string source;
    double value = 0.0;
    bool hasValue = false;
    bool flag = false;
    bool analysis = false;
    bool follow = false;
};

struct HostEvent {
    int version = 1;
    std::string session;
    std::uint64_t sequence = 0;
    std::string status = "empty";
    std::string trackId;
    double position = 0.0;
    double duration = 0.0;
    bool playing = false;
    bool ready = true;
    bool canPrevious = false;
    bool canNext = false;
    bool canToggle = false;
    bool canSeek = false;
    std::string error;
    double volume = 1.0;
    bool muted = false;
    AudioAnalysis analysis;
};

std::optional<HostCommand> DecodeHostCommand(std::string_view text);
std::string EncodeHostCommand(const HostCommand& command);
std::optional<HostEvent> DecodeHostEvent(std::string_view text);
std::string EncodeHostEvent(const HostEvent& event);

}  // namespace lume::music
