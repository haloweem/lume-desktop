#include "platform/windows/dock/dock_reflection.h"
#include <cstdlib>
#include <iostream>
using namespace lume::platform::windows;
namespace {
void Check(bool ok,const char* message){if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
unsigned Channel(std::uint32_t pixel,unsigned shift){return pixel>>shift&255;}
}
int main(){
    WidgetBitmap solid{8,64,std::vector<std::uint32_t>(8*64,0xffc06030)};
    const auto reflection=MakeDockReflection(solid);
    Check(reflection.width==8&&reflection.height==64,"reflection retains source geometry");
    Check(Channel(reflection.pixels[0],24)>=205&&Channel(reflection.pixels[0],24)<=211,"contact reflection retains about 82 percent opacity");
    Check(Channel(reflection.pixels[16*8],24)>90&&Channel(reflection.pixels[16*8],24)<115,"middle reflection stays colored but softer");
    for(unsigned y=1;y<64;++y)Check(Channel(reflection.pixels[y*8],24)<=Channel(reflection.pixels[(y-1)*8],24),"fade decreases smoothly downwards");
    for(unsigned y=32;y<64;++y)Check(reflection.pixels[y*8]==0,"bottom vanishes without an opaque cutoff");
    for(const auto pixel:reflection.pixels){
        const auto a=Channel(pixel,24),r=Channel(pixel,16),g=Channel(pixel,8),b=Channel(pixel,0);
        Check(r<=a&&g<=a&&b<=a,"reflection is valid premultiplied BGRA");
        if(a>20)Check(std::abs(static_cast<int>(r)-2*static_cast<int>(g))<=1&&
            std::abs(static_cast<int>(g)-2*static_cast<int>(b))<=1,"source color ratios survive without a white or gray tint");
    }
    WidgetBitmap rows{1,4,{0xff0000ff,0xff00ff00,0xffff0000,0x80402010}};
    const auto flipped=MakeDockReflection(rows);
    Check(Channel(flipped.pixels[0],24)==105&&Channel(flipped.pixels[0],16)==52,"mirror starts from the source bottom and retains partial alpha");
    Check(MakeDockReflection({}).pixels.empty()&&MakeDockReflection({2,2,{1}}).pixels.empty(),"invalid sources are rejected");
    std::cout<<"PASS Dock reflection contact strength, vertical fade, color ratios and alpha\n";
}
