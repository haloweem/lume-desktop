#pragma once
#include "core/dock/dock_model.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <algorithm>
#include <cmath>

namespace lume::platform::windows {
// Keep the full icon-sized bitmap so drawing preserves its width; the ratio
// controls only how much of the mirrored image has nontransparent pixels.
inline WidgetBitmap MakeDockReflection(const WidgetBitmap& source, int ratio, dock::Edge edge = dock::Edge::Bottom){
    if(ratio<=0||!source.width||!source.height||source.pixels.size()!=
        static_cast<std::size_t>(source.width)*source.height)return {};
    const bool across = dock::Vertical(edge);
    const bool reverse = edge == dock::Edge::Top || edge == dock::Edge::Left;
    const unsigned extent = across ? source.width : source.height;
    const unsigned span = across ? source.height : source.width;
    const auto rows=std::max(1u,static_cast<unsigned>(std::lround(extent*(std::min(ratio,100)/100.0))));
    WidgetBitmap result{source.width,source.height,{}};result.pixels.resize(source.pixels.size());
    const float depth=std::max(1.f,static_cast<float>(rows)-1.f);
    for(unsigned y=0;y<rows;++y){
        const float t=static_cast<float>(y)/depth;
        const float opacity=1-t*t*(3-2*t);
        const unsigned input = reverse ? y : extent - 1 - y;
        const unsigned output = reverse ? extent - 1 - y : y;
        const auto index = [&](unsigned depthIndex, unsigned position) {
            return across ? static_cast<std::size_t>(position) * source.width + depthIndex :
                            static_cast<std::size_t>(depthIndex) * source.width + position;
        };
        for(unsigned x=0;x<span;++x){
            const auto pixel = source.pixels[index(input, x)];
            const auto channel=[&](unsigned shift){
                return static_cast<std::uint32_t>(std::lround(((pixel>>shift)&255)*opacity))<<shift;
            };
            result.pixels[index(output, x)]=channel(0)|channel(8)|channel(16)|channel(24);
        }
    }
    return result;
}
}
