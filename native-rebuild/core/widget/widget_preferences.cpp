#include "core/widget/widget_preferences.h"
#include "core/settings/fixed_json_reader.h"
namespace lume::widget {
Preferences Defaults(Limits limits){Preferences p;p.width=limits.defaultWidth;p.height=limits.defaultHeight;return p;}
bool Valid(const Preferences& p,Limits l){
    return p.width>=l.minWidth&&p.width<=l.maxWidth&&p.height>=l.minHeight&&p.height<=l.maxHeight&&
        p.x>=-100000&&p.x<=100000&&p.y>=-100000&&p.y<=100000&&p.panelColor>=0&&p.panelColor<=0xffffff;
}
Preferences Clamped(Preferences p,Limits l){
    p.width=p.width<l.minWidth?l.minWidth:p.width>l.maxWidth?l.maxWidth:p.width;
    p.height=p.height<l.minHeight?l.minHeight:p.height>l.maxHeight?l.maxHeight:p.height;
    p.x=p.x<-100000?-100000:p.x>100000?100000:p.x;
    p.y=p.y<-100000?-100000:p.y>100000?100000:p.y;
    return p;
}
bool Decode(const std::string& input,Preferences& out,Limits limits){
    if(input.size()>8192)return false;
    settings::Reader reader(input);auto p=Defaults(limits);unsigned seen=0;
    if(!reader.Take('{'))return false;
    do{
        std::string key;if(!reader.String(key)||!reader.Take(':'))return false;unsigned bit=0;
        if(key=="version"){bit=1;int version=0;if(!reader.Integer(version)||version!=1)return false;}
        else if(key=="enabled"){bit=2;if(!reader.Boolean(p.enabled))return false;}
        else if(key=="locked"){bit=4;if(!reader.Boolean(p.locked))return false;}
        else if(key=="material"){bit=8;std::string value;if(!reader.String(value)||(value!="liquid-glass"&&value!="frosted-glass"&&value!="solid"))return false;p.solid=value=="solid";if(!p.solid)p.frosted=value=="frosted-glass";}
        else if(key=="placed"){bit=16;if(!reader.Boolean(p.placed))return false;}
        else if(key=="x"){bit=32;if(!reader.Integer(p.x))return false;}
        else if(key=="y"){bit=64;if(!reader.Integer(p.y))return false;}
        else if(key=="width"){bit=128;if(!reader.Integer(p.width))return false;}
        else if(key=="height"){bit=256;if(!reader.Integer(p.height))return false;}
        else if(key=="panelColor"){bit=512;if(!reader.Integer(p.panelColor)||p.panelColor<0||p.panelColor>0xffffff)return false;}  // Optional.
        else return false;
        if(seen&bit)return false;
        seen|=bit;
    }while(reader.Take(','));
    if(!reader.Take('}')||!reader.End()||(seen&511)!=511||!Valid(p,limits))return false;
    out=p;return true;
}
bool Encode(const Preferences& p,std::string& out,Limits limits){
    if(!Valid(p,limits))return false;
    out=std::string("{\"version\":1,\"enabled\":")+(p.enabled?"true":"false")+
        ",\"locked\":"+(p.locked?"true":"false")+",\"material\":\""+(p.solid?"solid":p.frosted?"frosted-glass":"liquid-glass")+
        "\",\"placed\":"+(p.placed?"true":"false")+",\"x\":"+std::to_string(p.x)+",\"y\":"+std::to_string(p.y)+
        ",\"width\":"+std::to_string(p.width)+",\"height\":"+std::to_string(p.height)+
        (p.panelColor!=0x2c2c2e?",\"panelColor\":"+std::to_string(p.panelColor):std::string{})+"}";
    return true;
}
}
