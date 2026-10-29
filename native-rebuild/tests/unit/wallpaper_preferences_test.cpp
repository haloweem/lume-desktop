#include "core/wallpaper/playback_preferences.h"
#include <cstdlib>
#include <iostream>
#include <map>
using namespace lume;
namespace {
void Expect(bool b,const char* why){if(!b){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
struct Files:settings::IFileStore {
    std::map<std::string,std::string> entries;bool fail=false;
    bool Read(const std::string& p,std::string& out)const override {
        const auto it=entries.find(p);if(it==entries.end())return false;out=it->second;return true;
    }
    bool Exists(const std::string& p)const override{return entries.count(p)!=0;}
    bool Write(const std::string& p,const std::string& v)override{if(fail)return false;entries[p]=v;return true;}
    bool Rename(const std::string& a,const std::string& b)override {
        if(fail||!Exists(a))return false;
        entries[b]=entries[a];entries.erase(a);return true;
    }
    bool Remove(const std::string& p)override{entries.erase(p);return true;}
};
}
int main() {
    wallpaper::PlaybackPreferences p;std::string text;
    p.displays.push_back({UINT64_MAX,"local-a",true,0,true});
    Expect(wallpaper::EncodePlaybackPreferences(p,text),"encode original mute/cover preferences");
    wallpaper::PlaybackPreferences restored;
    Expect(wallpaper::DecodePlaybackPreferences(text,restored)&&restored.displays.size()==1&&
        restored.displays.front().display==UINT64_MAX&&restored.displays.front().paused&&!restored.pauseFullscreen,
        "restore stable monitor and manual pause without precision loss");
    Files files;wallpaper::PlaybackPreferencesStore store(files);
    Expect(store.Save("wallpaper.json",p),"save valid preferences");
    const auto first=files.entries["wallpaper.json"];
    p.pauseFullscreen=true;Expect(store.Save("wallpaper.json",p),"replace settings atomically");
    Expect(files.entries["wallpaper.json.bak"]==first,"keep valid previous settings");
    files.entries["wallpaper.json"]="broken";
    Expect(store.Load("wallpaper.json").status=="recovered","recover intact backup");
    files.entries["wallpaper.json"]=R"({"version":9,"pauseFullscreen":false,"displays":[]})";
    const auto unknown=files.entries["wallpaper.json"];
    Expect(!store.Load("wallpaper.json").writable&&!store.Save("wallpaper.json",p)&&
        files.entries["wallpaper.json"]==unknown,"never downgrade unknown schema even with old backup");
    for(const auto* version:{"2.0","\"2\"","null","true"}) {
        files.entries["wallpaper.json"]=std::string("{\"version\":")+version+",\"pauseFullscreen\":false,\"displays\":[]}";
        Expect(!store.Load("wallpaper.json").writable&&!store.Save("wallpaper.json",p),
            "an explicitly unsupported version is not corrupt data eligible for overwrite");
    }
    files.entries.clear();files.fail=true;
    Expect(!store.Save("wallpaper.json",p)&&files.entries.empty(),"failed writes do not create successful settings");
    files.fail=false;
    p.displays.push_back(p.displays.front());
    Expect(!wallpaper::EncodePlaybackPreferences(p,text),"reject duplicate screen ownership");
    Expect(!wallpaper::DecodePlaybackPreferences(R"({"version":1,"version":1,"pauseFullscreen":false,"displays":[]})",restored),
        "reject duplicate keys");
    Expect(!wallpaper::DecodePlaybackPreferences(std::string(70000,' '),restored),"bounded configuration");
    return 0;
}
