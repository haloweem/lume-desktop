#include "core/weather/weather_preferences.h"
#include "vendor/nlohmann/json.hpp"
#include <array>
#include <set>
#include <stdexcept>
namespace lume::weather {
bool DecodePreferences(const std::string& text,Preferences& out){
    if(text.empty()||text.size()>8192)return false;
    try{
        using Json=nlohmann::json;
        std::array<std::set<std::string>,4> keys;
        const auto value=Json::parse(text,[&](int depth,Json::parse_event_t event,Json& parsed){
            if(depth<0||depth>=static_cast<int>(keys.size()))throw std::runtime_error("settings depth");
            if(event==Json::parse_event_t::object_start)keys[static_cast<std::size_t>(depth)].clear();
            if(event==Json::parse_event_t::key&&!keys[static_cast<std::size_t>(depth-1)].insert(parsed.get<std::string>()).second)
                throw std::runtime_error("duplicate key");
            return true;
        });
        Preferences p;
        if(!value.is_object()||value.size()!=2||!value.contains("city")||!value["city"].is_string()||
            !value.contains("window"))return false;
        p.city=value["city"].get<std::string>();
        if(!ValidCity(p.city)||!widget::Decode(value["window"].dump(),p.window,WindowLimits))return false;
        out=std::move(p);return true;
    }catch(...){return false;}
}
bool EncodePreferences(const Preferences& p,std::string& out){
    std::string window;
    if(!ValidCity(p.city)||!widget::Encode(p.window,window,WindowLimits))return false;
    out="{\"city\":\""+p.city+"\",\"window\":"+window+"}";return true;
}
LoadedPreferences PreferencesStore::Load(const std::string& path)const{
    LoadedPreferences loaded;
    if(path.empty()){loaded.state=widget::LoadState::Failed;return loaded;}
    std::string text;
    if(files_.Exists(path)){
        if(files_.Read(path,text)&&DecodePreferences(text,loaded.value)){loaded.state=widget::LoadState::Loaded;return loaded;}
        loaded.state=widget::LoadState::Failed;
    }
    if(files_.Exists(path+".bak"))
        loaded.state=files_.Read(path+".bak",text)&&DecodePreferences(text,loaded.value)?
            widget::LoadState::Recovered:widget::LoadState::Failed;
    return loaded;
}
bool PreferencesStore::Save(const std::string& path,const Preferences& p){
    std::string text;if(path.empty()||!EncodePreferences(p,text))return false;
    std::string original;bool validOriginal=false;
    if(files_.Exists(path)){
        Preferences previous;
        if(!files_.Read(path,original))return false;
        validOriginal=DecodePreferences(original,previous);
        if(!validOriginal){
            bool isolated=false;
            for(unsigned i=0;i<1024;++i){
                const auto destination=path+".corrupt"+(i?"."+std::to_string(i):"");
                if(!files_.Exists(destination)){isolated=files_.Rename(path,destination);break;}
            }
            if(!isolated)return false;
        }
    }
    const auto temporary=path+".tmp",backup=path+".bak.tmp";
    const auto fail=[&]{files_.Remove(temporary);files_.Remove(backup);return false;};
    if(!files_.Write(temporary,text))return fail();
    if(validOriginal&&(!files_.Write(backup,original)||!files_.Rename(backup,path+".bak")))return fail();
    if(!files_.Rename(temporary,path))return fail();
    return true;
}
}
