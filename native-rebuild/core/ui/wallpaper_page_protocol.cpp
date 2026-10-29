#include "core/ui/wallpaper_page_protocol.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include "vendor/nlohmann/json.hpp"
#include <charconv>
#include <set>
#include <stdexcept>
namespace lume::ui {
bool ValidWallpaperId(std::string_view id) noexcept {
    return lume::wallpaper::IsSafeWallpaperId(id);
}
std::optional<WallpaperPageCommand> ParseWallpaperPageJson(std::wstring_view input) noexcept {
    if(input.empty()||input.size()>1024)return {};
    try {
        std::string text;text.reserve(input.size());
        for(const auto c:input){if(static_cast<std::uint32_t>(c)>127)return {};text.push_back(static_cast<char>(c));}
        using Json=nlohmann::json;
        std::set<std::string> keys;
        const auto value=Json::parse(text,[&](int depth,Json::parse_event_t event,Json& parsed){
            if(depth>1)throw std::runtime_error("nested wallpaper command");
            if(event==Json::parse_event_t::key&&!keys.insert(parsed.get<std::string>()).second)
                throw std::runtime_error("duplicate wallpaper key");
            return true;
        });
        if(!value.is_object()||!value.contains("version")||!value["version"].is_number_integer()||
            value["version"]!=1||value.value("scope",std::string{})!="wallpaper"||
            !value.contains("action")||!value["action"].is_string())return {};
        const auto name=value["action"].get<std::string>();
        WallpaperPageCommand command;
        bool hasId=false,hasDisplay=false,hasValue=false;
        if(name=="snapshot")command.action=WallpaperAction::Snapshot;
        else if(name=="refresh")command.action=WallpaperAction::Refresh;
        else if(name=="import")command.action=WallpaperAction::Import;
        else if(name=="stopAll")command.action=WallpaperAction::StopAll;
        else if(name=="cancel")command.action=WallpaperAction::Cancel;
        else if(name=="preview"){command.action=WallpaperAction::Preview;hasId=true;}
        else if(name=="endPreview")command.action=WallpaperAction::EndPreview;
        else if(name=="download"){command.action=WallpaperAction::Download;hasId=true;}
        else if(name=="remove"){command.action=WallpaperAction::Remove;hasId=true;}
        else if(name=="apply"){command.action=WallpaperAction::Apply;hasId=hasDisplay=true;}
        else if(name=="applyAll"){command.action=WallpaperAction::ApplyAll;hasId=true;}
        else if(name=="like"){command.action=WallpaperAction::Like;hasId=hasValue=true;}
        else if(name=="stats"){command.action=WallpaperAction::Stats;hasId=true;}
        else if(name=="endStats")command.action=WallpaperAction::EndStats;
        else if(name=="pause"){command.action=WallpaperAction::Pause;hasDisplay=true;}
        else if(name=="resume"){command.action=WallpaperAction::Resume;hasDisplay=true;}
        else if(name=="stop"){command.action=WallpaperAction::Stop;hasDisplay=true;}
        else if(name=="fit"){command.action=WallpaperAction::Fit;hasDisplay=hasValue=true;}
        else if(name=="volume"){command.action=WallpaperAction::Volume;hasDisplay=hasValue=true;}
        else if(name=="pauseFullscreen"){command.action=WallpaperAction::PauseFullscreen;hasValue=true;}
        else return {};
        if(value.size()!=3+static_cast<std::size_t>(hasId)+hasDisplay+hasValue)return {};
        if(hasId) {
            if(!value.contains("id")||!value["id"].is_string())return {};
            command.id=value["id"].get<std::string>();
            if(!ValidWallpaperId(command.id))return {};
        }
        if(hasDisplay) {
            if(!value.contains("display")||!value["display"].is_string())return {};
            const auto display=value["display"].get<std::string>();
            if(display.empty()||display.front()=='0'||display.size()>20)return {};
            const auto parsed=std::from_chars(display.data(),display.data()+display.size(),command.display);
            if(parsed.ec!=std::errc{}||parsed.ptr!=display.data()+display.size()||!command.display)return {};
        }
        if(hasValue) {
            if(!value.contains("value"))return {};
            if(command.action==WallpaperAction::Volume) {
                if(!value["value"].is_number_integer()||value["value"]<0||value["value"]>100)return {};
                command.volume=value["value"].get<int>();
            } else if(command.action==WallpaperAction::Fit) {
                if(!value["value"].is_string())return {};
                const auto fit=value["value"].get<std::string>();
                if(fit!="cover"&&fit!="contain")return {};
                command.flag=fit=="cover";
            } else {
                if(!value["value"].is_boolean())return {};
                command.flag=value["value"].get<bool>();
            }
        }
        return command;
    }catch(...){return {};}
}
}
