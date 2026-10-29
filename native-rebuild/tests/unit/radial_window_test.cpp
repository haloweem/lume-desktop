#include "platform/windows/radial/radial_window.h"
#include "platform/windows/radial/radial_shortcut.h"
#include <objbase.h>
#include <cstdlib>
#include <cmath>
#include <iostream>
using namespace lume::platform::windows;
void Expect(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
void SectorCoverage(unsigned dpi){
    WidgetCanvas canvas;WidgetBitmap baseline,selected;
    Expect(canvas.Begin(320,320,dpi),"sector baseline begins");
    RadialWindow::Draw(canvas,-1);Expect(canvas.Finish(baseline),"sector baseline finishes");
    for(int slot=0;slot<10;++slot){
        const auto point=lume::radial::SlotPosition(slot);
        unsigned visible=0,bright=0;
        for(unsigned y=0;y<baseline.height;++y)for(unsigned x=0;x<baseline.width;++x){
            const float px=(x+.5f)*96/dpi,py=(y+.5f)*96/dpi;
            if(std::abs(px-point.x)>12||py<point.y-18||py>point.y+6)continue;
            const auto alpha=baseline.pixels[static_cast<std::size_t>(y)*baseline.width+x]>>24;
            if(alpha>10)++visible;
            if(alpha>128)++bright;
        }
        Expect(visible>5,"every slot keeps a visible icon or placeholder at each DPI");
        Expect(slot==6?bright>20:bright==0,"only lower-left slot 6 renders the bright screenshot icon");
    }
    for(int slot=0;slot<10;++slot){
        Expect(canvas.Begin(320,320,dpi),"sector drawing begins");
        RadialWindow::Draw(canvas,slot);Expect(canvas.Finish(selected),"sector drawing finishes");
        unsigned samples=0,blue=0,leaks=0;
        for(unsigned y=0;y<selected.height;++y)for(unsigned x=0;x<selected.width;++x){
            const float px=(x+.5f)*96/dpi,py=(y+.5f)*96/dpi;
            const auto index=static_cast<std::size_t>(y)*selected.width+x;
            const float radius=std::hypot(px-160,py-160);
            if(radius<79||radius>149||(baseline.pixels[index]>>24)>4)continue;
            const int hit=lume::radial::HitSlot(px,py);
            const float angle=std::atan2(px-160,160-py)*180/3.14159265f;
            const float offset=std::remainder(angle-slot*36.f,360.f);
            const auto pixel=selected.pixels[index];
            const int b=pixel&255,r=(pixel>>16)&255;
            if(hit==slot&&std::abs(offset)<16){
                ++samples;if((pixel>>24)>70&&b-r>25)++blue;
            }else if(hit!=slot&&std::abs(offset)>20&&pixel!=baseline.pixels[index])++leaks;
        }
        Expect(samples>100&&blue*1000>=samples*995,
               "all ten hover regions cover the full blue annular sector, not a small circle");
        Expect(leaks==0,"hover sector does not spill into neighboring slots");
        Expect(selected.pixels[static_cast<std::size_t>(selected.height/2)*selected.width+selected.width/2]==0,
               "blue sector never paints the transparent center");
    }
}
int main(){
    Expect(lume::radial::ScreenshotSlot==6,"core screenshot slot stays at lower-left slot 6");
    const auto screenshot=lume::radial::SlotPosition(6);
    Expect(screenshot.x<160&&screenshot.y>160,"slot 6 is in the lower-left quadrant");
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    for(unsigned dpi:{96u,120u,144u,192u}){
        SectorCoverage(dpi);
        WidgetCanvas canvas;WidgetBitmap bitmap;
        Expect(canvas.Begin(320,320,dpi),"canvas begin");
        RadialWindow::Draw(canvas,0);Expect(canvas.Finish(bitmap),"draw finishes");
        const auto center=bitmap.pixels[static_cast<std::size_t>(bitmap.height/2)*bitmap.width+bitmap.width/2];
        Expect((center>>24)==0,"foreground center stays transparent");
        unsigned visible=0;for(auto pixel:bitmap.pixels)if((pixel>>24)>20)++visible;
        Expect(visible>200,"icons and labels render at each DPI");
    }
    unsigned action=0;
    RadialWindow window(GetModuleHandleW(nullptr),[&](UINT value){action=value;},[]{});
    Expect(window.Create()&&!window.Visible(),"created window is initially hidden");
    POINT point{0,0};Expect(window.Show(point,false),"popup opens and clamps at monitor edge");
    RECT r{};GetWindowRect(window.Handle(),&r);MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&monitor);
    Expect(r.left>=monitor.rcWork.left&&r.top>=monitor.rcWork.top,"popup stays inside work area");
    HRGN region=CreateRectRgn(0,0,0,0);Expect(GetWindowRgn(window.Handle(),region)!=ERROR,"popup input has explicit ring region");
    Expect(!PtInRegion(region,(r.right-r.left)/2,(r.bottom-r.top)/2),"center is excluded from input region");
    DeleteObject(region);
    const auto slotPoint=[&](int slot){
        const auto position=lume::radial::SlotPosition(slot);
        const auto dpi=GetDpiForWindow(window.Handle());
        return MAKELPARAM(static_cast<int>(std::lround(position.x*dpi/96)),
                         static_cast<int>(std::lround(position.y*dpi/96)));
    };
    for(int slot=0;slot<10;++slot){
        action=0;
        SendMessageW(window.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,slotPoint(slot));
        SendMessageW(window.Handle(),WM_LBUTTONUP,0,slotPoint(slot));
        Expect(action==(slot==6?RadialWindow::Screenshot:0),
               "only slot 6 activates screenshot; top, translation and placeholders stay inactive");
    }
    action=0;
    SendMessageW(window.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,slotPoint(6));
    SendMessageW(window.Handle(),WM_LBUTTONUP,0,slotPoint(0));
    Expect(action==0,"pressing screenshot then releasing top does not activate");
    SendMessageW(window.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,slotPoint(0));
    SendMessageW(window.Handle(),WM_LBUTTONUP,0,slotPoint(6));
    Expect(action==0,"pressing top then releasing screenshot does not activate");
    SendMessageW(window.Handle(),WM_LBUTTONUP,0,slotPoint(6));
    Expect(action==0,"screenshot release without a matching press does not activate");
    SendMessageW(window.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,slotPoint(6));
    SendMessageW(window.Handle(),WM_MOUSELEAVE,0,0);
    SendMessageW(window.Handle(),WM_LBUTTONUP,0,slotPoint(6));
    Expect(action==0,"leaving the ring cancels screenshot activation");
    SendMessageW(window.Handle(),WM_KEYDOWN,VK_ESCAPE,0);
    Expect(action==RadialWindow::Dismiss,"escape requests dismissal");
    action=0;SendMessageW(window.Handle(),RadialShortcut::ReleaseMessage,0,0);
    Expect(action==RadialWindow::Dismiss,"shortcut release routes to immediate dismissal");
    window.Hide();Expect(!window.Visible()&&window.MaterialState()==WidgetMaterialState::Detached,"hide releases glass");
    window.Stop();Expect(!window.Handle(),"stop destroys window");
    CoUninitialize();std::cout<<"PASS radial foreground DPI, input ring and lifecycle\n";
}
