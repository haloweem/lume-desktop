#include "core/organizer/organizer_model.h"
#include <algorithm>
#include <cstdint>
#include <set>
#include <utility>
namespace lume::organizer {
namespace {
struct Codepoint { std::uint32_t value;std::size_t begin,length; };
std::optional<std::vector<Codepoint>> Utf8(std::string_view text){
    if(text.size()>MaxStateBytes)return {};
    std::vector<Codepoint> out;
    for(std::size_t i=0;i<text.size();){
        const auto first=static_cast<unsigned char>(text[i]);
        const unsigned length=first<0x80?1:(first&0xe0)==0xc0?2:(first&0xf0)==0xe0?3:(first&0xf8)==0xf0?4:0;
        if(!length||i+length>text.size())return {};
        std::uint32_t value=first&(length==1?0x7f:length==2?0x1f:length==3?0xf:7);
        for(unsigned j=1;j<length;++j){
            const auto byte=static_cast<unsigned char>(text[i+j]);if((byte&0xc0)!=0x80)return {};
            value=(value<<6)|(byte&0x3f);
        }
        if((length==2&&value<0x80)||(length==3&&value<0x800)||(length==4&&value<0x10000)||
            value>0x10ffff||(value>=0xd800&&value<=0xdfff))return {};
        out.push_back({value,i,length});i+=length;
    }
    return out;
}
bool Space(std::uint32_t value){
    return (value>=9&&value<=13)||value==32||value==0xa0||value==0x1680||(value>=0x2000&&value<=0x200a)||
        value==0x2028||value==0x2029||value==0x202f||value==0x205f||value==0x3000||value==0xfeff;
}
bool Kind(ItemKind kind){return kind==ItemKind::File||kind==ItemKind::Folder||kind==ItemKind::Shortcut;}
bool SameBounds(const std::optional<Bounds>& a,const std::optional<Bounds>& b){
    if(!a||!b)return !a&&!b;
    return a->x==b->x&&a->y==b->y&&a->width==b->width&&a->height==b->height;
}
bool ValidCollection(const Collection& collection,const TextKey& pathKey){
    const auto title=SanitizeTitle(collection.title);
    if((collection.id!=DefaultCollectionId&&!ValidId(collection.id))||!title||*title!=collection.title||
        (collection.bounds&&!ValidBounds(*collection.bounds))||collection.items.size()>MaxItems||
        (collection.mode!=Mode::Expanded&&collection.mode!=Mode::Collapsed)||
        (collection.layout!=Layout::Icons&&collection.layout!=Layout::List)||collection.iconScale<50||collection.iconScale>120||
        (collection.desktopRole!=DesktopRole::None&&collection.desktopRole!=DesktopRole::Applications&&collection.desktopRole!=DesktopRole::Files)||!pathKey)return false;
    std::set<std::string> ids,paths;
    for(const auto& item:collection.items){
        const auto path=NormalizeLocalPath(item.path);
        if(!ValidId(item.id)||!ids.insert(item.id).second||!path||*path!=item.path||!Kind(item.kind)||
            item.name!=DisplayName(item.path,item.kind))return false;
        const auto key=pathKey(item.path);
        if(key.empty()||!paths.insert(key).second)return false;
    }
    if(collection.dismissed.size()>MaxDismissed||(collection.desktopRole==DesktopRole::None&&!collection.dismissed.empty()))return false;
    std::set<std::string> dismissed;
    for(const auto& value:collection.dismissed){
        const auto path=NormalizeLocalPath(value);
        if(!path||*path!=value||!dismissed.insert(pathKey(value)).second)return false;
    }
    return true;
}
}
State Defaults(){
    Collection collection;collection.id=DefaultCollectionId;collection.title=u8"桌面收纳";
    return {{std::move(collection)}};
}
bool ValidId(std::string_view id){
    if(id.size()!=36)return false;
    for(std::size_t i=0;i<id.size();++i){
        if(i==8||i==13||i==18||i==23){if(id[i]!='-')return false;}
        else if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')||(id[i]>='A'&&id[i]<='F')))return false;
    }
    return true;
}
bool ValidBounds(const Bounds& value){
    return value.x>=-100000&&value.x<=100000&&value.y>=-100000&&value.y<=100000&&
        value.width>=120&&value.width<=10000&&value.height>=160&&value.height<=10000;
}
std::optional<std::string> NormalizeLocalPath(std::string_view input){
    if(input.size()<3||input.size()>32767*4||!((input[0]>='A'&&input[0]<='Z')||(input[0]>='a'&&input[0]<='z'))||
        input[1]!=':'||(input[2]!='\\'&&input[2]!='/')||input.substr(2).find(':')!=std::string_view::npos)return {};
    const auto characters=Utf8(input);if(!characters)return {};
    std::size_t units=0;
    for(const auto& c:*characters){if(c.value<32)return {};units+=c.value>0xffff?2:1;}
    if(units>32767)return {};
    std::vector<std::string_view> segments;
    for(std::size_t offset=3;offset<input.size();){
        const auto end=input.find_first_of("/\\",offset);
        const auto segment=input.substr(offset,end==std::string_view::npos?input.size()-offset:end-offset);
        if(segment==".."){if(!segments.empty())segments.pop_back();}
        else if(!segment.empty()&&segment!=".")segments.push_back(segment);
        if(end==std::string_view::npos)break;
        offset=end+1;
    }
    std::string path(input.substr(0,2));path+='\\';
    for(std::size_t i=0;i<segments.size();++i){if(i)path+='\\';path+=segments[i];}
    return path;
}
std::optional<std::string> SanitizeTitle(std::string_view text){
    const auto characters=Utf8(text);if(!characters)return {};
    std::string title;std::size_t count=0,lastLength=0,lastCount=0;
    for(const auto& c:*characters){
        const bool control=c.value<32||c.value==127,space=control||Space(c.value);
        if(space&&title.empty())continue;
        if(control)title+=' ';else title+=text.substr(c.begin,c.length);
        ++count;
        if(!space){lastLength=title.size();lastCount=count;if(lastCount>MaxTitleLength)return {};}
    }
    title.resize(lastLength);
    if(title.empty()||lastCount>MaxTitleLength)return {};
    return title;
}
std::string DisplayName(std::string_view path,ItemKind kind){
    auto name=path.substr(path.find_last_of("/\\")==std::string_view::npos?0:path.find_last_of("/\\")+1);
    if(name.empty())name=path;
    if(kind==ItemKind::Shortcut&&name.size()>4){
        const auto suffix=name.substr(name.size()-4);
        if(suffix[0]=='.'&&(suffix[1]=='l'||suffix[1]=='L')&&(suffix[2]=='n'||suffix[2]=='N')&&(suffix[3]=='k'||suffix[3]=='K'))
            name.remove_suffix(4);
    }
    return std::string(name);
}
bool Valid(const State& state,const TextKey& pathKey){
    if(state.collections.empty()||state.collections.size()>MaxCollections)return false;
    std::set<std::string> ids;
    unsigned applications=0,files=0;
    for(const auto& collection:state.collections)
        if(!ids.insert(collection.id).second||!ValidCollection(collection,pathKey)||
            (collection.desktopRole==DesktopRole::Applications&&++applications>1)||
            (collection.desktopRole==DesktopRole::Files&&++files>1))return false;
    // One level of grouping only: a tab's parent is an existing host.
    for(const auto& collection:state.collections){
        if(collection.parent.empty())continue;
        const auto* host=Find(state,collection.parent);
        if(collection.parent==collection.id||!host||!host->parent.empty()||!collection.active.empty())return false;
    }
    for(const auto& collection:state.collections){
        if(!collection.parent.empty()||collection.active.empty())continue;
        const auto* tab=Find(state,collection.active);
        if(!tab||(tab->id!=collection.id&&tab->parent!=collection.id))return false;
    }
    return true;
}
const Collection* Host(const State& state,std::string_view id){
    const auto* collection=Find(state,id);
    if(!collection)return nullptr;
    return collection->parent.empty()?collection:Find(state,collection->parent);
}
std::vector<const Collection*> Tabs(const State& state,std::string_view hostId){
    std::vector<const Collection*> tabs;
    const auto* host=Find(state,hostId);
    if(!host||!host->parent.empty())return tabs;
    tabs.push_back(host);
    for(const auto& collection:state.collections)if(collection.parent==hostId)tabs.push_back(&collection);
    return tabs;
}
const Collection* ActiveTab(const State& state,std::string_view hostId){
    const auto* host=Find(state,hostId);
    if(!host||!host->parent.empty())return nullptr;
    if(!host->active.empty()){
        const auto* tab=Find(state,host->active);
        if(tab&&(tab==host||tab->parent==host->id))return tab;
    }
    return host;
}
namespace {
// Promotes the first child of hostId to host, moving window state onto it.
void PromoteFirstChild(State& state,const std::string& hostId){
    const auto* host=Find(state,hostId);if(!host)return;
    const auto heir=std::find_if(state.collections.begin(),state.collections.end(),
        [&](const auto& value){return value.parent==hostId;});
    if(heir==state.collections.end())return;
    const auto heirId=heir->id;
    heir->parent.clear();heir->bounds=host->bounds;
    if(heir->id!=DefaultCollectionId)heir->enabled=host->enabled;
    heir->locked=host->locked;
    heir->frosted=host->frosted;heir->mode=host->mode;heir->layout=host->layout;
    heir->iconScale=host->iconScale;
    heir->active=host->active==hostId||host->active==heirId?std::string{}:host->active;
    for(auto& value:state.collections)if(value.parent==hostId&&value.id!=heirId)value.parent=heirId;
}
}
Change MergeGroup(State& state,std::string_view sourceId,std::string_view targetId){
    const auto* source=Host(state,sourceId);const auto* target=Host(state,targetId);
    if(!source||!target||source==target)return Change::Invalid;
    const std::string sourceHost=source->id,targetHost=target->id;
    const auto mode=target->mode;const auto layout=target->layout;const auto iconScale=target->iconScale;
    std::vector<Collection> moved,kept;
    for(auto& value:state.collections){
        if(value.id==sourceHost||value.parent==sourceHost)moved.push_back(std::move(value));
        else kept.push_back(std::move(value));
    }
    // Tabs mirror their host's presentation so layout code can read any tab.
    for(auto& value:moved){
        value.parent=targetHost;value.active.clear();value.bounds.reset();
        if(value.id!=DefaultCollectionId)value.enabled=true;
        value.mode=mode;value.layout=layout;value.iconScale=iconScale;
    }
    // Insert after the target group's last tab so tab order follows drops.
    auto at=kept.begin();
    for(auto it=kept.begin();it!=kept.end();++it)if(it->id==targetHost||it->parent==targetHost)at=it+1;
    const std::string first=moved.front().id;
    kept.insert(at,std::make_move_iterator(moved.begin()),std::make_move_iterator(moved.end()));
    state.collections=std::move(kept);
    Find(state,targetHost)->active=first;
    return Change::Changed;
}
Change DetachTab(State& state,std::string_view tabId,Bounds bounds){
    auto* tab=Find(state,tabId);if(!tab||!ValidBounds(bounds))return Change::Invalid;
    const std::string id(tabId);
    if(tab->parent.empty()){
        // Detaching a host leaves its siblings behind under a promoted heir.
        if(std::none_of(state.collections.begin(),state.collections.end(),[&](const auto& v){return v.parent==id;}))
            return Change::Unchanged;
        PromoteFirstChild(state,id);
        tab=Find(state,id);tab->active.clear();tab->bounds=bounds;tab->mode=Mode::Expanded;
        return Change::Changed;
    }
    const auto* host=Find(state,tab->parent);if(!host)return Change::Invalid;
    tab->bounds=bounds;
    if(tab->id!=DefaultCollectionId)tab->enabled=true;
    tab->locked=false;tab->frosted=host->frosted;
    tab->mode=Mode::Expanded;tab->layout=host->layout;tab->active.clear();
    tab->iconScale=host->iconScale;
    const std::string hostId=tab->parent;tab->parent.clear();
    auto* owner=Find(state,hostId);if(owner->active==id)owner->active.clear();
    return Change::Changed;
}
Change ActivateTab(State& state,std::string_view hostId,std::string_view tabId){
    auto* host=Find(state,hostId);const auto* tab=Find(state,tabId);
    if(!host||!host->parent.empty()||!tab||(tab!=host&&tab->parent!=hostId))return Change::Invalid;
    const std::string next=tab==host?std::string{}:std::string(tabId);
    if(ActiveTab(state,hostId)==tab&&host->active==next)return Change::Unchanged;
    host->active=next;return Change::Changed;
}
Change MoveTab(State& state,std::string_view tabId,std::size_t to){
    const auto* host=Host(state,tabId);if(!host)return Change::Invalid;
    const std::string oldHost=host->id;
    std::vector<std::string> order;
    for(const auto* tab:Tabs(state,oldHost))order.push_back(tab->id);
    const auto from=static_cast<std::size_t>(std::find(order.begin(),order.end(),tabId)-order.begin());
    if(from>=order.size())return Change::Invalid;
    to=std::min(to,order.size()-1);
    if(from==to)return Change::Unchanged;
    const auto moving=order[from];
    order.erase(order.begin()+static_cast<std::ptrdiff_t>(from));
    order.insert(order.begin()+static_cast<std::ptrdiff_t>(to),moving);
    const std::string newHost=order.front();
    if(newHost!=oldHost){
        // The new front tab takes over the window; the visible tab is kept.
        auto* previous=Find(state,oldHost);auto* next=Find(state,newHost);
        const auto visible=previous->active.empty()?oldHost:previous->active;
        next->parent.clear();next->bounds=previous->bounds;
        if(next->id!=DefaultCollectionId)next->enabled=previous->enabled;
        next->locked=previous->locked;
        next->frosted=previous->frosted;next->mode=previous->mode;next->layout=previous->layout;
        next->iconScale=previous->iconScale;
        next->active=visible==newHost?std::string{}:visible;
        previous->parent=newHost;previous->active.clear();previous->bounds.reset();
        if(previous->id!=DefaultCollectionId)previous->enabled=true;
        for(auto& value:state.collections)if(value.parent==oldHost)value.parent=newHost;
    }
    // Rewrite the group's slots in the new order, keeping its place in state.
    std::vector<Collection> group;
    for(const auto& id:order)group.push_back(*Find(state,id));
    std::size_t slot=0;
    for(auto& value:state.collections)
        if(value.id==newHost||value.parent==newHost)value=std::move(group[slot++]);
    SyncGroups(state);
    return Change::Changed;
}
void SyncGroups(State& state){
    for(auto& tab:state.collections){
        if(tab.parent.empty())continue;
        const auto* host=Find(state,tab.parent);if(!host)continue;
        tab.mode=host->mode;tab.layout=host->layout;tab.frosted=host->frosted;tab.locked=host->locked;
        tab.iconScale=host->iconScale;
        if(tab.id!=DefaultCollectionId)tab.enabled=true;
        tab.bounds.reset();tab.active.clear();
    }
}
bool PruneClosedEmpty(State& state){
    const auto before=state.collections.size();
    std::set<std::string> parents;
    for(const auto& value:state.collections)if(!value.parent.empty())parents.insert(value.parent);
    state.collections.erase(std::remove_if(state.collections.begin(),state.collections.end(),[&](const auto& value){
        return value.id!=DefaultCollectionId&&value.desktopRole==DesktopRole::None&&value.parent.empty()&&!value.enabled&&value.items.empty()&&
            !parents.count(value.id);
    }),state.collections.end());
    return state.collections.size()!=before;
}
Change DeleteTab(State& state,std::string_view tabId){
    const auto* tab=Find(state,tabId);
    if(!tab||tabId==DefaultCollectionId||state.collections.size()==1)return Change::Invalid;
    const std::string id(tabId);
    // An automatic desktop tab stays deleted; its entries, and any desktop
    // entry deleted with a custom tab, are not collected again.
    if(tab->desktopRole==DesktopRole::Applications)state.applicationsRetired=true;
    if(tab->desktopRole==DesktopRole::Files)state.filesRetired=true;
    std::vector<std::string> dismissed;
    if(tab->desktopRole==DesktopRole::None)for(const auto& item:tab->items)if(item.desktop)dismissed.push_back(item.path);
    if(tab->parent.empty())PromoteFirstChild(state,id);
    else{auto* host=Find(state,tab->parent);if(host&&host->active==id)host->active.clear();}
    state.collections.erase(std::find_if(state.collections.begin(),state.collections.end(),
        [&](const auto& value){return value.id==id;}));
    for(const auto& path:dismissed)DismissDesktopPath(state,path);
    return Change::Changed;
}
Change MoveItems(State& state,std::string_view fromId,std::string_view toId,const std::vector<std::string>& itemIds,const TextKey& pathKey){
    auto* from=Find(state,fromId);auto* to=Find(state,toId);
    if(!from||!to||!pathKey||itemIds.empty())return Change::Invalid;
    if(from==to)return Change::Unchanged;
    std::set<std::string> keys;for(const auto& item:to->items)keys.insert(pathKey(item.path));
    std::set<std::string> wanted(itemIds.begin(),itemIds.end());
    std::vector<Item> moving,staying;
    for(auto& item:from->items)(wanted.count(item.id)?moving:staying).push_back(item);
    if(moving.size()!=wanted.size())return Change::Invalid;
    std::size_t added=0;
    for(const auto& item:moving)if(!keys.count(pathKey(item.path)))++added;
    if(to->items.size()+added>MaxItems)return Change::Limit;
    std::set<std::string> ids;for(const auto& item:to->items)ids.insert(item.id);
    for(auto& item:moving){
        if(!keys.insert(pathKey(item.path)).second)continue;
        if(ids.count(item.id))return Change::Invalid;
        to->items.push_back(std::move(item));
    }
    from->items=std::move(staying);
    return Change::Changed;
}
Collection* Find(State& state,std::string_view id){
    const auto found=std::find_if(state.collections.begin(),state.collections.end(),[&](const auto& entry){return entry.id==id;});
    return found==state.collections.end()?nullptr:&*found;
}
const Collection* Find(const State& state,std::string_view id){
    const auto found=std::find_if(state.collections.begin(),state.collections.end(),[&](const auto& entry){return entry.id==id;});
    return found==state.collections.end()?nullptr:&*found;
}
Change AddItems(Collection& collection,const std::vector<Item>& batch,const TextKey& pathKey){
    if(batch.size()>1024||!ValidCollection(collection,pathKey))return Change::Invalid;
    std::set<std::string> ids,paths;auto items=collection.items;
    for(const auto& item:items){ids.insert(item.id);paths.insert(pathKey(item.path));}
    for(const auto& item:batch){
        const auto path=NormalizeLocalPath(item.path);if(!path)return Change::Invalid;
        const auto key=pathKey(*path);if(key.empty())return Change::Invalid;
        if(paths.count(key))continue;
        if(!ValidId(item.id)||!ids.insert(item.id).second||!Kind(item.kind))return Change::Invalid;
        if(items.size()>=MaxItems)return Change::Limit;
        paths.insert(key);items.push_back({item.id,*path,DisplayName(*path,item.kind),item.kind,item.desktop});
    }
    if(items.size()==collection.items.size())return Change::Unchanged;
    collection.items=std::move(items);return Change::Changed;
}
Change RemoveItem(Collection& collection,std::string_view id){
    const auto found=std::find_if(collection.items.begin(),collection.items.end(),[&](const auto& item){return item.id==id;});
    if(found==collection.items.end())return Change::Unchanged;
    if(collection.desktopRole!=DesktopRole::None&&found->desktop&&
       std::find(collection.dismissed.begin(),collection.dismissed.end(),found->path)==collection.dismissed.end()){
        // Bounded: the oldest dismissal gives way (that entry may return).
        if(collection.dismissed.size()>=MaxDismissed)collection.dismissed.erase(collection.dismissed.begin());
        collection.dismissed.push_back(found->path);
    }
    collection.items.erase(found);return Change::Changed;
}
void DismissDesktopPath(State& state,const std::string& path){
    Collection* target=nullptr;
    for(auto& collection:state.collections){
        if(collection.desktopRole==DesktopRole::None)continue;
        if(std::find(collection.dismissed.begin(),collection.dismissed.end(),path)!=collection.dismissed.end())return;
        if(!target||collection.desktopRole==DesktopRole::Applications)target=&collection;
    }
    if(!target)return;
    if(target->dismissed.size()>=MaxDismissed)target->dismissed.erase(target->dismissed.begin());
    target->dismissed.push_back(path);
}
Change RemoveItem(State& state,std::string_view collectionId,std::string_view id){
    auto* collection=Find(state,collectionId);if(!collection)return Change::Invalid;
    if(collection->desktopRole!=DesktopRole::None)return RemoveItem(*collection,id);
    const auto found=std::find_if(collection->items.begin(),collection->items.end(),[&](const auto& item){return item.id==id;});
    if(found==collection->items.end())return Change::Unchanged;
    const auto item=*found;collection->items.erase(found);
    if(item.desktop)DismissDesktopPath(state,item.path);
    return Change::Changed;
}
Change Reorder(Collection& collection,const std::vector<std::string>& order){
    if(order.size()!=collection.items.size())return Change::Invalid;
    std::set<std::string> ids;std::vector<Item> items;bool changed=false;
    for(std::size_t i=0;i<order.size();++i){
        if(!ids.insert(order[i]).second)return Change::Invalid;
        const auto found=std::find_if(collection.items.begin(),collection.items.end(),[&](const auto& item){return item.id==order[i];});
        if(found==collection.items.end())return Change::Invalid;
        changed|=collection.items[i].id!=order[i];items.push_back(*found);
    }
    if(!changed)return Change::Unchanged;
    collection.items=std::move(items);return Change::Changed;
}
Change Rename(Collection& collection,std::string_view text){
    const auto title=SanitizeTitle(text);if(!title)return Change::Invalid;
    if(*title==collection.title)return Change::Unchanged;
    collection.title=*title;return Change::Changed;
}
Change CreateCollection(State& state,std::string id,std::string_view text,std::optional<Bounds> bounds){
    const auto title=SanitizeTitle(text);
    if((id!=DefaultCollectionId&&!ValidId(id))||Find(state,id)||!title||(bounds&&!ValidBounds(*bounds)))return Change::Invalid;
    if(state.collections.size()>=MaxCollections)return Change::Limit;
    Collection collection;collection.id=std::move(id);collection.title=*title;collection.bounds=bounds;
    state.collections.push_back(std::move(collection));return Change::Changed;
}
Change RemoveCollection(State& state,std::string_view id){
    if(id==DefaultCollectionId)return Change::Invalid;
    const auto found=std::find_if(state.collections.begin(),state.collections.end(),[&](const auto& value){return value.id==id;});
    if(found==state.collections.end())return Change::Invalid;
    if(state.collections.size()==1)return Change::Invalid;
    state.collections.erase(found);return Change::Changed;
}
Change Reset(Collection& collection){
    const auto title=collection.desktopRole==DesktopRole::Applications?u8"桌面":collection.desktopRole==DesktopRole::Files?u8"文件":
        collection.id==DefaultCollectionId?u8"桌面收纳":u8"新建收纳";
    if(collection.title==title&&SameBounds(collection.bounds,{})&&collection.mode==Mode::Expanded&&collection.layout==Layout::Icons&&collection.iconScale==70)
        return Change::Unchanged;
    collection.title=title;collection.bounds.reset();collection.mode=Mode::Expanded;collection.layout=Layout::Icons;
    collection.iconScale=70;
    return Change::Changed;
}
namespace {
std::vector<std::string_view> SearchTerms(const std::string& normalized){
    const auto characters=Utf8(normalized);
    if(!characters)return {};
    std::vector<std::string_view> terms;std::optional<std::size_t> begin;
    for(const auto& c:*characters){
        if(Space(c.value)){if(begin){terms.emplace_back(normalized.data()+*begin,c.begin-*begin);begin.reset();}}
        else if(!begin)begin=c.begin;
    }
    if(begin)terms.emplace_back(normalized.data()+*begin,normalized.size()-*begin);
    return terms;
}
}
bool HasSearchTerms(std::string_view query,const TextKey& key){
    return key&&!SearchTerms(key(query)).empty();
}
std::vector<std::size_t> Search(const Collection& collection,std::string_view query,const TextKey& key){
    if(!key)return {};
    const auto normalized=key(query);
    if(!Utf8(normalized))return {};
    const auto terms=SearchTerms(normalized);
    std::vector<std::size_t> result;
    for(std::size_t i=0;i<collection.items.size();++i){
        const auto name=key(collection.items[i].name);
        if(std::all_of(terms.begin(),terms.end(),[&](const auto term){return name.find(term)!=std::string::npos;}))result.push_back(i);
    }
    return result;
}
}
