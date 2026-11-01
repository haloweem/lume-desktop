#include "core/dynamic_island/island_customization.h"
#include "vendor/nlohmann/json.hpp"

namespace lume::dynamic_island {
std::optional<Customization> ParseCustomization(std::string_view text) noexcept {
    try {
        if(text.empty()||text.size()>4096)return {};
        const auto j=nlohmann::json::parse(text,nullptr,false);
        if(!j.is_object()||j.size()!=7||!j.contains("version")||j["version"]!=1||
           !j.contains("customIcons")||!j["customIcons"].is_boolean()||
           !j.contains("overview")||!j["overview"].is_boolean()||
           !j.contains("enabled")||!j["enabled"].is_array()||j["enabled"].size()!=ExpandedWidgetIds.size()||
           !j.contains("order")||!j["order"].is_array()||j["order"].size()!=ExpandedWidgetIds.size())return {};
        Customization result;result.customIcons=j["customIcons"].get<bool>();result.overview=j["overview"].get<bool>();
        const auto side=[&](const char* key,MiniSelection& out){
            if(!j.contains(key))return false;const auto& value=j[key];
            if(!value.is_object()||value.size()!=2||!value.contains("widget")||!value["widget"].is_string()||
               !value.contains("style")||!value["style"].is_string())return false;
            out.widget=ParseMiniWidget(value["widget"].get<std::string>());
            const auto style=value["style"].get<std::string>();
            if(!Valid(out.widget)||(style!="circle"&&style!="capsule"))return false;
            out.style=style=="circle"?MiniStyle::Circle:MiniStyle::Capsule;return true;
        };
        if(!side("left",result.left)||!side("right",result.right))return {};
        std::array<bool,ExpandedWidgetIds.size()> seen{};
        for(std::size_t i=0;i<ExpandedWidgetIds.size();++i){
            if(!j["enabled"][i].is_boolean()||!j["order"][i].is_string())return {};
            result.enabled[i]=j["enabled"][i].get<bool>();const auto id=j["order"][i].get<std::string>();
            const auto it=std::find(ExpandedWidgetIds.begin(),ExpandedWidgetIds.end(),id);
            if(it==ExpandedWidgetIds.end())return {};const auto index=static_cast<std::size_t>(it-ExpandedWidgetIds.begin());
            if(seen[index])return {};seen[index]=true;result.order[i]=static_cast<ExpandedWidget>(index);
        }
        return result;
    }catch(...){return {};}
}
std::string SerializeCustomization(const Customization& value){
    const auto side=[](const MiniSelection& s){return nlohmann::json{{"widget",MiniWidgetId(s.widget)},
        {"style",s.style==MiniStyle::Circle?"circle":"capsule"}};};
    auto order=nlohmann::json::array();for(const auto card:value.order)order.push_back(ExpandedWidgetIds[static_cast<std::size_t>(card)]);
    return nlohmann::json{{"version",1},{"customIcons",value.customIcons},{"overview",value.overview},
        {"left",side(value.left)},{"right",side(value.right)},{"enabled",value.enabled},{"order",order}}.dump();
}
}
