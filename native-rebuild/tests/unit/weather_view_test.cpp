#include "platform/windows/weather/weather_view.h"
#include "weather_fixtures.h"
#include <windows.h>
#include <objbase.h>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
using namespace lume::platform::windows;
void Expect(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
namespace {
template<class T> struct ScopedCom {
    T* value=nullptr;
    ~ScopedCom(){if(value)value->Release();}
    T** Put(){return &value;}
    T* operator->()const{return value;}
};
WidgetBitmap OriginalCloud(){
    const auto module=GetModuleHandleW(nullptr);
    const auto resource=FindResourceW(module,MAKEINTRESOURCEW(6204),RT_RCDATA);
    const auto data=LoadResource(module,resource);const auto size=SizeofResource(module,resource);
    Expect(resource&&data&&size,"original cloud fixture resource");
    ScopedCom<IWICImagingFactory> factory;ScopedCom<IWICStream> stream;ScopedCom<IWICBitmapDecoder> decoder;
    ScopedCom<IWICBitmapFrameDecode> frame;ScopedCom<IWICFormatConverter> converter;
    Expect(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.Put())))&&
        SUCCEEDED(factory->CreateStream(stream.Put()))&&SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(LockResource(data)),size))&&
        SUCCEEDED(factory->CreateDecoderFromStream(stream.value,nullptr,WICDecodeMetadataCacheOnLoad,decoder.Put()))&&
        SUCCEEDED(decoder->GetFrame(0,frame.Put()))&&SUCCEEDED(factory->CreateFormatConverter(converter.Put()))&&
        SUCCEEDED(converter->Initialize(frame.value,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)),
        "decode independent full-resolution cloud fixture");
    WidgetBitmap result;Expect(SUCCEEDED(frame->GetSize(&result.width,&result.height)),"cloud fixture size");
    result.pixels.resize(static_cast<std::size_t>(result.width)*result.height);
    Expect(SUCCEEDED(converter->CopyPixels(nullptr,result.width*4,static_cast<UINT>(result.pixels.size()*4),
        reinterpret_cast<BYTE*>(result.pixels.data()))),"cloud fixture pixels");
    return result;
}
double AreaAlpha(const WidgetBitmap& source,unsigned x,unsigned y,unsigned width,unsigned height){
    const double sx=static_cast<double>(source.width)/width,sy=static_cast<double>(source.height)/height;
    const double left=x*sx,right=(x+1)*sx,top=y*sy,bottom=(y+1)*sy;double total=0;
    for(unsigned iy=static_cast<unsigned>(std::floor(top));iy<std::min(source.height,static_cast<unsigned>(std::ceil(bottom)));++iy)
        for(unsigned ix=static_cast<unsigned>(std::floor(left));ix<std::min(source.width,static_cast<unsigned>(std::ceil(right)));++ix){
            const double coverage=(std::min(right,static_cast<double>(ix+1))-std::max(left,static_cast<double>(ix)))*
                (std::min(bottom,static_cast<double>(iy+1))-std::max(top,static_cast<double>(iy)));
            total+=(source.pixels[static_cast<std::size_t>(iy)*source.width+ix]>>24)*coverage;
        }
    return total/(sx*sy);
}
void CheckCloudMinification(){
    const auto source=OriginalCloud();WidgetCanvas canvas;WidgetBitmap frame;
    for(const unsigned dpi:{96u,120u,144u,192u}){
        Expect(canvas.Begin(64,64,dpi)&&canvas.ImageResource(6204,{0,0,32,32})&&canvas.Finish(frame),
            "small cloud renders at actual DPI");
        const unsigned edge=32*dpi/96;double error=0;unsigned samples=0;
        for(unsigned y=0;y<edge;++y)for(unsigned x=0;x<edge;++x){
            const double expected=AreaAlpha(source,x,y,edge,edge);
            if(expected<=2||expected>=253)continue;
            error+=std::abs(expected-static_cast<double>(frame.pixels[static_cast<std::size_t>(y)*frame.width+x]>>24));
            ++samples;
        }
        const double average=samples?error/samples:255;
        std::cout<<"cloud "<<dpi<<" DPI edge alpha error "<<average<<" across "<<samples<<" pixels\n";
        Expect(samples>20&&average<2,"cloud minification preserves pixel-area edge coverage instead of undersampling");
        for(auto pixel:frame.pixels)Expect((pixel&255)<=(pixel>>24)&&((pixel>>8)&255)<=(pixel>>24)&&
            ((pixel>>16)&255)<=(pixel>>24),"scaled cloud remains premultiplied");
    }
    Expect(canvas.Begin(160,160,120),"size-cache churn begins");
    const auto generation=canvas.SurfaceGeneration();
    for(unsigned size=12;size<100;++size){
        Expect(canvas.ImageResource(6204,{0,0,static_cast<float>(size),static_cast<float>(size)}),"resized resource draws");
        Expect(canvas.CachedImages()==1&&canvas.CachedImageVariants()<=2,"at most two physical-size variants per PNG");
    }
    Expect(canvas.Finish(frame)&&canvas.SurfaceGeneration()==generation,"resource sizes reuse the canvas surface");
}
void CheckLongLocation(){
    WidgetCanvas canvas;WidgetBitmap first,extended;
    WeatherReading reading;
    reading.snapshot=lume::weather::ParseMet(weather_fixture::MetBody().dump(),weather_fixture::Shanghai,
        "auto",weather_fixture::Now,weather_fixture::Now,weather_fixture::Local);
    Expect(reading.snapshot.has_value(),"long location fixture parsed");
    const std::string location=u8"黄区 (堪萨斯州里诺县)";
    const auto wide=WeatherText(location);
    for(const unsigned dpi:{96u,120u,144u,192u}){
        Expect(canvas.Begin(320,152,dpi),"long location uses original default size");
        const auto fitted=FitWeatherLabel(canvas,wide,96);
        Expect(!fitted.empty()&&fitted.back()==L'\u2026'&&fitted.substr(0,2)==wide.substr(0,2)&&
            canvas.MeasureText(fitted,10)<=96,"long location keeps its beginning and a full fitting ellipsis");
        Expect(FitWeatherLabel(canvas,L"上海市",96)==L"上海市","short location stays unchanged");
        reading.snapshot->cityName=location;
        DrawWeather(canvas,320,152,reading,"auto");Expect(canvas.Finish(first),"long location paints");
        const auto text=reading.snapshot->cityName;
        reading.snapshot->cityName=location+u8" · 更长的地区说明用于确认尾部不会覆盖边界";
        Expect(canvas.Begin(320,152,dpi),"longer location begins");
        DrawWeather(canvas,320,152,reading,"auto");Expect(canvas.Finish(extended),"longer location paints");
        Expect(first.pixels==extended.pixels,"longer hidden suffix cannot replace the displayed prefix or overlap weather controls");
        Expect(text==location&&reading.snapshot->cityName!=location,"display clipping never rewrites source location data");
        const std::wstring surrogate=L"黄区 \U0001f30f 超长地点名称";
        Expect(canvas.Begin(320,152,dpi),"unicode location begins");
        for(float width:{18.f,24.f,31.f,47.f}){
            const auto label=FitWeatherLabel(canvas,surrogate,width);
            Expect(label.empty()||label.size()<2||label[label.size()-2]<0xd800||label[label.size()-2]>0xdbff,
                "location ellipsis does not leave half of a UTF-16 character");
        }
        Expect(canvas.Finish(extended),"unicode location finishes");
    }
}
void CheckDomesticIcons(){
    const auto now=*lume::weather::ParseUtc("2026-09-21T18:35:00Z");
    for(const auto code:{"501","502","503","511","900","999"}){
        auto page=weather_fixture::PublicPage();
        const auto at=page.find("151.png");
        Expect(at!=std::string::npos,"domestic icon fixture exists");
        page.replace(at,3,code);
        WeatherReading reading;
        reading.snapshot=lume::weather::ParseQweather(page,weather_fixture::Shanghai,"auto",now,now);
        Expect(reading.snapshot.has_value(),"domestic observation parses without inventing a weather code");
        for(const unsigned dpi:{96u,120u,144u,192u}){
            WidgetCanvas canvas;WidgetBitmap frame;
            Expect(canvas.Begin(320,152,dpi),"domestic icon frame begins");
            DrawWeather(canvas,320,152,reading,"auto");
            Expect(canvas.Finish(frame),"domestic icon frame finishes");
            unsigned visible=0;
            // Only the upper-left icon area, excluding the temperature.
            for(unsigned y=18*dpi/96;y<61*dpi/96;++y)
                for(unsigned x=17*dpi/96;x<64*dpi/96;++x)
                    if((frame.pixels[static_cast<std::size_t>(y)*frame.width+x]>>24)>20)++visible;
            Expect(visible>100,"every domestic condition keeps a visible current icon");
            WidgetBitmap changed;
            const double temperature=reading.snapshot->temperature;
            reading.snapshot->temperature=39;
            Expect(canvas.Begin(320,152,dpi),"changed temperature begins");
            DrawWeather(canvas,320,152,reading,"auto");
            Expect(canvas.Finish(changed),"changed temperature finishes");
            reading.snapshot->temperature=temperature;
            for(unsigned y=18*dpi/96;y<61*dpi/96;++y)
                for(unsigned x=17*dpi/96;x<64*dpi/96;++x)
                    Expect(frame.pixels[static_cast<std::size_t>(y)*frame.width+x]==
                        changed.pixels[static_cast<std::size_t>(y)*changed.width+x],
                        "temperature never shifts into the reserved current icon area");
            if(std::string(code)=="501"){
                const unsigned gapY=53*dpi/96;
                unsigned transparent=0;
                for(unsigned x=20*dpi/96;x<61*dpi/96;++x)
                    if((frame.pixels[static_cast<std::size_t>(gapY)*frame.width+x]>>24)==0)++transparent;
                Expect(transparent>30*dpi/96,"fog has separated mist bands instead of an opaque tile");
            }
            unsigned headerPixels=0;
            for(unsigned y=2*dpi/96;y<18*dpi/96;++y)
                for(unsigned x=271*dpi/96;x<291*dpi/96;++x)
                    if(frame.pixels[static_cast<std::size_t>(y)*frame.width+x]>>24)++headerPixels;
            Expect(headerPixels==0,"weather view never draws a header refresh glyph");
        }
    }
}
}
int main(int argc,char** argv){
    // Measured from original renderer.js + widget.css in headless Edge at 96 DPI.
    Expect(WeatherLayoutFor(320,152).hourTop==71&&WeatherLayoutFor(320,152).hourWidth==43.5f,"source minimum-height grid overflow retained");
    Expect(WeatherLayoutFor(480,220).hourTop==96&&WeatherLayoutFor(720,360).hourTop==236,"source expanded forecast alignment");
    Expect(WeatherIconResource(0,true)==6201&&WeatherIconResource(0,false)==6202,"original day/night assets");
    Expect(WeatherIconResource(2,true)==6203&&WeatherIconResource(2,false)==6204,"night partly cloud uses cloud");
    Expect(WeatherIconResource(999,true)==0,"unknown data never shows invented sunshine");
    Expect(WeatherTemperature(-1.5)==L"-1"&&WeatherTemperature(1.5)==L"2","original JS rounding including below zero");
    Expect(WeatherHourLabel("2026-09-26T06:00")==L"6"&&WeatherHourLabel("2026-09-26T06:30")==L"06:30","original hour labels");
    Expect(WeatherText(u8"拉薩市")==L"拉萨市","external text simplified at display boundary");
    WeatherReading reading;reading.error=lume::weather::ProviderError::Unavailable;
    Expect(WeatherStatus(reading)==L"定位或天气不可用，可在设置中选择城市","no data error");
    reading.snapshot.emplace();reading.snapshot->cityName=u8"蘇州市";reading.snapshot->condition=u8"多雲";
    reading.snapshot->temperature=22;reading.snapshot->weatherCode=0;
    reading.snapshot->forecast.push_back({"2026-09-26",25,18,0,u8"晴"});
    for(int i=0;i<6;++i)reading.snapshot->hourly.push_back({"2026-09-26T06:00",20.0+i,i*20,u8"多云",true});
    Expect(WeatherStatus(reading)==L"更新失败 · 显示上次天气","stale data error stays honest");
    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    CheckDomesticIcons();
    CheckCloudMinification();
    CheckLongLocation();
    {
        WidgetCanvas canvas;WidgetBitmap bitmap;
        for(const unsigned dpi:{96u,144u}){
            Expect(canvas.Begin(320,152,dpi),"original size");
            DrawWeather(canvas,320,152,reading,"auto");
            Expect(canvas.Finish(bitmap),"weather rendered");
            Expect(bitmap.width==320*dpi/96&&bitmap.pixels[0]==0,"DPI and transparent material boundary");
            Expect(canvas.CachedImages()>0&&canvas.CachedImages()<=8,"original icons loaded into bounded cache");
            Expect(canvas.Begin(320,152,dpi),"asset validation");
            for(unsigned id=6201;id<=6208;++id)Expect(canvas.ImageResource(id,{20,20,47,43}),"each original PNG resource decodes");
            Expect(!canvas.ImageResource(60000,{20,20,47,43}),"missing resource explicit");
            Expect(canvas.Finish(bitmap)&&canvas.CachedImages()==8,"all assets bounded");
        }
        if(argc==3&&std::string(argv[1])=="--capture"){
            const auto directory=std::filesystem::u8path(argv[2]);
            std::filesystem::create_directories(directory);
            WeatherReading sample;
            sample.snapshot=lume::weather::ParseMet(weather_fixture::MetBody().dump(),weather_fixture::Shanghai,
                "shanghai",weather_fixture::Now,weather_fixture::Now,weather_fixture::Local);
            Expect(sample.snapshot.has_value(),"visual fixture parsed through production codec");
            const auto encoded=lume::weather::EncodeCache(*sample.snapshot);
            Expect(encoded.has_value(),"visual fixture is valid");
            std::ofstream(directory/"snapshot.json")<<*encoded;
            for(const auto dimensions:{std::pair{320,152},std::pair{480,220},std::pair{720,360}}){
                Expect(canvas.Begin(static_cast<float>(dimensions.first),static_cast<float>(dimensions.second),96),"visual capture begins");
                DrawWeather(canvas,static_cast<float>(dimensions.first),static_cast<float>(dimensions.second),sample,"shanghai");
                Expect(canvas.Finish(bitmap),"visual capture finishes");
                std::ofstream output(directory/(std::to_string(dimensions.first)+".bgra"),std::ios::binary);
                output.write(reinterpret_cast<const char*>(bitmap.pixels.data()),static_cast<std::streamsize>(bitmap.pixels.size()*4));
                Expect(output.good(),"visual pixels saved");
            }
            sample.snapshot->cityName=u8"黄区 (堪萨斯州里诺县)";
            for(const auto dpi:{96u,120u,144u,192u}){
                Expect(canvas.Begin(320,152,dpi),"long location capture begins");
                DrawWeather(canvas,320,152,sample,"auto");
                Expect(canvas.Finish(bitmap),"long location capture finishes");
                std::ofstream output(directory/("long-location-"+std::to_string(dpi)+".bgra"),std::ios::binary);
                output.write(reinterpret_cast<const char*>(bitmap.pixels.data()),static_cast<std::streamsize>(bitmap.pixels.size()*4));
                Expect(output.good(),"long location pixels saved");
            }
            const auto observed=*lume::weather::ParseUtc("2026-09-21T18:35:00Z");
            for(const auto code:{"501","502","503","999"}){
                auto page=weather_fixture::PublicPage();
                page.replace(page.find("151.png"),3,code);
                sample.snapshot=lume::weather::ParseQweather(page,weather_fixture::Shanghai,"auto",observed,observed);
                Expect(sample.snapshot.has_value(),"domestic capture parses through production codec");
                sample.snapshot->condition=std::string(code)=="501"?u8"雾":
                    std::string(code)=="502"?u8"霾":std::string(code)=="503"?u8"扬沙":u8"未知";
                for(const auto dpi:{96u,120u,144u,192u}){
                    Expect(canvas.Begin(320,152,dpi),"domestic capture begins");
                    DrawWeather(canvas,320,152,sample,"auto");
                    Expect(canvas.Finish(bitmap),"domestic capture finishes");
                    std::ofstream output(directory/("domestic-"+std::string(code)+"-"+std::to_string(dpi)+".bgra"),std::ios::binary);
                    output.write(reinterpret_cast<const char*>(bitmap.pixels.data()),static_cast<std::streamsize>(bitmap.pixels.size()*4));
                    Expect(output.good(),"domestic pixels saved");
                }
            }
        }
    }
    CoUninitialize();std::cout<<"weather original assets, layout and labels passed\n";
}
