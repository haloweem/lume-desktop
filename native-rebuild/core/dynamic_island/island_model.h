#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace lume::dynamic_island {
// One timed lyric line: seconds into the track; end is the next line's start
// when the line has no own end.
struct LyricCue { double start = 0, end = 0; std::wstring text; };
using LyricCues = std::shared_ptr<const std::vector<LyricCue>>;
// Public projection only: no file paths, credentials, playback host or Dock types.
struct Media {
    std::uint64_t session = 0;
    std::string id;
    std::string artwork;
    std::wstring title, artist;
    bool available = false, playing = false;
    double position = 0, duration = 0;
    float energy = 0;
    std::array<float, 16> spectrum{};
    std::string app;  // UTF-8 AppUserModelID of the playing program; empty for Lume's own player.
    // Timed lines of the track, shared and immutable; null without lyrics or
    // when position is not the true playback position. The island picks the
    // line itself from position, carried forward while playing.
    LyricCues lyrics;
};
enum class Action { Toggle, Previous, Next, Focus, CancelFocus, Settings, Close, Tick, ExpansionChanged,
                    MaterialNone, MaterialLiquid, MaterialFrosted, Reset, WeatherSettings, ToggleMessages };

// Current weather as the island needs it: a bounded projection of the weather
// component's reading. The island never fetches, locates or stores weather.
struct Weather {
    struct Hour {std::wstring label;int temperature=0,code=999;bool isDay=true;};
    bool enabled = false;    // the weather component is running
    bool available = false;  // a reading has arrived
    std::wstring city;       // display name, may already include the district
    std::wstring condition;  // provider condition text
    int temperature = 0;
    bool hasRange = false;   // today's forecast range is known
    int high = 0, low = 0;
    int code = 999;          // WMO weather code, for the glyph
    bool isDay = true;
    bool failed = false;
    std::array<Hour,6> hourly{};
    std::size_t hours = 0;
};
class Model final {
public:
    void Update(Media);
    const Media& Playback() const { return media_; }
    void StartFocus(std::uint64_t now);
    void CancelFocus();
    bool Tick(std::uint64_t now);
    unsigned RemainingSeconds() const { return remaining_; }
    bool Focusing() const { return deadline_ != 0; }
    bool Finished() const { return finished_; }
private:
    Media media_;
    std::uint64_t deadline_ = 0;
    unsigned remaining_ = 0;
    bool finished_ = false;
};
}
