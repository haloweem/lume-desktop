#include "platform/windows/calendar/calendar_layout.h"
#include <algorithm>
#include <cmath>
namespace lume::platform::windows {
namespace {
// The todo theme keeps its schedule panes always open, so the agenda toggle does not apply.
CalendarLayout BoardLayout(float width,float height,float titleWidth,calendar::BoardView view) noexcept {
    CalendarLayout l;
    const bool compact=width<=560||height<=400;
    const float px=compact?16.0f:22.0f,pt=compact?14.0f:20.0f,pb=compact?12.0f:18.0f;
    const float w=std::max(0.0f,width-2*px-2),h=std::max(0.0f,height-pt-pb-2),x=px+1,y=pt+1;
    l.content={x,y,w,h};
    const bool all=view==calendar::BoardView::All;
    // Too narrow for two columns: 全 keeps the schedule and drops the month.
    const bool single=width<460;
    const float rw=single?0.0f:std::clamp(w*.46f,180.0f,380.0f),gap=single?0.0f:compact?14.0f:24.0f,rx=x+w-rw,lw=std::max(0.0f,w-rw-gap);
    l.monthFont=15;l.yearFont=15;l.weekdayFont=12;l.dayFont=compact?13.0f:15.0f;l.numberDiameter=compact?24.0f:28.0f;
    // Header: the selected day, the view switcher, new (outside 全) and today.
    l.boardHeading={x,y,lw,54};
    l.boardToday={x+lw-52,y+3,52,26};
    constexpr float segment=34;
    float segX=l.boardToday.x-36-4*segment,segY=y+3,top=y+(compact?62.0f:70.0f);
    if(segX<x+150){segX=x;segY=y+58;top=segY+26+(compact?8.0f:12.0f);}
    for(int i=0;i<4;++i)l.boardViews[static_cast<std::size_t>(i)]={segX+static_cast<float>(i)*segment,segY,segment,26};
    if(titleWidth<=0)titleWidth=96;
    if(!single){
        l.header={rx,y,rw,32};
        titleWidth=std::clamp(titleWidth+8,0.0f,std::max(0.0f,rw-68));
        l.previous={rx,y+1,30,30};l.next={rx+rw-30,y+1,30,30};
        l.title={rx+(rw-titleWidth)/2,y+1,titleWidth,30};
    }else if(!all){
        titleWidth=std::clamp(titleWidth+8,0.0f,std::max(0.0f,w-68));
        l.previous={x,top,30,30};l.next={x+w-30,top,30,30};
        l.title={x+(w-titleWidth)/2,top,titleWidth,30};
        top+=38;
    }
    if(all){
        const float bottom=height>=380?std::clamp(h*.22f,88.0f,140.0f):0;
        l.boardUpcoming={x,l.content.Bottom()-bottom,w,bottom};
        const float upper=bottom>0?l.boardUpcoming.y-14:l.content.Bottom();
        if(!single){
            l.weekdays={rx,y+38,rw,20};
            l.days={rx,l.weekdays.Bottom()+4,rw,std::max(0.0f,upper-l.weekdays.Bottom()-4)};
            for(int i=0;i<42;++i) l.cells[static_cast<std::size_t>(i)]={rx+(i%7)*rw/7,l.days.y+(i/7)*l.days.height/6,rw/7,l.days.height/6};
        }
        const float card=compact?64.0f:76.0f;
        l.boardNext={x,top,lw,card};
        l.boardDay={x,l.boardNext.Bottom()+14,lw,std::max(0.0f,upper-l.boardNext.Bottom()-14)};
    }else{
        l.boardBody={x,top,w,std::max(0.0f,l.content.Bottom()-top)};
        if(view==calendar::BoardView::Month){
            l.weekdays={x,top,w,20};
            l.days={x,top+22,w,std::max(0.0f,l.content.Bottom()-top-22)};
            for(int i=0;i<42;++i) l.cells[static_cast<std::size_t>(i)]={x+(i%7)*w/7,l.days.y+(i/7)*l.days.height/6,w/7,l.days.height/6};
        }
    }
    l.create={l.boardToday.x-32,y+2,28,28};
    l.compactPicker=width<=360||height<=360;
    const float pw=std::min(304.0f,std::max(0.0f,width-40));
    l.picker={std::clamp(l.title.x+l.title.width/2-pw/2,1.0f,std::max(1.0f,width-pw-1)),l.title.Bottom()+6,pw,l.compactPicker?203.0f:231.0f};
    l.picker.height=std::min(l.picker.height,std::max(0.0f,height-l.picker.y-1));
    l.cornerRadius=std::max(22.0f,std::min(width*0.064705882353f,height*0.061111111111f));
    return l;
}
}  // namespace
CalendarLayout CalculateCalendarLayout(calendar::Theme theme,float width,float height,float titleWidth,bool agendaOpen,calendar::BoardView view) noexcept {
    if(theme==calendar::Theme::Todo) return BoardLayout(width,height,titleWidth,view);
    CalendarLayout l;
    const bool split=theme!=calendar::Theme::Spacious;
    float px=split?26.0f:28.0f,pt=24,pb=20,hh=split?48.0f:52.0f,wh=24,gap=split?8.0f:10.0f,colgap=22;
    if(split) {
        l.monthFont=22; l.yearFont=22; l.weekdayFont=13; l.dayFont=21;
        if(width<=680||height<=420) { px=18; pt=18; pb=14; colgap=14; l.summaryFont=82; l.dayFont=17; l.summaryLunarFont=13; l.summaryRemainingFont=11; }
        if(width<=480) { px=12; pt=16; pb=12; colgap=8; gap=6; l.summaryFont=56; l.dayFont=14; l.weekdayFont=11; l.dayGap=0; l.summaryBottomPadding=0; l.summaryLunarFont=11; }
    } else {
        if(width<=600||height<=460) { px=18; pt=18; pb=14; hh=42; wh=20; gap=6; l.monthFont=30; l.yearFont=16; l.dayFont=19; l.numberDiameter=28; }
        if(width<=380||height<=390) { px=12; pt=14; pb=12; hh=36; wh=18; gap=4; l.monthFont=26; l.yearFont=14; l.weekdayFont=11; l.dayFont=16; l.numberDiameter=22; l.lunarHeight=12; l.dayGap=0; }
    }
    if(agendaOpen){hh=width<=600?36.0f:48.0f;wh=width<=600?18.0f:24.0f;}
    const float w=std::max(0.0f,width-2*px-2),h=std::max(0.0f,height-pt-pb-2),x=px+1,y=pt+1;
    l.content={x,y,w,h}; l.header={x,y,w,hh};
    const float left=split?std::max(0.0f,(w-colgap)/2.35f):0;
    const float agendaWidth=agendaOpen?std::max(110.0f,std::floor(w*.29f*64)/64):0;
    const float dx=agendaOpen?x:split?x+left+colgap:x;
    const float dw=agendaOpen?w-agendaWidth-(width<=600?8.0f:20.0f):split?w-left-colgap:w;
    l.weekdays={dx,y+hh+gap,dw,wh};
    l.days={dx,l.weekdays.Bottom()+gap,dw,std::max(0.0f,l.content.Bottom()-l.weekdays.Bottom()-gap)};
    if(split&&!agendaOpen) l.summary={x,l.weekdays.y,left,l.content.Bottom()-l.weekdays.y};
    if(agendaOpen)l.agenda={l.content.Right()-agendaWidth,l.weekdays.y,agendaWidth,l.content.Bottom()-l.weekdays.y};
    float nav=34,hgap=10,action=34,actionHeight=34,actionGap=8,navDraw=34;
    if(!split && (height<=460)) navDraw=30;
    if(!split && (height<=390)) navDraw=28;
    if(width<=600) { nav=26; navDraw=26; hgap=4; action=26; actionHeight=30; actionGap=2; l.monthFont=20; l.yearFont=12; }
    if(titleWidth<=0) titleWidth=width<=600?(split?95.640625f:73.859375f):(split?105.203125f:123.015625f);
    titleWidth=std::clamp(titleWidth,0.0f,std::max(0.0f,w-nav*2-hgap*3-action*2-actionGap));
    l.previous={x,y+(hh-navDraw)/2,navDraw,navDraw};
    const float titleHeight=width<=600?32.0f:split?40.0f:hh;
    l.title={x+nav+hgap,y+(hh-titleHeight)/2,titleWidth,titleHeight};
    l.next={l.title.Right()+hgap,y+(hh-navDraw)/2,navDraw,navDraw};
    l.list={l.content.Right()-action,y+(hh-actionHeight)/2,action,actionHeight};
    l.create={l.list.x-actionGap-action,l.list.y,action,actionHeight};
    l.compactPicker=width<=360||height<=360;
    const float pickerWidth=std::min(304.0f,split?width-40:w);
    l.picker={x,y+hh+(l.compactPicker?6.0f:8.0f),pickerWidth,l.compactPicker?203.0f:231.0f};
    l.picker.height=std::min(l.picker.height,std::max(0.0f,height-l.picker.y-1));
    l.cornerRadius=std::max(22.0f,std::min(width*0.064705882353f,height*0.061111111111f));
    for(int i=0;i<42;++i) l.cells[static_cast<std::size_t>(i)]={dx+(i%7)*dw/7,l.days.y+(i/7)*l.days.height/6,dw/7,l.days.height/6};
    return l;
}
CalendarHit CalendarLayout::HitTest(float x,float y) const noexcept {
    if(previous.Contains(x,y)) return {CalendarHitKind::Previous,-1};
    if(title.Contains(x,y)) return {CalendarHitKind::Title,-1};
    if(next.Contains(x,y)) return {CalendarHitKind::Next,-1};
    if(create.Contains(x,y)) return {CalendarHitKind::Create,-1};
    if(list.Contains(x,y)) return {CalendarHitKind::List,-1};
    if(boardToday.Contains(x,y)) return {CalendarHitKind::Today,-1};
    for(int i=0;i<4;++i) if(boardViews[static_cast<std::size_t>(i)].Contains(x,y)) return {CalendarHitKind::View,i};
    for(int i=0;i<42;++i) if(cells[static_cast<std::size_t>(i)].Contains(x,y)) return {CalendarHitKind::Day,i};
    return {};
}
CalendarRect CalendarLayout::Highlight(int index) const noexcept {
    if(index<0||index>=42) return {};
    auto r=cells[static_cast<std::size_t>(index)]; const auto w=std::min(52.0f,r.width),h=std::min(52.0f,r.height);
    return {r.x+(r.width-w)/2,r.y+(r.height-h)/2,w,h};
}
} // namespace lume::platform::windows
