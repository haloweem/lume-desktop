#include "platform/windows/weather/weather_tooltip.h"
#include "platform/windows/weather/weather_view.h"
#include "platform/windows/weather/weather_timezone.h"
#include "platform/windows/widget_ui/direct_write_tooltip.h"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
namespace lume::platform::windows {
WeatherTipText WeatherTips(const WeatherReading& reading,const weather::LocalTime& local){
    WeatherTipText tips;
    tips[8]=WeatherStatus(reading);
    if(!reading.snapshot)return tips;
    const auto& data=*reading.snapshot;
    tips[0]=WeatherText(data.cityName)+(data.cityId=="auto"?L" · IP 定位":L"")+L"\n"+WeatherText(data.condition)+L"\n"+
        (data.remainingDay?L"今日余下时段高低温（逐小时预报估算，不含此前实测温度）":L"今日最高 / 最低温");
    const auto time=[&](weather::Millis millis){
        const auto result=local?local(millis,data.timezone):std::nullopt;
        if(!result||result->size()!=16)return std::wstring(L"未知");
        auto text=*result;text[10]=' ';return WeatherText(text);
    };
    // Keep provenance available even while an old reading is being refreshed.
    tips[0]+=L"\n"+time(data.validAt)+(data.source==weather::Source::Qweather?L" 观测":L" 预报");
    if(data.source==weather::Source::MetNorway)tips[0]+=L"\n数据发布 "+time(data.sourceUpdatedAt);
    tips[0]+=L"\n获取 "+time(data.updatedAt)+L"\n"+WeatherText(data.timezone);
    tips[0]+=data.source==weather::Source::Qweather?L"\n和风天气公开页面":L"\n挪威气象局 · CC BY 4.0";
    for(std::size_t i=0;i<std::min<std::size_t>(6,data.hourly.size());++i){
        auto timeText=data.hourly[i].time;
        if(timeText.size()>10)timeText[10]=' ';
        tips[i+2]=WeatherText(timeText)+L" · "+WeatherText(data.hourly[i].condition);
    }
    return tips;
}
WeatherTooltip::~WeatherTooltip(){Stop();}
bool WeatherTooltip::Attach(HWND owner){
    if(window_&&owner_==owner)return true;
    Stop();if(!owner)return false;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};
    if(!InitCommonControlsEx(&controls))return false;
    window_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window_)return false;
    if(!direct_write_tooltip::Attach(window_)){Stop();return false;}
    owner_=owner;
    SendMessageW(window_,TTM_SETMAXTIPWIDTH,0,480);
    for(std::size_t i=0;i<text_.size();++i){
        // This layout is accepted by both the default and v6 common controls;
        // no manifest dependency is required solely for native tooltips.
        TTTOOLINFOW info{};info.cbSize=TTTOOLINFOW_V2_SIZE;info.hwnd=owner_;info.uId=i+1;
        info.uFlags=TTF_SUBCLASS|TTF_TRANSPARENT;info.lpszText=text_[i].data();
        if(!SendMessageW(window_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info))){Stop();return false;}
    }
    RECT r{};GetClientRect(owner_,&r);const auto dpi=GetDpiForWindow(owner_);const float scale=96.f/(dpi?dpi:96);
    Resize(r.right*scale,r.bottom*scale);
    return true;
}
void WeatherTooltip::Update(const WeatherReading& reading){
    if(!window_)return;
    WeatherTipText next;
    if(reading.snapshot){
        WeatherTimezone timezone;
        next=WeatherTips(reading,[&](auto millis,auto zone){return timezone.Local(millis,zone);});
    }else next=WeatherTips(reading,{});
    if(next==text_)return;
    text_=std::move(next);
    for(std::size_t i=0;i<text_.size();++i){
        TTTOOLINFOW info{};info.cbSize=TTTOOLINFOW_V2_SIZE;info.hwnd=owner_;info.uId=i+1;info.lpszText=text_[i].data();
        SendMessageW(window_,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&info));
    }
}
void WeatherTooltip::Resize(float w,float h){
    if(!window_)return;
    auto dpi=GetDpiForWindow(owner_);if(!dpi)dpi=96;
    if(w==width_&&h==height_&&dpi==dpi_)return;
    width_=w;height_=h;dpi_=dpi;
    const auto layout=WeatherLayoutFor(w,h);
    const float left=layout.left,top=layout.top,hourWidth=layout.hourWidth;
    std::array<WidgetRect,9> rects;
    rects[0]={left,std::max(24.f,top),w-2*left,top+41-std::max(24.f,top)};
    rects[1]={};rects[8]={20,h-14,w-40,12};
    for(std::size_t i=0;i<6;++i)rects[i+2]={left+static_cast<float>(i)*(hourWidth+5),layout.hourTop,hourWidth,layout.hourHeight};
    const auto pixel=[dpi](float value){return static_cast<LONG>(std::lround(value*dpi/96));};
    for(std::size_t i=0;i<rects.size();++i){
        const auto& r=rects[i];TTTOOLINFOW info{};info.cbSize=TTTOOLINFOW_V2_SIZE;info.hwnd=owner_;info.uId=i+1;
        info.rect={pixel(r.x),pixel(r.y),pixel(r.x+r.width),pixel(r.y+r.height)};
        SendMessageW(window_,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&info));
    }
}
void WeatherTooltip::Stop(){
    if(window_)DestroyWindow(window_);
    window_=nullptr;owner_=nullptr;text_={};width_=height_=0;dpi_=0;
}
}
