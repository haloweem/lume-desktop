#include "platform/windows/dynamic_island/island_weather_page.h"
#include "platform/windows/widget_ui/text_fit.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace lume::platform::windows {

// The island draws its own coarse weather glyphs from canvas primitives, so
// the page does not depend on the weather widget's RCDATA icons.
void DrawIslandWeatherGlyph(WidgetCanvas& c, float cx, float cy, float size, int code, bool day, float alpha) {
    const float r = size * .5f;
    const bool thunder = code >= 95 && code <= 99;
    const bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
    const bool rain = !snow && ((code >= 51 && code <= 67) || (code >= 80 && code <= 82));
    const bool fog = code == 45 || code == 48;
    const bool cloud = code == 1 || code == 2 || code == 3 || rain || snow || thunder || fog;
    if(code!=0&&!cloud){
        c.Text(L"?",{cx-r,cy-r*.74f,size,size*.8f},size*.62f,0xffffff,alpha*.6f,
               WidgetTextWeight::Medium,WidgetTextAlign::Center);
        return;
    }
    const float sunX = cx - (cloud ? r * .22f : 0.f), sunY = cloud ? cy - r * .18f : cy;
    if (code <= 2) {
        c.AnnularSector(sunX, sunY, r * .30f, r * .42f, 0, 360, 0xffffff, alpha);
        if (day) {
            for (int i = 0; i < 8; ++i) {
                const float a = static_cast<float>(i) * 3.14159265f / 4.f;
                const float x = sunX + std::cos(a) * r * .52f, y = sunY + std::sin(a) * r * .52f;
                c.Line(x, y, x + std::cos(a) * r * .14f, y + std::sin(a) * r * .14f, 0xffffff, alpha, 1.6f);
            }
        } else {
            // A ring alone reads as a letter; a few stars make it night.
            c.AnnularSector(sunX + r * .46f, sunY - r * .40f, 0, r * .05f, 0, 360, 0xffffff, alpha * .9f);
            c.AnnularSector(sunX + r * .56f, sunY + r * .12f, 0, r * .035f, 0, 360, 0xffffff, alpha * .75f);
            c.AnnularSector(sunX - r * .30f, sunY - r * .52f, 0, r * .04f, 0, 360, 0xffffff, alpha * .8f);
        }
    }
    if (!cloud) return;
    // Cloud body: three lobes and a rounded base.
    const float bodyY = cy + r * .08f;
    c.AnnularSector(cx - r * .34f, bodyY, 0, r * .26f, 0, 360, 0xffffff, alpha);
    c.AnnularSector(cx + r * .04f, bodyY - r * .16f, 0, r * .30f, 0, 360, 0xffffff, alpha);
    c.AnnularSector(cx + r * .36f, bodyY, 0, r * .22f, 0, 360, 0xffffff, alpha);
    c.Rounded({cx - r * .56f, bodyY, r * 1.12f, r * .30f}, r * .15f, 0xffffff, alpha);
    if (fog) {
        for (int i = 0; i < 3; ++i)
            c.Line(cx - r * .48f, cy + r * .40f + static_cast<float>(i) * r * .16f,
                   cx + r * .48f, cy + r * .40f + static_cast<float>(i) * r * .16f, 0xffffff, alpha * .8f, 1.5f);
        return;
    }
    if (thunder) {
        c.Line(cx + r * .06f, cy + r * .30f, cx - r * .08f, cy + r * .56f, 0xffd60a, alpha, 2.2f);
        c.Line(cx - r * .08f, cy + r * .56f, cx + r * .08f, cy + r * .54f, 0xffd60a, alpha, 2.2f);
        c.Line(cx + r * .08f, cy + r * .54f, cx - r * .04f, cy + r * .78f, 0xffd60a, alpha, 2.2f);
        return;
    }
    // Only precipitating conditions draw anything below the cloud.
    if (!rain && !snow) return;
    for (int i = 0; i < 3; ++i) {
        const float x = cx - r * .26f + static_cast<float>(i) * r * .26f;
        if (snow) c.AnnularSector(x, cy + r * .58f, 0, r * .055f, 0, 360, 0xffffff, alpha);
        else c.Line(x, cy + r * .40f, x - r * .10f, cy + r * .62f, 0xffffff, alpha * .85f, 1.6f);
    }
}
void DrawIslandWeatherPage(WidgetCanvas& c, const dynamic_island::Weather& weather, float w, float h, float alpha) {
    if (alpha <= .01f) return;
    if (!weather.available) {
        c.Text(L"--°",{0,h/2-51,w,62},48,0xffffff,.92f*alpha,WidgetTextWeight::Medium,WidgetTextAlign::Center);
        c.Text(weather.failed?L"天气暂不可用":L"正在获取天气",{24,h/2+22,w-48,20},12,0xffffff,
               .5f*alpha,WidgetTextWeight::Normal,WidgetTextAlign::Center);
        return;
    }
    // A compact overview above a single row of six hourly forecasts.
    c.Text(FitWeightedText(c,weather.city.empty()?L"当前位置":weather.city,w-52,13,WidgetTextWeight::Semibold),
           {26,17,w-52,20},13,0xffffff,.88f*alpha,WidgetTextWeight::Semibold);
    const auto temp=std::to_wstring(weather.temperature)+L"°";
    const float tempWidth=c.MeasureText(temp,52,WidgetTextWeight::Medium);
    c.Text(temp,{24,40,tempWidth+4,70},52,0xffffff,alpha,WidgetTextWeight::Medium);
    const float detailX=std::max(136.f,28+tempWidth),detailWidth=std::max(0.f,w-114-detailX);
    c.Text(FitWeightedText(c,weather.condition.empty()?L"--":weather.condition,detailWidth,12,WidgetTextWeight::Normal),
           {detailX,58,detailWidth,18},12,0xffffff,.88f*alpha);
    if (weather.hasRange) {
        const auto range=L"↑ "+std::to_wstring(weather.high)+L"°   ↓ "+std::to_wstring(weather.low)+L"°";
        c.Text(FitWeightedText(c,range,detailWidth,10,WidgetTextWeight::Normal),{detailX,81,detailWidth,16},10,0xffffff,.5f*alpha);
    }
    DrawIslandWeatherGlyph(c,w-65,73,66,weather.code,weather.isDay,alpha);
    c.Line(26,119,w-26,119,0xffffff,.10f*alpha,1);
    const std::size_t count=std::min(weather.hours,weather.hourly.size());
    const float column=(w-44)/6;
    for(std::size_t i=0;i<6;++i){
        const float x=22+static_cast<float>(i)*column,cx=x+column/2;
        const auto* hour=i<count?&weather.hourly[i]:nullptr;
        c.Text(hour?hour->label:L"--",{x,129,column,15},10,0xffffff,.48f*alpha,
               WidgetTextWeight::Normal,WidgetTextAlign::Center);
        if(hour)DrawIslandWeatherGlyph(c,cx,157,23,hour->code,hour->isDay,alpha*.9f);
        c.Text(hour?std::to_wstring(hour->temperature)+L"°":L"--",{x,174,column,18},12,0xffffff,.93f*alpha,
               WidgetTextWeight::Medium,WidgetTextAlign::Center);
    }
}

}  // namespace lume::platform::windows
