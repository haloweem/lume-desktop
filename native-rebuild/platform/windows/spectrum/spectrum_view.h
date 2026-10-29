#pragma once
#include "core/spectrum/spectrum.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <vector>

namespace lume::platform::windows {
// 样式二: thin solid bars in the style's 组件颜色 with a faded reflection;
// `levels` are 0..1 per bar. Silent bars leave a short stub. Draws nothing behind.
void DrawSpectrumBlocks(WidgetCanvas&,float width,float height,const std::vector<float>& levels,
                        const spectrum::BarStyle&);
// 样式三: rainbow spindles (green to pink) mirrored about the middle line;
// silent bars rest as short pills.
void DrawSpectrumWaves(WidgetCanvas&,float width,float height,const std::vector<float>& levels,
                       const spectrum::BarStyle&);
// 样式一: short rainbow blocks stacked on a hairline, pink through orange, with
// a shorter faded reflection. Silent bars leave only the hairline.
void DrawSpectrumPulse(WidgetCanvas&,float width,float height,const std::vector<float>& levels,
                       const spectrum::BarStyle&);
// Where the spectrum takes the mouse: a low strip along the line the bars
// rise from (the 70% baseline, or the middle line of 样式三), as wide as the
// row of bars. It depends on the layout only, never on the audio, so the
// widget stays grabbable when silent and tall bars never block what is below.
WidgetRect SpectrumGrip(spectrum::Style,float width,float height,const spectrum::BarStyle&);
}
