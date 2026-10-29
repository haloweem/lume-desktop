#include "platform/windows/calendar/calendar_todo_ui.h"
#include <iostream>
#include <vector>
using namespace lume::platform::windows;
int main() {
    int failures=0;
    for(unsigned dpi:{96u,120u,144u,168u,192u,288u}) {
        const int size=todo_ui::Px(30,dpi);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void *bits=nullptr;
        auto dc=CreateCompatibleDC(nullptr);
        auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        auto old=SelectObject(dc,bitmap);
        lume::calendar::Preferences prefs;
        todo_ui::Surface surface(dpi,prefs);
        DRAWITEMSTRUCT item{};item.hDC=dc;item.rcItem={0,0,size,size};
        surface.Button(item,false,RGB(255,105,97),todo_ui::Surface::Appearance::Swatch);
        GdiFlush();
        int blended=0;
        const auto *pixels=static_cast<const unsigned*>(bits);
        // A hard GDI circle has only background, fill and border colors.
        // Smooth coverage must produce additional colors at the curved edge.
        for(int i=0;i<size*size;++i) {
            const auto rgb=pixels[i]&0xffffffu;
            if(rgb!=0x1f2126u&&rgb!=0xff6961u&&rgb!=0x848588u)++blended;
        }
        if(blended<12){std::cerr<<"swatch lacks edge coverage at "<<dpi<<" DPI: "<<blended<<'\n';++failures;}
        surface.Background(dc,{0,0,size,size});GdiFlush();
        int covered[4]{};
        for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
            const auto alpha=pixels[y*size+x]>>24;
            if(alpha>0&&alpha<255)++covered[(y>=size/2?2:0)+(x>=size/2?1:0)];
            if((pixels[y*size+x]&255)>alpha||((pixels[y*size+x]>>8)&255)>alpha||((pixels[y*size+x]>>16)&255)>alpha) {
                std::cerr<<"non-premultiplied frame pixel at "<<dpi<<" DPI\n";++failures;break;
            }
        }
        for(int count:covered)if(count<3){std::cerr<<"frame lacks fractional alpha at "<<dpi<<" DPI\n";++failures;}
        if((pixels[0]>>24)!=0||(pixels[(size/2)*size+size/2]>>24)!=255) {
            std::cerr<<"frame must have transparent outside and opaque interior\n";++failures;
        }
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    }
    return failures?1:0;
}
