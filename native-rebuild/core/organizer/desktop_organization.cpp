#include "core/organizer/desktop_organization.h"
#include <algorithm>
#include <map>
#include <set>
namespace lume::organizer {
namespace {
constexpr const char* ApplicationsId="d0c00000-0000-4000-8000-000000000001";
constexpr const char* FilesId="d0c00000-0000-4000-8000-000000000002";
Collection* Role(State& state,DesktopRole role){
    for(auto& collection:state.collections)if(collection.desktopRole==role)return &collection;
    return nullptr;
}
}
State DesktopDefaults(){
    auto state=Defaults();auto& desktop=state.collections.front();
    desktop.title=u8"桌面";desktop.desktopRole=DesktopRole::Applications;
    desktop.bounds=Bounds{0,0,260,680};
    Collection files;files.id=FilesId;files.title=u8"文件";files.desktopRole=DesktopRole::Files;
    files.parent=DefaultCollectionId;state.collections.push_back(std::move(files));
    SyncGroups(state);
    return state;
}
bool EnsureDesktopCollections(State& state){
    bool changed=false;
    for(const auto role:{DesktopRole::Applications,DesktopRole::Files}){
        if(Role(state,role))continue;
        if(role==DesktopRole::Applications?state.applicationsRetired:state.filesRetired)continue;
        const auto title=role==DesktopRole::Applications?u8"桌面":u8"文件";
        auto found=std::find_if(state.collections.begin(),state.collections.end(),[&](const auto& collection){
            return collection.desktopRole==DesktopRole::None&&(collection.title==title||
                (role==DesktopRole::Applications&&collection.id==DefaultCollectionId&&collection.title==u8"桌面收纳"));
        });
        if(found==state.collections.end()){
            if(state.collections.size()>=MaxCollections)continue;
            std::string id=role==DesktopRole::Applications?ApplicationsId:FilesId;
            if(Find(state,id))continue;
            Collection next;next.id=id;next.title=title;state.collections.push_back(std::move(next));
            found=state.collections.end()-1;
        }
        found->desktopRole=role;
        if(role==DesktopRole::Applications&&found->title==u8"桌面收纳")found->title=title;
        changed=true;
    }
    if(changed){
        const auto* desktop=Role(state,DesktopRole::Applications);const auto* files=Role(state,DesktopRole::Files);
        if(desktop&&files){
            const std::string desktopId=desktop->id,filesId=files->id;
            const auto* desktopHost=Host(state,desktopId);const auto* filesHost=Host(state,filesId);
            if(desktopHost&&filesHost&&desktopHost!=filesHost){
                const std::string hostId=desktopHost->id;
                MergeGroup(state,filesHost->id,hostId);ActivateTab(state,hostId,desktopId);
            }
        }
    }
    return changed;
}
bool RestoreDesktopCollections(State& state){
    const bool retired=state.applicationsRetired||state.filesRetired;
    state.applicationsRetired=state.filesRetired=false;
    return EnsureDesktopCollections(state)||retired;
}
DesktopSync SyncDesktop(State& state,const DesktopSnapshot& source,const TextKey& key){
    DesktopSync result;if(!source.complete||!key)return result;
    auto next=state;result.changed=EnsureDesktopCollections(next);
    auto* applications=Role(next,DesktopRole::Applications);auto* files=Role(next,DesktopRole::Files);
    if(!applications&&!files){
        // Both desktop tabs were deleted by the user: nothing is collected.
        if(result.changed&&Valid(next,key))state=std::move(next);else result.changed=false;
        return result;
    }
    std::map<std::string,const DesktopEntry*> live;
    for(const auto& entry:source.entries){
        const auto normalized=NormalizeLocalPath(entry.item.path);
        if(!normalized||*normalized!=entry.item.path||!ValidId(entry.item.id))continue;
        live.emplace(key(entry.item.path),&entry);
    }
    // Entries the user took out stay out. A dismissal ends only when the user
    // puts the entry back into a collection: an entry missing from one desktop
    // scan (a shortcut an update rewrites, a briefly unavailable folder) must
    // not return on the next. MaxDismissed bounds the list.
    // Present: in any collection, so an entry the user moved to a custom tab
    // is not added to the desktop tabs a second time.
    std::set<std::string> present,dismissed;
    for(const auto& collection:next.collections)
        for(const auto& item:collection.items)present.insert(key(item.path));
    std::vector<Collection*> roles;for(auto* collection:{applications,files})if(collection)roles.push_back(collection);
    for(auto* collection:roles){
        std::vector<std::string> keep;
        for(auto& path:collection->dismissed){
            const auto identity=key(path);
            if(present.count(identity)||!dismissed.insert(identity).second){result.changed=true;continue;}
            keep.push_back(std::move(path));
        }
        collection->dismissed=std::move(keep);
    }
    for(const auto& identity:dismissed)live.erase(identity);
    // Entries already shown in a custom collection stay there only.
    std::set<std::string> custom;
    for(const auto& collection:next.collections)if(collection.desktopRole==DesktopRole::None)
        for(const auto& item:collection.items)custom.insert(key(item.path));
    std::vector<DesktopEntry> moving;
    std::map<std::string,std::string> origins;
    std::set<std::string> retained;
    for(auto* collection:roles){
        const auto role=collection->desktopRole;
        std::vector<Item> keep;
        for(auto item:collection->items){
            const auto identity=key(item.path);const auto found=live.find(identity);
            if(found==live.end()){
                if(item.desktop){result.changed=true;continue;}
                keep.push_back(std::move(item));continue;
            }
            const auto& entry=*found->second;
            const auto expected=entry.application?DesktopRole::Applications:DesktopRole::Files;
            if(!item.desktop){item.desktop=true;result.changed=true;}
            const auto name=DisplayName(entry.item.path,entry.item.kind);
            if(item.kind!=entry.item.kind||item.name!=name){item.kind=entry.item.kind;item.name=name;result.changed=true;}
            // The proper tab was deleted by the user: leave the entry where it is.
            if(role!=expected&&(expected==DesktopRole::Applications?applications:files)){
                origins[identity]=collection->id;moving.push_back({std::move(item),entry.application});result.changed=true;}
            else{retained.insert(identity);keep.push_back(std::move(item));}
        }
        collection->items=std::move(keep);
    }
    for(const auto& [identity,entry]:live)if(!retained.count(identity)&&!custom.count(identity)&&
        std::none_of(moving.begin(),moving.end(),[&](const auto& value){return key(value.item.path)==identity;}))moving.push_back(*entry);
    for(auto& entry:moving){
        auto* target=entry.application?applications:files;const auto identity=key(entry.item.path);
        if(!target)continue;  // That desktop tab was deleted by the user.
        if(!retained.insert(identity).second)continue;
        if(target->items.size()>=MaxItems){
            result.limited=true;const auto origin=origins.find(identity);
            if(origin!=origins.end())Find(next,origin->second)->items.push_back(entry.item);
            continue;
        }
        entry.item.desktop=true;entry.item.name=DisplayName(entry.item.path,entry.item.kind);
        target->items.push_back(std::move(entry.item));result.changed=true;
    }
    if(result.changed&&Valid(next,key))state=std::move(next);
    else if(result.changed)result.changed=false;
    return result;
}
}
