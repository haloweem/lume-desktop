#pragma once
#include "platform/windows/dynamic_island/island_window.h"
#include <algorithm>
#include <cmath>

namespace lume::platform::windows {
struct IslandClockProgress {
    double day=0,week=0;
    unsigned weekday=1;
};
inline IslandClockProgress ReadIslandClockProgress(const SYSTEMTIME& now) {
    const auto seconds=std::min<unsigned>(now.wHour,23)*3600u+
        std::min<unsigned>(now.wMinute,59)*60u+std::min<unsigned>(now.wSecond,59);
    const auto weekday=(static_cast<unsigned>(now.wDayOfWeek)%7+6)%7+1;
    const double day=seconds/86400.;
    return {day,(weekday-1+day)/7.,weekday};
}

struct IslandClockFlipState {
    SYSTEMTIME previous{},current{};
    std::uint64_t minute=0,observedAt=0,startedAt=0;
    bool initialized=false,running=false;
    static constexpr unsigned DurationMs=480;
    static std::uint64_t Minute(const SYSTEMTIME& time){
        auto value=time;value.wSecond=value.wMilliseconds=0;FILETIME file{};
        if(!SystemTimeToFileTime(&value,&file))return 0;
        return ((static_cast<std::uint64_t>(file.dwHighDateTime)<<32)|file.dwLowDateTime)/600000000ULL;
    }
    bool Observe(const SYSTEMTIME& time,std::uint64_t now,bool animate){
        const auto next=Minute(time);
        const bool changed=!initialized||next!=minute;
        const bool start=changed&&initialized&&animate&&minute&&next==minute+1&&now>=observedAt&&now-observedAt<=2500;
        if(changed){previous=current;current=time;minute=next;running=start;startedAt=now;}
        else current=time;
        if(!animate){running=false;previous=current;}
        initialized=true;observedAt=now;return start;
    }
    bool Active(std::uint64_t now)const{return running&&now>=startedAt&&now-startedAt<DurationMs;}
    float Progress(std::uint64_t now)const{return Active(now)?static_cast<float>(now-startedAt)/DurationMs:1.f;}
    void Stop(){running=false;}
};

inline void DrawIslandFlipClock(WidgetCanvas& c,WidgetRect card,const SYSTEMTIME& now,float alpha,
                                const SYSTEMTIME* previous=nullptr,float phase=1.f){
    const float scale=std::min({1.f,card.width/200.f,card.height/144.f});
    const float left=card.x+(card.width-196*scale)/2,top=card.y+(card.height-96*scale)/2;
    phase=std::isfinite(phase)?std::clamp(phase,0.f,1.f):1.f;
    const auto number=[](unsigned value){wchar_t text[3]{};swprintf_s(text,L"%02u",value);return std::wstring(text);};
    const auto hour=[](const SYSTEMTIME& time){return (static_cast<unsigned>(time.wHour)%12u)?static_cast<unsigned>(time.wHour)%12u:12u;};
    c.PushClip(card);
    for(unsigned index=0;index<2;++index){
        const WidgetRect tile{left+index*102*scale,top,94*scale,96*scale};
        const float middle=top+48*scale;
        const auto fresh=number(index?now.wMinute:hour(now));
        const auto old=previous?number(index?previous->wMinute:hour(*previous)):fresh;
        const bool flip=previous&&phase<1&&(old!=fresh||(!index&&(previous->wHour>=12)!=(now.wHour>=12)));
        const float font=82*scale*std::min(1.f,90*scale/std::max(1.f,c.MeasureText(L"88",82*scale,WidgetTextWeight::Bold)));
        const WidgetRect text{tile.x,tile.y-4*scale,tile.width,99*scale};
        const auto face=[&](const std::wstring& digits,bool pm,float factor=1.f,float brightness=1.f){
            c.FoldText(digits,text,font,middle,factor,0xbebebe,alpha*brightness,WidgetTextWeight::Bold);
            if(!index)c.FoldText(pm?L"PM":L"AM",{tile.x+7*scale,tile.y+78*scale,20*scale,12*scale},
                8*scale,middle,factor,0xbebebe,alpha*brightness,WidgetTextWeight::Bold,WidgetTextAlign::Left);
        };
        c.Rounded(tile,8*scale,0x101010,alpha);
        c.PushClip({tile.x,tile.y,tile.width,48*scale});
        face(fresh,now.wHour>=12);c.PopClip();
        c.PushClip({tile.x,middle,tile.width,48*scale});
        face(flip?old:fresh,flip?previous->wHour>=12:now.wHour>=12);c.PopClip();
        if(flip){
            const bool first=phase<.5f;
            const float factor=std::abs(std::cos(phase*3.14159265358979323846f));
            c.PushClip({tile.x,first?tile.y:middle,tile.width,48*scale});
            c.Rounded({tile.x,middle-48*scale*factor,tile.width,96*scale*factor},
                std::min(8*scale,24*scale*factor),first?0x121212:0x151515,alpha);
            face(first?old:fresh,first?previous->wHour>=12:now.wHour>=12,factor,.72f+.28f*factor);
            c.PopClip();
        }
        c.Line(tile.x,middle,tile.x+tile.width,middle,0x000000,.96f*alpha,1.1f*scale);
        c.Line(tile.x+2*scale,middle+1.1f*scale,tile.x+tile.width-2*scale,middle+1.1f*scale,0xffffff,.035f*alpha,.5f*scale);
    }
    c.PopClip();
}

inline void DrawIslandPixelClock(WidgetCanvas& c,WidgetRect card,const SYSTEMTIME& now,float alpha){
    // Six columns by ten rows; thick square strokes, independent of fonts.
    static constexpr unsigned digits[10][10]{
        {63,63,51,51,51,51,51,51,63,63},
        {12,28,60,12,12,12,12,12,63,63},
        {63,63,3,3,63,63,48,48,63,63},
        {63,63,3,3,31,31,3,3,63,63},
        {51,51,51,51,63,63,3,3,3,3},
        {63,63,48,48,63,63,3,3,63,63},
        {63,63,48,48,63,63,51,51,63,63},
        {63,63,3,3,6,6,12,12,12,12},
        {63,63,51,51,63,63,51,51,63,63},
        {63,63,51,51,63,63,3,3,63,63}};
    const float scale=std::min({1.f,card.width/200.f,card.height/144.f});
    const float pitch=4*scale,cell=3.35f*scale;
    const float left=card.x+(card.width-47*pitch+.65f*scale)/2,top=card.y+(card.height-10*pitch+.65f*scale)/2;
    const unsigned values[]{now.wHour/10u,now.wHour%10u,now.wMinute/10u,now.wMinute%10u,now.wSecond/10u,now.wSecond%10u};
    c.PushClip(card);
    for(unsigned col=0;col<=47;++col)c.Line(left+col*pitch,top-3*pitch,left+col*pitch,top+13*pitch,
        0xffffff,(.035f-.025f*col/47)*alpha,.35f*scale);
    for(int row=-3;row<=13;++row)c.Line(left,top+row*pitch,left+47*pitch,top+row*pitch,0xffffff,.025f*alpha,.35f*scale);
    float x=left;
    for(unsigned index=0;index<6;++index){
        for(unsigned row=0;row<10;++row)for(unsigned col=0;col<6;++col)
            if(digits[values[index]%10][row]&(1u<<(5-col)))c.Rounded({x+col*pitch,top+row*pitch,cell,cell},0,0xd7d7d7,alpha);
        x+=7*pitch;
        if(index==1||index==3){
            for(const unsigned row:{2u,3u,6u,7u})for(unsigned col=0;col<2;++col)
                c.Rounded({x+col*pitch,top+row*pitch,cell,cell},0,0xd7d7d7,alpha);
            x+=3*pitch;
        }
    }
    c.PopClip();
}

// Uses the window's existing one-second time snapshot and foreground canvas.
inline void DrawIslandClockCard(WidgetCanvas& c,WidgetRect card,const SYSTEMTIME& now,
    bool english,IslandClockStyle style,float alpha=1.f,const SYSTEMTIME* previous=nullptr,float flipPhase=1.f) {
    if(alpha<=.01f||card.width<=0||card.height<=0)return;
    if(style==IslandClockStyle::Flip){DrawIslandFlipClock(c,card,now,alpha,previous,flipPhase);return;}
    if(style==IslandClockStyle::Pixel){DrawIslandPixelClock(c,card,now,alpha);return;}
    static constexpr const wchar_t* chineseDays[]{L"周日",L"周一",L"周二",L"周三",L"周四",L"周五",L"周六"};
    static constexpr const wchar_t* englishDays[]{L"Sun",L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat"};
    static constexpr const wchar_t* englishMonths[]{L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};
    const auto weekday=static_cast<unsigned>(now.wDayOfWeek)%7;
    const auto month=std::clamp(static_cast<unsigned>(now.wMonth),1u,12u);
    if(style==IslandClockStyle::Simple){
        const std::wstring date=english?
            std::wstring(englishDays[weekday])+L", "+englishMonths[month-1]+L" "+std::to_wstring(now.wDay):
            std::to_wstring(now.wMonth)+L"月"+std::to_wstring(now.wDay)+L"日 "+chineseDays[weekday];
        wchar_t time[6]{};
        swprintf_s(time,L"%02u:%02u",now.wHour,now.wMinute);
        const float scale=std::min(1.f,card.width/200.f);
        const float textWidth=std::max(1.f,card.width-16*scale);
        const float top=card.y+(card.height-94*scale)/2;
        const float dateSize=14*scale,timeSize=54*scale;
        const float dateFit=std::min(1.f,textWidth/std::max(1.f,c.MeasureText(date,dateSize,WidgetTextWeight::Semibold)));
        const float timeFit=std::min(1.f,textWidth/std::max(1.f,c.MeasureText(L"88:88",timeSize,WidgetTextWeight::Bold)));
        c.PushClip(card);
        c.Text(date,{card.x,top,card.width,20*scale},dateSize*dateFit,0xffffff,.72f*alpha,
            WidgetTextWeight::Semibold,WidgetTextAlign::Center);
        c.Text(time,{card.x,top+20*scale,card.width,74*scale},timeSize*timeFit,0xffffff,alpha,
            WidgetTextWeight::Bold,WidgetTextAlign::Center);
        c.PopClip();
        return;
    }

    if(style==IslandClockStyle::Digital){
        const float scale=std::min({1.f,card.width/200.f,card.height/144.f});
        const float left=card.x+(card.width-184*scale)/2,top=card.y+(card.height-38*scale)/2;
        // Segment order: top, upper right, lower right, bottom, lower left,
        // upper left, middle. Polygons keep this face independent of installed fonts.
        static constexpr unsigned segments[]{0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
        const unsigned numbers[]{now.wHour/10u,now.wHour%10u,now.wMinute/10u,now.wMinute%10u,now.wSecond/10u,now.wSecond%10u};
        c.PushClip(card);
        const auto horizontal=[&](float x,float y){
            c.Polygon({x+3.8f*scale,y,x+16.2f*scale,y,x+18*scale,y+1.8f*scale,
                x+16.2f*scale,y+3.6f*scale,x+3.8f*scale,y+3.6f*scale,x+2*scale,y+1.8f*scale},0xffffff,alpha);
        };
        const auto vertical=[&](float x,float y){
            c.Polygon({x+1.8f*scale,y,x+3.6f*scale,y+1.8f*scale,x+3.6f*scale,y+13.8f*scale,
                x+1.8f*scale,y+15.6f*scale,x,y+13.8f*scale,x,y+1.8f*scale},0xffffff,alpha);
        };
        float x=left;
        for(unsigned i=0;i<6;++i){
            const unsigned mask=segments[numbers[i]%10];
            if(mask&0x01)horizontal(x,top);
            if(mask&0x02)vertical(x+16.4f*scale,top+2*scale);
            if(mask&0x04)vertical(x+16.4f*scale,top+20.4f*scale);
            if(mask&0x08)horizontal(x,top+34.4f*scale);
            if(mask&0x10)vertical(x,top+20.4f*scale);
            if(mask&0x20)vertical(x,top+2*scale);
            if(mask&0x40)horizontal(x,top+17.2f*scale);
            x+=28*scale;
            if(i==1||i==3){
                c.Polygon({x,top+11*scale,x+4*scale,top+11*scale,x+4*scale,top+15*scale,x,top+15*scale},0xffffff,alpha);
                c.Polygon({x,top+23*scale,x+4*scale,top+23*scale,x+4*scale,top+27*scale,x,top+27*scale},0xffffff,alpha);
                x+=12*scale;
            }
        }
        c.PopClip();
        return;
    }

    if(style==IslandClockStyle::Date){
        const float scale=std::min({1.f,card.width/200.f,card.height/144.f});
        const float textWidth=184*scale,top=card.y+(card.height-108*scale)/2;
        wchar_t time[6]{},date[11]{};
        swprintf_s(time,L"%02u:%02u",now.wHour,now.wMinute);
        swprintf_s(date,L"%04u.%02u.%02u",now.wYear,now.wMonth,now.wDay);
        const float timeSize=64*scale*std::min(1.f,textWidth/std::max(1.f,c.MeasureText(L"88:88",64*scale,WidgetTextWeight::Bold)));
        c.PushClip(card);
        c.Text(time,{card.x,top,card.width,82*scale},timeSize,0xffffff,alpha,WidgetTextWeight::Bold,WidgetTextAlign::Center);
        c.Text(date,{card.x,top+82*scale,card.width,26*scale},18*scale,0xffffff,alpha,WidgetTextWeight::Bold,WidgetTextAlign::Center);
        c.PopClip();
        return;
    }

    constexpr std::uint32_t violet=0x6668f5,light=0xe6e8fb;
    const float scale=std::min({1.f,card.width/200.f,card.height/144.f});
    const float left=card.x+(card.width-168*scale)/2,top=card.y+(card.height-126*scale)/2;
    const float width=168*scale;
    const std::wstring date=english?std::wstring(englishMonths[month-1])+L" "+std::to_wstring(now.wDay)+L" ":
        std::to_wstring(now.wMonth)+L"月"+std::to_wstring(now.wDay)+L"日 ";
    const std::wstring dayName=english?englishDays[weekday]:chineseDays[weekday];
    const float dateSize=13*scale;
    const float dateWidth=c.MeasureText(date,dateSize,WidgetTextWeight::Semibold);
    const float dayWidth=c.MeasureText(dayName,dateSize,WidgetTextWeight::Semibold);
    const float dateX=left+(width-dateWidth-dayWidth)/2;
    c.PushClip(card);
    c.Text(date,{dateX,top,dateWidth+1,20*scale},dateSize,light,.84f*alpha,WidgetTextWeight::Semibold);
    c.Text(dayName,{dateX+dateWidth,top,dayWidth+1,20*scale},dateSize,violet,alpha,WidgetTextWeight::Semibold);

    wchar_t hours[3]{},minutes[4]{},seconds[9]{};
    swprintf_s(hours,L"%02u",now.wHour);
    swprintf_s(minutes,L":%02u",now.wMinute);
    swprintf_s(seconds,L"%02u:%02u:%02u",now.wHour,now.wMinute,now.wSecond);
    const float timeSize=52*scale*std::min(1.f,width/std::max(1.f,c.MeasureText(L"88:88",52*scale,WidgetTextWeight::Bold)));
    const float hoursWidth=c.MeasureText(hours,timeSize,WidgetTextWeight::Bold);
    const float minutesWidth=c.MeasureText(minutes,timeSize,WidgetTextWeight::Bold);
    const float timeX=left+(width-hoursWidth-minutesWidth)/2;
    c.Text(hours,{timeX,top+18*scale,hoursWidth+1,66*scale},timeSize,violet,alpha,WidgetTextWeight::Bold);
    c.Text(minutes,{timeX+hoursWidth,top+18*scale,minutesWidth+1,66*scale},timeSize,light,alpha,WidgetTextWeight::Bold);

    const auto progress=ReadIslandClockProgress(now);
    const double values[]{progress.day,progress.week};
    const wchar_t* labels[]{english?L"Today":L"今日",english?L"This week":L"本周"};
    const std::wstring details[]{seconds,(english?L"Day ":L"周")+std::to_wstring(progress.weekday)+L"/7"};
    const float columnWidth=76*scale;
    for(unsigned i=0;i<2;++i){
        const float x=left+i*92*scale;
        const float labelSize=(english?10.f:11.f)*scale;
        const float labelWidth=c.MeasureText(labels[i],labelSize,WidgetTextWeight::Semibold);
        c.Text(labels[i],{x,top+85*scale,labelWidth+1,17*scale},labelSize,i?light:violet,
            (i?.8f:1.f)*alpha,WidgetTextWeight::Semibold);
        const auto percent=std::to_wstring(static_cast<unsigned>(std::lround(values[i]*100)))+L"%";
        c.Text(percent,{x+labelWidth+4*scale,top+85*scale,columnWidth-labelWidth-4*scale,17*scale},
            labelSize,light,.9f*alpha,WidgetTextWeight::Semibold);
        c.Rounded({x,top+104*scale,columnWidth,3*scale},1.5f*scale,light,.18f*alpha);
        if(values[i]>0)c.Rounded({x,top+104*scale,columnWidth*static_cast<float>(values[i]),3*scale},
            1.5f*scale,i?light:violet,(i?.68f:1.f)*alpha);
        c.Text(details[i],{x,top+110*scale,columnWidth,15*scale},10*scale,light,.57f*alpha,WidgetTextWeight::Medium);
    }
    c.PopClip();
}
}
