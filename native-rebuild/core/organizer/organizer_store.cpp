#include "core/organizer/organizer_store.h"
#include "vendor/nlohmann/json.hpp"
#include <set>
#include <array>
#include <algorithm>
namespace lume::organizer {
namespace {
using Json=nlohmann::json;
const char* KindName(ItemKind kind){return kind==ItemKind::Folder?"folder":kind==ItemKind::Shortcut?"shortcut":"file";}
std::optional<ItemKind> ParseKind(const Json& value){
    if(!value.is_string())return {};
    const auto text=value.get_ref<const std::string&>();
    if(text=="file")return ItemKind::File;
    if(text=="folder")return ItemKind::Folder;
    if(text=="shortcut")return ItemKind::Shortcut;
    return {};
}
Json BoundsJson(const std::optional<Bounds>& value){
    if(!value)return nullptr;
    return Json{{"x",value->x},{"y",value->y},{"width",value->width},{"height",value->height}};
}
// Exact key sets. Const operator[] on a missing key is undefined behaviour
// in Release builds, so every lookup below goes through HasKeys()/at().
bool HasKeys(const Json& value,std::initializer_list<const char*> required,std::initializer_list<const char*> optional={}){
    if(!value.is_object())return false;
    std::size_t present=0;
    for(const auto* key:required)if(!value.contains(key))return false;
    for(const auto* key:optional)present+=value.contains(key)?1:0;
    return value.size()==required.size()+present;
}
std::optional<Bounds> ParseBounds(const Json& value){
    if(value.is_null())return std::nullopt;
    if(!HasKeys(value,{"x","y","width","height"}))return {};
    Bounds result;
    try {
        const auto& x=value.at("x");const auto& y=value.at("y");
        const auto& width=value.at("width");const auto& height=value.at("height");
        if(!x.is_number_integer()||!y.is_number_integer()||!width.is_number_integer()||!height.is_number_integer())return {};
        if(x< -100000||x>100000||y< -100000||y>100000||width<120||width>10000||height<160||height>10000)return {};
        result.x=x.get<int>();result.y=y.get<int>();result.width=width.get<int>();result.height=height.get<int>();
    }catch(...){return {};}
    return ValidBounds(result)?std::optional<Bounds>(result):std::nullopt;
}
}
bool Encode(const State& state,std::string& output){
    try {
        const auto key=[](std::string_view value){return std::string(value);};
        if(!Valid(state,key))return false;
        Json collections=Json::array();
        for(const auto& collection:state.collections){
            Json items=Json::array();
            for(const auto& item:collection.items){
                Json row={{"id",item.id},{"path",item.path},{"name",item.name},{"kind",KindName(item.kind)}};
                if(item.desktop)row["desktop"]=true;
                items.push_back(std::move(row));
            }
            Json row={{"id",collection.id},{"version",1},{"title",collection.title},
                {"bounds",BoundsJson(collection.bounds)},{"enabled",collection.enabled},
                {"locked",collection.locked},{"frosted",collection.frosted},
                {"mode",collection.mode==Mode::Collapsed?"collapsed":"expanded"},
                {"layout",collection.layout==Layout::List?"list":"icons"},{"items",std::move(items)}};
            // Written only when set, so files without tab groups keep the
            // exact earlier shape and remain readable by earlier builds.
            if(!collection.parent.empty())row["parent"]=collection.parent;
            if(!collection.active.empty())row["active"]=collection.active;
            row["iconScale"]=collection.iconScale;
            if(collection.desktopRole!=DesktopRole::None)
                row["desktopRole"]=collection.desktopRole==DesktopRole::Applications?"applications":"files";
            if(!collection.dismissed.empty())row["dismissed"]=collection.dismissed;
            collections.push_back(std::move(row));
        }
        Json document{{"version",2},{"collections",std::move(collections)}};
        // Written only when set, as above.
        Json retired=Json::array();
        if(state.applicationsRetired)retired.push_back("applications");
        if(state.filesRetired)retired.push_back("files");
        if(!retired.empty())document["retiredDesktop"]=std::move(retired);
        output=document.dump();
        return output.size()<=MaxStateBytes;
    }catch(...){return false;}
}
bool Decode(std::string_view text,State& output,const TextKey& pathKey){
    if(text.empty()||text.size()>MaxStateBytes||!pathKey)return false;
    try {
        std::array<std::set<std::string>,9> keys;
        const auto value=Json::parse(text.begin(),text.end(),[&](int depth,Json::parse_event_t event,Json& parsed){
            if(depth<0||depth>=static_cast<int>(keys.size()))throw std::runtime_error("organizer depth");
            if(event==Json::parse_event_t::object_start)keys[static_cast<std::size_t>(depth)].clear();
            if(event==Json::parse_event_t::key&&!keys[static_cast<std::size_t>(depth-1)].insert(parsed.get<std::string>()).second)
                throw std::runtime_error("organizer duplicate key");
            return true;
        });
        if(!HasKeys(value,{"version","collections"},{"retiredDesktop"})||value.value("version",0)!=2)return false;
        const auto& rows=value.at("collections");
        if(!rows.is_array()||rows.empty()||rows.size()>MaxCollections)return false;
        State state;std::set<std::string> collectionIds;
        if(value.contains("retiredDesktop")){
            const auto& retired=value.at("retiredDesktop");
            if(!retired.is_array()||retired.empty()||retired.size()>2)return false;
            for(const auto& role:retired){
                if(role=="applications"&&!state.applicationsRetired)state.applicationsRetired=true;
                else if(role=="files"&&!state.filesRetired)state.filesRetired=true;
                else return false;
            }
        }
        for(const auto& input:rows){
            if(!HasKeys(input,{"id","version","title","bounds","enabled","mode","layout","items"},
                    {"locked","frosted","parent","active","iconScale","desktopRole","dismissed"})||
                input.contains("locked")!=input.contains("frosted")||
                !input.at("id").is_string()||!input.at("title").is_string()||
                !input.at("enabled").is_boolean()||!input.at("items").is_array()||input.at("items").size()>MaxItems||
                input.value("version",0)!=1)return false;
            Collection collection;collection.id=input.at("id").get<std::string>();collection.title=input.at("title").get<std::string>();
            if((collection.id!=DefaultCollectionId&&!ValidId(collection.id))||!collectionIds.insert(collection.id).second)return false;
            const auto title=SanitizeTitle(collection.title);if(!title||*title!=collection.title)return false;
            collection.enabled=input.at("enabled").get<bool>();
            if(input.contains("locked")&&!input.at("locked").is_boolean())return false;
            if(input.contains("frosted")&&!input.at("frosted").is_boolean())return false;
            collection.locked=input.value("locked",false);
            collection.frosted=input.value("frosted",false);
            // Missing in old files means the original 100%, not the new default.
            collection.iconScale=100;
            if(input.contains("iconScale")){
                const auto& scale=input.at("iconScale");
                if(!scale.is_number_integer()||scale<50||scale>150)return false;
                collection.iconScale=std::min(120,scale.get<int>());
            }
            if(input.contains("desktopRole")){
                const auto& role=input.at("desktopRole");
                if(role=="applications")collection.desktopRole=DesktopRole::Applications;
                else if(role=="files")collection.desktopRole=DesktopRole::Files;
                else return false;
            }
            if(input.contains("dismissed")){
                const auto& dismissed=input.at("dismissed");
                if(!dismissed.is_array()||dismissed.empty()||dismissed.size()>MaxDismissed)return false;
                for(const auto& path:dismissed){
                    if(!path.is_string())return false;
                    collection.dismissed.push_back(path.get<std::string>());
                }
            }
            const auto& mode=input.at("mode");const auto& layout=input.at("layout");
            if(mode=="collapsed")collection.mode=Mode::Collapsed;else if(mode!="expanded")return false;
            if(layout=="list")collection.layout=Layout::List;else if(layout!="icons")return false;
            for(const auto* key:{"parent","active"})
                if(input.contains(key)&&(!input.at(key).is_string()||input.at(key).get_ref<const std::string&>().empty()))return false;
            collection.parent=input.value("parent",std::string{});
            collection.active=input.value("active",std::string{});
            if(!input.at("bounds").is_null()){
                const auto bounds=ParseBounds(input.at("bounds"));if(!bounds)return false;collection.bounds=bounds;
            }
            std::set<std::string> ids,paths;
            for(const auto& row:input.at("items")){
                if(!HasKeys(row,{"id","path","name","kind"},{"desktop"})||!row.at("id").is_string()||
                    !row.at("path").is_string()||!row.at("name").is_string())return false;
                if(row.contains("desktop")&&!row.at("desktop").is_boolean())return false;
                const auto kind=ParseKind(row.at("kind"));if(!kind)return false;
                const auto& rowPath=row.at("path").get_ref<const std::string&>();
                const auto& rowId=row.at("id").get_ref<const std::string&>();
                const auto normalized=NormalizeLocalPath(rowPath);
                if(!normalized||*normalized!=rowPath||!ValidId(rowId)||!ids.insert(rowId).second)return false;
                const auto pathKeyValue=pathKey(*normalized);
                if(pathKeyValue.empty()||!paths.insert(pathKeyValue).second)return false;
                if(row.at("name")!=DisplayName(*normalized,*kind))return false;
                collection.items.push_back({rowId,*normalized,row.at("name").get<std::string>(),*kind,row.value("desktop",false)});
            }
            state.collections.push_back(std::move(collection));
        }
        if(!Valid(state,pathKey))return false;
        output=std::move(state);return true;
    }catch(...){return false;}
}
LoadedState Store::Load(const std::string& path)const{
    LoadedState result{Defaults(),LoadState::Fresh};if(path.empty()){result.state=LoadState::Failed;return result;}
    std::string text;
    if(files_.Exists(path)){
        if(files_.ReadBounded(path,MaxStateBytes,text)&&Decode(text,result.value,pathKey_)){result.state=LoadState::Loaded;return result;}
        result.state=LoadState::Failed;
    }
    if(files_.Exists(path+".bak")&&files_.ReadBounded(path+".bak",MaxStateBytes,text)&&Decode(text,result.value,pathKey_))result.state=LoadState::Recovered;
    else if(files_.Exists(path+".bak"))result.state=LoadState::Failed;
    return result;
}
bool Store::Save(const std::string& path,const State& value){
    std::string encoded;if(path.empty()||!Valid(value,pathKey_)||!Encode(value,encoded))return false;
    std::string original;bool validOriginal=false;
    if(files_.Exists(path)){
        if(!files_.ReadBounded(path,MaxStateBytes,original))return false;
        State old;validOriginal=Decode(original,old,pathKey_);
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
    if(!files_.Write(temporary,encoded))return fail();
    if(validOriginal&&(!files_.Write(backup,original)||!files_.Rename(backup,path+".bak")))return fail();
    if(!files_.Rename(temporary,path))return fail();
    return true;
}
}
