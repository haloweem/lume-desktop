#include "platform/windows/material/annular_region.h"
#include <cstdlib>
#include <cmath>
#include <iostream>
using namespace lume::platform::windows;
namespace {
int creates=0,deletes=0,publishes=0,failCreate=0;bool combine=true,publish=true;
HRGN WINAPI Create(int,int,int,int){++creates;return creates==failCreate?nullptr:reinterpret_cast<HRGN>(static_cast<INT_PTR>(creates));}
BOOL WINAPI Delete(HGDIOBJ){++deletes;return TRUE;}
int WINAPI Combine(HRGN,HRGN,HRGN,int){return combine?COMPLEXREGION:ERROR;}
int WINAPI Publish(HWND,HRGN,BOOL){++publishes;return publish?1:0;}
void Expect(bool value,const char* text){if(!value){std::cerr<<text<<'\n';std::exit(1);}}
void Reset(){creates=deletes=publishes=failCreate=0;combine=publish=true;}
}
int main(){
    const AnnularRegionApi api{Create,Combine,Delete,Publish};const RECT outer{0,0,320,320},inner{84,84,236,236};
    Reset();failCreate=1;Expect(!PublishRegion(nullptr,CreateAnnularRegion(outer,inner,api),api)&&publishes==0&&deletes==0,"outer allocation failure cannot remove an existing input region");
    Reset();failCreate=2;Expect(!PublishRegion(nullptr,CreateAnnularRegion(outer,inner,api),api)&&publishes==0&&deletes==1,"inner allocation failure cannot publish a solid circle");
    Reset();combine=false;Expect(!PublishRegion(nullptr,CreateAnnularRegion(outer,inner,api),api)&&publishes==0&&deletes==2,"combine failure retires both regions without publication");
    Reset();publish=false;Expect(!PublishRegion(nullptr,CreateAnnularRegion(outer,inner,api),api)&&publishes==1&&deletes==2,"failed publication releases the unpublished region");
    Reset();Expect(PublishRegion(nullptr,CreateAnnularRegion(outer,inner,api),api)&&publishes==1&&deletes==1,"successful publication transfers only the valid annulus");
    for(const unsigned dpi:{96u,120u,144u,192u,240u,288u,384u,768u}){
        const int size=MulDiv(320,static_cast<int>(dpi),96);
        const int edge=MulDiv(8,static_cast<int>(dpi),96),hole=MulDiv(84,static_cast<int>(dpi),96);
        const int end=MulDiv(236,static_cast<int>(dpi),96);
        const auto region=CreateAnnularRegion({edge,edge,size-edge,size-edge},{hole,hole,end,end});
        Expect(region!=nullptr,"real antialias input envelope allocates");
        RECT bounds{};GetRgnBox(region,&bounds);
        std::cout<<"input envelope bounds "<<bounds.left<<','<<bounds.top<<','<<bounds.right<<','<<bounds.bottom<<'\n';
        const float center=size/2.f,inside=76.f*dpi/96,outside=152.f*dpi/96;
        unsigned fractional=0,clipped=0;
        for(int y=0;y<size;++y)for(int x=0;x<size;++x){
            const float distance=std::hypot(x+.5f-center,y+.5f-center);
            if(std::abs(distance-inside)>.708f&&std::abs(distance-outside)>.708f)continue;
            unsigned coverage=0;
            for(int sy=0;sy<8;++sy)for(int sx=0;sx<8;++sx){
                const float radial=std::hypot(x+(sx+.5f)/8-center,y+(sy+.5f)/8-center);
                if(radial>=inside&&radial<=outside)++coverage;
            }
            if(coverage&&coverage<64){++fractional;if(!PtInRegion(region,x,y)){
                if(clipped<5)std::cout<<"clipped AA pixel "<<dpi<<" DPI: "<<x<<','<<y<<" radius="<<distance<<" coverage="<<coverage<<'\n';
                ++clipped;
            }}
        }
        std::cout<<"annular envelope "<<dpi<<" DPI: fractional="<<fractional<<" clipped="<<clipped<<'\n';
        Expect(fractional>100&&clipped==0,"input envelope retains every fractional inner and outer edge pixel");
        Expect(!PtInRegion(region,size/2,size/2)&&!PtInRegion(region,0,0),"antialias gutter preserves center and corner click-through");
        DeleteObject(region);
    }
    std::cout<<"PASS annular input region failure ownership and fail-closed publication\n";
}
