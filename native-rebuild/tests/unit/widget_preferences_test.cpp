#include "core/widget/widget_preferences.h"
#include <cstdlib>
#include <iostream>
using namespace lume::widget;
void Expect(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    const Limits limits{340,216,320,204,720,720};
    auto prefs=Defaults(limits);std::string text;
    Expect(prefs.width==340&&prefs.height==216&&!prefs.enabled,"original defaults");
    Expect(Encode(prefs,text,limits),"encode performance dimensions");
    Preferences decoded;
    Expect(Decode(text,decoded,limits)&&decoded.height==216,"roundtrip");
    prefs.enabled=true;prefs.locked=true;prefs.frosted=true;prefs.placed=true;prefs.x=-1200;prefs.y=-100;
    Expect(Encode(prefs,text,limits)&&Decode(text,decoded,limits)&&decoded.x==-1200&&decoded.locked&&decoded.frosted,"negative monitor coordinates survive");
    const auto saved=decoded;
    {
        auto custom=Defaults(limits);custom.width=393;custom.height=307;custom.x=-937;custom.y=219;
        custom.placed=true;custom.enabled=false;custom.locked=false;custom.frosted=false;
        custom.solid=true;custom.panelColor=0x123456;std::string value;Preferences restored;
        Expect(Encode(custom,value,limits)&&Decode(value,restored,limits)&&restored.width==393&&
            restored.height==307&&restored.x==-937&&restored.y==219&&restored.placed&&
            !restored.enabled&&!restored.locked&&restored.solid&&restored.panelColor==0x123456,
            "all shared widget geometry color and explicit false choices survive roundtrip");
    }
    Expect(!Decode(text+"x",decoded,limits)&&decoded.x==saved.x,"invalid input never changes destination");
    auto duplicate=text;duplicate.insert(1,"\"enabled\":true,");
    Expect(!Decode(duplicate,decoded,limits),"duplicate keys rejected");
    prefs.width=100;
    Expect(!Encode(prefs,text,limits),"invalid geometry not persisted");
    Expect(!Decode(std::string(9000,' '),decoded,limits),"bounded input");
    {
        // A live window measured on a display with another DPI can come out a
        // pixel outside the limits; clamping keeps the save from rolling the
        // widget back to off.
        auto edge=Defaults(limits);edge.width=limits.maxWidth+1;edge.height=limits.minHeight-1;
        edge.x=-100001;edge.y=100001;
        const auto clamped=Clamped(edge,limits);
        Expect(clamped.width==limits.maxWidth&&clamped.height==limits.minHeight&&
            clamped.x==-100000&&clamped.y==100000,"a DPI-rounded window is clamped into the limits");
        auto inside=Defaults(limits);inside.width=400;inside.height=300;inside.x=50;inside.y=60;
        const auto kept=Clamped(inside,limits);
        Expect(kept.width==400&&kept.height==300&&kept.x==50&&kept.y==60,
            "clamping preserves geometry already inside the limits");
    }
    std::cout<<"widget preferences passed\n";
}
