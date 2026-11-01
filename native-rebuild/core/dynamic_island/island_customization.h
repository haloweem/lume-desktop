#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace lume::dynamic_island {
// Stable identifiers are shared with the page protocol and persisted files.
// Each side is independent; None deliberately leaves its display slot empty.
enum class MiniWidget { None, Date, Weekday, Lunar, Clock, DayProgress, WeekProgress, MonthProgress,
    YearProgress, Battery, Cpu, Memory, Disk, Weather, Temperature, Humidity, Wind,
    Network, Music, Focus, Planet, Flame, Heart, Gift, Butterfly, Moon, Snow,
    Orbit, Coffee, Cat, Tree, Count };
inline constexpr std::array<std::string_view, static_cast<std::size_t>(MiniWidget::Count)> MiniWidgetIds{
    "none","date","weekday","lunar","clock","dayProgress","weekProgress","monthProgress","yearProgress",
    "battery","cpu","memory","disk","weather","temperature","humidity","wind",
    "network","music","focus","planet","flame","heart","gift","butterfly",
    "moon","snow","orbit","coffee","cat","tree"};
enum class MiniStyle { Circle, Capsule };
inline constexpr bool Valid(MiniWidget widget) {
    return static_cast<unsigned>(widget) < static_cast<unsigned>(MiniWidget::Count);
}
inline MiniWidget ParseMiniWidget(std::string_view id) {
    const auto it=std::find(MiniWidgetIds.begin(),MiniWidgetIds.end(),id);
    return it==MiniWidgetIds.end()?MiniWidget::Count:static_cast<MiniWidget>(it-MiniWidgetIds.begin());
}
inline constexpr std::string_view MiniWidgetId(MiniWidget widget) {
    return Valid(widget)?MiniWidgetIds[static_cast<std::size_t>(widget)]:std::string_view{};
}
enum class ExpandedWidget { Music, Calendar, Weather, Performance, Todo, Reminder, Focus, Launchpad, Actions, Count };
inline constexpr std::array<std::string_view,static_cast<std::size_t>(ExpandedWidget::Count)> ExpandedWidgetIds{
    "music","calendar","weather","performance","todo","reminder","focus","launchpad","actions"};
struct MiniSelection {
    MiniWidget widget=MiniWidget::None;
    MiniStyle style=MiniStyle::Circle;
};
inline bool operator==(const MiniSelection& a,const MiniSelection& b) {return a.widget==b.widget&&a.style==b.style;}
struct Customization {
    bool customIcons=false;
    bool overview=false;
    MiniSelection left{MiniWidget::Date,MiniStyle::Circle};
    MiniSelection right{MiniWidget::Battery,MiniStyle::Circle};
    std::array<bool,ExpandedWidgetIds.size()> enabled{true,true,true,true,true,true,false,false,false};
    std::array<ExpandedWidget,ExpandedWidgetIds.size()> order{ExpandedWidget::Music,ExpandedWidget::Calendar,
        ExpandedWidget::Weather,ExpandedWidget::Performance,ExpandedWidget::Todo,ExpandedWidget::Reminder,
        ExpandedWidget::Focus,ExpandedWidget::Launchpad,ExpandedWidget::Actions};
    bool Enabled(ExpandedWidget widget)const {
        const auto index=static_cast<std::size_t>(widget);return index<enabled.size()&&enabled[index];
    }
    bool Move(ExpandedWidget widget,int direction) {
        const auto it=std::find(order.begin(),order.end(),widget);
        if(it==order.end()||(direction<0&&it==order.begin())||(direction>0&&it+1==order.end()))return false;
        if(direction!=1&&direction!=-1)return false;
        std::iter_swap(it,it+direction);return true;
    }
    bool MoveTo(ExpandedWidget widget,std::size_t target) {
        if(target>=order.size())return false;
        const auto found=std::find(order.begin(),order.end(),widget);
        if(found==order.end())return false;
        const auto source=static_cast<std::size_t>(found-order.begin());if(source==target)return false;
        const auto value=*found;
        if(target<source)std::move_backward(order.begin()+static_cast<std::ptrdiff_t>(target),found,found+1);
        else std::move(found+1,order.begin()+static_cast<std::ptrdiff_t>(target+1),found);
        order[target]=value;return true;
    }
};
inline bool operator==(const Customization& a,const Customization& b) {
    return a.customIcons==b.customIcons&&a.overview==b.overview&&a.left==b.left&&a.right==b.right&&a.enabled==b.enabled&&a.order==b.order;
}
std::optional<Customization> ParseCustomization(std::string_view) noexcept;
std::string SerializeCustomization(const Customization&);
inline bool NeedsPerformance(MiniWidget widget) {
    return widget==MiniWidget::Cpu||widget==MiniWidget::Memory||widget==MiniWidget::Network;
}
inline bool Animated(MiniWidget widget) {
    return widget>=MiniWidget::Planet&&widget<MiniWidget::Count;
}
} // namespace lume::dynamic_island
