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
    const auto reflection=MakeDockReflection(solid,50);
    Check(reflection.width==8&&reflection.height==64,"reflection bitmap keeps the icon dimensions");
    Check(Channel(reflection.pixels[0],24)==255,"contact uses source alpha before draw opacity");
    Check(Channel(reflection.pixels[16*8],24)>110&&Channel(reflection.pixels[16*8],24)<132,"middle reflection stays colored but softer");
    for(unsigned y=1;y<reflection.height;++y)Check(Channel(reflection.pixels[y*8],24)<=Channel(reflection.pixels[(y-1)*8],24),"fade decreases smoothly downwards");
    Check(reflection.pixels[30*8]!=0&&reflection.pixels[31*8]==0&&reflection.pixels[32*8]==0,
        "50 percent limits reflection height without scaling its width");
    Check(reflection.pixels.back()==0,"bottom vanishes without an opaque cutoff");
    for(const auto pixel:reflection.pixels){
        const auto a=Channel(pixel,24),r=Channel(pixel,16),g=Channel(pixel,8),b=Channel(pixel,0);
        Check(r<=a&&g<=a&&b<=a,"reflection is valid premultiplied BGRA");
        if(a>20)Check(std::abs(static_cast<int>(r)-2*static_cast<int>(g))<=1&&
            std::abs(static_cast<int>(g)-2*static_cast<int>(b))<=1,"source color ratios survive without a white or gray tint");
    }
    WidgetBitmap rows{1,4,{0xff0000ff,0xff00ff00,0xffff0000,0x80402010}};
    const auto flipped=MakeDockReflection(rows,100);
    Check(flipped.height==4&&flipped.pixels[0]==rows.pixels.back()&&Channel(flipped.pixels[1],16)>0,
        "100 percent mirrors the whole icon from the bottom and retains partial alpha");
    const auto half=MakeDockReflection(rows,50);
    Check(half.width==rows.width&&half.height==rows.height&&half.pixels[0]==rows.pixels.back()&&half.pixels[2]==0,
        "50 percent keeps full draw dimensions while reflecting only half the icon height");
    Check(MakeDockReflection(rows,0).pixels.empty(),"zero ratio does not produce a reflection");
    WidgetBitmap stripes{2,2,{0xff202000,0xff404000,0xff606000,0xff808000}};
    using lume::dock::Edge;
    Check(MakeDockReflection(stripes,50,Edge::Top).pixels[2]==stripes.pixels[0],"top reflects the top row from below");
    Check(MakeDockReflection(stripes,50,Edge::Left).pixels[1]==stripes.pixels[0],"left reflects the left column from the right");
    Check(MakeDockReflection(stripes,50,Edge::Right).pixels[0]==stripes.pixels[1],"right reflects the right column from the left");
    Check(MakeDockReflection({},50).pixels.empty()&&MakeDockReflection({2,2,{1}},50).pixels.empty(),"invalid sources are rejected");
    std::cout<<"PASS Dock reflection ratio, vertical fade, color ratios and alpha\n";
}
