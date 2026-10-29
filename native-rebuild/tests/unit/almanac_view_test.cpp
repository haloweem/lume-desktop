#include "platform/windows/almanac/almanac_view.h"
#include <objbase.h>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace lume::platform::windows;
void Expect(bool value,const char* text){if(!value){std::cerr<<text<<'\n';std::exit(1);}}
int main(){
    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    {
        AlmanacView view;WidgetCanvas canvas;WidgetBitmap bitmap;
        view.SetDate({1900,1,1});
        Expect(view.Available()&&view.Revision()==1,"original data applied");
        Expect(canvas.Begin(320,152,96),"original size");
        view.Draw(canvas,320,152);Expect(canvas.Finish(bitmap),"draw");
        Expect(bitmap.pixels.front()==0,"no separate opaque material");
        const auto panels=view.Panels(320,152);
        Expect(panels.size()==2&&panels[0].radius==8&&panels[0].y==44,"original two shared glass panels");
        Expect(view.ActivityAt(panels[0].x+20,panels[0].y+15)==0,"yi hit region");
        Expect(view.ActivityAt(20,60)==-1,"date area remains draggable");
        Expect(view.Scroll(0,40)&&view.Offset(0)==40,"wheel scroll");
        Expect(view.Scroll(0,1000000,true)&&view.Offset(0)<1000000,"End clamps to text end");
        Expect(!view.Scroll(0,1000000,true),"no unnecessary repaint at end");
        const auto before=view.Offset(0);
        Expect(!view.Scroll(0,std::numeric_limits<float>::quiet_NaN())&&view.Offset(0)==before,"nonfinite scroll rejected");
        view.SetDate({1900,1,1});Expect(view.Revision()==1&&view.Offset(0)==before,"same date does not rebuild data or reset position");
        view.SetDate({2026,9,26});Expect(view.Revision()==2&&view.Offset(0)==0,"cross-day resets scroll and original content");
        view.SetDate({2101,1,1});Expect(!view.Available(),"out of range explicit");
    }
    CoUninitialize();std::cout<<"almanac original layout and scrolling passed\n";
}
