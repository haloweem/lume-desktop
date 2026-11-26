#include "platform/windows/widget_ui/widget_canvas.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
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
        Expect(canvas.Begin(340,216,96),"scaled workspace drawing begins");
        canvas.Scale(.5f);canvas.Offset(20,10);
        canvas.PushClip({0,0,30,20});canvas.Rounded({0,0,80,80},0,0x58b3ff);canvas.PopClip();
        Expect(canvas.Finish(bitmap),"scaled and translated workspace finishes");
        Expect(bitmap.pixels[8*340+15]!=0&&bitmap.pixels[8*340+5]==0&&bitmap.pixels[8*340+30]==0,
               "scale applies equally to content, logical offset and clip geometry");
        Expect(canvas.Begin(340,216,96),"ordinary drawing follows a scaled workspace");
        canvas.Rounded({20,10,10,10},0,0xffffff);Expect(canvas.Finish(bitmap),"ordinary drawing finishes");
        Expect(bitmap.pixels[15*340+25]==0xffffffff&&bitmap.pixels[7*340+12]==0&&canvas.SurfaceGeneration()==generation,
               "begin resets workspace transform without reallocating the retained surface");
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
    {
        WidgetCanvas canvas;WidgetBitmap singleLine,wrapped,bitmap;
        const std::wstring original=L"CPU 25% · 提醒";
        Expect(canvas.Begin(240,220,96),"baseline text before wrapped layouts");
        const float originalWidth=canvas.MeasureText(original,14.5f,WidgetTextWeight::Semibold);
        canvas.Text(original,{20,10,190,28},14.5f,0xffffff,1,WidgetTextWeight::Semibold);
        Expect(canvas.Finish(singleLine),"single-line baseline finishes");

        const std::wstring chinese=L"北京未来几小时可能降温，请适当添衣，出门前留意最新天气变化。";
        Expect(canvas.Begin(240,220,96),"wrapped Chinese text begins");
        const float chineseWide=canvas.MeasureWrappedText(chinese,200,14.5f);
        const float chineseNarrow=canvas.MeasureWrappedText(chinese,90,14.5f);
        Expect(chineseWide>0&&chineseNarrow>chineseWide+14,
               "narrow Chinese content has a full measured multiline height");
        canvas.WrappedText(chinese,{20,16,90,68},14.5f);
        Expect(canvas.Finish(wrapped),"wrapped Chinese content finishes");
        unsigned chineseInk=0;
        for(unsigned y=0;y<220;++y)for(unsigned x=0;x<240;++x){
            const auto pixel=wrapped.pixels[y*240+x];
            if(x<20||x>=110||y<16||y>=84)Expect(pixel==0,"wrapped text stays inside its clipped viewport");
            else if(pixel)++chineseInk;
        }
        Expect(chineseInk>0,"clipped Chinese viewport contains visible text");

        Expect(canvas.Begin(240,220,96),"explicit line breaks begin");
        const float oneLine=canvas.MeasureWrappedText(L"第一行",160,14.5f);
        const float twoLines=canvas.MeasureWrappedText(L"第一行\n第二行",160,14.5f);
        Expect(twoLines>oneLine+10,"explicit newline contributes a second complete measured line");
        canvas.WrappedText(L"第一行\n第二行",{20,20,160,80},14.5f);
        Expect(canvas.Finish(bitmap),"explicit line breaks finish");
        unsigned firstLineInk=0,secondLineInk=0;
        for(unsigned y=20;y<100;++y)for(unsigned x=20;x<180;++x){
            if(!bitmap.pixels[y*240+x])continue;
            if(static_cast<float>(y)<20+oneLine)++firstLineInk;else ++secondLineInk;
        }
        Expect(firstLineInk>0&&secondLineInk>0,"both lines paint from the top of the text block");

        Expect(canvas.Begin(240,220,96),"unbroken English text begins");
        const std::wstring unbroken(100,L'W');
        const float englishWide=canvas.MeasureWrappedText(unbroken,200,14.5f);
        const float englishNarrow=canvas.MeasureWrappedText(unbroken,60,14.5f);
        Expect(englishNarrow>englishWide+14&&englishNarrow>oneLine*3,
               "a long word without spaces wraps instead of overflowing horizontally");
        canvas.WrappedText(unbroken,{30,15,60,120},14.5f);
        Expect(canvas.Finish(bitmap),"unbroken English text finishes");
        unsigned lowerInk=0;
        for(unsigned y=0;y<220;++y)for(unsigned x=0;x<240;++x){
            const auto pixel=bitmap.pixels[y*240+x];
            if(x<30||x>=90||y<15||y>=135)Expect(pixel==0,"emergency word wrapping preserves the viewport bounds");
            else if(pixel&&static_cast<float>(y)>=15+oneLine)++lowerInk;
        }
        Expect(lowerInk>0,"unbroken text remains visible on later wrapped lines");

        for(unsigned index=0;index<12;++index){
            Expect(canvas.Begin(240,220,96),"independent wrapped cache entry begins");
            const auto text=std::to_wstring(index)+L"\n"+chinese;
            Expect(canvas.MeasureWrappedText(text,130,14.5f)>oneLine,"each distinct block has a complete measured height");
            canvas.WrappedText(text,{20,16,130,100},14.5f);
            Expect(canvas.Finish(bitmap),"distinct wrapped cache entry finishes");
        }
        Expect(canvas.Begin(240,220,96),"wrapped layout rebuilds after bounded cache turnover");
        Expect(std::abs(canvas.MeasureWrappedText(chinese,90,14.5f)-chineseNarrow)<.001f,
               "cache turnover preserves the complete Chinese text metrics");
        canvas.WrappedText(chinese,{20,16,90,68},14.5f);
        Expect(canvas.Finish(bitmap)&&bitmap.pixels==wrapped.pixels,
               "cache turnover reproduces the same clipped wrapped pixels");

        Expect(canvas.Begin(240,220,96),"single-line text after wrapped layouts");
        Expect(std::abs(canvas.MeasureText(original,14.5f,WidgetTextWeight::Semibold)-originalWidth)<.001f,
               "wrapped layout alignment and wrapping cannot mutate single-line metrics");
        canvas.Text(original,{20,10,190,28},14.5f,0xffffff,1,WidgetTextWeight::Semibold);
        Expect(canvas.Finish(bitmap)&&bitmap.pixels==singleLine.pixels,
               "wrapped text and clipping leave existing single-line pixels unchanged");
    }
    {
        WidgetCanvas canvas;WidgetBitmap frame;
        Expect(canvas.Begin(200,160,96),"folded text starts on a retained canvas");
        canvas.Scale(.5f);canvas.Offset(20,10);canvas.PushClip({20,20,160,60});
        canvas.FoldText(L"12",{20,20,160,120},80,80,.45f,0xffffff,1,WidgetTextWeight::Bold);
        canvas.PopClip();canvas.Rounded({0,0,6,6},0,0x00aaff);
        canvas.Scale(1);canvas.Offset(0,0);canvas.Rounded({185,145,5,5},0,0xffffff);
        Expect(canvas.Finish(frame),"folded text finishes");
        Expect(frame.pixels[6*200+11]==0xff00aaff&&frame.pixels[147*200+187]==0xffffffff,
               "fold restores both the workspace transform and later ordinary drawing");
        unsigned ink=0;
        for(unsigned y=0;y<160;++y)for(unsigned x=0;x<200;++x){
            const bool glyph=x>=20&&x<100&&y>=15&&y<45;
            const bool marker=(x>=10&&x<13&&y>=5&&y<8)||(x>=185&&x<190&&y>=145&&y<150);
            if(glyph&&frame.pixels[y*200+x])++ink;
            else if(!glyph&&!marker)Expect(frame.pixels[y*200+x]==0,"folded glyphs respect the caller's clip");
        }
        Expect(ink>0,"folded text remains visible within its transformed viewport");
    }
    {
        WidgetCanvas canvas;WidgetBitmap reference,translated;
        for(const unsigned dpi:{96u,144u,192u}){
            Expect(canvas.Begin(100,30,dpi),"serif translation reference begins");
            canvas.SerifText(L"嫁娶",{20,4,40,20},11.5f,0x287f65,1,WidgetTextWeight::Bold);
            Expect(canvas.Finish(reference),"serif translation reference finishes");
            Expect(canvas.Begin(100,30,dpi),"serif translated frame begins");
            canvas.Offset(96.f/static_cast<float>(dpi),0);
            canvas.SerifText(L"嫁娶",{20,4,40,20},11.5f,0x287f65,1,WidgetTextWeight::Bold);
            Expect(canvas.Finish(translated),"serif translated frame finishes");
            unsigned ink=0;
            for(unsigned y=0;y<reference.height;++y)for(unsigned x=1;x<reference.width;++x){
                const auto a=reference.pixels[y*reference.width+x-1],b=translated.pixels[y*translated.width+x];
                if(a)++ink;
                for(const unsigned shift:{0u,8u,16u,24u})
                    Expect(std::abs(static_cast<int>((a>>shift)&255)-static_cast<int>((b>>shift)&255))<=3,
                           "integer-pixel translation preserves small serif glyph coverage at each DPI");
            }
            Expect(ink>0,"serif translation fixture contains actual printed glyphs");
        }
        Expect(canvas.Begin(180,40,144),"ordinary font reference begins");
        canvas.Text(L"Ordinary 中文 UI",{8,8,160,24},12,0xffffff);
        Expect(canvas.Finish(reference),"ordinary font reference finishes");
        Expect(canvas.Begin(180,40,144),"mixed font frame begins");
        canvas.SerifText(L"宜",{8,8,20,24},12,0xffffff,0);
        canvas.Text(L"Ordinary 中文 UI",{8,8,160,24},12,0xffffff);
        Expect(canvas.Finish(translated)&&translated.pixels==reference.pixels,
               "serif rendering parameters do not leak into ordinary UI text");
    }
    {
        WidgetCanvas canvas;WidgetBitmap frame;
        for(const unsigned dpi:{96u,120u,144u,192u})for(const float size:{38.f,54.f,82.f}){
            unsigned highest=1000,lowest=0,firstBaseline=1000,lastBaseline=0;
            for(wchar_t digit=L'0';digit<=L'9';++digit){
                Expect(canvas.Begin(120,120,dpi),"lining digit frame begins");
                canvas.SerifText(std::wstring(1,digit),{0,0,120,120},size,0xffffff,1,WidgetTextWeight::Bold);
                Expect(canvas.Finish(frame),"lining digit frame finishes");
                unsigned top=frame.height,bottom=0;
                for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){
                    if((frame.pixels[y*frame.width+x]>>24)<128)continue;
                    top=std::min(top,y);bottom=std::max(bottom,y);
                }
                Expect(top<bottom,"every digit has visible ink");
                highest=std::min(highest,top);lowest=std::max(lowest,top);
                firstBaseline=std::min(firstBaseline,bottom);lastBaseline=std::max(lastBaseline,bottom);
            }
            const unsigned tolerance=(dpi+95)/96;
            Expect(lowest-highest<=tolerance&&lastBaseline-firstBaseline<=tolerance,
                   "all ten serif digits share cap height and baseline across widget sizes and DPI");
        }
    }
    CoUninitialize();
    std::cout<<"widget canvas cache and transparent foreground passed\n";
}
