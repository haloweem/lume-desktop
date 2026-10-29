#include "platform/windows/performance/performance_view.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace lume::platform::windows {
namespace {
std::wstring Fixed(double value,unsigned digits){
    std::wostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(digits)<<value;return out.str();
}
}
std::wstring PerformancePercent(std::optional<double> value){
    return value&&std::isfinite(*value)?std::to_wstring(static_cast<int>(std::round(std::clamp(*value,0.,100.))))+L"%":L"--";
}
std::wstring PerformanceSpeed(std::optional<double> value){
    if(!value||!std::isfinite(*value))return L"--";
    auto amount=std::max(0.,*value);unsigned unit=0;
    while(amount>=1024&&unit<3){amount/=1024;++unit;}
    const wchar_t* units[]={L"B/s",L"KB/s",L"MB/s",L"GB/s"};
    return Fixed(amount,amount<10&&unit?1:0)+L" "+units[unit];
}
std::wstring PerformanceMemory(const performance::Snapshot& s){
    if(!s.totalMemory)return L"--";
    constexpr double gib=1024.*1024*1024;
    return Fixed(s.usedMemory/gib,1)+L" / "+Fixed(s.totalMemory/gib,1)+L" GB";
}
void DrawPerformance(WidgetCanvas& c,float width,float height,const std::optional<performance::Snapshot>& snapshot,bool failed,float alpha){
    if(alpha<=.01f)return;
    const auto s=snapshot.value_or(performance::Snapshot{});
    // The original #widget keeps its 1 DIP transparent border in native-glass mode.
    const float gap=30,column=(width-42-gap)/2,row=(height-38-20-16-16)/2;
    c.Text(L"性能",{21,19,column,20},13,0xffffff,alpha,true);
    c.Text(failed?L"暂不可用":snapshot?L"实时":L"采样中",{width-105,19,84,20},10,0xffffff,.64f*alpha,false,WidgetTextAlign::Right);
    const float statusWidth=failed?40.f:snapshot?20.f:30.f;
    c.Rounded({width-21-statusWidth-9,27,4,4},2,failed?0xffb875:0x79e6b3,alpha);
    const auto metric=[&](const wchar_t* label,std::optional<double> value,const std::wstring& detail,float x,float y,std::uint32_t color){
        // CSS content: 23.1px label, 7px gap, 4px meter, 7px gap, 12px detail.
        y+=std::max(0.f,(row-53.1f)/2);
        // CSS baseline alignment places the small label below the large percentage.
        c.Text(label,{x,y+6.1f,column,16},12,0xffffff,.82f*alpha);
        c.Text(PerformancePercent(value),{x,y,column,23.1f},21,0xffffff,alpha,true,WidgetTextAlign::Right);
        const bool valid=value&&std::isfinite(*value);
        c.Rounded({x,y+30.1f,column,4},2,0xffffff,(valid?.16f:.064f)*alpha);
        if(valid)c.Rounded({x,y+30.1f,column*static_cast<float>(std::clamp(*value,0.,100.))/100,4},2,color,alpha);
        c.Text(detail,{x,y+41.1f,column,12},10,0xffffff,.65f*alpha);
    };
    const float x2=21+column+gap,y2=55+row+16;
    metric(L"CPU",s.cpu,L"处理器使用率",21,55,0x58b3ff);
    metric(L"GPU",s.gpu,L"显卡使用率",x2,55,0xffb488);
    metric(L"内存",s.memory,PerformanceMemory(s),21,y2,0x79e6b3);
    const float networkY=y2+std::max(0.f,(row-55.8f)/2);
    constexpr auto rateWeight=static_cast<WidgetTextWeight>(550);
    c.Text(L"网络",{x2,networkY,column,16},12,0xffffff,.82f*alpha);
    c.Text(L"↓",{x2,networkY+22,column,14.4f},11,0x8bcaff,alpha);
    c.Text(PerformanceSpeed(s.download),{x2+12,networkY+22,column-12,14.4f},12,0xffffff,alpha,rateWeight,WidgetTextAlign::Right);
    c.Text(L"↑",{x2,networkY+41.4f,column,14.4f},11,0x9be5cb,alpha);
    c.Text(PerformanceSpeed(s.upload),{x2+12,networkY+41.4f,column-12,14.4f},12,0xffffff,alpha,rateWeight,WidgetTextAlign::Right);
}
}
