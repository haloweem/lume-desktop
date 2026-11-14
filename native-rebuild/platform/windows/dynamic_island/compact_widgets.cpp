#include "platform/windows/dynamic_island/compact_widgets.h"
#include "platform/windows/widget_ui/text_fit.h"
#include "core/calendar/lunar_calendar.h"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <array>
#include <filesystem>
#include <memory>
#include <wincodec.h>
#include <wrl/client.h>

namespace lume::platform::windows {
namespace {
using W=dynamic_island::CompactWidget;
using Microsoft::WRL::ComPtr;
std::shared_ptr<const WidgetBitmap> Icon(W kind) {
    static std::array<std::shared_ptr<const WidgetBitmap>,dynamic_island::CompactWidgetIds.size()> icons;
    static std::array<bool,dynamic_island::CompactWidgetIds.size()> attempted{};
    const auto index=static_cast<std::size_t>(kind);
    if(index>=icons.size()||attempted[index])return index<icons.size()?icons[index]:nullptr;
    attempted[index]=true;
    wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);
    if(!length||length>=32768)return {};
    const auto name=dynamic_island::CompactWidgetId(kind);
    const auto file=std::filesystem::path(executable).parent_path()/L"ui/assets/icons/island-compact"/
        (std::wstring(name.begin(),name.end())+L".png");
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> convert;
    UINT width=0,height=0;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))||
       FAILED(decoder->GetFrame(0,&frame))||FAILED(frame->GetSize(&width,&height))||
       width!=64||height!=64||FAILED(factory->CreateFormatConverter(&convert))||
       FAILED(convert->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return {};
    auto bitmap=std::make_shared<WidgetBitmap>();bitmap->width=width;bitmap->height=height;
    bitmap->pixels.resize(static_cast<std::size_t>(width)*height);
    if(FAILED(convert->CopyPixels(nullptr,width*4,width*height*4,reinterpret_cast<BYTE*>(bitmap->pixels.data()))))return {};
    icons[index]=bitmap;return bitmap;
}
std::wstring Wide(std::string_view s) {
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(size<=0)return {};std::wstring out(static_cast<std::size_t>(size),0);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),size);return out;
}
CompactValue Text(std::wstring text,std::uint32_t color=0x8abfff){return {std::move(text),{},{},{},color,true};}
CompactValue Percent(std::optional<double> number,std::uint32_t color) {
    if(!number||!std::isfinite(*number))return {};
    const double value=std::clamp(*number,0.,100.);
    auto out=Text(std::to_wstring(static_cast<int>(std::lround(value)))+L"%",color);out.progress=value/100;return out;
}
CompactValue Number(std::optional<double> number,const wchar_t* suffix,std::uint32_t color=0x70d9ed) {
    if(!number||!std::isfinite(*number))return {};
    return Text(std::to_wstring(static_cast<int>(std::lround(*number)))+suffix,color);
}
std::wstring Rate(double number) {
    wchar_t value[32]{};const bool mega=number>=1048576;
    swprintf_s(value,L"%.1f %s",number/(mega?1048576:1024),mega?L"MB/s":L"KB/s");return value;
}
}
void CompactData::Refresh() {
    GetLocalTime(&time);const auto now=GetTickCount64();
    if(refreshed&&now-refreshed<1000)return;refreshed=now;
    SYSTEM_POWER_STATUS power{};battery.reset();hasBattery=false;plugged=false;charging=false;
    if(GetSystemPowerStatus(&power)) {
        hasBattery=power.BatteryFlag!=255&&!(power.BatteryFlag&128);plugged=power.ACLineStatus==1;
        charging=hasBattery&&(power.BatteryFlag&8)!=0;
        if(hasBattery&&power.BatteryLifePercent<=100)battery=power.BatteryLifePercent;
    }
    if(!diskAt||now-diskAt>=15000) {
        diskAt=now;disk.reset();wchar_t directory[MAX_PATH]{},volume[MAX_PATH]{};ULARGE_INTEGER total{},free{};
        if(GetWindowsDirectoryW(directory,MAX_PATH)&&GetVolumePathNameW(directory,volume,MAX_PATH)&&
           GetDiskFreeSpaceExW(volume,nullptr,&total,&free)&&total.QuadPart)
            disk=(1-static_cast<double>(free.QuadPart)/static_cast<double>(total.QuadPart))*100;
    }
}
CompactValue ReadCompact(W kind,const CompactData& data,const dynamic_island::Weather& weather,
    const std::optional<performance::Snapshot>& metrics,bool english) {
    const auto& t=data.time;const calendar::Date date{t.wYear,t.wMonth,t.wDay};
    const wchar_t* zh[]={L"周日",L"周一",L"周二",L"周三",L"周四",L"周五",L"周六"};
    const wchar_t* en[]={L"Sun",L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat"};
    const auto weekday=english?en[t.wDayOfWeek%7]:zh[t.wDayOfWeek%7];
    const double day=(t.wHour*3600.+t.wMinute*60.+t.wSecond)/86400.;
    wchar_t value[48]{};
    const bool fresh=metrics&&GetTickCount64()-metrics->monotonicMillis<6000;
    switch(kind) {
    case W::None:return {};
    case W::Lunar:{const auto lunar=calendar::LookupLunar(date);return lunar.lunarDate.empty()?CompactValue{}:Text(Wide(lunar.lunarDate),0xffb875);}
    case W::Calendar:swprintf_s(value,L"%s %02u",weekday,t.wDay);return Text(value);
    case W::Date:swprintf_s(value,L"%u/%u",t.wMonth,t.wDay);return Text(value,0xff8f9c);
    case W::Weekday:return Text(weekday);
    case W::Clock:swprintf_s(value,L"%02u:%02u",t.wHour,t.wMinute);return Text(value);
    case W::DayProgress:return Percent(day*100,0xffaf61);
    case W::WeekProgress:return Percent(((t.wDayOfWeek+6)%7+day)*100/7,0x6bdfaa);
    case W::MonthProgress:return Percent((t.wDay-1+day)*100/(std::max)(1,calendar::DaysInMonth(t.wYear,t.wMonth)),0x75b9ff);
    case W::QuarterProgress:{const int first=(t.wMonth-1)/3*3+1;int days=0,passed=0;
        for(int month=first;month<first+3;++month){const int count=calendar::DaysInMonth(t.wYear,month);days+=count;if(month<t.wMonth)passed+=count;}
        return Percent((passed+t.wDay-1+day)*100/(std::max)(1,days),0xffd36e);}
    case W::YearProgress:return Percent((calendar::DayOfYear(date)-1+day)*100/(calendar::DaysInMonth(t.wYear,2)==29?366.:365.),0xd393ff);
    case W::Network:if(fresh&&metrics->upload&&metrics->download){auto out=Text(L"↑ "+Rate(*metrics->upload),0x73e4b2);out.secondary=L"↓ "+Rate(*metrics->download);return out;}return {};
    case W::Power:return Text(data.plugged?(english?L"AC power":L"接通电源"):data.hasBattery?(english?L"On battery":L"电池供电"):(english?L"No battery":L"无电池"));
    case W::Battery:return Percent(data.battery,0x75e5a5);
    case W::Cpu:return Percent(fresh?metrics->cpu:std::optional<double>{},0x7be0a2);
    case W::Memory:return Percent(fresh?metrics->memory:std::optional<double>{},0x91b8ff);
    case W::Disk:return Percent(data.disk,0xffce6b);
    case W::CpuMemory:{auto out=Percent(fresh?metrics->cpu:std::optional<double>{},0x7be0a2);
        if(fresh&&metrics->memory){out.secondProgress=*metrics->memory/100;out.secondary=std::to_wstring(static_cast<int>(std::lround(*metrics->memory)))+L"%";}return out;}
    case W::Files:return Text(std::to_wstring(data.files)+(english?L" files":L" 个文件"));
    default:break;
    }
    if(!weather.available)return {};
    switch(kind) {
    case W::Weather:return weather.condition.empty()?CompactValue{}:Text(weather.condition,0xa9dfff);
    case W::TemperatureRange:if(weather.hasRange)return Text(std::to_wstring(weather.low)+L"°–"+std::to_wstring(weather.high)+L"°",0xffb562);return {};
    case W::Humidity:return Number(weather.humidity,L"%");
    case W::WindSpeed:if(weather.windSpeed){swprintf_s(value,L"%.1f m/s",*weather.windSpeed/3.6);return Text(value,0x7cdbd1);}return {};
    case W::WindDirection:if(weather.windDirection){const wchar_t* directions[]={L"北",L"东北",L"东",L"东南",L"南",L"西南",L"西",L"西北"};
        const wchar_t* directionsEn[]={L"N",L"NE",L"E",L"SE",L"S",L"SW",L"W",L"NW"};const int at=static_cast<int>(std::floor((*weather.windDirection+22.5)/45))%8;
        return Text(english?directionsEn[at]:std::wstring(directions[at])+L"风",0x7cdbd1);}return {};
    case W::Uv:return Number(weather.uv,L"",0xffcf66);
    case W::SunriseSunset:if(!weather.sunrise.empty()&&!weather.sunset.empty()){auto out=Text(L"↑ "+weather.sunrise,0xffcf66);out.secondary=L"↓ "+weather.sunset;return out;}return {};
    case W::Pressure:return Number(weather.pressure,L" hPa",0xffcf66);
    case W::Temperature:return Number(weather.temperature,L"°",0xffb562);
    case W::Visibility:return Number(weather.visibility,L" km");
    case W::FeelsLike:return Number(weather.apparentTemperature,L"°",0xffb562);
    case W::Precipitation:return Number(weather.precipitation,L"%",0x75b9ff);
    default:return {};
    }
}
void DrawCompact(WidgetCanvas& c,W kind,WidgetRect r,const CompactData& data,const dynamic_island::Weather& weather,
    const std::optional<performance::Snapshot>& metrics,bool english,float alpha,bool right) {
    if(kind==W::None||alpha<=.01f)return;
    const auto value=ReadCompact(kind,data,weather,metrics,english);
    const auto alignedX=[&](float width){return right?r.x+r.width-width:r.x;};
    const float cy=r.y+r.height/2;
    const bool meter=(kind>=W::DayProgress&&kind<=W::YearProgress)||
        kind==W::Cpu||kind==W::Memory||kind==W::Disk||kind==W::CpuMemory||kind==W::Battery;
    if(meter){
        if(!value.available&&!value.secondProgress){
            c.Text(L"N/A",r,10,0x929292,alpha,WidgetTextWeight::Medium,right?WidgetTextAlign::Right:WidgetTextAlign::Left);
            return;
        }
        const float radius=11,cx=alignedX(radius*2)+radius;
        c.AnnularSector(cx,cy,radius-2,radius,0,360,0x353535,alpha);
        if(value.progress)c.AnnularSector(cx,cy,radius-2,radius,0,static_cast<float>(*value.progress*360),value.color,alpha);
        if(kind==W::CpuMemory){
            c.AnnularSector(cx,cy,5,7,0,360,0x353535,alpha);
            if(value.secondProgress)c.AnnularSector(cx,cy,5,7,0,static_cast<float>(*value.secondProgress*360),0xffd36e,alpha);
        }else c.Text(value.text,{cx-9,cy-6,18,12},6.5f,0xffffff,alpha,WidgetTextWeight::Bold,WidgetTextAlign::Center);
        return;
    }
    if(kind==W::Clock){
        const float cx=alignedX(24)+12;constexpr double pi=3.141592653589793;
        for(int i=0;i<4;++i){
            const double a=i*pi/2;
            c.Line(cx+static_cast<float>(std::sin(a))*9,cy-static_cast<float>(std::cos(a))*9,
                   cx+static_cast<float>(std::sin(a))*11,cy-static_cast<float>(std::cos(a))*11,0x909090,alpha,1.5f);
        }
        for(const auto hand:{std::pair<double,float>{(data.time.wHour%12+data.time.wMinute/60.)*pi/6,5.f},
            std::pair<double,float>{data.time.wMinute*pi/30,8.f}})
            c.Line(cx,cy,cx+static_cast<float>(std::sin(hand.first))*hand.second,cy-static_cast<float>(std::cos(hand.first))*hand.second,0xffffff,alpha,1.5f);
        return;
    }
    if(kind==W::Power){
        const float x=alignedX(18);
        c.Rounded({x,cy-6,18,12},1.5f,0x989898,alpha);
        c.Rounded({x+1.5f,cy-4.5f,15,9},.5f,0x303030,alpha);
        c.Line(x+9,cy+6,x+9,cy+9,0x989898,alpha,1.5f);
        c.Line(x+5,cy+9,x+13,cy+9,0x989898,alpha,1.5f);
        return;
    }
    if(kind==W::Lunar||kind==W::Date){
        std::wstring first,last;
        if(kind==W::Date){first=std::to_wstring(data.time.wMonth)+L"/";last=std::to_wstring(data.time.wDay);}
        else{
            const auto split=value.text.find(L'月');
            if(split!=std::wstring::npos){first=value.text.substr(0,split)+L"/";last=value.text.substr(split+1);}
            else first=value.text;
        }
        constexpr float font=11;
        const float firstWidth=c.MeasureText(first,font,WidgetTextWeight::Semibold);
        const float lastWidth=c.MeasureText(last,font,WidgetTextWeight::Semibold);
        const float x=alignedX(firstWidth+lastWidth);
        c.Text(first,{x,cy-9,firstWidth+1,18},font,0xf4f7fb,alpha,WidgetTextWeight::Semibold);
        c.Text(last,{x+firstWidth,cy-9,lastWidth+1,18},font,0xe83332,alpha,WidgetTextWeight::Semibold);
        return;
    }
    // Only weather and file widgets use a semantic pictogram. Text and meter
    // widgets must not acquire a second generic icon from the picker assets.
    const bool pictogram=kind>=W::Weather||kind==W::Files;
    const bool two=!value.secondary.empty();const float font=two?9.f:11.f;
    const std::wstring label=kind==W::Files?std::to_wstring(data.files):value.text;
    const float iconWidth=pictogram?21.f:0.f;
    const float textWidth=(std::min)(r.width-iconWidth,(std::max)(c.MeasureText(label,font,WidgetTextWeight::Semibold),
        two?c.MeasureText(value.secondary,font,WidgetTextWeight::Normal):0.f));
    float x=alignedX(iconWidth+textWidth);
    if(pictogram){if(const auto icon=Icon(kind))c.ImageBitmap(icon,{x,cy-8.5f,17,17},alpha);x+=iconWidth;}
    const float top=cy-(two?12.f:9.f);
    c.Text(FitWeightedText(c,label,textWidth,font,WidgetTextWeight::Semibold),{x,top,textWidth+1,two?12.f:18.f},font,
        value.available?(kind==W::Network?0x35c978:0xf4f7fb):0xa0afc2,alpha,WidgetTextWeight::Semibold);
    if(two)c.Text(FitWeightedText(c,value.secondary,textWidth,font,WidgetTextWeight::Normal),{x,top+12,textWidth+1,12},font,
        0x59a7f0,alpha,WidgetTextWeight::Normal);
}
}
