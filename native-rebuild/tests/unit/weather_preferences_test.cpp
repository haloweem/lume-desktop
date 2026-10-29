#include "core/weather/weather_preferences.h"
#include <cstdlib>
#include <iostream>
#include <map>
using namespace lume;
namespace {
void Expect(bool value,const char* why){if(!value){std::cerr<<why<<'\n';std::exit(1);}}
struct Files:settings::IFileStore {
    std::map<std::string,std::string> data;bool fail=false;
    bool Read(const std::string& p,std::string& out)const override{auto it=data.find(p);if(it==data.end())return false;out=it->second;return true;}
    bool Exists(const std::string& p)const override{return data.count(p)!=0;}
    bool Write(const std::string& p,const std::string& v)override{if(fail)return false;data[p]=v;return true;}
    bool Rename(const std::string& a,const std::string& b)override{if(fail||!Exists(a))return false;data[b]=data[a];data.erase(a);return true;}
    bool Remove(const std::string& p)override{data.erase(p);return true;}
};
}
int main(){
    weather::Preferences p,read;std::string text;
    Expect(p.city=="auto"&&!p.window.enabled&&p.window.width==320&&p.window.height==152,"original defaults");
    p.city="shanghai";p.window.enabled=true;p.window.x=-800;p.window.placed=true;
    Expect(weather::EncodePreferences(p,text)&&weather::DecodePreferences(text,read)&&read.city=="shanghai"&&read.window.x==-800,"atomic city and geometry roundtrip");
    const auto original=text;text.insert(1,"\"city\":\"auto\",");
    Expect(!weather::DecodePreferences(text,read)&&read.city=="shanghai","duplicate city rejected without mutating destination");
    text=original;text.insert(text.find("\"enabled\""),"\"enabled\":false,");
    Expect(!weather::DecodePreferences(text,read),"nested duplicate rejected");
    p.city="invalid";
    Expect(!weather::EncodePreferences(p,text),"unknown city rejected");
    Expect(!weather::DecodePreferences(std::string(9000,' '),read),"bounded settings");
    Files files;weather::PreferencesStore store(files);
    Expect(store.Load("weather").state==widget::LoadState::Fresh,"fresh file");
    p.city="shanghai";Expect(store.Save("weather",p),"first save");
    p.city="beijing";Expect(store.Save("weather",p),"second save");
    Expect(store.Load("weather").value.city=="beijing","current file wins");
    files.data["weather"]="broken";
    const auto recovered=store.Load("weather");
    Expect(recovered.state==widget::LoadState::Recovered&&recovered.value.city=="shanghai","backup recovery");
    Expect(store.Save("weather",p)&&files.data["weather.corrupt"]=="broken","corrupt data isolated");
    const auto saved=files.data["weather"];files.fail=true;p.city="auto";
    Expect(!store.Save("weather",p)&&files.data["weather"]==saved,"save failure preserves existing settings");
    std::cout<<"weather preferences passed\n";
}
