#include "platform/windows/almanac/almanac_view.h"
#include "platform/windows/text_util.h"
#include <algorithm>
#include <cmath>
namespace lume::platform::windows {
namespace {
using lume::text::Wide;
std::wstring Join(const std::vector<std::string_view>& values){
    std::wstring result;for(const auto value:values){if(!result.empty())result+=L"、";result+=Wide(value);}return result;
}
bool Contains(WidgetRect r,float x,float y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
}
void AlmanacView::SetDate(calendar::Date date){
    if(date==date_&&revision_)return;
    date_=date;day_=almanac::Lookup(date);offsets_={};maximum_={};++revision_;
    lunar_=day_?Wide(day_->lunarDate):L"日期超出范围";
    year_=day_?Wide(day_->year):L"支持 1900—2100 年";
    cycle_=day_?Wide(day_->cycle):L"";
    activities_={day_?Join(day_->yi):L"--",day_?Join(day_->ji):L"--"};
}
std::vector<LumeMaterialPanel> AlmanacView::Panels(float width,float height)const{
    const bool small=width-34<=340;
    const float gap=small?14.f:22.f,column=(width-34-gap)/2.08f;
    const float x=17+column+gap,y=18+(small?26.f:22.f),rowGap=small?8.f:12.f;
    const float rowHeight=std::max(1.f,(height-14-y-rowGap)/2);
    return {{x,y,column*1.08f,rowHeight,8},{x,y+rowHeight+rowGap,column*1.08f,rowHeight,8}};
}
void AlmanacView::Draw(WidgetCanvas& c,float width,float height){
    const bool small=width-34<=340;
    const float gap=small?14.f:22.f,column=(width-34-gap)/2.08f,seal=small?22.f:26.f;
    const float headingSize=small?12.f:14.f,cycleSize=small?11.f:14.f;
    c.Rounded({17,18,seal,seal},6,0xd5ac25,.85f);
    c.Text(L"历",{17,18,seal,seal},small?14.f:16.f,0xffffff,.85f,false,WidgetTextAlign::Center);
    const float headingX=17+seal+(small?8.f:10.f);
    c.Text(L"黄历",{headingX,18,column-headingX+17,seal},headingSize,0xffffff,.85f);
    constexpr auto lunarWeight=static_cast<WidgetTextWeight>(650); // Original #almanac-lunar.
    const float lunarSize=std::min(32.f,column*32/std::max(1.f,c.MeasureText(lunar_,32,lunarWeight)));
    const float lunarHeight=lunarSize*1.4f,cycleHeight=cycleSize*2.8f+2;
    const float dateGap=std::max(small?6.f:12.f,(height-32-seal-lunarHeight-cycleHeight)/2);
    c.Text(lunar_,{17,18+seal+dateGap,column,lunarHeight},lunarSize,0xffffff,1,lunarWeight);
    const float cycleY=18+seal+dateGap+lunarHeight+dateGap;
    c.Text(year_,{17,cycleY,column,cycleSize*1.4f},cycleSize,0xffffff,.72f);
    c.Text(cycle_,{17,cycleY+cycleSize*1.4f+2,column,cycleSize*1.4f},cycleSize,0xffffff,.72f);
    const auto panels=Panels(width,height);
    for(unsigned i=0;i<2;++i){
        const auto panel=panels[i];rows_[i]={panel.x,panel.y,panel.width,panel.height};
        const float pad=small?10.f:16.f,badgeGap=small?8.f:12.f,font=small?12.f:15.f;
        const float badgeY=panel.y+(panel.height-seal)/2;
        c.Rounded({panel.x+pad,badgeY,seal,seal},seal/2,i?0x9f5147:0x687842);
        c.Text(i?L"忌":L"宜",{panel.x+pad,badgeY,seal,seal},small?14.f:16.f,0xffffff,1,false,WidgetTextAlign::Center);
        const float x=panel.x+pad+seal+badgeGap;
        viewports_[i]={x,panel.y+2,std::max(0.f,panel.width-pad*2-seal-badgeGap),panel.height-4};
        maximum_[i]=std::max(0.f,c.MeasureText(activities_[i],font)-viewports_[i].width);
        offsets_[i]=std::clamp(offsets_[i],0.f,maximum_[i]);
        c.ScrollText(activities_[i],viewports_[i],font,offsets_[i]);
        // The original overlay scrollbar is absent in the unfocused default view.
        if(maximum_[i]>0&&(focused_==static_cast<int>(i)||offsets_[i]>0)){
            const auto r=viewports_[i];const float thumb=std::max(6.f,r.width*r.width/(r.width+maximum_[i]));
            c.Rounded({r.x+(r.width-thumb)*offsets_[i]/maximum_[i],r.y+r.height-3,thumb,2},1,0xffffff,.3f);
        }
        if(focused_==static_cast<int>(i)){
            const auto r=viewports_[i];
            c.Line(r.x,r.y,r.x+r.width,r.y,0x0a84ff,1,2);
            c.Line(r.x,r.y+r.height,r.x+r.width,r.y+r.height,0x0a84ff,1,2);
            c.Line(r.x,r.y,r.x,r.y+r.height,0x0a84ff,1,2);
            c.Line(r.x+r.width,r.y,r.x+r.width,r.y+r.height,0x0a84ff,1,2);
        }
    }
}
int AlmanacView::ActivityAt(float x,float y)const{
    for(int i=0;i<2;++i)if(Contains(rows_[i],x,y))return i;
    return -1;
}
bool AlmanacView::Scroll(int row,float value,bool absolute){
    if(row<0||row>1||!std::isfinite(value))return false;
    const float next=std::clamp(absolute?value:offsets_[row]+value,0.f,maximum_[row]);
    if(next==offsets_[row])return false;
    offsets_[row]=next;return true;
}
}
