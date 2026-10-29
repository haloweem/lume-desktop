#include "core/spectrum/spectrum.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <set>
#include <stdexcept>
namespace lume::spectrum {
namespace {
bool ValidBar(const BarStyle& b){
    return b.barWidth>=MinBarWidth&&b.barWidth<=MaxBarWidth&&b.barGap>=MinBarGap&&b.barGap<=MaxBarGap&&
        b.color>=0&&b.color<=0xffffff&&b.alpha>=0&&b.alpha<=255;
}
// `colored`: the object carries "color" and "alpha" (样式二). A file from
// before 组件颜色 has only width and gap, and reads as white.
bool ReadBar(const nlohmann::json& value,BarStyle& out,bool colored){
    const bool hasColor=colored&&value.is_object()&&value.size()==4;
    if(!value.is_object()||(value.size()!=2&&!hasColor)||!value.contains("barWidth")||!value["barWidth"].is_number_integer()||
       !value.contains("barGap")||!value["barGap"].is_number_integer())return false;
    out.barWidth=value["barWidth"].get<int>();out.barGap=value["barGap"].get<int>();
    if(hasColor){
        if(!value.contains("color")||!value["color"].is_number_integer()||!value.contains("alpha")||!value["alpha"].is_number_integer())return false;
        out.color=value["color"].get<int>();out.alpha=value["alpha"].get<int>();
    }
    // 样式二 used to be square blocks at 9/1. That untouched default becomes
    // the new thin-bar default 4/1; a width the user chose is kept.
    else if(colored&&out.barWidth==9&&out.barGap==1)out.barWidth=4;
    return ValidBar(out);
}
std::string WriteBar(const BarStyle& b,bool colored){
    return "{\"barWidth\":"+std::to_string(b.barWidth)+",\"barGap\":"+std::to_string(b.barGap)+
        (colored?",\"color\":"+std::to_string(b.color)+",\"alpha\":"+std::to_string(b.alpha):std::string())+"}";
}
}
const char* StyleName(Style style){return style==Style::Pulse?"pulse":style==Style::Blocks?"blocks":"waves";}
int ScalePercent(const widget::Preferences& window){
    const int percent=(window.width*100+WindowLimits.defaultWidth/2)/WindowLimits.defaultWidth;
    return std::clamp(percent,MinScale,MaxScale);
}
widget::Preferences Scaled(widget::Preferences window,int percent){
    percent=std::clamp(percent,MinScale,MaxScale);
    const double aspect=window.width>0?static_cast<double>(window.height)/window.width:
        static_cast<double>(WindowLimits.defaultHeight)/WindowLimits.defaultWidth;
    const int width=(WindowLimits.defaultWidth*percent+50)/100;
    window.width=std::clamp(width,WindowLimits.minWidth,WindowLimits.maxWidth);
    window.height=std::clamp(static_cast<int>(std::lround(window.width*aspect)),WindowLimits.minHeight,WindowLimits.maxHeight);
    return window;
}
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
        // A file from before 样式一 existed has only "blocks": that was the only look, not a
        // choice, so it opens on the new default 样式一 and keeps its blocks settings.
        // "waves" (样式三) came later still and is optional; a file without it
        // reads the default.
        if(!value.is_object()||value.size()<3||value.size()>5||!value.contains("window")||
            !value.contains("style")||!value["style"].is_string()||!value.contains("blocks")||
            value.size()!=3u+value.contains("pulse")+value.contains("waves")||(value.contains("waves")&&!value.contains("pulse")))return false;
        const auto style=value["style"].get<std::string>();
        if(style=="pulse")p.style=Style::Pulse;else if(style=="blocks")p.style=Style::Blocks;
        else if(style=="waves"&&value.contains("waves"))p.style=Style::Waves;else return false;
        if(!value.contains("pulse"))p.style=Style::Pulse;
        if(!ReadBar(value["blocks"],p.blocks,true)||(value.contains("pulse")&&!ReadBar(value["pulse"],p.pulse,false))||
           (value.contains("waves")&&!ReadBar(value["waves"],p.waves,false)))return false;
        if(!widget::Decode(value["window"].dump(),p.window,WindowLimits))return false;
        // The first release's default window was 880x120, too short for the
        // bars to rise; one still at that aspect (at any size) opens at the
        // current default aspect instead.
        auto& w=p.window;
        if(std::abs(w.height*880-w.width*120)<=880){
            w.height=std::clamp((w.width*WindowLimits.defaultHeight+WindowLimits.defaultWidth/2)/WindowLimits.defaultWidth,
                                WindowLimits.minHeight,WindowLimits.maxHeight);
        }
        out=std::move(p);return true;
    }catch(...){return false;}
}
bool EncodePreferences(const Preferences& p,std::string& out){
    std::string window;
    if(!ValidBar(p.pulse)||!ValidBar(p.blocks)||!ValidBar(p.waves)||!widget::Encode(p.window,window,WindowLimits))return false;
    out=std::string("{\"style\":\"")+StyleName(p.style)+"\",\"pulse\":"+WriteBar(p.pulse,false)+
        ",\"blocks\":"+WriteBar(p.blocks,true)+",\"waves\":"+WriteBar(p.waves,false)+",\"window\":"+window+"}";
    return true;
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

std::size_t BarCount(float width,int barWidth,int barGap){
    const float pitch=static_cast<float>(std::max(1,barWidth+barGap));
    const float usable=width-16+static_cast<float>(barGap);  // 8 DIP padding each side
    return usable<pitch?0:std::min<std::size_t>(512,static_cast<std::size_t>(usable/pitch));
}

std::vector<float> Magnitudes(const std::vector<float>& frame){
    const std::size_t n=FrameSize;
    std::vector<std::complex<float>> data(n);
    constexpr float pi=3.14159265358979f;
    for(std::size_t i=0;i<n&&i<frame.size();++i){
        const float hann=.5f-.5f*std::cos(2*pi*static_cast<float>(i)/static_cast<float>(n-1));
        data[i]={frame[i]*hann,0};
    }
    // Iterative radix-2 FFT.
    for(std::size_t i=1,j=0;i<n;++i){
        std::size_t bit=n>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j)std::swap(data[i],data[j]);
    }
    for(std::size_t length=2;length<=n;length<<=1){
        const float angle=-2*pi/static_cast<float>(length);
        const std::complex<float> step(std::cos(angle),std::sin(angle));
        for(std::size_t i=0;i<n;i+=length){
            std::complex<float> w(1,0);
            for(std::size_t k=0;k<length/2;++k){
                const auto u=data[i+k],v=data[i+k+length/2]*w;
                data[i+k]=u+v;data[i+k+length/2]=u-v;w*=step;
            }
        }
    }
    std::vector<float> out(n/2);
    // Normalise so a full-scale sine reads about 1.
    for(std::size_t i=0;i<out.size();++i)out[i]=std::abs(data[i])*4/static_cast<float>(n);
    return out;
}

namespace {
constexpr float LowCut=50,HighCut=10000;
// Weight of a band at f kHz: f above 1 kHz (cava's equaliser), f^0.6 below.
// Tuned on a recording of the reference playing the same audio, where its
// bass bars sit lower than the mids beside them; the level stays linear.
constexpr float BassTilt=.6f;
// First bin of each band plus one past the last band (count+1 entries).
std::vector<std::size_t> BandEdges(std::size_t count,unsigned sampleRate,std::size_t bins){
    std::vector<std::size_t> edges(count+1,0);
    if(!count||!bins||!sampleRate)return edges;
    const float high=std::min(HighCut,static_cast<float>(sampleRate)*.5f);
    const float binHz=static_cast<float>(sampleRate)/static_cast<float>(FrameSize);
    for(std::size_t n=0;n<=count;++n){
        const float hz=LowCut*std::pow(high/LowCut,static_cast<float>(n)/static_cast<float>(count));
        auto bin=static_cast<std::size_t>(hz/binHz);
        if(n&&bin<=edges[n-1])bin=edges[n-1]+1;
        edges[n]=std::min(bin,bins);
    }
    return edges;
}
}
std::pair<std::size_t,std::size_t> BandBins(std::size_t index,std::size_t count,unsigned sampleRate,std::size_t bins){
    const auto edges=BandEdges(count,sampleRate,bins);
    if(index>=count||!bins)return {0,0};
    const auto first=std::min(edges[index],bins-1);
    return {first,std::clamp(edges[index+1]?edges[index+1]-1:0,first,bins-1)};
}

std::vector<float> Bands(const std::vector<float>& magnitudes,unsigned sampleRate,std::size_t count){
    std::vector<float> out(count,0.f);
    if(!count||magnitudes.empty()||!sampleRate)return out;
    const auto edges=BandEdges(count,sampleRate,magnitudes.size());
    const float binHz=static_cast<float>(sampleRate)/static_cast<float>(FrameSize);
    for(std::size_t b=0;b<count;++b){
        const auto first=std::min(edges[b],magnitudes.size()-1);
        const auto last=std::clamp(edges[b+1]?edges[b+1]-1:0,first,magnitudes.size()-1);
        float sum=0;
        for(std::size_t i=first;i<=last;++i)sum+=magnitudes[i];
        // Music is quieter the higher it goes; weighting by frequency evens
        // the bars out across the width (1 kHz reads as is).
        const float khz=static_cast<float>(first)*binHz/1000,weight=khz>=1?khz:std::pow(khz,BassTilt);
        out[b]=sum/static_cast<float>(last-first+1)*weight;
    }
    return out;
}

std::vector<float> StereoBands(const std::vector<float>& left,const std::vector<float>& right,
                               unsigned sampleRate,std::size_t count){
    const std::size_t half=count/2;
    auto out=Bands(left,sampleRate,half);
    std::reverse(out.begin(),out.end());
    const auto second=Bands(right,sampleRate,count-half);
    out.insert(out.end(),second.begin(),second.end());
    return out;
}

void Motion::Step(std::vector<float>& shown,const std::vector<float>& bands,float seconds){
    const auto n=bands.size();
    if(peak_.size()!=n){
        peak_.assign(n,0.f);fall_.assign(n,0.f);memory_.assign(n,0.f);previous_.assign(n,0.f);
        sensitivity_=1;settling_=true;
    }
    shown.resize(n);
    if(seconds<=0)return;
    // cava's constants are per frame at 60 frames a second.
    constexpr float NoiseReduction=.77f,FallStep=.028f,Headroom=1.1f;
    const float frames=std::clamp(seconds*60,.25f,4.f);
    const float gravity=std::max(1.f,1.54f/NoiseReduction),keep=std::pow(NoiseReduction,frames);
    bool overshoot=false,silent=true;
    for(std::size_t i=0;i<n;++i){
        if(bands[i]>0)silent=false;
        float value=bands[i]*sensitivity_;
        if(value<previous_[i]){
            value=std::max(0.f,peak_[i]*(1-fall_[i]*fall_[i]*gravity));
            fall_[i]+=FallStep*frames;
        }else{peak_[i]=value;fall_[i]=0;}
        previous_[i]=value;
        value=memory_[i]*keep+value*(1-keep)/(1-NoiseReduction);
        memory_[i]=value;
        if(value>1)overshoot=true;
        // Auto sensitivity aims at 1; drawn at 1.1 the bars reach the heights
        // the reference shows at the same size, the loudest just touching the top.
        shown[i]=std::clamp(value*Headroom,0.f,1.f);
    }
    if(overshoot){sensitivity_*=std::pow(.98f,frames);settling_=false;}
    else if(!silent){sensitivity_*=std::pow(1.001f,frames);if(settling_)sensitivity_*=std::pow(1.1f,frames);}
}
}
