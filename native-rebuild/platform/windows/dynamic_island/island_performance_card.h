#pragma once
#include "platform/windows/dynamic_island/compact_widgets.h"
#include <algorithm>
#include <cmath>

namespace lume::platform::windows {
// Draws only supplied readings. Unknown counters remain unknown; the caller
// owns the existing shared performance sampler and power-status snapshot.
inline void DrawIslandPerformanceCard(WidgetCanvas& canvas,WidgetRect card,
    const std::optional<performance::Snapshot>& snapshot,const CompactData& power,bool english,float alpha=1.f) {
    if(alpha<=.01f||card.width<=0||card.height<=0)return;
    const auto reading=[](std::optional<double> value)->std::optional<double>{
        if(!value||!std::isfinite(*value))return std::nullopt;
        return std::clamp(*value,0.,100.);
    };
    const std::optional<double> values[]{reading(snapshot?snapshot->cpu:std::nullopt),
        reading(snapshot?snapshot->gpu:std::nullopt),reading(snapshot?snapshot->memory:std::nullopt)};
    const wchar_t* labels[]{L"CPU",L"GPU",L"MEM"};
    const std::uint32_t colors[]{0x36b9ef,0xd46df5,0xffb66e};
    const float scale=std::min({1.f,card.width/276.f,card.height/144.f});
    const float groupWidth=276*scale,left=card.x+(card.width-groupWidth)/2;
    const float groupHeight=(power.hasBattery?144.f:92.f)*scale;
    const float top=card.y+(card.height-groupHeight)/2;
    canvas.PushClip(card);
    for(unsigned index=0;index<3;++index){
        const float cx=left+(45+93*static_cast<float>(index))*scale,cy=top+51*scale;
        canvas.Text(labels[index],{cx-37*scale,top,74*scale,16*scale},10.5f*scale,0xffffff,.62f*alpha,
            WidgetTextWeight::Semibold,WidgetTextAlign::Center);
        canvas.AnnularSector(cx,cy,24*scale,35*scale,0,360,0xffffff,.12f*alpha);
        if(values[index]&&*values[index]>0){
            const float sweep=static_cast<float>(*values[index])*3.6f;
            canvas.AnnularSector(cx,cy,24*scale,35*scale,0,sweep,colors[index],alpha);
            if(sweep<360){
                const float angle=sweep*3.14159265358979323846f/180.f;
                const float endX=cx+std::sin(angle)*29.5f*scale,endY=cy-std::cos(angle)*29.5f*scale;
                canvas.Ellipse({cx-5.5f*scale,cy-35*scale,11*scale,11*scale},colors[index],alpha);
                canvas.Ellipse({endX-5.5f*scale,endY-5.5f*scale,11*scale,11*scale},colors[index],alpha);
            }
        }
        const auto value=values[index]?std::to_wstring(static_cast<unsigned>(std::lround(*values[index])))+L"%":L"—";
        canvas.Text(value,{cx-25*scale,cy-16*scale,50*scale,32*scale},17*scale,0xffffff,alpha,
            WidgetTextWeight::Bold,WidgetTextAlign::Center);
    }
    // BatteryFlag distinguishes battery-equipped machines from desktops.
    // A battery with temporarily unknown capacity still has an honest — row.
    if(power.hasBattery){
        const auto battery=reading(power.battery);
        const float batteryTop=top+94*scale;
        const bool charging=power.charging;
        const std::uint32_t batteryColor=charging?0x48d998:0x36b9ef;
        const auto status=charging?(english?L"Charging":L"充电中"):
            power.plugged?(battery&&*battery>=100?(english?L"Fully charged":L"电量已满"):
                (english?L"Plugged in":L"已接通电源")):(english?L"On battery":L"电池供电");
        canvas.Rounded({left,batteryTop,groupWidth,50*scale},12*scale,0xffffff,.055f*alpha);
        const auto value=battery?std::to_wstring(static_cast<unsigned>(std::lround(*battery)))+L"%":L"—";
        canvas.Text(value,{left+12*scale,batteryTop+1*scale,100*scale,30*scale},25*scale,0xffffff,alpha,WidgetTextWeight::Bold);
        if(charging){
            const float bx=left+115*scale,by=batteryTop+8*scale;
            canvas.Polygon({bx+8*scale,by,bx,by+12*scale,bx+6*scale,by+12*scale,
                bx+3*scale,by+21*scale,bx+14*scale,by+8*scale,bx+8*scale,by+8*scale},batteryColor,alpha);
        }
        canvas.Text(status,{left+136*scale,batteryTop+6*scale,128*scale,22*scale},10.5f*scale,
            charging?batteryColor:0xffffff,(charging?.9f:.65f)*alpha,WidgetTextWeight::Medium,WidgetTextAlign::Right);
        const float barX=left+12*scale,barY=batteryTop+38*scale,barWidth=groupWidth-24*scale;
        canvas.Rounded({barX,barY,barWidth,6*scale},3*scale,0xffffff,.13f*alpha);
        if(battery&&*battery>0)canvas.Rounded({barX,barY,barWidth*static_cast<float>(*battery/100.),6*scale},3*scale,batteryColor,alpha);
    }
    canvas.PopClip();
}
}
