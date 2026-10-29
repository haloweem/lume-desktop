#include "core/wallpaper/playback_preferences.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include "vendor/nlohmann/json.hpp"
#include <array>
#include <charconv>
#include <set>
#include <stdexcept>
namespace lume::wallpaper {
namespace {
using Json=nlohmann::json;
constexpr std::size_t MaxBytes=65536;
Json Parse(const std::string& text) {
    if(text.empty()||text.size()>MaxBytes)throw std::runtime_error("wallpaper settings size");
    std::array<std::set<std::string>,6> keys;
    return Json::parse(text,[&](int depth,Json::parse_event_t event,Json& parsed){
        if(depth<0||depth>=static_cast<int>(keys.size()))throw std::runtime_error("wallpaper settings depth");
        if(event==Json::parse_event_t::object_start)keys[static_cast<std::size_t>(depth)].clear();
        if(event==Json::parse_event_t::key&&
            !keys[static_cast<std::size_t>(depth-1)].insert(parsed.get<std::string>()).second)
            throw std::runtime_error("wallpaper duplicate key");
        return true;
    });
}
bool UnknownVersion(const std::string& text) {
    try {
        const auto json=Parse(text);
        return json.is_object()&&json.contains("version")&&
            (!json["version"].is_number_integer()||json["version"]!=1);
    }catch(...){return false;}
}
}
bool EncodePlaybackPreferences(const PlaybackPreferences& p,std::string& out) {
    if(p.displays.size()>32)return false;
    try {
        Json displays=Json::array();std::set<std::uint64_t> ids;
        for(const auto& item:p.displays) {
            if(!item.display||!ids.insert(item.display).second||!IsSafeWallpaperId(item.wallpaperId)||
                item.volume<0||item.volume>100)return false;
            displays.push_back({{"display",std::to_string(item.display)},{"wallpaperId",item.wallpaperId},
                {"paused",item.paused},{"volume",item.volume},{"fit",item.cover?"cover":"contain"}});
        }
        const auto text=Json{{"version",1},{"pauseFullscreen",p.pauseFullscreen},{"displays",displays}}.dump();
        if(text.size()>MaxBytes)return false;
        out=text;return true;
    }catch(...){return false;}
}
bool DecodePlaybackPreferences(const std::string& text,PlaybackPreferences& out) {
    try {
        const auto json=Parse(text);
        if(!json.is_object()||json.size()!=3||!json.contains("version")||!json["version"].is_number_integer()||
            json["version"]!=1||!json.contains("pauseFullscreen")||!json["pauseFullscreen"].is_boolean()||
            !json.contains("displays")||!json["displays"].is_array()||json["displays"].size()>32)return false;
        PlaybackPreferences result;result.pauseFullscreen=json["pauseFullscreen"].get<bool>();
        std::set<std::uint64_t> ids;
        for(const auto& value:json["displays"]) {
            if(!value.is_object()||value.size()!=5||!value.contains("display")||!value["display"].is_string()||
                !value.contains("wallpaperId")||!value["wallpaperId"].is_string()||
                !value.contains("paused")||!value["paused"].is_boolean()||
                !value.contains("volume")||!value["volume"].is_number_integer()||value["volume"]<0||value["volume"]>100||
                !value.contains("fit")||!value["fit"].is_string())return false;
            PlaybackPreference item;
            const auto display=value["display"].get<std::string>();
            if(display.empty()||display.front()=='0'||display.size()>20)return false;
            const auto number=std::from_chars(display.data(),display.data()+display.size(),item.display);
            if(number.ec!=std::errc{}||number.ptr!=display.data()+display.size()||!item.display||!ids.insert(item.display).second)return false;
            item.wallpaperId=value["wallpaperId"].get<std::string>();
            if(!IsSafeWallpaperId(item.wallpaperId))return false;
            const auto fit=value["fit"].get<std::string>();
            if(fit!="cover"&&fit!="contain")return false;
            item.cover=fit=="cover";item.volume=value["volume"].get<int>();item.paused=value["paused"].get<bool>();
            result.displays.push_back(std::move(item));
        }
        out=std::move(result);return true;
    }catch(...){return false;}
}
LoadedPlaybackPreferences PlaybackPreferencesStore::Load(const std::string& path)const {
    LoadedPlaybackPreferences result;
    if(path.empty()){result.status="failed";result.writable=false;return result;}
    std::string text;
    const bool exists=files_.Exists(path),backup=files_.Exists(path+".bak");
    if(!exists&&!backup)return result;
    if(exists&&files_.ReadBounded(path,MaxBytes,text)) {
        if(UnknownVersion(text)){result.status="failed";result.writable=false;return result;}
        if(DecodePlaybackPreferences(text,result.value)){result.status="loaded";return result;}
    }
    if(backup&&files_.ReadBounded(path+".bak",MaxBytes,text)&&DecodePlaybackPreferences(text,result.value)) {
        result.status="recovered";return result;
    }
    result.status="failed";result.writable=false;return result;
}
bool PlaybackPreferencesStore::Save(const std::string& path,const PlaybackPreferences& p) {
    if(path.empty()||!Load(path).writable)return false;
    std::string text;if(!EncodePlaybackPreferences(p,text))return false;
    std::string previous;PlaybackPreferences decoded;
    const bool validPrevious=files_.ReadBounded(path,MaxBytes,previous)&&DecodePlaybackPreferences(previous,decoded);
    const auto temporary=path+".tmp",backupTemporary=path+".bak.tmp";
    const auto fail=[&]{files_.Remove(temporary);files_.Remove(backupTemporary);return false;};
    if(!files_.Write(temporary,text))return fail();
    if(validPrevious&&(!files_.Write(backupTemporary,previous)||!files_.Rename(backupTemporary,path+".bak")))return fail();
    if(!files_.Rename(temporary,path))return fail();
    return true;
}
}
