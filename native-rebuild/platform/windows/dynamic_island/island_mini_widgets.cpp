#include "platform/windows/dynamic_island/island_mini_widgets.h"
#include "platform/windows/dynamic_island/island_weather_page.h"
#include "platform/windows/widget_ui/text_fit.h"
#include "core/calendar/lunar_calendar.h"
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace lume::platform::windows {
namespace {
using dynamic_island::MiniWidget;
constexpr float Pi=3.14159265358979323846f;
std::wstring Wide(std::string_view value){
    const auto count=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring result(static_cast<std::size_t>((std::max)(0,count)),L'\0');
    if(count)MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),count);
    return result;
}
struct Value {std::wstring text=L"—";std::optional<double> progress;std::uint32_t color=0x30d158;};
Value Percent(std::optional<double> p,std::uint32_t color=0x30d158){
    if(!p||!std::isfinite(*p))return {};
    const auto value=std::clamp(*p,0.,100.);return {std::to_wstring(static_cast<int>(std::lround(value)))+L"%",value/100.,color};
}
Value Read(MiniWidget kind,const IslandMiniData& data,const IslandPresentation& view,
           const dynamic_island::Weather& weather,const std::optional<performance::Snapshot>& metrics){
    const auto& t=data.time;const calendar::Date date{t.wYear,t.wMonth,t.wDay};
    const double day=(t.wHour*3600.+t.wMinute*60.+t.wSecond)/86400.;
    wchar_t value[32]{};
    switch(kind){
    case MiniWidget::Date:swprintf_s(value,L"%u/%u",t.wMonth,t.wDay);return {value,{},0x0a84ff};
    case MiniWidget::Weekday:{
        const wchar_t* zh[]={L"周日",L"周一",L"周二",L"周三",L"周四",L"周五",L"周六"};
        const wchar_t* en[]={L"Sun",L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat"};
        return {view.english?en[t.wDayOfWeek%7]:zh[t.wDayOfWeek%7],{},0x0a84ff};}
    case MiniWidget::Lunar:{const auto lunar=calendar::LookupLunar(date);return {lunar.lunarDate.empty()?L"—":Wide(lunar.lunarDate),{},0xff9f0a};}
    case MiniWidget::Clock:swprintf_s(value,L"%02u:%02u",t.wHour,t.wMinute);return {value,{},0xffffff};
    case MiniWidget::DayProgress:return Percent(day*100,0xff9f0a);
    case MiniWidget::WeekProgress:return Percent(((t.wDayOfWeek+6)%7+day)*100/7.,0x30d158);
    case MiniWidget::MonthProgress:return Percent((t.wDay-1+day)*100/(std::max)(1,calendar::DaysInMonth(t.wYear,t.wMonth)),0x0a84ff);
    case MiniWidget::YearProgress:return Percent((calendar::DayOfYear(date)-1+day)*100/(calendar::DaysInMonth(t.wYear,2)==29?366.:365.),0xbf5af2);
    case MiniWidget::Battery:return Percent(data.battery,0x30d158);
    case MiniWidget::Cpu:return Percent(metrics?metrics->cpu:std::optional<double>{},0x0a84ff);
    case MiniWidget::Memory:return Percent(metrics?metrics->memory:std::optional<double>{},0xff453a);
    case MiniWidget::Disk:return Percent(data.disk,0xffd60a);
    case MiniWidget::Network:{
        if(!metrics||!metrics->download)return {};
        const double bytes=*metrics->download;
        swprintf_s(value,bytes>=1048576?L"%.1fM":L"%.0fK",bytes/(bytes>=1048576?1048576:1024));return {value,{},0x64d2ff};}
    case MiniWidget::Weather:return {weather.available?std::to_wstring(weather.temperature)+L"°":L"—",{},0xffd60a};
    case MiniWidget::Temperature:return {weather.available?std::to_wstring(weather.temperature)+L"°":L"—",{},0xff9f0a};
    case MiniWidget::Humidity:return Percent(weather.available?weather.humidity:std::optional<double>{},0x64d2ff);
    case MiniWidget::Wind:
        if(weather.available&&weather.windSpeed){swprintf_s(value,L"%.1f km/h",*weather.windSpeed);return {value,{},0x64d2ff};}return {};
    case MiniWidget::Focus:
        if(view.focusing){swprintf_s(value,L"%02u:%02u",view.remaining/60,view.remaining%60);return {value,1-view.remaining/1500.,0xff9f0a};}
        return {view.finished?(view.english?L"Done":L"完成"):L"25:00",{},0xff9f0a};
    case MiniWidget::Music:return {view.media.available?(view.media.playing?L"▶":L"Ⅱ"):L"♪",{},0x30d158};
    default:return {};
    }
}
void Decoration(WidgetCanvas& c,MiniWidget kind,WidgetRect r,float alpha){
    const float cx=r.x+r.width/2,cy=r.y+r.height/2,s=(std::min)(r.width,r.height)*.70f,q=s/2;
    const float phase=static_cast<float>(GetTickCount64()%8000)/8000.f*2*Pi;
    const float wave=std::sin(phase*2),pulse=1+.07f*wave;
    const auto dot=[&](float x,float y,float radius,std::uint32_t color){c.Ellipse({x-radius,y-radius,radius*2,radius*2},color,alpha);};
    switch(kind){
    case MiniWidget::Planet:
        c.Ellipse({cx-q,cy-q*.35f,q*2,q*.7f},0xbf5af2,alpha*.7f);
        dot(cx,cy,q*.65f,0x9d80ef);c.Line(cx-q,cy+q*.18f,cx+q,cy-q*.18f,0xdecfff,alpha,1.6f);break;
    case MiniWidget::Flame:
        c.Polygon({cx-q*.8f,cy+q*.7f,cx-q*.65f,cy,cx-q*.15f,cy-q*(1+.12f*wave),cx+q*.15f,cy-q*.2f,cx+q*.5f,cy-q*.6f,cx+q*.85f,cy+q*.55f,cx,cy+q},0xff8a25,alpha,.7f);
        c.Polygon({cx-q*.35f,cy+q*.7f,cx,cy-q*.1f,cx+q*.4f,cy+q*.7f},0xffda64,alpha,.7f);break;
    case MiniWidget::Heart:{
        std::vector<float> pts;pts.reserve(64);
        for(int i=0;i<32;++i){const float a=i*2*Pi/32;const float x=16*std::pow(std::sin(a),3.f);
            const float y=13*std::cos(a)-5*std::cos(2*a)-2*std::cos(3*a)-std::cos(4*a);
            pts.push_back(cx+x*q/18*pulse);pts.push_back(cy-y*q/18*pulse);}
        c.Polygon(pts,0xff617a,alpha);break;}
    case MiniWidget::Gift:
        c.Rounded({cx-q*.8f,cy-q*.2f,q*1.6f,q*1.15f},2,0xff9f0a,alpha);
        c.Line(cx,cy-q*.25f,cx,cy+q*.95f,0xffe9a0,alpha,2);c.Line(cx-q*.8f,cy+q*.2f,cx+q*.8f,cy+q*.2f,0xffe9a0,alpha,2);
        c.Ellipse({cx-q*.6f,cy-q*.7f,q*.6f,q*.5f},0xffc566,alpha);c.Ellipse({cx,cy-q*.7f,q*.6f,q*.5f},0xffc566,alpha);break;
    case MiniWidget::Butterfly:{
        const float wing=q*(.65f+.18f*wave);
        c.Ellipse({cx-wing,cy-q*.8f,wing,q},0xc48aff,alpha);c.Ellipse({cx,cy-q*.8f,wing,q},0xc48aff,alpha);
        c.Ellipse({cx-wing*.8f,cy,wing*.8f,q*.65f},0x6ad6e9,alpha);c.Ellipse({cx,cy,wing*.8f,q*.65f},0x6ad6e9,alpha);
        c.Line(cx,cy-q*.75f,cx,cy+q*.6f,0xffffff,alpha,1.4f);break;}
    case MiniWidget::Moon:
        c.AnnularSector(cx,cy,q*.55f,q*.85f,30,290,0xffe5a4,alpha);dot(cx+q*.62f,cy-q*.65f,q*.08f,0xffffff);break;
    case MiniWidget::Snow:
        for(int i=0;i<6;++i){const float a=Pi*i/3+phase*.15f;c.Line(cx,cy,cx+std::cos(a)*q,cy+std::sin(a)*q,0xb7e8ff,alpha,1.4f);}break;
    case MiniWidget::Orbit:
        c.AnnularSector(cx,cy,q*.68f,q*.74f,0,360,0x818baf,alpha*.6f);dot(cx,cy,q*.19f,0xcdd8ff);
        dot(cx+std::cos(phase)*q*.72f,cy+std::sin(phase)*q*.72f,q*.17f,0x89a9ff);break;
    case MiniWidget::Coffee:
        c.Rounded({cx-q*.7f,cy-q*.1f,q*1.25f,q*.9f},3,0xe6c7a4,alpha);
        c.AnnularSector(cx+q*.65f,cy+q*.22f,q*.2f,q*.35f,0,180,0xe6c7a4,alpha);
        c.Line(cx-q*.25f,cy-q*.45f,cx-q*.2f+wave,cy-q*.95f,0xffffff,alpha*.6f,1);break;
    case MiniWidget::Cat:
        c.Polygon({cx-q*.75f,cy-q*.3f,cx-q*.7f,cy-q*.9f,cx-q*.25f,cy-q*.55f,cx+q*.25f,cy-q*.55f,cx+q*.7f,cy-q*.9f,cx+q*.75f,cy-q*.3f},0xf4b472,alpha);
        c.Ellipse({cx-q*.8f,cy-q*.6f,q*1.6f,q*1.45f},0xf4b472,alpha);dot(cx-q*.3f,cy,q*.1f,0x242424);dot(cx+q*.3f,cy,q*.1f,0x242424);
        c.Line(cx-q*.15f,cy+q*.35f,cx+q*.15f,cy+q*.35f,0x664634,alpha,1);break;
    case MiniWidget::Tree:
        c.Rounded({cx-1.5f,cy+q*.3f,3,q*.6f},1,0x9b7153,alpha);
        c.Polygon({cx+wave*.5f,cy-q,cx-q*.8f,cy+q*.45f,cx+q*.8f,cy+q*.45f},0x57cb82,alpha);
        c.Polygon({cx,cy-q*.6f,cx-q,cy+q*.8f,cx+q,cy+q*.8f},0x2cae68,alpha);break;
    default:break;
    }
}
}
void IslandMiniData::Refresh(const dynamic_island::Customization& customization){
    GetLocalTime(&time);const auto wants=[&](MiniWidget k){return customization.customIcons&&(customization.left.widget==k||customization.right.widget==k);};
    battery.reset();
    if(wants(MiniWidget::Battery)){SYSTEM_POWER_STATUS power{};
        if(GetSystemPowerStatus(&power)&&power.BatteryFlag!=255&&!(power.BatteryFlag&128)&&power.BatteryLifePercent<=100)battery=power.BatteryLifePercent;}
    if(wants(MiniWidget::Disk)&&(!diskAt||GetTickCount64()-diskAt>=10000)){
        diskAt=GetTickCount64();disk.reset();wchar_t root[MAX_PATH]{};ULARGE_INTEGER total{},free{};
        if(GetWindowsDirectoryW(root,MAX_PATH)&&root[1]==L':'){root[3]=0;
            if(GetDiskFreeSpaceExW(root,nullptr,&total,&free)&&total.QuadPart)disk=(1-static_cast<double>(free.QuadPart)/static_cast<double>(total.QuadPart))*100.;}
    }
}
float IslandMiniWidth(const dynamic_island::MiniSelection& selection){
    if(selection.widget==MiniWidget::None)return 0;
    return selection.style==dynamic_island::MiniStyle::Circle?30.f:70.f;
}
void DrawIslandMiniWidget(WidgetCanvas& c,const dynamic_island::MiniSelection& selection,WidgetRect r,
    const IslandMiniData& data,const IslandPresentation& view,const dynamic_island::Weather& weather,
    const std::optional<performance::Snapshot>& metrics,float alpha){
    if(selection.widget==MiniWidget::None||alpha<=.01f)return;
    const bool circle=selection.style==dynamic_island::MiniStyle::Circle;
    c.Rounded(r,r.height/2,0x050609,.52f*alpha);
    if(dynamic_island::Animated(selection.widget)){Decoration(c,selection.widget,r,alpha);return;}
    const auto value=Read(selection.widget,data,view,weather,metrics);
    const float cx=r.x+r.width/2,cy=r.y+r.height/2;
    if(selection.widget==MiniWidget::Lunar&&circle){
        const auto month=value.text.find(L'月');
        if(month!=std::wstring::npos){
            c.Text(value.text.substr(0,month+1),{r.x+2,cy-11,r.width-4,11},7.5f,0xffffff,alpha,false,WidgetTextAlign::Center);
            c.Text(value.text.substr(month+1),{r.x+2,cy,r.width-4,12},8.5f,0xffffff,alpha,true,WidgetTextAlign::Center);return;
        }
    }
    if(selection.widget==MiniWidget::Wind&&circle&&weather.available&&weather.windSpeed){
        const auto split=value.text.find(L' ');
        c.Text(value.text.substr(0,split),{r.x+2,cy-10,r.width-4,12},9,0xffffff,alpha,true,WidgetTextAlign::Center);
        c.Text(L"km/h",{r.x+2,cy+2,r.width-4,10},6.5f,0xffffff,alpha*.7f,false,WidgetTextAlign::Center);return;
    }
    if(selection.widget==MiniWidget::Music&&view.media.playing&&
       std::any_of(view.media.spectrum.begin(),view.media.spectrum.end(),[](float v){return v>0;})){
        for(std::size_t i=0;i<4;++i){const auto height=3.f+15.f*std::clamp(view.media.spectrum[i*4],0.f,1.f);
            c.Rounded({cx-10+6*static_cast<float>(i),cy-height/2,3,height},1.5f,0x30d158,alpha);}return;
    }
    if(selection.widget==MiniWidget::Clock&&circle){
        c.AnnularSector(cx,cy,11,11.8f,0,360,0xffffff,alpha*.4f);
        const float minute=(data.time.wMinute+data.time.wSecond/60.f)*Pi/30.f,hour=(data.time.wHour%12+data.time.wMinute/60.f)*Pi/6.f;
        c.Line(cx,cy,cx+std::sin(hour)*6,cy-std::cos(hour)*6,0xffffff,alpha,1.6f);
        c.Line(cx,cy,cx+std::sin(minute)*9,cy-std::cos(minute)*9,0xffffff,alpha,1.1f);return;
    }
    if(selection.widget==MiniWidget::Weather&&weather.available&&circle){
        DrawIslandWeatherGlyph(c,cx,cy,23,weather.code,weather.isDay,alpha);return;
    }
    if(value.progress){
        const float p=static_cast<float>(std::clamp(*value.progress,0.,1.));
        if(circle){c.AnnularSector(cx,cy,12,13,0,360,0xffffff,alpha*.15f);c.AnnularSector(cx,cy,12,13,0,p*360,value.color,alpha);}
        else{c.Rounded({r.x+9,r.y+r.height-6,r.width-18,2},1,0xffffff,alpha*.16f);if(p>0)c.Rounded({r.x+9,r.y+r.height-6,(r.width-18)*p,2},1,value.color,alpha);}
    }
    const float size=circle?(value.text.size()>4?7.5f:9.5f):10.5f;
    const auto text=FitWeightedText(c,value.text,r.width-6,size,WidgetTextWeight::Semibold);
    c.Text(text,{r.x+3,r.y+(r.height-15)/2-(value.progress&&!circle?2.f:0.f),r.width-6,15},size,0xffffff,alpha,WidgetTextWeight::Semibold,WidgetTextAlign::Center);
}
}
