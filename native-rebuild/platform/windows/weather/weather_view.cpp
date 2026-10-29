#include "platform/windows/weather/weather_view.h"
#include "core/weather/weather_places.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
namespace lume::platform::windows {
WeatherLayout WeatherLayoutFor(float w,float h){
    const bool spacious=w>=400&&h>=210;
    WeatherLayout out;out.left=spacious?25.f:17.f;out.top=spacious?25.f:18.f;
    out.contentWidth=w-2*out.left;out.hourWidth=(out.contentWidth-25)/6;out.hourHeight=h>=210?85.f:63.f;
    // The original grid overflows its 53px slot at the default 152px height.
    // Align-end cannot move the 63px item above the overview + 10px gap.
    out.hourTop=std::max(out.top+53,h-(spacious?39.f:28.f)-out.hourHeight);
    return out;
}
unsigned WeatherIconResource(int code,bool day){
    if(code<0)return 0;
    if(code==0)return day?6201:6202;
    if(code<=2)return day?6203:6204;
    if(code==3)return 6204;
    if(code<=48)return 6205;
    if(code<=67||(code>=80&&code<=82))return 6206;
    if(code<=77||(code>=85&&code<=86))return 6207;
    if(code>=95&&code<=99)return 6208;
    return 0;
}
std::wstring WeatherText(std::string_view source){
    if(source.empty()||source.size()>4096)return {};
    const auto simplified=weather::SimplifyWeatherPlaceName(source);
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,simplified.data(),static_cast<int>(simplified.size()),nullptr,0);
    if(size<=0)return {};
    std::wstring text(static_cast<std::size_t>(size),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,simplified.data(),static_cast<int>(simplified.size()),text.data(),size);
    return text;
}
std::wstring WeatherTemperature(double value){
    if(!std::isfinite(value)||value< -273||value>1000)return L"--";
    return std::to_wstring(static_cast<int>(std::floor(value+.5)));
}
std::wstring WeatherHourLabel(std::string_view value){
    if(value.size()!=16||value[10]!='T'||value[13]!=':')return L"--";
    if(value.substr(14)=="00"){
        const auto hour=value.substr(11,2);
        return WeatherText(hour[0]=='0'?hour.substr(1):hour);
    }
    return WeatherText(value.substr(11));
}
std::wstring WeatherStatus(const WeatherReading& reading){
    if(reading.error!=weather::ProviderError::None)
        return reading.snapshot?L"更新失败 · 显示上次天气":L"定位或天气不可用，可在设置中选择城市";
    if(reading.loading&&reading.snapshot)return L"正在更新 · 显示上次天气";
    if(reading.cacheWriteFailed)return L"天气已更新，本地缓存保存失败";
    return {};
}
std::wstring FitWeatherLabel(WidgetCanvas& canvas,const std::wstring& text,float width){
    if(text.empty()||!std::isfinite(width)||width<=0)return {};
    if(canvas.MeasureText(text,10)<=width)return text;
    const std::wstring ellipsis=L"\u2026";
    // Keep complete UTF-16 characters and a small glyph-bearing margin. The
    // original weather-location spans use one line with text-overflow:ellipsis.
    const float available=std::max(0.f,width-1);
    if(canvas.MeasureText(ellipsis,10)>available)return {};
    const auto prefix=[&](std::size_t count){
        if(count&&count<text.size()&&text[count-1]>=0xd800&&text[count-1]<=0xdbff&&
            text[count]>=0xdc00&&text[count]<=0xdfff)--count;
        return text.substr(0,count)+ellipsis;
    };
    std::size_t first=0,last=text.size();
    while(first<last){
        const auto middle=first+(last-first+1)/2;
        if(canvas.MeasureText(prefix(middle),10)<=available)first=middle;else last=middle-1;
    }
    return prefix(first);
}
namespace {
void DrawWeatherIcon(WidgetCanvas& canvas,unsigned resource,WidgetRect area){
    if(resource==6205){
        // The legacy fog bitmap is an opaque tile; keep the cloud artwork and
        // separate mist bands so the condition remains readable on glass.
        if(canvas.ImageResource(6204,{area.x+area.width*.12f,area.y,area.width*.76f,area.height*.62f})){
            canvas.Rounded({area.x+area.width*.04f,area.y+area.height*.69f,area.width*.92f,area.height*.065f},
                area.height*.0325f,0xe4e9ee,.95f);
            canvas.Rounded({area.x+area.width*.14f,area.y+area.height*.88f,area.width*.72f,area.height*.065f},
                area.height*.0325f,0xe4e9ee,.8f);
            return;
        }
    }else if(resource&&canvas.ImageResource(resource,area))return;
    // Unknown conditions retain their source label, never an invented forecast.
    canvas.Rounded({area.x+area.width*.15f,area.y+area.height*.1f,area.width*.7f,area.height*.8f},
        area.height*.18f,0xffffff,.12f);
    canvas.Text(L"?",{area.x,area.y+area.height*.12f,area.width,area.height*.75f},
        area.height*.6f,0xffffff,.9f,WidgetTextWeight::Medium,WidgetTextAlign::Center);
}
}
void DrawWeather(WidgetCanvas& canvas,float w,float h,const WeatherReading& reading,std::string_view city){
    const auto* data=reading.snapshot?&*reading.snapshot:nullptr;
    const auto layout=WeatherLayoutFor(w,h);
    const float left=layout.left,top=layout.top;
    const auto temp=data?WeatherTemperature(data->temperature):L"--";
    float x=left;
    DrawWeatherIcon(canvas,data?WeatherIconResource(data->weatherCode,data->isDay):0,{x,top,47,43});
    x+=55;
    const float tempWidth=canvas.MeasureText(temp,32,WidgetTextWeight::Bold);
    canvas.Text(temp,{x,top+5.5f,tempWidth+1,32},32,0xffffff,1,WidgetTextWeight::Bold);
    x+=tempWidth+8;
    const auto* day=data&&!data->forecast.empty()?&data->forecast[0]:nullptr;
    const auto high=L"H:"+(day?WeatherTemperature(day->high):L"--")+L"°C";
    const auto low=L"L:"+(day?WeatherTemperature(day->low):L"--")+L"°C";
    const float rangeWidth=std::max(canvas.MeasureText(high,10),canvas.MeasureText(low,10));
    canvas.Text(high,{x,top+9.5f,rangeWidth+1,12},10);
    canvas.Text(low,{x,top+21.5f,rangeWidth+1,12},10);
    auto name=data?WeatherText(data->cityName):L"当前位置";
    if(!data)for(const auto& item:weather::Cities())if(city==item.id)name=WeatherText(item.name);
    const float locationWidth=std::max(0.f,std::min(96.f,w-left-(x+rangeWidth+10)));
    canvas.Text(FitWeatherLabel(canvas,name,locationWidth),{w-left-locationWidth,top+7,locationWidth,13},10,0xffffff,1,false,WidgetTextAlign::Right);
    const auto condition=data?WeatherText(data->condition):reading.error==weather::ProviderError::None?L"正在获取":L"暂不可用";
    canvas.Text(FitWeatherLabel(canvas,condition,locationWidth),
        {w-left-locationWidth,top+23,locationWidth,13},10,0xffffff,1,false,WidgetTextAlign::Right);
    const float hourHeight=layout.hourHeight,hourWidth=layout.hourWidth;
    for(std::size_t i=0;i<6;++i){
        const auto* hour=data&&i<data->hourly.size()?&data->hourly[i]:nullptr;
        const float hx=left+static_cast<float>(i)*(hourWidth+5),hy=layout.hourTop;
        canvas.Text(hour?WeatherHourLabel(hour->time):L"--",{hx,hy,hourWidth,16},12,0xffffff,.85f,false,WidgetTextAlign::Center);
        if(hour){
            const bool daylight=hour->isDay.value_or(hour->time.size()>=13&&hour->time.substr(11,2)>="06"&&hour->time.substr(11,2)<"18");
            DrawWeatherIcon(canvas,WeatherIconResource(hour->weatherCode,daylight),{hx+(hourWidth-30)/2,hy+(hourHeight-28)/2,30,27});
        }
        canvas.Text(hour?WeatherTemperature(hour->temperature)+L"°C":L"--",{hx,hy+hourHeight-17,hourWidth,17},13,0xffffff,1,WidgetTextWeight::Medium,WidgetTextAlign::Center);
    }
    canvas.Text(WeatherStatus(reading),{20,h-14,w-40,12},9,0xffd5ae,1,false,WidgetTextAlign::Right);
}
}
