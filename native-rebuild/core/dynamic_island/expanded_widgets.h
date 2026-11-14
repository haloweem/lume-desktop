#pragma once
#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace lume::dynamic_island {
enum class ExpandedWidget { Music, CalendarWeather, Focus, System, Todos, Clock };
inline constexpr std::array<std::string_view,6> ExpandedWidgetIds{"music","calendar-weather","focus","system","todos","clock"};
inline constexpr unsigned ExpandedLayoutLegacyMaximum=1295; // Four base-6 digits; saved values never change.
inline constexpr unsigned ExpandedLayoutClockOffset=ExpandedLayoutLegacyMaximum+1;
inline constexpr unsigned ExpandedLayoutClockMaximum=ExpandedLayoutClockOffset+2400; // Four base-7 digits for layouts containing Clock.
inline constexpr unsigned ExpandedLayoutFiveOffset=ExpandedLayoutClockMaximum+1;
inline constexpr unsigned ExpandedLayoutMaximum=ExpandedLayoutFiveOffset+719; // 6P5 ordered, distinct five-widget layouts.
inline bool DecodeExpandedLayout(unsigned code,std::vector<ExpandedWidget>& output) {
    if(code>ExpandedLayoutMaximum)return false;
    if(code>=ExpandedLayoutFiveOffset){
        unsigned rank=code-ExpandedLayoutFiveOffset;
        std::array<unsigned,5> digits{};
        for(int index=4;index>=0;--index){const unsigned radix=6-static_cast<unsigned>(index);digits[index]=rank%radix;rank/=radix;}
        std::vector<ExpandedWidget> remaining{ExpandedWidget::Music,ExpandedWidget::CalendarWeather,ExpandedWidget::Focus,
            ExpandedWidget::System,ExpandedWidget::Todos,ExpandedWidget::Clock},values;
        for(const auto digit:digits){values.push_back(remaining[digit]);remaining.erase(remaining.begin()+digit);}
        output=std::move(values);return true;
    }
    const bool extended=code>=ExpandedLayoutClockOffset;
    const unsigned base=extended?7:6;
    if(extended)code-=ExpandedLayoutClockOffset;
    std::vector<ExpandedWidget> values;unsigned seen=0;
    while(code){
        const unsigned digit=code%base;code/=base;
        if(!digit||values.size()>=4||(seen&(1u<<digit)))return false;
        seen|=1u<<digit;values.insert(values.begin(),static_cast<ExpandedWidget>(digit-1));
    }
    // Ordinary layouts retain exactly one representation in the legacy range.
    if(extended&&!(seen&(1u<<6)))return false;
    output=std::move(values);return true;
}
inline unsigned EncodeExpandedLayout(const std::vector<ExpandedWidget>& values) {
    if(values.size()>5)return ExpandedLayoutMaximum+1;
    unsigned seen=0;
    for(const auto value:values){
        const auto id=static_cast<unsigned>(value);
        if(id>=ExpandedWidgetIds.size()||(seen&(1u<<id)))return ExpandedLayoutMaximum+1;
        seen|=1u<<id;
    }
    if(values.size()==5){
        unsigned rank=0,used=0,index=0;
        for(const auto value:values){
            const auto id=static_cast<unsigned>(value);unsigned digit=0;
            for(unsigned lower=0;lower<id;++lower)if(!(used&(1u<<lower)))++digit;
            rank=rank*(6-index++)+digit;used|=1u<<id;
        }
        return ExpandedLayoutFiveOffset+rank;
    }
    const bool extended=(seen&(1u<<static_cast<unsigned>(ExpandedWidget::Clock)))!=0;
    unsigned code=0;for(const auto value:values)code=code*(extended?7:6)+static_cast<unsigned>(value)+1;
    return code+(extended?ExpandedLayoutClockOffset:0);
}
inline bool HasExpandedWidget(const std::vector<ExpandedWidget>& values,ExpandedWidget value) {
    for(const auto item:values)if(item==value)return true;return false;
}
}
