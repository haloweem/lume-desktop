#include "platform/windows/performance/performance_view.h"
#include <windows.h>
#include <objbase.h>
#include <limits>
#include <cstdlib>
#include <iostream>
using namespace lume::platform::windows;
void Expect(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    Expect(PerformancePercent({})==L"--","unavailable percentage");
    Expect(PerformancePercent(25.5)==L"26%"&&PerformancePercent(110)==L"100%","original rounded clamped percent");
    Expect(PerformancePercent(std::numeric_limits<double>::quiet_NaN())==L"--","invalid percentage");
    Expect(PerformanceSpeed(1024)==L"1.0 KB/s"&&PerformanceSpeed(10240)==L"10 KB/s","source speed precision");
    Expect(PerformanceSpeed(0)==L"0 B/s"&&PerformanceSpeed({})==L"--","zero and unavailable are distinct");
    lume::performance::Snapshot s;s.usedMemory=12ull<<30;s.totalMemory=16ull<<30;s.memory=75;
    Expect(PerformanceMemory(s)==L"12.0 / 16.0 GB","source memory precision");
    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    {
        WidgetCanvas canvas;WidgetBitmap bitmap;
        Expect(canvas.Begin(340,216,96),"canvas");s.cpu=50;s.gpu=25;s.download=1024;s.upload=0;
        DrawPerformance(canvas,340,216,s,false);
        Expect(canvas.Finish(bitmap),"performance foreground");
        unsigned visible=0;for(auto pixel:bitmap.pixels)if(pixel>>24)++visible;
        Expect(visible>2000&&visible<18000,"content present without opaque glass replacement");
        Expect(bitmap.pixels[0]==0,"outer corner remains transparent");
    }
    CoUninitialize();std::cout<<"performance source formatting and render passed\n";
}
