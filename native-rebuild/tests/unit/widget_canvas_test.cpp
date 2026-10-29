#include "platform/windows/widget_ui/widget_canvas.h"
#include <windows.h>
#include <objbase.h>
#include <cstdlib>
#include <iostream>
using namespace lume::platform::windows;
void Expect(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM startup");
    {
        WidgetCanvas canvas;WidgetBitmap bitmap;
        Expect(canvas.SurfaceGeneration()==0,"construction creates no surface");
        Expect(canvas.Begin(340,216,96),"transparent foreground begins at original performance size");
        canvas.Rounded({20,60,100,4},2,0x58b3ff);
        Expect(canvas.Finish(bitmap)&&bitmap.width==340&&bitmap.height==216,"DIP dimensions at 96 dpi");
        Expect(bitmap.pixels[0]==0,"canvas does not draw an opaque material");
        Expect((bitmap.pixels[62*340+50]>>24)==255,"foreground meter is visible");
        const auto generation=canvas.SurfaceGeneration();
        for(int i=0;i<10;++i){
            Expect(canvas.Begin(340,216,96),"repeat drawing");
            canvas.Text(L"CPU 25%",{20,20,200,24},21);
            Expect(canvas.Finish(bitmap),"finish cached drawing");
        }
        Expect(canvas.SurfaceGeneration()==generation,"one-second samples reuse the surface");
        Expect(bitmap.pixels[62*340+50]==0,"new sample clears previous foreground");
        Expect(canvas.Begin(160,80,96),"scroll drawing begins");
        const float measured=canvas.MeasureText(L"祭祀、出行、嫁娶、冠笄、安床",12);
        Expect(measured>60&&measured<400,"measure full ordered activity text");
        canvas.ScrollText(L"祭祀、出行、嫁娶、冠笄、安床",{60,20,60,24},12,40);
        Expect(canvas.Finish(bitmap),"clipped text finishes");
        unsigned ink=0;
        for(unsigned y=0;y<80;++y)for(unsigned x=0;x<160;++x){
            if(x<60||x>=120||y<20||y>=44)Expect(bitmap.pixels[y*160+x]==0,"scrolled text cannot paint outside viewport");
            else if(bitmap.pixels[y*160+x])++ink;
        }
        Expect(ink>0,"scroll viewport contains visible original text");
        Expect(canvas.Begin(340,216,144)&&canvas.Finish(bitmap),"DPI resize");
        Expect(bitmap.width==510&&bitmap.height==324,"DPI uses physical pixels");
        Expect(canvas.SurfaceGeneration()==generation+2,"scroll canvas and DPI each replace the surface once");
        Expect(!canvas.Begin(-1,216,96)&&!canvas.Begin(340,216,0),"invalid dimensions rejected");
        Expect(!canvas.Begin(1000000,1000000,96),"unbounded allocation rejected");
        const auto icon=std::make_shared<const WidgetBitmap>(WidgetBitmap{2,2,
            {0xffff0000,0xffff0000,0xffff0000,0xffff0000}});
        Expect(canvas.Begin(160,80,96),"prepared image begins");
        Expect(canvas.ImageBitmap(icon,{20,10,48,48})&&canvas.ImageBitmap(icon,{80,10,48,48}),
            "prepared image reused at two positions");
        Expect(canvas.CachedBitmaps()==1,"identity cache does not duplicate immutable image");
        Expect(canvas.Finish(bitmap)&&bitmap.pixels[20*160+30]==0xffff0000,
            "prepared BGRA image painted with correct color and alpha");
        Expect(canvas.Begin(160,80,96)&&canvas.Finish(bitmap)&&canvas.CachedBitmaps()==0,
            "unused prepared images released after a frame");
        const auto frame=std::make_shared<const WidgetBitmap>(WidgetBitmap{400,300,
            std::vector<std::uint32_t>(400*300,0xffff0000)});
        Expect(canvas.Begin(160,80,96),"retained UI frame begins");
        Expect(!canvas.ImageBitmap(frame,{0,0,160,80}),"icon size bound remains enforced");
        Expect(canvas.ImageFrame(frame,{0,0,160,80},.5f),"full UI frame draws above the icon size bound");
        Expect(canvas.Finish(bitmap)&&((bitmap.pixels[0]>>24)&255)>=126&&((bitmap.pixels[0]>>24)&255)<=129,
               "UI frame opacity is applied to premultiplied pixels");
        Expect(canvas.Begin(160,80,96)&&canvas.ImageFrame(frame,{0,0,160,80})&&canvas.Finish(bitmap)&&
               bitmap.pixels[0]==0xffff0000,"retained UI frame stretches to destination without aspect-fit margins");
        Expect(canvas.Begin(160,80,96),"nested viewport drawing begins");
        const auto clipGeneration=canvas.SurfaceGeneration();
        Expect(canvas.PushClip({20,10,60,40}),"outer viewport accepted");
        canvas.Rounded({0,0,160,80},0,0xff0000);
        Expect(canvas.PushClip({30,20,10,10}),"nested viewport accepted");
        canvas.Rounded({0,0,160,80},0,0x00ff00);
        canvas.PopClip();
        canvas.Rounded({70,40,40,20},0,0x0000ff);
        Expect(canvas.Finish(bitmap),"finish balances remaining clip");
        Expect(bitmap.pixels[15*160+25]==0xffff0000&&bitmap.pixels[25*160+35]==0xff00ff00&&
               bitmap.pixels[45*160+75]==0xff0000ff&&bitmap.pixels[45*160+95]==0,
               "nested clip intersects pixels and pop restores outer viewport");
        Expect(canvas.Begin(160,80,96),"next frame after clip");
        canvas.Rounded({0,0,160,80},0,0xffffff);
        Expect(canvas.Finish(bitmap)&&bitmap.pixels[0]==0xffffffff&&canvas.SurfaceGeneration()==clipGeneration,
               "clip does not leak into next frame or allocate a second surface");
    }
    CoUninitialize();
    std::cout<<"widget canvas cache and transparent foreground passed\n";
}
