#pragma once
#include "platform/windows/widget_ui/widget_canvas.h"
#include <algorithm>
#include <cmath>

namespace lume::platform::windows {
// Cached source pixels: retain hue and saturation, then fade all premultiplied
// channels together. The lower half is transparent, so no hard cut is visible.
inline WidgetBitmap MakeDockReflection(const WidgetBitmap& source){
    if(!source.width||!source.height||source.pixels.size()!=
        static_cast<std::size_t>(source.width)*source.height)return {};
    WidgetBitmap result{source.width,source.height,{}};result.pixels.resize(source.pixels.size());
    const float depth=std::max(1.f,static_cast<float>(source.height)*.5f-1.f);
    for(unsigned y=0;y<source.height;++y){
        const float t=std::clamp(static_cast<float>(y)/depth,0.f,1.f);
        const float opacity=.82f*(1-t*t*(3-2*t));
        const auto* input=source.pixels.data()+static_cast<std::size_t>(source.height-1-y)*source.width;
        auto* output=result.pixels.data()+static_cast<std::size_t>(y)*source.width;
        for(unsigned x=0;x<source.width;++x){
            const auto channel=[&](unsigned shift){
                return static_cast<std::uint32_t>(std::lround(((input[x]>>shift)&255)*opacity))<<shift;
            };
            output[x]=channel(0)|channel(8)|channel(16)|channel(24);
        }
    }
    return result;
}
}
