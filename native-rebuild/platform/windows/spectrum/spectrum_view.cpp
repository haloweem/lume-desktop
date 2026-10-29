#include "platform/windows/spectrum/spectrum_view.h"
#include <algorithm>
#include <cmath>
namespace lume::platform::windows {
namespace {
std::uint32_t Hue(float degrees,float saturation=.78f){
    degrees=std::fmod(degrees,360.f);if(degrees<0)degrees+=360;
    const float s=saturation,v=1.f,c=v*s,x=c*(1-std::fabs(std::fmod(degrees/60,2.f)-1)),m=v-c;
    float r=0,g=0,b=0;
    if(degrees<60){r=c;g=x;}else if(degrees<120){r=x;g=c;}else if(degrees<180){g=c;b=x;}
    else if(degrees<240){g=x;b=c;}else if(degrees<300){r=x;b=c;}else{r=c;b=x;}
    const auto byte=[&](float value){return static_cast<std::uint32_t>(std::lround((value+m)*255));};
    return byte(r)<<16|byte(g)<<8|byte(b);
}
}
void DrawSpectrumBlocks(WidgetCanvas& canvas,float width,float height,const std::vector<float>& levels,
                        const spectrum::BarStyle& style){
    if(levels.empty())return;
    // As in the reference: thin solid bars in the 组件颜色, rising from a
    // common baseline at 70%, over a reflection that reaches 40% as far
    // below and fades out. A resting bar keeps a 2 px stub.
    const float bar=static_cast<float>(style.barWidth),gap=static_cast<float>(style.barGap),pitch=bar+gap;
    const float baseline=std::floor(height*.7f),room=std::max(0.f,baseline-4),stub=2;
    const float total=static_cast<float>(levels.size())*pitch-gap,left=std::floor((width-total)/2);
    const auto color=static_cast<std::uint32_t>(style.color);
    const float alpha=static_cast<float>(style.alpha)/255.f;
    for(std::size_t i=0;i<levels.size();++i){
        const float x=left+static_cast<float>(i)*pitch;
        const float lit=std::max(stub,std::clamp(levels[i],0.f,1.f)*room);
        canvas.Rounded({x,baseline-lit,bar,lit},0,color,alpha*.95f);
        // The reflection fades in steps, darkest farthest from the baseline.
        const float mirror=std::min(lit*.4f,height-baseline-1);
        constexpr int Steps=6;
        for(int s=0;s<Steps;++s){
            const float y0=mirror*static_cast<float>(s)/Steps,y1=mirror*static_cast<float>(s+1)/Steps;
            if(y1-y0<.25f)continue;
            canvas.Rounded({x,baseline+1+y0,bar,y1-y0},0,color,
                           alpha*.38f*(1-static_cast<float>(s)/Steps));
        }
    }
}
void DrawSpectrumPulse(WidgetCanvas& canvas,float width,float height,const std::vector<float>& levels,
                       const spectrum::BarStyle& style){
    if(levels.empty())return;
    const float bar=static_cast<float>(style.barWidth),gap=static_cast<float>(style.barGap),pitch=bar+gap;
    // As in the reference: a 1 px line and square blocks on a fixed grid. The
    // height is continuous and the top block is cut to it, so a bar glides
    // instead of jumping a whole block at a time.
    const float line=1,rowGap=std::max(1.f,gap),row=bar+rowGap;
    // The hairline sits at 70%: bars rise above it, the reflection reaches 40% as far below.
    const float baseline=std::floor(height*.7f),room=std::max(0.f,baseline-line-4);
    const float total=static_cast<float>(levels.size())*pitch-gap,left=std::floor((width-total)/2);
    // Cells of `extent` px from `origin`, growing up (-1) or down (+1).
    const auto cells=[&](float x,float origin,float extent,int direction,std::uint32_t color,float alpha,float fade){
        for(int r=0;static_cast<float>(r)*row<extent;++r){
            const float size=std::min(bar,extent-static_cast<float>(r)*row);
            if(size<.5f)break;
            const float y=direction<0?origin-static_cast<float>(r)*row-size:origin+static_cast<float>(r)*row;
            canvas.Rounded({x,y,bar,size},0,color,alpha*std::pow(fade,static_cast<float>(r)));
        }
    };
    for(std::size_t i=0;i<levels.size();++i){
        const float t=levels.size()>1?static_cast<float>(i)/static_cast<float>(levels.size()-1):0;
        // Pink, violet, blue, cyan, green, yellow, then orange.
        const auto color=Hue(330-t*300,.88f);
        const float x=left+static_cast<float>(i)*pitch;
        canvas.Rounded({x,baseline-line,bar,line},0,color,.95f);
        const float lit=std::clamp(levels[i],0.f,1.f)*room;
        cells(x,baseline-line-rowGap,lit,-1,color,.95f,1);
        cells(x,baseline+rowGap,std::min(lit*.4f,height-baseline-rowGap),1,color,.3f,.7f);
    }
}
}
namespace lume::platform::windows {
WidgetRect SpectrumGrip(spectrum::Style style,float width,float height,const spectrum::BarStyle& bars){
    const auto count=spectrum::BarCount(width,bars.barWidth,bars.barGap);
    if(!count||height<=0)return {};
    const float pitch=static_cast<float>(bars.barWidth+bars.barGap);
    const float total=static_cast<float>(count)*pitch-static_cast<float>(bars.barGap),left=std::floor((width-total)/2);
    // The same lines the drawings rise from; the strip reaches a little
    // above (the resting bars) and below (the start of the reflection).
    const float line=style==spectrum::Style::Waves?std::floor(height/2):std::floor(height*.7f);
    constexpr float Above=14,Below=8;
    const float top=std::max(0.f,line-Above),bottom=std::min(height,line+Below);
    return {left,top,total,std::max(0.f,bottom-top)};
}
void DrawSpectrumWaves(WidgetCanvas& canvas,float width,float height,const std::vector<float>& levels,
                       const spectrum::BarStyle& style){
    if(levels.empty())return;
    // As in the reference: slim spindles mirrored about the middle line,
    // green through cyan, blue and violet to pink. A resting bar is a short
    // pill so the line of them never vanishes.
    const float bar=static_cast<float>(style.barWidth),gap=static_cast<float>(style.barGap),pitch=bar+gap;
    const float middle=std::floor(height/2),reach=std::max(0.f,middle-2),rest=bar*1.4f;
    const float total=static_cast<float>(levels.size())*pitch-gap,left=std::floor((width-total)/2);
    for(std::size_t i=0;i<levels.size();++i){
        const float t=levels.size()>1?static_cast<float>(i)/static_cast<float>(levels.size()-1):0;
        const auto color=Hue(130+t*190,.82f);
        const float span=std::max(rest,std::clamp(levels[i],0.f,1.f)*reach*2);
        canvas.Ellipse({left+static_cast<float>(i)*pitch,middle-span/2,bar,span},color,.95f);
    }
}
}
