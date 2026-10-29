#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lume::platform::windows {
// CSS: 0 8px 24px rgba(0,0,0,.18), border-radius:8px. CSS blur radius
// corresponds to Gaussian sigma=blur/2. Pixels are premultiplied BGRA.
struct CalendarCssShadow {
    int width=0,height=0,padding=0;
    std::vector<std::uint32_t> pixels;
};
inline float CalendarRoundedCoverage(float x,float y,float width,float height,float radius) {
    if(width<=0||height<=0)return 0;
    radius=std::min({radius,width*.5f,height*.5f});
    const float dx=std::abs(x-width*.5f)-(width*.5f-radius);
    const float dy=std::abs(y-height*.5f)-(height*.5f-radius);
    const float distance=std::hypot(std::max(dx,0.f),std::max(dy,0.f))+
        std::min(std::max(dx,dy),0.f)-radius;
    return std::clamp(.5f-distance,0.f,1.f);
}
inline CalendarCssShadow MakeCalendarCssShadow(int width,int height,float scale) {
    CalendarCssShadow result;
    const float sigma=12.f*scale,offset=8.f*scale,radius=8.f*scale;
    const int reach=static_cast<int>(std::ceil(3*sigma));
    result.padding=reach+static_cast<int>(std::ceil(offset))+1;
    result.width=width+2*result.padding;result.height=height+2*result.padding;
    const auto count=static_cast<std::size_t>(result.width)*result.height;
    std::vector<float> mask(count),horizontal(count),kernel(2*reach+1);
    float sum=0;
    for(int i=-reach;i<=reach;++i){kernel[i+reach]=std::exp(-i*i/(2*sigma*sigma));sum+=kernel[i+reach];}
    for(auto& value:kernel)value/=sum;
    for(int y=0;y<result.height;++y)for(int x=0;x<result.width;++x)
        mask[static_cast<std::size_t>(y)*result.width+x]=CalendarRoundedCoverage(
            x+.5f-result.padding,y+.5f-result.padding-offset,static_cast<float>(width),static_cast<float>(height),radius);
    for(int y=0;y<result.height;++y)for(int x=0;x<result.width;++x) {
        float value=0;for(int k=-reach;k<=reach;++k)if(x+k>=0&&x+k<result.width)
            value+=mask[static_cast<std::size_t>(y)*result.width+x+k]*kernel[k+reach];
        horizontal[static_cast<std::size_t>(y)*result.width+x]=value;
    }
    result.pixels.resize(count);
    for(int y=0;y<result.height;++y)for(int x=0;x<result.width;++x) {
        float value=0;for(int k=-reach;k<=reach;++k)if(y+k>=0&&y+k<result.height)
            value+=horizontal[static_cast<std::size_t>(y+k)*result.width+x]*kernel[k+reach];
        // Outer box shadows are clipped out of the original border box.
        const float interior=CalendarRoundedCoverage(x+.5f-result.padding,y+.5f-result.padding,
            static_cast<float>(width),static_cast<float>(height),radius);
        result.pixels[static_cast<std::size_t>(y)*result.width+x]=
            static_cast<std::uint32_t>(std::lround(value*(1-interior)*.18f*255))<<24;
    }
    return result;
}
// CSS inset 0 1px 0 white14%, clipped inside the one-DIP border.
inline float CalendarCssInsetCoverage(float x,float y,float width,float height,float scale) {
    const float border=scale,radius=std::max(0.f,7.f*scale);
    const float a=CalendarRoundedCoverage(x-border,y-border,width-2*border,height-2*border,radius);
    const float b=CalendarRoundedCoverage(x-border,y-border-scale,width-2*border,height-2*border,radius);
    return a*(1-b)*.14f;
}
}
