#include "core/widget/widget_preferences_store.h"
namespace lume::widget {
LoadedPreferences PreferencesStore::Load(const std::string& path)const{
    LoadedPreferences result{Defaults(limits_),LoadState::Fresh};
    if(path.empty()){result.state=LoadState::Failed;return result;}
    std::string text;
    if(files_.Exists(path)){
        if(files_.Read(path,text)&&Decode(text,result.value,limits_)){result.state=LoadState::Loaded;return result;}
        result.state=LoadState::Failed;
    }
    if(files_.Exists(path+".bak")){
        if(files_.Read(path+".bak",text)&&Decode(text,result.value,limits_))result.state=LoadState::Recovered;
        else result.state=LoadState::Failed;
    }
    return result;
}
bool PreferencesStore::Save(const std::string& path,const Preferences& value){
    std::string text;if(path.empty()||!Encode(value,text,limits_))return false;
    std::string original;bool validOriginal=false;
    if(files_.Exists(path)){
        Preferences old;
        if(!files_.Read(path,original))return false;
        validOriginal=Decode(original,old,limits_);
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
