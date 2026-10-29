#include "core/organizer/organizer_model.h"
#include "core/organizer/organizer_store.h"
#include "core/organizer/desktop_organization.h"
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <cstdio>
#include <map>
#include "vendor/nlohmann/json.hpp"
using namespace lume::organizer;
void Expect(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    auto state=Defaults();
    Expect(state.collections.size()==1&&state.collections[0].id=="default"&&state.collections[0].title==u8"桌面收纳",
        "original default collection must be present");
    const TextKey key=[](std::string_view text){std::string out(text);for(char& c:out)if(c>='A'&&c<='Z')c+=32;return out;};
    const auto id=[](unsigned index){char text[37]{};std::snprintf(text,sizeof(text),"00000000-0000-4000-8000-%012u",index);return std::string(text);};
    Expect(Valid(state,key)&&state.collections[0].enabled&&state.collections[0].items.empty(),"original reference-store defaults");
    Expect(NormalizeLocalPath("c:/Docs/../Work//file.txt")==std::optional<std::string>("c:\\Work\\file.txt"),"Windows paths normalized without touching target");
    Expect(NormalizeLocalPath("C:/../../")==std::optional<std::string>("C:\\"),"path traversal cannot escape drive root");
    for(const auto path:{"relative.txt","C:relative","\\\\server\\share\\a","\\\\?\\C:\\file","C:\\file:stream","C:\\a\nb","https://example.test"})
        Expect(!NormalizeLocalPath(path),"reject network, device, relative, stream and control paths");
    Expect(!NormalizeLocalPath(std::string("C:\\bad\xc0\xaf")),"reject malformed UTF8");
    Expect(SanitizeTitle(u8"　 桌面\n收纳　")==std::optional<std::string>(u8"桌面 收纳"),"source control replacement and Unicode trim");
    std::string emoji;for(int i=0;i<80;++i)emoji+=u8"📁";
    Expect(SanitizeTitle(emoji).has_value()&&!SanitizeTitle(emoji+u8"📁"),"title limit counts Unicode codepoints like Array.from");
    auto* collection=Find(state,"default");Expect(collection!=nullptr,"default lookup");
    std::vector<Item> batch={{id(1),"C:/Docs/Report 2026.TXT","ignored",ItemKind::File},
        {id(2),"C:/Tools/编辑器.LNK","ignored",ItemKind::Shortcut},{id(3),"D:/Folder/","ignored",ItemKind::Folder}};
    Expect(AddItems(*collection,batch,key)==Change::Changed&&collection->items.size()==3,"atomic batch inserts file shortcut and folder references");
    Expect(collection->items[1].name==u8"编辑器"&&collection->items[2].path=="D:\\Folder","display names derived from normalized references");
    batch={{id(4),"c:/docs/report 2026.txt",{},ItemKind::File}};
    Expect(AddItems(*collection,batch,key)==Change::Unchanged,"duplicate Windows path ignored across letter case");
    batch={{id(4),"C:/good",{},ItemKind::File},{id(5),"../bad",{},ItemKind::File}};
    Expect(AddItems(*collection,batch,key)==Change::Invalid&&collection->items.size()==3,
        "bad later item rejects whole batch without partial insertion");
    Expect(Reorder(*collection,{id(2),id(1),id(1)})==Change::Invalid&&collection->items[0].id==id(1),"duplicate reorder cannot lose a reference");
    Expect(Reorder(*collection,{id(3),id(1),id(2)})==Change::Changed&&collection->items[0].id==id(3),"exact ID permutation preserves identity");
    Expect(Search(*collection,"REPORT 2026",key)==std::vector<std::size_t>{1},"search requires every normalized name token");
    Expect(Search(*collection,"report missing",key).empty()&&Search(*collection,"  ",key).size()==3,"search no-match and empty-query behavior");
    Expect(!HasSearchTerms(u8"　 \t",key)&&HasSearchTerms(std::string(200,'x'),key),
        "whitespace query permits reorder and the full 200-character query stays a filter");
    collection->enabled=false;collection->mode=Mode::Collapsed;collection->layout=Layout::List;collection->bounds=Bounds{20,20,420,392};
    Expect(Rename(*collection,u8"资料")==Change::Changed&&Reset(*collection)==Change::Changed,"reset original presentation");
    Expect(collection->items.size()==3&&!collection->enabled&&!collection->bounds&&collection->mode==Mode::Expanded&&collection->layout==Layout::Icons,
        "reset retains targets and explicit enabled state");
    Expect(RemoveItem(*collection,id(1))==Change::Changed&&collection->items.size()==2&&RemoveItem(*collection,id(1))==Change::Unchanged,
        "removal only changes in-memory reference list; domain has no filesystem delete capability");
    Expect(RemoveCollection(state,"default")==Change::Invalid,"default collection protected");
    Expect(CreateCollection(state,id(10))==Change::Changed&&Find(state,id(10))->title==u8"新建收纳","independent named collection");
    Expect(CreateCollection(state,id(10))==Change::Invalid&&RemoveCollection(state,id(10))==Change::Changed,"duplicate collection ID rejected");
    Expect(Valid(state,key),"model remains valid after mixed operations");
    auto full=Defaults();auto& filled=full.collections[0];batch.clear();
    for(unsigned i=0;i<MaxItems;++i)batch.push_back({id(i+20),"C:\\files\\"+std::to_string(i),{},ItemKind::File});
    Expect(AddItems(filled,batch,key)==Change::Changed&&filled.items.size()==120,"original 120-item capacity");
    batch={{id(200),"C:\\extra",{},ItemKind::File}};
    Expect(AddItems(filled,batch,key)==Change::Limit&&filled.items.size()==120,"overflow preserves all old references");
    for(unsigned i=1;i<MaxCollections;++i)Expect(CreateCollection(full,id(300+i))==Change::Changed,"collection capacity insertion");
    Expect(CreateCollection(full,id(400))==Change::Limit&&full.collections.size()==64,"original 64-collection capacity");
    Expect(Valid(full,key),"bounded full state is valid");
    struct Files:lume::settings::IFileStore{
        std::map<std::string,std::string> data;bool fail=false;
        bool Read(const std::string& p,std::string& out)const override{auto i=data.find(p);if(i==data.end())return false;out=i->second;return true;}
        bool Exists(const std::string& p)const override{return data.count(p)!=0;}
        bool Write(const std::string& p,const std::string& v)override{if(fail)return false;data[p]=v;return true;}
        bool Rename(const std::string& a,const std::string& b)override{if(fail||!Exists(a))return false;data[b]=data[a];data.erase(a);return true;}
        bool Remove(const std::string& p)override{data.erase(p);return true;}
    } files;
    Store store(files,key);std::string encoded;Expect(Encode(state,encoded),"state codec encodes full state");
    Expect(files.Write("organizer.json",encoded), "fixture state written");
    auto loaded=store.Load("organizer.json");
    Expect(loaded.state==LoadState::Loaded&&loaded.value.collections.size()==1,"store restores collections");
    files.data["organizer.json"]="{bad";files.data["organizer.json.bak"]=encoded;
    loaded=store.Load("organizer.json");Expect(loaded.state==LoadState::Recovered&&loaded.value.collections.size()==1,"store recovers valid backup");
    const auto before=files.data["organizer.json"];files.fail=true;
    Expect(!store.Save("organizer.json",state)&&files.data["organizer.json"]==before,"failed save preserves original");
    files.fail=false;Expect(store.Save("organizer.json",state)&&files.data["organizer.json.corrupt"]==before,"invalid primary is isolated before save");
    State decoded;
    {
        auto scaled=state;scaled.collections.front().iconScale=120;scaled.collections.front().bounds=Bounds{0,0,120,216};
        std::string text;Expect(Encode(scaled,text)&&Decode(text,decoded,key)&&decoded.collections.front().iconScale==120&&
            decoded.collections.front().bounds->width==120,"icon scale and narrow geometry survive roundtrip");
        auto invalid=nlohmann::json::parse(text);
        for(const auto& scale:{nlohmann::json(49),nlohmann::json(151),nlohmann::json(90.5),nlohmann::json("100")}){
            invalid["collections"][0]["iconScale"]=scale;Expect(!Decode(invalid.dump(),decoded,key),"invalid icon scale rejected");
        }
        auto legacy=nlohmann::json::parse(encoded);legacy["collections"][0].erase("iconScale");
        Expect(Decode(legacy.dump(),decoded,key)&&decoded.collections.front().iconScale==100,"legacy state defaults to original icon size");
        invalid["collections"][0]["iconScale"]=150;Expect(Decode(invalid.dump(),decoded,key)&&decoded.collections.front().iconScale==120,"old larger icons migrate to new maximum");
        Expect(Reset(scaled.collections.front())==Change::Changed&&scaled.collections.front().iconScale==70,"reset restores icon size");
    }
    Expect(!Decode("{\"version\":2,\"version\":2,\"collections\":[]}",decoded,key),"duplicate keys rejected");
    auto bad=nlohmann::json::parse(encoded);
    bad["collections"][0]["bounds"]={{"x",0},{"y",0},{"width",4294967716LL},{"height",392}};
    Expect(!Decode(bad.dump(),decoded,key),"oversized integer cannot wrap into valid bounds");
    bad=nlohmann::json::parse(encoded);
    const auto marker=std::string("\"version\":2");
    const auto duplicate=encoded.substr(0,encoded.find(marker))+marker+","+encoded.substr(encoded.find(marker));
    Expect(!Decode(duplicate,decoded,key),"valid-looking duplicate version is rejected");
    auto duplicates=state;
    auto duplicateItem=duplicates.collections[0].items[0];duplicateItem.id=id(1000);
    duplicateItem.path="d:\\folder";duplicateItem.name="folder";duplicates.collections[0].items.push_back(duplicateItem);
    Expect(!store.Save("organizer.json",duplicates),"store validates case-insensitive path identity before committing");
    {
        // Missing or renamed keys must fail decoding, never dereference end().
        auto shape=nlohmann::json::parse(encoded);
        for(const auto* missing:{"mode","layout","enabled","bounds","items"}){
            auto broken=shape;broken["collections"][0].erase(missing);broken["collections"][0]["renamed"]=1;
            Expect(!Decode(broken.dump(),decoded,key),"renamed collection key is rejected safely");
        }
        auto rowBroken=shape;
        if(!rowBroken["collections"][0]["items"].empty()){
            rowBroken["collections"][0]["items"][0].erase("kind");rowBroken["collections"][0]["items"][0]["type"]="file";
            Expect(!Decode(rowBroken.dump(),decoded,key),"renamed item key is rejected safely");
        }
        Expect(!Decode(R"({"version":2,"collection":[]})",decoded,key),"renamed top-level key is rejected safely");
        auto boundsBroken=shape;boundsBroken["collections"][0]["bounds"]={{"x",0},{"y",0},{"w",420},{"height",392}};
        Expect(!Decode(boundsBroken.dump(),decoded,key),"renamed bounds key is rejected safely");
    }
    {
        // Tab groups: merge, activate, detach, delete and move between tabs.
        State groups=Defaults();
        Expect(CreateCollection(groups,id(201),u8"工作",Bounds{100,100,500,400})==Change::Changed&&
            CreateCollection(groups,id(202),u8"游戏",Bounds{700,100,420,392})==Change::Changed,"create three boxes");
        Find(groups,id(202))->items.push_back({id(301),"C:\\Games\\a.lnk","a",ItemKind::Shortcut});
        Find(groups,"default")->items.push_back({id(302),"C:\\Docs\\b.txt","b.txt",ItemKind::File});
        Find(groups,id(201))->iconScale=73;
        Expect(MergeGroup(groups,id(202),id(201))==Change::Changed&&Find(groups,id(202))->iconScale==73,"merge inherits host icon size");
        Expect(Find(groups,id(202))->parent==id(201)&&!Find(groups,id(202))->bounds&&Find(groups,id(201))->active==id(202),
            "merged box becomes the active tab and drops its window bounds");
        Expect(Valid(groups,key),"grouped state stays valid");
        Expect(Tabs(groups,id(201)).size()==2&&ActiveTab(groups,id(201))->id==id(202),"tab list and active tab");
        Expect(MergeGroup(groups,id(202),id(201))==Change::Invalid,"merging inside one group is refused");
        Expect(MergeGroup(groups,"default",id(202))==Change::Changed,"merging onto a tab targets its host");
        const auto tabs=Tabs(groups,id(201));
        Expect(tabs.size()==3&&tabs[1]->id==id(202)&&tabs[2]->id=="default","tabs keep drop order");
        Expect(Find(groups,"default")->items.size()==1,"merge keeps every item");
        std::string groupedText;Expect(Encode(groups,groupedText),"grouped state encodes");
        State reloaded;Expect(Decode(groupedText,reloaded,key)&&Tabs(reloaded,id(201)).size()==3&&
            ActiveTab(reloaded,id(201))->id=="default","tab groups survive a save and reload");
        auto orphan=nlohmann::json::parse(groupedText);
        for(auto& row:orphan["collections"])if(row["id"]==id(202))row["parent"]=id(999);
        Expect(!Decode(orphan.dump(),reloaded,key),"a tab whose host is missing is rejected");
        Expect(ActivateTab(groups,id(201),id(201))==Change::Changed&&ActiveTab(groups,id(201))->id==id(201),"activate host tab");
        Expect(ActivateTab(groups,id(201),id(201))==Change::Unchanged,"activating the visible tab is a no-op");
        Expect(ActivateTab(groups,"default",id(202))==Change::Invalid,"a tab is not a host");
        {
            // Reordering: [201,202,default] -> move default to the front.
            auto moved=groups;
            Expect(ActivateTab(moved,id(201),id(202))==Change::Changed,"show the middle tab");
            const auto window=*Find(moved,id(201))->bounds;
            Expect(MoveTab(moved,"default",0)==Change::Changed,"move the last tab to the front");
            const auto order=Tabs(moved,"default");
            Expect(order.size()==3&&order[1]->id==id(201)&&order[2]->id==id(202),"front tab becomes the host, others follow");
            Expect(Find(moved,"default")->bounds&&Find(moved,"default")->bounds->x==window.x&&!Find(moved,id(201))->bounds,
                "the new host inherits the window");
            Expect(Find(moved,"default")->iconScale==73,"promoted tab inherits host icon size");
            Expect(ActiveTab(moved,"default")->id==id(202)&&Valid(moved,key),"the visible tab is kept");
            Expect(MoveTab(moved,id(202),0)==Change::Changed&&ActiveTab(moved,id(202))->id==id(202)&&Valid(moved,key),
                "moving the visible tab to the front keeps it visible");
            // Now [202,default,201]; move default to the end.
            Expect(MoveTab(moved,"default",2)==Change::Changed&&Tabs(moved,id(202))[2]->id=="default","move a tab to the end");
            Expect(MoveTab(moved,"default",9)==Change::Unchanged&&MoveTab(moved,id(999),0)==Change::Invalid,"clamped and invalid moves");
        }
        Expect(MoveItems(groups,id(202),"default",{id(301)},key)==Change::Changed&&
            Find(groups,"default")->items.size()==2&&Find(groups,id(202))->items.empty(),"move icons between tabs");
        Expect(DetachTab(groups,"default",Bounds{50,60,300,320})==Change::Changed&&Find(groups,"default")->parent.empty()&&
            Find(groups,"default")->bounds->x==50,"detach a tab into its own window");
        Expect(DetachTab(groups,id(201),Bounds{10,10,420,392})==Change::Changed&&Find(groups,id(202))->parent.empty()&&
            Find(groups,id(202))->bounds&&Find(groups,id(202))->bounds->width==500,"detaching a host promotes the next tab with the window size");
        Expect(Valid(groups,key),"state stays valid after detaches");
        Expect(MergeGroup(groups,id(202),"default")==Change::Changed&&DeleteTab(groups,"default")==Change::Invalid,
            "the default box can be a tab but is never deleted");
        Expect(DeleteTab(groups,id(202))==Change::Changed&&!Find(groups,id(202))&&Valid(groups,key)&&
            ActiveTab(groups,"default")->id=="default","deleting the active tab falls back to the host");
        Expect(MergeGroup(groups,"default",id(201))==Change::Changed&&DeleteTab(groups,id(201))==Change::Changed&&
            Find(groups,"default")->parent.empty()&&Find(groups,"default")->bounds&&Valid(groups,key),
            "deleting a host promotes its tab, even the default box");
        State pruned=Defaults();
        Expect(CreateCollection(pruned,id(401),u8"空")==Change::Changed&&CreateCollection(pruned,id(402),u8"有")==Change::Changed,"pruning fixture");
        Find(pruned,id(401))->enabled=false;Find(pruned,id(402))->enabled=false;
        Find(pruned,id(402))->items.push_back({id(403),"C:\\x.txt","x.txt",ItemKind::File});
        Find(pruned,"default")->enabled=false;
        Expect(PruneClosedEmpty(pruned)&&!Find(pruned,id(401))&&Find(pruned,id(402))&&Find(pruned,"default"),
            "only empty closed non-default boxes are pruned");
    }
    {
        auto grouped=Defaults();const std::string host="00000000-0000-4000-8000-000000000501";
        Expect(CreateCollection(grouped,host,u8"分组")==Change::Changed&&
            MergeGroup(grouped,"default",host)==Change::Changed,"default tab close regression fixture");
        Find(grouped,"default")->enabled=false;SyncGroups(grouped);
        Expect(!Find(grouped,"default")->enabled,"group synchronization must preserve the module disabled flag on a default child tab");
        std::string groupedText;State groupedReloaded;
        Expect(Encode(grouped,groupedText)&&Decode(groupedText,groupedReloaded,key)&&!Find(groupedReloaded,"default")->enabled,
            "disabled grouped organizer stays disabled after persistence round trip");
        Expect(DetachTab(grouped,"default",Bounds{0,0,420,392})==Change::Changed&&!Find(grouped,"default")->enabled,
            "detaching default does not silently reenable the module");
        Expect(MergeGroup(grouped,"default",host)==Change::Changed&&!Find(grouped,"default")->enabled,
            "merging default does not silently reenable the module");
        Expect(MoveTab(grouped,"default",0)==Change::Changed&&!Find(grouped,"default")->enabled,
            "promoting default through tab reorder preserves the module switch");
        Expect(MoveTab(grouped,"default",1)==Change::Changed&&!Find(grouped,"default")->enabled,
            "demoting default through tab reorder preserves the module switch");
        Expect(DeleteTab(grouped,host)==Change::Changed&&!Find(grouped,"default")->enabled,
            "promoting default after host deletion preserves the module switch");
    }
    {
        auto desktop=DesktopDefaults();
        Expect(desktop.collections.size()==2&&desktop.collections[0].title==u8"桌面"&&desktop.collections[1].title==u8"文件"&&
            desktop.collections[0].parent.empty()&&desktop.collections[1].parent=="default"&&
            desktop.collections[0].iconScale==70&&desktop.collections[1].iconScale==70,"one default window with Desktop and Files tabs uses 70 percent icons");
        Expect(CreateCollection(desktop,id(900),u8"自定义")==Change::Changed&&desktop.collections.back().iconScale==70,"new manual collection also defaults to 70 percent");
        const auto filesId=desktop.collections[1].id;
        DesktopSnapshot source;source.complete=true;
        source.entries={{{id(901),"C:\\Desktop\\app.lnk",{},ItemKind::Shortcut},true},
            {{id(902),"C:\\Desktop\\Folder",{},ItemKind::Folder},false},
            {{id(903),"C:\\Desktop\\report.docx",{},ItemKind::File},false},
            {{id(904),"C:\\Desktop\\photo.png",{},ItemKind::File},false},
            {{id(905),"C:\\Desktop\\movie.mp4",{},ItemKind::File},false}};
        auto result=SyncDesktop(desktop,source,key);
        Expect(result.changed&&!result.limited&&Find(desktop,"default")->items.size()==1&&Find(desktop,filesId)->items.size()==4,
            "apps go into Desktop; folders documents images and videos go into Files");
        const auto appId=Find(desktop,"default")->items[0].id;
        std::string beforeScan;Expect(Encode(desktop,beforeScan),"desktop collections encode");
        result=SyncDesktop(desktop,source,key);
        std::string after;Expect(!result.changed&&Encode(desktop,after)&&beforeScan==after,"unchanged scan preserves IDs order and makes no write");
        source.complete=false;source.entries.clear();
        Expect(!SyncDesktop(desktop,source,key).changed&&Find(desktop,filesId)->items.size()==4,"failed desktop enumeration cannot erase cached references");
        source.complete=true;
        source.entries={{{id(906),"C:\\Desktop\\app.lnk",{},ItemKind::Shortcut},true},
            {{id(907),"C:\\Desktop\\renamed.png",{},ItemKind::File},false}};
        Expect(SyncDesktop(desktop,source,key).changed&&Find(desktop,"default")->items[0].id==appId&&Find(desktop,filesId)->items.size()==1,
            "desktop delete and rename reconcile only managed references while preserving surviving IDs");
        Expect(AddItems(*Find(desktop,filesId),{{id(908),"D:\\manual.txt",{},ItemKind::File}},key)==Change::Changed,"manual file fixture");
        source.entries.clear();SyncDesktop(desktop,source,key);
        Expect(Find(desktop,"default")->items.empty()&&Find(desktop,filesId)->items.size()==1&&Find(desktop,filesId)->items[0].path=="D:\\manual.txt",
            "manual files outside Desktop survive automatic cleanup");
        auto legacy=Defaults();legacy.collections[0].iconScale=81;legacy.collections[0].items={{id(909),"C:\\Desktop\\Folder","Folder",ItemKind::Folder}};
        Expect(EnsureDesktopCollections(legacy)&&legacy.collections.size()==2&&legacy.collections[0].iconScale==81&&
            Host(legacy,legacy.collections[1].id)->id=="default","upgrade adds Files as a tab and preserves saved icon size");
        DesktopSnapshot classify{{{{id(910),"C:\\Desktop\\Folder",{},ItemKind::Folder},false}},true};
        Expect(SyncDesktop(legacy,classify,key).changed&&legacy.collections[0].items.empty()&&legacy.collections[1].items[0].id==id(909),
            "existing desktop references move to correct category without duplicate or ID change");
        State reloaded;std::string text;Expect(Encode(legacy,text)&&Decode(text,reloaded,key)&&reloaded.collections[1].items[0].desktop&&
            reloaded.collections[1].desktopRole==DesktopRole::Files,"automatic role and provenance survive restart");
        for(unsigned i=0;i<MaxItems;++i)classify.entries.push_back({{id(1000+i),"C:\\Desktop\\file"+std::to_string(i)+".txt",{},ItemKind::File},false});
        Expect(SyncDesktop(legacy,classify,key).limited&&legacy.collections[1].items.size()==MaxItems,"category capacity reports overflow without unbounded allocation");
    }
    {
        // An icon the user takes out (删除图标, or dragged onto the desktop)
        // stays out while it is on the desktop; it may come back only when the
        // user puts it back, or after it left the desktop and returned.
        auto desktop=DesktopDefaults();const auto filesId=desktop.collections[1].id;
        DesktopSnapshot source;source.complete=true;
        source.entries={{{id(1201),"C:\\Desktop\\a.txt",{},ItemKind::File},false},
            {{id(1202),"C:\\Desktop\\b.txt",{},ItemKind::File},false}};
        SyncDesktop(desktop,source,key);
        auto* tab=Find(desktop,filesId);
        const auto taken=tab->items[0];
        Expect(RemoveItem(*tab,taken.id)==Change::Changed&&tab->dismissed==std::vector<std::string>{taken.path},
            "taking a desktop icon out records its path");
        Expect(!SyncDesktop(desktop,source,key).changed&&Find(desktop,filesId)->items.size()==1,"sync does not add a dismissed icon back");
        State reloaded;std::string text;
        Expect(Encode(desktop,text)&&Decode(text,reloaded,key)&&Find(reloaded,filesId)->dismissed.size()==1,"dismissal survives restart");
        Expect(AddItems(*Find(desktop,filesId),{{id(1203),taken.path,{},ItemKind::File}},key)==Change::Changed&&
            SyncDesktop(desktop,source,key).changed&&Find(desktop,filesId)->dismissed.empty()&&Find(desktop,filesId)->items.size()==2,
            "putting the icon back ends the dismissal");
        Expect(RemoveItem(*Find(desktop,filesId),id(1203))==Change::Changed&&!Find(desktop,filesId)->dismissed.empty(),"dismissed again");
        // One scan without the entry (a shortcut an update rewrote, a folder
        // briefly unavailable) must not bring it back on the next scan.
        auto missing=source;missing.entries.erase(missing.entries.begin());
        SyncDesktop(desktop,missing,key);
        Expect(Find(desktop,filesId)->dismissed.size()==1,"an entry missing from one scan stays dismissed");
        Expect(!SyncDesktop(desktop,source,key).changed&&Find(desktop,filesId)->items.size()==1,
            "the dismissed entry does not return when it is scanned again");
        auto manual=Defaults();
        Expect(AddItems(manual.collections[0],{{id(1204),"D:\\x.txt",{},ItemKind::File}},key)==Change::Changed&&
            RemoveItem(manual.collections[0],id(1204))==Change::Changed&&manual.collections[0].dismissed.empty(),
            "removing from a manual collection records nothing");
        // Moved to a custom tab: shown there only, never added to 文件 again;
        // deleting it there keeps it out of every tab.
        auto custom=DesktopDefaults();const auto customFiles=custom.collections[1].id;
        SyncDesktop(custom,source,key);
        Expect(CreateCollection(custom,id(1300),u8"工作")==Change::Changed,"custom collection");
        const auto moved=Find(custom,customFiles)->items[0].id;
        Expect(MoveItems(custom,customFiles,id(1300),{moved},key)==Change::Changed,"move a desktop icon to a custom tab");
        for(int scan=0;scan<3;++scan)SyncDesktop(custom,source,key);
        Expect(Find(custom,customFiles)->items.size()==1&&Find(custom,id(1300))->items.size()==1,
            "an icon moved to a custom tab is not added to the desktop tab again");
        Expect(RemoveItem(custom,id(1300),moved)==Change::Changed,"delete it from the custom tab");
        for(int scan=0;scan<3;++scan)SyncDesktop(custom,source,key);
        Expect(Find(custom,customFiles)->items.size()==1&&Find(custom,id(1300))->items.empty(),
            "an icon deleted from a custom tab does not come back");
        // Deleting the 文件 tab: it is not recreated and its entries are not
        // collected again, also after a restart, until the organizer is reset.
        auto retire=DesktopDefaults();const auto retiredFiles=retire.collections[1].id;
        SyncDesktop(retire,source,key);
        Expect(DeleteTab(retire,retiredFiles)==Change::Changed&&retire.filesRetired,"delete the 文件 tab");
        for(int scan=0;scan<3;++scan){SyncDesktop(retire,source,key);EnsureDesktopCollections(retire);}
        const auto filesTab=[](const State& value){return std::any_of(value.collections.begin(),value.collections.end(),
            [](const auto& c){return c.desktopRole==DesktopRole::Files;});};
        std::size_t total=0;for(const auto& c:retire.collections)total+=c.items.size();
        Expect(!filesTab(retire)&&total==0,"a deleted desktop tab and its icons do not come back");
        State reopened;std::string saved;
        Expect(Encode(retire,saved)&&Decode(saved,reopened,key)&&reopened.filesRetired&&!reopened.applicationsRetired,
            "a deleted desktop tab stays deleted after a restart");
        EnsureDesktopCollections(reopened);
        Expect(!filesTab(reopened),"restart does not recreate the deleted tab");
        Expect(RestoreDesktopCollections(reopened)&&filesTab(reopened)&&!reopened.filesRetired,"organizer reset restores it");
        auto invalid=Defaults();invalid.collections[0].dismissed={"D:\\x.txt"};
        Expect(!Valid(invalid,key),"only desktop collections may hold dismissals");
    }
    std::cout<<"organizer model reference semantics passed\n";
}
