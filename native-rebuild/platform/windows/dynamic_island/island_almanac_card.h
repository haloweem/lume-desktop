#pragma once
#include "core/almanac/almanac.h"
#include "platform/windows/text_util.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <algorithm>
#include <array>

namespace lume::platform::windows {
inline constexpr float IslandAlmanacWidth=208,IslandAlmanacHeight=208;
struct IslandAlmanacReading {
    calendar::Date date{};
    std::wstring year,lunar;
    std::array<std::wstring,4> yi{},ji{},directions{};
    bool available=false;
};
inline IslandAlmanacReading ReadIslandAlmanac(calendar::Date date){
    IslandAlmanacReading out;out.date=date;
    if(const auto day=almanac::Lookup(date)){
        const auto wide=[](std::string_view text){return lume::text::Wide(std::string(text));};
        out.available=true;out.year=wide(day->year);out.lunar=wide(day->lunarDate);
        if(const auto at=out.year.find(L'(');at!=std::wstring::npos)out.year.resize(at);
        for(std::size_t i=0;i<4;++i){
            if(i<day->yi.size())out.yi[i]=wide(day->yi[i]);
            if(i<day->ji.size())out.ji[i]=wide(day->ji[i]);
        }
        out.directions={wide(day->directions.wealth),wide(day->directions.joy),
                        wide(day->directions.fortune),wide(day->directions.yangNoble)};
    }
    return out;
}
// A retained, native paper calendar: no image, network request or JS renderer.
// Sunrise and sunset are optional provider readings for this exact day.
inline void DrawIslandAlmanacCard(WidgetCanvas& c,WidgetRect card,const IslandAlmanacReading& day,
                                 const std::wstring& sunrise,const std::wstring& sunset,bool english,float alpha){
    if(alpha<=.01f)return;
    const float s=std::min(card.width/IslandAlmanacWidth,card.height/IslandAlmanacHeight);
    const float x=card.x+(card.width-IslandAlmanacWidth*s)/2,y=card.y+(card.height-IslandAlmanacHeight*s)/2;
    constexpr std::uint32_t ink=0x287f65,paper=0xf6f3eb;
    const auto r=[&](float a,float b,float w,float h){return WidgetRect{x+a*s,y+b*s,w*s,h*s};};
    const auto line=[&](float a,float b,float endX,float endY,float opacity=.54f){
        c.Line(x+a*s,y+b*s,x+endX*s,y+endY*s,ink,opacity*alpha,.55f*s);
    };
    const auto print=[&](const std::wstring& value,WidgetRect bounds,float size,std::uint32_t color=0x287f65,
                        WidgetTextWeight weight=WidgetTextWeight::Normal){
        c.SerifText(value,bounds,size*s,color,alpha,weight);
    };
    c.PushClip(card);
    c.Rounded(r(0,0,208,208),12*s,ink,alpha);
    c.Rounded(r(0,1.5f,208,206.5f),11.5f*s,paper,alpha);
    c.Rounded(r(0,2,208,17),10*s,0xe9e8e3,alpha);
    c.Rounded(r(30,8,13,2.5f),1.25f*s,0xc0c3bb,alpha);
    c.Rounded(r(165,8,13,2.5f),1.25f*s,0xc0c3bb,alpha);
    c.Ellipse(r(100,6,8,8),0xc8cbc3,alpha);
    c.Ellipse(r(101,7,6,6),0xe1e3dc,alpha);
    for(unsigned i=0;i<44;++i){
        const float px=10+static_cast<float>((i*43+17)%187),py=27+static_cast<float>((i*31+11)%173);
        c.Ellipse(r(px,py,.65f,.65f),0xa29d8d,.14f*alpha);
    }
    for(unsigned i=0;i<13;++i){
        const float at=static_cast<float>(i)*16;
        c.Line(x+at*s,y+21*s,x+(at+8)*s,y+22.7f*s,0xdad6cd,.65f*alpha,.7f*s);
        c.Line(x+(at+8)*s,y+22.7f*s,x+(at+16)*s,y+21*s,0xdad6cd,.65f*alpha,.7f*s);
    }
    line(7,30,201,30);line(7,201,201,201);line(7,30,7,201);line(201,30,201,201);
    for(float at:{54.f,150.f,174.f})line(7,at,201,at);
    line(68,30,68,54);line(140,30,140,54);
    line(51,54,51,150);line(157,54,157,150);
    for(float at:{55.5f,104.f,152.5f})line(at,174,at,201);
    static constexpr const wchar_t* months[]={L"",L"一月 Jan",L"二月 Feb",L"三月 Mar",L"四月 Apr",L"五月 May",L"六月 Jun",
        L"七月 Jul",L"八月 Aug",L"九月 Sep",L"十月 Oct",L"十一月 Nov",L"十二月 Dec"};
    const auto month=std::clamp(day.date.month,1,12);
    print(months[month],r(69,30,70,24),12,ink,WidgetTextWeight::Bold);
    const auto sun=[&](float left,const std::wstring& value,bool rise){
        const float cx=x+(left+8)*s,cy=y+42*s;
        c.AnnularSector(cx,cy,3.5f*s,4.15f*s,rise?270.f:90.f,180,ink,.8f*alpha);
        c.Line(cx-5.5f*s,cy,cx+5.5f*s,cy,ink,.8f*alpha,.65f*s);
        for(int i=-1;i<=1;++i)c.Line(cx+static_cast<float>(i)*4*s,cy+(rise?-7.f:5.5f)*s,
            cx+static_cast<float>(i)*4*s,cy+(rise?-5.5f:7.f)*s,ink,.65f*alpha,.65f*s);
        c.Text(value.empty()?L"--:--":value,r(left+16,31,39,22),8.4f*s,ink,.86f*alpha,WidgetTextWeight::Medium,WidgetTextAlign::Left);
    };
    sun(11,sunrise,true);sun(145,sunset,false);
    const auto activities=[&](float left,const wchar_t* label,const std::array<std::wstring,4>& values){
        c.Ellipse(r(left+8,63,28,18),ink,alpha);
        print(label,r(left+8,62,28,20),14,paper,WidgetTextWeight::Bold);
        for(unsigned i=0;i<4;++i){
            const auto value=values[i].empty()?(i?L"":L"—"):values[i];
            const float size=value.size()>3?8.5f:value.size()>2?10.f:11.5f;
            print(value,r(left+2,84+static_cast<float>(i)*15,40,15),size,ink,WidgetTextWeight::Bold);
        }
    };
    activities(7,L"宜",day.yi);activities(157,L"忌",day.ji);
    print(std::to_wstring(day.date.day),r(52,52,104,98),82,ink,WidgetTextWeight::Bold);
    static constexpr const wchar_t* zhWeek[]={L"周日",L"周一",L"周二",L"周三",L"周四",L"周五",L"周六"};
    static constexpr const wchar_t* enWeek[]={L"Sun",L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat"};
    const auto weekday=calendar::IsValidDate(day.date)?calendar::Weekday(day.date):0;
    const auto lunar=day.available?day.lunar:L"农历暂无数据";
    const auto footer=std::to_wstring(day.date.year)+L" · "+(day.available?day.year:L"—")+L" · "+lunar+L" · "+
        (english?enWeek[weekday]:zhWeek[weekday]);
    print(footer,r(8,150,192,24),9.1f,ink,WidgetTextWeight::Bold);
    static constexpr const wchar_t* labels[]={L"财神",L"喜神",L"福神",L"阳贵"};
    for(unsigned i=0;i<4;++i){
        const float left=7+static_cast<float>(i)*48.5f;
        print(labels[i],r(left,175,48.5f,12),8.5f,0x788477);
        print(day.directions[i].empty()?L"—":day.directions[i],r(left,187,48.5f,13),10.5f,ink,WidgetTextWeight::Bold);
    }
    c.PopClip();
}
}
