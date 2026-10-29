#pragma once
#include "core/settings/file_store.h"
#include "core/widget/widget_preferences_store.h"
#include <complex>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// Audio spectrum widget: a row of rainbow bars drawn from what the computer
// is playing (system loopback). Pure model, settings and analysis only.
namespace lume::spectrum {
inline constexpr widget::Limits WindowLimits{880,300,200,60,2640,600};
inline constexpr int MinBarWidth=2,MaxBarWidth=24;
inline constexpr int MinBarGap=0,MaxBarGap=12;
// 组件大小: percent of the default width; the current aspect ratio is kept.
inline constexpr int MinScale=50,MaxScale=300;

// Each look keeps its own settings, so switching styles never loses the
// other one's tuning. A new style adds an enum value, a BarStyle member and
// its name below.
enum class Style { Pulse, Blocks, Waves };  // 样式一, 样式二, 样式三
struct BarStyle {
    int barWidth=9;  // DIP
    int barGap=1;    // DIP between bars (and between the blocks of a bar)
    // 组件颜色 (RGB and opacity), as the clock widget's. Only 样式二 uses it;
    // 样式一 keeps its rainbow.
    int color=0xffffff,alpha=255;
};
struct Preferences {
    widget::Preferences window=widget::Defaults(WindowLimits);
    Style style=Style::Pulse;
    BarStyle pulse{9,1};   // flat blocks on a hairline, the line alone when silent
    BarStyle blocks{4,1};  // thin solid bars in one colour over a reflection
    BarStyle waves{5,1};   // rainbow spindles mirrored about a centre line
    BarStyle& Current(){return style==Style::Pulse?pulse:style==Style::Blocks?blocks:waves;}
    const BarStyle& Current()const{return style==Style::Pulse?pulse:style==Style::Blocks?blocks:waves;}
};
const char* StyleName(Style);
int ScalePercent(const widget::Preferences&);
// Window bounds at `percent` of the default width, same aspect, within limits.
widget::Preferences Scaled(widget::Preferences,int percent);
bool DecodePreferences(const std::string&,Preferences&);
bool EncodePreferences(const Preferences&,std::string&);
struct LoadedPreferences {
    Preferences value;
    widget::LoadState state=widget::LoadState::Fresh;
};
class PreferencesStore final {
public:
    explicit PreferencesStore(settings::IFileStore& files):files_(files){}
    LoadedPreferences Load(const std::string&) const;
    bool Save(const std::string&,const Preferences&);
private:
    settings::IFileStore& files_;
};

// Bars that fit a widget of `width` DIP with the given bar width and gap.
std::size_t BarCount(float width,int barWidth,int barGap);

// Frame size of the analysis window; samples are mono.
inline constexpr std::size_t FrameSize=8192;
// Magnitudes of the first FrameSize/2 FFT bins of a Hann-windowed frame.
std::vector<float> Magnitudes(const std::vector<float>& frame);
// The analysis follows cava's core, the engine of the reference look.
// FFT bins [first,last] read by band `index`: log-spaced from 50 Hz to
// 10 kHz, except that every band takes at least one bin of its own, so low
// bands never repeat the same bin as a flat plateau.
std::pair<std::size_t,std::size_t> BandBins(std::size_t index,std::size_t count,unsigned sampleRate,std::size_t bins);
// Mean linear magnitude of each band's bins times its frequency (cava's
// equaliser), unscaled: Motion turns these into 0..1 heights.
std::vector<float> Bands(const std::vector<float>& magnitudes,unsigned sampleRate,std::size_t count);
// cava's stereo layout, as in the reference: the bass of both channels meets
// in the middle and the treble spreads to the edges. The left half is the
// left channel mirrored (highest band at the left edge), the right half the
// right channel; an odd extra bar goes to the right.
std::vector<float> StereoBands(const std::vector<float>& left,const std::vector<float>& right,
                               unsigned sampleRate,std::size_t count);
// cava's smoothing per frame: auto sensitivity keeps the tallest bars just
// under the top, a falling bar drops under gravity (slow, then faster), and
// an integral filter (noise reduction 0.77) blends in the previous frames.
class Motion final {
public:
    // Writes 0..1 heights into `shown`; state restarts when the count changes.
    void Step(std::vector<float>& shown,const std::vector<float>& bands,float seconds);
private:
    std::vector<float> peak_,fall_,memory_,previous_;
    float sensitivity_=1;bool settling_=true;
};
}
