#pragma once
#include <algorithm>
#include <cmath>

namespace lume::platform::windows {
struct MediaTimelineReading {
    double position = 0, duration = 0;
    bool known = false;
};

// SMTC's default timestamp is the Windows epoch, not the start of a song.
// Total length alone does not make that an advancing playback timeline.
inline MediaTimelineReading ReadMediaTimeline(double duration, double position,
    double timestampAge, bool timestampPresent, bool playing) {
    MediaTimelineReading result;
    if (std::isfinite(duration) && duration > 0 && duration <= 604800) result.duration = duration;
    if (!timestampPresent || !std::isfinite(timestampAge) || timestampAge < -1 ||
        !std::isfinite(position) || position < 0 || position > 604800) return result;
    if (playing) position += (std::max)(0.0, timestampAge);
    if (!std::isfinite(position) || position > 604800) return result;
    result.position = result.duration > 0 ? (std::min)(position, result.duration) : position;
    result.known = true;
    return result;
}
}
