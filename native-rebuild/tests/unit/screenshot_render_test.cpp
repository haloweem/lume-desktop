#include "platform/windows/screenshot/screenshot_render.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace lume::platform::windows::capture;
using namespace lume::screenshot;
void Check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
void ArrowOptionTests(){
    const Image source{280,180,std::vector<std::uint32_t>(280*180,0xffeeeeee)};const Rect clip{0,0,280,180};
    Annotation a;a.tool=Tool::Arrow;a.width=6;a.points={{40,90},{240,90}};
    std::vector<Image> variants;
    for(int variant=0;variant<4;++variant){a.variant=variant;variants.push_back(Composite(source,{a},clip));
        for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x){const auto i=static_cast<std::size_t>(y)*source.width+x;
            if(variants.back().pixels[i]!=source.pixels[i])Check(Hit(a,{x,y},2),"eraser hits arrowhead and tapered body ink");
        }
    }
    const auto at=[](int x,int y){return static_cast<std::size_t>(y)*280+x;};
    Check(variants[2].pixels[at(58,95)]!=source.pixels[at(58,95)],"double arrow includes filled head at starting point");
    Check(variants[0].pixels[at(58,95)]==source.pixels[at(58,95)],"single arrow does not acquire a starting head");
    for(int i=0;i<4;++i)for(int j=0;j<i;++j)Check(variants[i].pixels!=variants[j].pixels,"all four arrow variants are visually distinct");
    Check(variants[3].pixels[at(42,92)]==source.pixels[at(42,92)],"tapered arrow has a narrow tail");
    Check(variants[3].pixels[at(210,94)]!=source.pixels[at(210,94)],"tapered arrow widens towards its filled head");
    for(int variant=0;variant<4;++variant)for(Point end:std::vector<Point>{{40,90},{41,90},{38,87},{20,20},{260,160}}){
        a.variant=variant;a.points.back()=end;const auto rendered=Composite(source,{a},clip);const auto bounds=AnnotationBounds(a,clip);
        for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x){const auto i=static_cast<std::size_t>(y)*source.width+x;
            if(rendered.pixels[i]!=source.pixels[i])Check(bounds.Contains({x,y}),"short reversed and degenerate arrow bounds cover all ink");
        }
    }
}
void TextOptionTests(){
    const Image source{640,400,std::vector<std::uint32_t>(640*400,0xffeeeeee)};const Rect clip{0,0,640,400};
    Annotation a;a.tool=Tool::Text;a.points={{25,20}};a.text=L"Wide fj MW\nSecond line";a.fontSize=42;a.fontFamily=L"Arial";
    const auto regular=Composite(source,{a},clip);
    for(const auto* family:{L"Arial",L"Courier New",L"Microsoft YaHei UI"})for(int style=0;style<8;++style){
        a.fontFamily=family;a.bold=(style&1)!=0;a.italic=(style&2)!=0;a.outline=(style&4)!=0;
        const auto rendered=Composite(source,{a},clip);const auto bounds=AnnotationBounds(a,clip);
        auto preview=source;RenderAnnotation(preview,a,clip);Check(preview.pixels==rendered.pixels,"styled text preview equals composite");
        std::size_t changed=0;
        for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x){const auto i=static_cast<std::size_t>(y)*source.width+x;
            if(rendered.pixels[i]!=source.pixels[i]){++changed;Check(bounds.Contains({x,y}),"styled text bounds cover all ink");Check(Hit(a,{x,y},0),"styled text hit envelope covers all ink");}
        }
        Check(changed>40,"every text family and style renders");
        if(style||std::wstring(family)!=L"Arial")Check(rendered.pixels!=regular.pixels,"font family and style options affect pixels");
    }
    a.bold=false;a.italic=false;a.outline=false;a.fontSize=0;a.fontFamily=L"Microsoft YaHei UI";
    const auto legacy=Composite(source,{a},clip);a.fontSize=a.width+18;
    Check(Composite(source,{a},clip).pixels==legacy.pixels,"zero font size preserves legacy width plus 18");
    a.fontSize=60;Check(Composite(source,{a},clip).pixels!=legacy.pixels,"explicit font size changes output");
    a.fontFamily=L"Missing Lume Font 123456";Check(Composite(source,{a},clip).pixels!=source.pixels,"missing font has usable fallback");
    a.fontFamily=L"Arial";a.text=L"MMMM";a.fontSize=90;a.bold=true;
    const auto filled=Composite(source,{a},clip);a.outline=true;const auto outlined=Composite(source,{a},clip);
    int hollow=0;for(std::size_t i=0;i<source.pixels.size();++i)if(filled.pixels[i]!=source.pixels[i]&&outlined.pixels[i]==source.pixels[i])++hollow;
    Check(hollow>100,"outlined text strokes glyph boundaries instead of filling them");
    for(int size:{1,8,200})for(bool outline:{false,true}){
        a.fontSize=size;a.outline=outline;a.italic=true;a.text=L"fj\n\u622a\u56fe";
        const auto rendered=Composite(source,{a},clip);const auto bounds=AnnotationBounds(a,clip);
        for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x){const auto i=static_cast<std::size_t>(y)*source.width+x;
            if(rendered.pixels[i]!=source.pixels[i])Check(bounds.Contains({x,y})&&Hit(a,{x,y},0),"tiny and maximum styled fonts retain ink coverage");
        }
    }
}
void NeutralTextTests(){
    for(const auto background:{0xffeeeeeeu,0xff222222u})for(int size:{16,24,32,48})for(bool outlined:{false,true}){
        const Image source{360,130,std::vector<std::uint32_t>(360*130,background)};
        Annotation a;a.tool=Tool::Text;a.points={{15,10}};a.text=L"Lume 123 \u622a\u56fe\n\u4e2d\u6587 f j";
        a.fontFamily=L"Microsoft YaHei UI";a.fontSize=size;a.color=background==0xffeeeeeeu?0x222222:0xeeeeee;a.outline=outlined;
        const auto rendered=Composite(source,{a},{0,0,360,130});int visible=0,smooth=0;
        for(auto pixel:rendered.pixels){
            const auto r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
            Check(r==g&&g==b,"neutral screenshot text has no RGB subpixel color fringe");
            if(pixel!=background){++visible;if(r!=0x22&&r!=0xee)++smooth;}
        }
        Check(visible>40&&smooth>10,"filled and outlined text retains visible grayscale antialiasing at scaled sizes");
    }
}
void IncrementalTests(const Image& source){
    const Rect clip{20,20,250,180};
    Annotation committed;committed.tool=Tool::Marker;committed.points={{25,35},{220,160}};committed.width=4;
    const auto base=Composite(source,{committed},clip);
    for(auto tool:{Tool::Shape,Tool::Arrow,Tool::Pen,Tool::Marker,Tool::Mosaic,Tool::Text})for(int variant=0;variant<4;++variant){
        Annotation a;a.tool=tool;a.variant=variant;a.width=9;a.points={{24,25},{160,120},{242,170}};
        a.text=L"Italic fj\t\u622a\u56fe\nsecond line\nthird";
        a.fontSize=32;a.fontFamily=L"Arial";a.bold=(variant&1)!=0;a.italic=(variant&2)!=0;a.outline=variant==3;a.brushSize=18;
        auto preview=base;RenderAnnotation(preview,a,clip);
        const auto expected=Composite(source,{committed,a},clip);
        Check(preview.pixels==expected.pixels,"single pending annotation matches full composite over committed image");
        const auto bounds=AnnotationBounds(a,clip);
        for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x){
            const auto i=static_cast<std::size_t>(y)*source.width+x;
            if(preview.pixels[i]!=base.pixels[i])Check(bounds.Contains({x,y}),"annotation bounds contain every changed antialiased pixel");
        }
        // A caller restores the previous bounds before painting the next complete pending stroke.
        for(int y=bounds.top;y<bounds.bottom;++y)for(int x=bounds.left;x<bounds.right;++x){
            const auto i=static_cast<std::size_t>(y)*source.width+x;preview.pixels[i]=base.pixels[i];
        }
        a.points.back()={80,70};RenderAnnotation(preview,a,clip);
        Check(preview.pixels==Composite(source,{committed,a},clip).pixels,"restoring old bounds removes all pending pixels");
    }
    Annotation empty;Check(AnnotationBounds(empty,clip)==Rect{},"empty annotation has empty bounds");
    Annotation outside;outside.points={{-100,-100},{-80,-80}};
    Check(AnnotationBounds(outside,clip)==Rect{},"off-selection annotation has empty bounds");
    auto unchanged=source;RenderAnnotation(unchanged,outside,clip);
    Check(unchanged.pixels==source.pixels,"off-selection render is harmless");
    RenderAnnotation(unchanged,committed,{100,100,20,20});
    Check(unchanged.pixels==source.pixels,"inverted clip is empty");
    Annotation extreme;extreme.width=std::numeric_limits<int>::max();extreme.points={{0,0}};
    Check(AnnotationBounds(extreme,clip)==clip,"bounds expansion does not overflow integer coordinates");
}
void BrushTests(const Image& source){
    const Rect clip{20,20,250,180};
    const auto at=[](int x,int y){return static_cast<std::size_t>(y)*300+x;};
    Annotation a;a.tool=Tool::Mosaic;a.width=3;a.points={{50,50},{160,50},{160,140}};
    for(int variant:{2,3}){
        a.variant=variant;const auto rendered=Composite(source,{a},clip);
        Check(rendered.pixels[at(100,50)]!=source.pixels[at(100,50)],"brush covers continuous intermediate segment");
        Check(rendered.pixels[at(38,50)]!=source.pixels[at(38,50)],"brush covers rounded starting cap");
        Check(rendered.pixels[at(160,152)]!=source.pixels[at(160,152)],"brush covers rounded ending cap");
        Check(rendered.pixels[at(80,110)]==source.pixels[at(80,110)],"brush leaves bounding-box gap unchanged");
        Annotation retraced=a;retraced.points={{50,50},{160,50},{160,140},{160,50},{50,50},{160,50},{160,140}};
        Check(Composite(source,{retraced},clip).pixels==rendered.pixels,"retracing one stroke never filters twice");
        Annotation split=a;split.points={{50,50},{90,50},{160,50},{160,95},{160,140}};
        Check(Composite(source,{split},clip).pixels==rendered.pixels,"brush is independent of collinear sample density");
        Annotation dot=a;dot.points={{100,100}};const auto dotted=Composite(source,{dot},clip);
        Check(dotted.pixels[at(100,100)]!=source.pixels[at(100,100)],"single-point brush paints a disk");
        Check(dotted.pixels[at(113,113)]==source.pixels[at(113,113)],"single-point brush is not a square");
        Annotation extended=a;extended.points.insert(extended.points.begin(),{40,60});
        const auto extension=Composite(source,{extended},clip);
        Check(extension.pixels[at(120,50)]==rendered.pixels[at(120,50)],"brush source sampling does not move with bounds");
        for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x)
            if(!clip.Contains({x,y}))Check(rendered.pixels[at(x,y)]==source.pixels[at(x,y)],"brush stays inside selection");
    }
}
Image Pattern(int width,int height){
    Image image{width,height,std::vector<std::uint32_t>(static_cast<std::size_t>(width)*height)};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        image.pixels[static_cast<std::size_t>(y)*width+x]=0xff000000u|((x*17+y*13)%256<<16)|((x*3+y*47)%256<<8)|((x*71+y*7)%256);
    return image;
}
void StrokeSmoothingAndEraserTests(){
    const auto source=Pattern(180,120);const Rect clip{0,0,180,120};
    const auto at=[](int x,int y){return static_cast<std::size_t>(y)*180+x;};
    Annotation pen;pen.tool=Tool::Pen;pen.width=3;pen.color=0xff0000;
    pen.points={{30,80},{50,40},{70,80},{90,40},{110,80}};
    const auto smoothed=Composite(source,{pen},clip);
    Check(smoothed.pixels[at(50,40)]==source.pixels[at(50,40)],"pen smoothing removes sharp intermediate mouse-sample spikes");
    Check(smoothed.pixels[at(70,60)]!=source.pixels[at(70,60)],"smoothed pen follows the stroke through its averaged path");
    Annotation marker=pen;marker.tool=Tool::Marker;
    const auto smoothMarker=Composite(source,{marker},clip);
    Check(smoothMarker.pixels[at(50,40)]==source.pixels[at(50,40)],"marker smoothing removes sharp intermediate mouse-sample spikes");

    Annotation shape;shape.tool=Tool::Shape;shape.width=5;shape.points={{30,30},{145,95}};
    const auto before=Composite(source,{shape},clip);
    Annotation eraser;eraser.tool=Tool::Eraser;eraser.brushSize=16;eraser.points={{30,30},{70,30}};
    const auto partiallyErased=Composite(source,{shape,eraser},clip);
    Check(partiallyErased.pixels[at(50,30)]==source.pixels[at(50,30)],"eraser restores covered shape pixels to the screenshot");
    Check(partiallyErased.pixels[at(110,30)]!=source.pixels[at(110,30)],"eraser preserves uncovered shape pixels");
    Check(partiallyErased.pixels!=before.pixels,"eraser stroke changes only the covered part of the composite");
    Annotation later;later.tool=Tool::Pen;later.width=5;later.color=0x00ff00;later.points={{50,20},{50,40}};
    const auto redrawn=Composite(source,{shape,eraser,later},clip);
    Check(redrawn.pixels[at(50,30)]!=source.pixels[at(50,30)],"annotations after an eraser stroke render normally");
}
Image ReferenceFilter(const Image& source,Rect rect,bool blur,int width){
    auto result=source;
    const auto index=[&](int x,int y){return static_cast<std::size_t>(y)*source.width+x;};
    const auto average=[&](const Image& input,int left,int top,int right,int bottom){
        std::uint64_t r=0,g=0,b=0,count=0;
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){const auto p=input.pixels[index(x,y)];r+=(p>>16)&255;g+=(p>>8)&255;b+=p&255;++count;}
        return 0xff000000u|static_cast<unsigned>((r/count)<<16)|static_cast<unsigned>((g/count)<<8)|static_cast<unsigned>(b/count);
    };
    if(!blur){
        const int block=std::max(6,width*3);
        for(int y=rect.top;y<rect.bottom;y+=block)for(int x=rect.left;x<rect.right;x+=block){
            const int right=std::min(rect.right,x+block),bottom=std::min(rect.bottom,y+block);
            const auto color=average(source,x,y,right,bottom);
            for(int py=y;py<bottom;++py)for(int px=x;px<right;++px)result.pixels[index(px,py)]=color;
        }
    }else{
        const int radius=std::clamp(width*2,3,40);auto horizontal=source;
        for(int y=rect.top;y<rect.bottom;++y)for(int x=rect.left;x<rect.right;++x)
            horizontal.pixels[index(x,y)]=average(source,std::max(rect.left,x-radius),y,std::min(rect.right,x+radius+1),y+1);
        for(int y=rect.top;y<rect.bottom;++y)for(int x=rect.left;x<rect.right;++x)
            result.pixels[index(x,y)]=average(horizontal,x,std::max(rect.top,y-radius),x+1,std::min(rect.bottom,y+radius+1));
    }
    return result;
}
void FilterReferenceTests(){
    const auto source=Pattern(137,109);const Rect clip{7,11,129,103};
    for(int width:{0,1,3,9,25})for(int variant=0;variant<4;++variant)for(int brushSize:{0,1,10,61}){
        Annotation a;a.tool=Tool::Mosaic;a.width=width;a.variant=variant;a.brushSize=brushSize;
        a.points=variant<2?std::vector<Point>{{18,20},{113,89}}:std::vector<Point>{{-8,20},{50,90},{90,24},{122,77},{50,90},{50,90}};
        const auto domain=variant<2?Normalize(a.points.front(),a.points.back()):clip;
        const auto filtered=ReferenceFilter(source,domain,variant==1||variant==3,width);
        const auto rendered=Composite(source,{a},clip);
        const auto bounds=AnnotationBounds(a,clip);
        auto poisoned=source;
        for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x)if(!bounds.Contains({x,y}))poisoned.pixels[static_cast<std::size_t>(y)*source.width+x]=0xffff00ff;
        RenderAnnotation(poisoned,a,clip);
        for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x){
            const auto i=static_cast<std::size_t>(y)*source.width+x;
            const bool covered=clip.Contains({x,y})&&(variant<2?domain.Contains({x,y}):Hit(a,{x,y},0));
            Check(rendered.pixels[i]==(covered?filtered.pixels[i]:source.pixels[i]),"filter matches immutable reference and exact capsule union");
            if(bounds.Contains({x,y}))Check(poisoned.pixels[i]==rendered.pixels[i],"reported bounds include all filter source reads");
            else Check(poisoned.pixels[i]==0xffff00ff,"reported bounds contain all filter writes");
        }
    }
    // A narrow source exercises single-column masks and truncated blur windows.
    const auto thin=Pattern(1,20);Annotation dot;dot.tool=Tool::Mosaic;dot.variant=3;dot.points={{0,8}};
    Check(Composite(thin,{dot},{0,0,1,20}).pixels==ReferenceFilter(thin,{0,0,1,20},true,3).pixels,"single-pixel-wide images support blur");
    Annotation extreme;extreme.tool=Tool::Mosaic;extreme.variant=2;extreme.width=3;
    extreme.points={{std::numeric_limits<int>::max(),std::numeric_limits<int>::max()},{std::numeric_limits<int>::max(),std::numeric_limits<int>::max()},{20,20}};
    const auto huge=Composite(source,{extreme},clip);
    Check(huge.Valid()&&huge.pixels[0]==source.pixels[0],"off-image brush coordinates remain bounded");
    extreme.points={{std::numeric_limits<int>::min(),std::numeric_limits<int>::min()},{std::numeric_limits<int>::min(),std::numeric_limits<int>::min()},{20,20}};
    Check(Composite(source,{extreme},clip).Valid(),"negative extreme brush coordinates remain bounded");
    Annotation outside=extreme;outside.points={{-100,-100},{-80,-80}};
    Check(Composite(source,{outside},clip).pixels==source.pixels,"fully off-image brush is a no-op");
}
int main(){
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
    Check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"start GDI+");
    ArrowOptionTests();
    TextOptionTests();
    NeutralTextTests();
    Image source{300,200,std::vector<std::uint32_t>(60000,0xffeeeeee)};
    for(int y=0;y<200;++y)for(int x=0;x<300;++x)source.pixels[static_cast<std::size_t>(y)*300+x]=((x+y)%2)?0xffeeeeee:0xff222222;
    BrushTests(source);
    IncrementalTests(source);
    StrokeSmoothingAndEraserTests();
    FilterReferenceTests();
    for(auto tool:{Tool::Shape,Tool::Arrow,Tool::Pen,Tool::Marker,Tool::Mosaic,Tool::Text}){
        for(int variant=0;variant<(tool==Tool::Shape||tool==Tool::Mosaic||tool==Tool::Arrow?4:1);++variant){
            Annotation a;a.tool=tool;a.points={{40,40},{150,100}};a.text=L"Lume \u622a\u56fe";a.width=5;a.variant=variant;
            const auto rendered=Composite(source,{a},{20,20,250,180});
            Check(rendered.Valid(),"annotation compositor returns complete image");
            std::size_t changed=0;for(std::size_t i=0;i<source.pixels.size();++i)if(source.pixels[i]!=rendered.pixels[i])++changed;
            Check(changed>20,"every annotation tool changes actual output pixels");
            Check(rendered.pixels[0]==source.pixels[0],"outside selection remains untouched");
            Check(source.pixels[0]==0xff222222,"source remains immutable");
        }
    }
    for(int i=0;i<CommandCount;++i){
        Gdiplus::Bitmap bitmap(40,40,PixelFormat32bppARGB);Gdiplus::Graphics graphics(&bitmap);
        graphics.Clear(Gdiplus::Color(0,0,0,0));DrawIcon(graphics,static_cast<Command>(i),8,8,24,Gdiplus::Color(255,50,50,50));
        int covered=0;for(int y=0;y<40;++y)for(int x=0;x<40;++x){Gdiplus::Color pixel;bitmap.GetPixel(x,y,&pixel);if(pixel.GetA())++covered;}
        Check(covered>20&&covered<900,"every toolbar icon is nonblank and framed");
    }
    Gdiplus::GdiplusShutdown(token);std::cout<<"PASS screenshot annotation compositor and toolbar glyphs\n";
}
