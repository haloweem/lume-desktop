#include "apps/shell/spectrum_session.h"
#include "platform/windows/spectrum/spectrum_view.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
namespace {
void Expect(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
class Files:public lume::settings::IFileStore{
public:
    bool Read(const std::string& p,std::string& out)const override{auto i=values.find(p);if(i==values.end())return false;out=i->second;return true;}
    bool Exists(const std::string& p)const override{return values.count(p)!=0;}
    bool Write(const std::string& p,const std::string& text)override{if(fail)return false;values[p]=text;return true;}
    bool Rename(const std::string& a,const std::string& b)override{if(fail||!values.count(a))return false;values[b]=values[a];values.erase(a);return true;}
    bool Remove(const std::string& p)override{values.erase(p);return true;}
    std::map<std::string,std::string> values;bool fail=false;
};
// Writes each rendered style for a by-eye check when LUME_SPECTRUM_PREVIEW
// names a path prefix: <prefix>-pulse.png and <prefix>-blocks.png.
void SavePreview(const lume::platform::windows::WidgetBitmap& bitmap,const wchar_t* style){
    wchar_t prefix[MAX_PATH]{};
    if(!GetEnvironmentVariableW(L"LUME_SPECTRUM_PREVIEW",prefix,MAX_PATH))return;
    const auto file=std::wstring(prefix)+L"-"+style+L".png";const auto* path=file.c_str();
    IWICImagingFactory* factory=nullptr;IWICStream* stream=nullptr;IWICBitmapEncoder* encoder=nullptr;IWICBitmapFrameEncode* frame=nullptr;
    Expect(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&
        SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path,GENERIC_WRITE))&&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&
        SUCCEEDED(encoder->Initialize(stream,WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&
        SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->SetSize(bitmap.width,bitmap.height)),"preview encoder");
    auto format=GUID_WICPixelFormat32bppPBGRA;
    Expect(SUCCEEDED(frame->SetPixelFormat(&format))&&
        SUCCEEDED(frame->WritePixels(bitmap.height,bitmap.width*4,static_cast<UINT>(bitmap.pixels.size()*4),
            reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(bitmap.pixels.data()))))&&
        SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"preview write");
    frame->Release();encoder->Release();stream->Release();factory->Release();
}
}
int main(){
    using lume::ui::PageCommand;
    namespace spectrum=lume::spectrum;
    // Protocol: per-style settings live under the style name.
    Expect(lume::ui::ParsePageCommand(L"v1|spectrum|enabled|true")==PageCommand::SpectrumEnable,"enable command");
    Expect(lume::ui::ParsePageCommand(L"v1|spectrum|reset")==PageCommand::SpectrumReset,"reset command");
    Expect(lume::ui::ParsePageCommand(L"v1|spectrum|blocks|barWidth|9")==
        static_cast<PageCommand>(static_cast<int>(PageCommand::SpectrumBarWidthFirst)+7),"bar width command");
    Expect(lume::ui::ParsePageCommand(L"v1|spectrum|blocks|barGap|12")==PageCommand::SpectrumBarGapLast,"bar gap command");
    Expect(lume::ui::ParsePageCommand(L"v1|spectrum|blocks|barWidth|25")==PageCommand::Invalid,"bar width bound");
    Expect(lume::ui::ParsePageCommand(L"v1|spectrum|blocks|barGap|01")==PageCommand::Invalid,"canonical numbers only");
    Expect(lume::ui::ParsePageCommand(L"v1|spectrum|style|pulse")==PageCommand::SpectrumStylePulse&&
        lume::ui::ParsePageCommand(L"v1|spectrum|style|blocks")==PageCommand::SpectrumStyleBlocks,"style commands");
    Expect(lume::ui::ParsePageCommand(L"v1|spectrum|size|300")==PageCommand::SpectrumSizeLast&&
        lume::ui::ParsePageCommand(L"v1|spectrum|size|49")==PageCommand::Invalid,"size command");
    Expect(lume::ui::ParsePageCommand(L"v1|spectrum|pulse|barGap|0")==PageCommand::SpectrumPulseBarGapFirst,"pulse settings are their own");
    Expect(lume::ui::ParsePageCommand(L"v1|spectrum|style|waves")==PageCommand::SpectrumStyleWaves&&
           lume::ui::ParsePageCommand(L"v1|spectrum|waves|barWidth|5")==static_cast<PageCommand>(static_cast<int>(PageCommand::SpectrumWavesBarWidthFirst)+3),"样式三 commands");

    // Preferences round trip and strict decoding.
    spectrum::Preferences prefs;prefs.blocks={14,3};std::string text;
    Expect(spectrum::EncodePreferences(prefs,text),"encode");
    spectrum::Preferences decoded;
    Expect(spectrum::DecodePreferences(text,decoded)&&decoded.blocks.barWidth==14&&decoded.blocks.barGap==3,"round trip");
    Expect(text.find("\"style\":\"pulse\"")!=std::string::npos&&decoded.style==spectrum::Style::Pulse,"样式一 is the default");
    Expect(spectrum::DecodePreferences("{\"style\":\"blocks\",\"blocks\":{\"barWidth\":5,\"barGap\":2},\"window\""+text.substr(text.find(":",text.find("\"window\""))),decoded)&&
        decoded.style==spectrum::Style::Pulse&&decoded.blocks.barWidth==5&&decoded.pulse.barWidth==9,"a file from before 样式一 opens on 样式一");
    {
        // 样式二's 组件颜色 round-trips; a file from before it reads white,
        // and its untouched old 9/1 default becomes 4/1.
        spectrum::Preferences colored;colored.blocks.color=0x80ffff;colored.blocks.alpha=200;std::string coloredText;
        Expect(spectrum::EncodePreferences(colored,coloredText)&&spectrum::DecodePreferences(coloredText,decoded)&&
               decoded.blocks.color==0x80ffff&&decoded.blocks.alpha==200,"组件颜色 round trip");
    {
        spectrum::Preferences waves;waves.style=spectrum::Style::Waves;waves.waves={7,2};std::string wavesText;
        Expect(spectrum::EncodePreferences(waves,wavesText)&&spectrum::DecodePreferences(wavesText,decoded)&&
               decoded.style==spectrum::Style::Waves&&decoded.waves.barWidth==7&&decoded.waves.barGap==2,"样式三 round trip");
        auto before=wavesText;const auto at=before.find(",\"waves\":");before.erase(at,before.find('}',at)+1-at);
        before.replace(before.find("\"waves\""),7,"\"pulse\"");
        Expect(spectrum::DecodePreferences(before,decoded)&&decoded.waves.barWidth==5&&decoded.waves.barGap==1,"a file from before 样式三 reads its 5 px default");
    }
        auto before=coloredText;
        const auto at=before.find(",\"color\":");before.erase(at,before.find('}',at)-at);
        before.replace(before.find("\"blocks\":{\"barWidth\":4"),22,"\"blocks\":{\"barWidth\":9");
        Expect(spectrum::DecodePreferences(before,decoded)&&decoded.blocks.color==0xffffff&&decoded.blocks.alpha==255&&
               decoded.blocks.barWidth==4,"a file from before 组件颜色 opens white at 4 px");
    }
    {
        spectrum::Preferences old;old.window.width=1080;old.window.height=147;std::string oldText;
        Expect(spectrum::EncodePreferences(old,oldText)&&spectrum::DecodePreferences(oldText,decoded)&&
               decoded.window.width==1080&&decoded.window.height==368,"a window at the old 880x120 aspect opens taller");
        old.window.height=200;Expect(spectrum::EncodePreferences(old,oldText)&&spectrum::DecodePreferences(oldText,decoded)&&
               decoded.window.height==200,"a window the user shaped keeps its height");
    }
    const auto doubled=spectrum::Scaled(spectrum::Preferences{}.window,200);
    Expect(doubled.width==1760&&doubled.height==600&&spectrum::ScalePercent(doubled)==200,"size keeps the aspect ratio");
    auto strip=spectrum::Preferences{}.window;strip.width=1000;strip.height=60;
    Expect(spectrum::Scaled(strip,50).height==60&&spectrum::ScalePercent(spectrum::Scaled(strip,300))==300,"size stays within limits");
    auto bad=text;bad.replace(bad.find("\"barGap\":3"),10,"\"barGap\":30");
    Expect(!spectrum::DecodePreferences(bad,decoded),"out of range gap rejected");
    Expect(!spectrum::DecodePreferences("{\"style\":\"ripples\""+text.substr(text.find(',')),decoded),"unknown style rejected");

    // Analysis: a 1 kHz sine lights the band holding 1 kHz, not the bass.
    std::vector<float> frame(spectrum::FrameSize);
    for(std::size_t i=0;i<frame.size();++i)frame[i]=.5f*std::sin(2*3.14159265f*1000.f*static_cast<float>(i)/48000.f);
    const auto bands=spectrum::Bands(spectrum::Magnitudes(frame),48000,64);
    const auto loud=static_cast<std::size_t>(std::max_element(bands.begin(),bands.end())-bands.begin());
    const auto [lowBin,highBin]=spectrum::BandBins(loud,64,48000,spectrum::FrameSize/2);
    const auto sineBin=static_cast<std::size_t>(1000.f/(48000.f/spectrum::FrameSize)+.5f);
    Expect(lowBin<=sineBin&&sineBin<=highBin+1&&bands[0]<bands[loud]*.05f,"sine lands in its band");
    const auto bins=spectrum::FrameSize/2,lowest=spectrum::BandBins(0,64,48000,bins).first;
    Expect(lowest==static_cast<std::size_t>(50.f/(48000.f/spectrum::FrameSize))&&spectrum::BandBins(1,64,48000,bins).first==lowest+1,"low bands each take a bin of their own");
    Expect(spectrum::Bands(spectrum::Magnitudes(std::vector<float>(spectrum::FrameSize,0.f)),48000,8)==std::vector<float>(8,0.f),"silence is empty");
    {
        // Stereo: the left channel's 1 kHz shows mirrored on the left half only.
        const auto quiet=spectrum::Magnitudes(std::vector<float>(spectrum::FrameSize,0.f));
        const auto stereo=spectrum::StereoBands(spectrum::Magnitudes(frame),quiet,48000,128);
        const auto peak=static_cast<std::size_t>(std::max_element(stereo.begin(),stereo.end())-stereo.begin());
        Expect(stereo.size()==128&&peak==63-loud&&*std::max_element(stereo.begin()+64,stereo.end())==0.f,
               "left channel mirrored on the left half");
    }
    {
        // Steady sound settles at the top; on silence a bar first
        // eases, then drops faster, and is gone within about a second.
        spectrum::Motion motion;std::vector<float> shown,steady{.002f,.01f,.004f};
        for(int i=0;i<600;++i)motion.Step(shown,steady,1.f/60);
        Expect(shown[1]>.9f&&shown[1]<=1.f&&shown[0]<shown[1],"auto sensitivity settles peaks at the top");
        const float top=shown[1];std::vector<float> quiet(3,0.f);
        motion.Step(shown,quiet,1.f/60);const float first=top-shown[1];
        for(int i=0;i<10;++i)motion.Step(shown,quiet,1.f/60);
        const float before=shown[1];motion.Step(shown,quiet,1.f/60);
        Expect(first<.25f&&before-shown[1]>0,"falls under gravity");
        for(int i=0;i<60;++i)motion.Step(shown,quiet,1.f/60);
        Expect(shown[1]<.02f,"silence settles");
    }
    Expect(spectrum::BarCount(880,9,1)==86&&spectrum::BarCount(10,9,1)==0,"bar count fits width");

    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    {
        // The drawing: rainbow left to right, bars above a faded reflection.
        lume::platform::windows::WidgetCanvas canvas;lume::platform::windows::WidgetBitmap bitmap;
        std::vector<float> levels(spectrum::BarCount(880,9,1));
        for(std::size_t i=0;i<levels.size();++i)levels[i]=.25f+.7f*std::fabs(std::sin(static_cast<float>(i)*.23f));
        Expect(canvas.Begin(880,120,96),"canvas");
        spectrum::BarStyle cyan=spectrum::Preferences{}.blocks;cyan.color=0x80ffff;
        lume::platform::windows::DrawSpectrumBlocks(canvas,880,120,levels,cyan);
        Expect(canvas.Finish(bitmap)&&bitmap.width==880,"render");
        const auto pixel=[&](unsigned x,unsigned y){return bitmap.pixels[y*bitmap.width+x];};
        const auto red=[](std::uint32_t p){return (p>>16)&255;};const auto blue=[](std::uint32_t p){return p&255;};
        unsigned barX=0;while(barX<880&&(pixel(barX,80)>>24)<200)++barX;
        Expect(barX<880&&red(pixel(barX,80))<blue(pixel(barX,80))&&blue(pixel(barX,80))>200,"样式二 draws in its 组件颜色");
        Expect((pixel(barX,86)>>24)>0&&(pixel(barX,80)>>24)>(pixel(barX,86)>>24),"reflection is fainter than the bar");
        SavePreview(bitmap,L"blocks");
        Expect(canvas.Begin(880,120,96),"canvas");
        lume::platform::windows::DrawSpectrumPulse(canvas,880,120,levels,{8,2});
        Expect(canvas.Finish(bitmap),"render pulse");
        Expect(red(pixel(14,83))>blue(pixel(14,83))/2&&(pixel(14,83)>>24)>200,"样式一 draws its hairline");
        Expect((pixel(14,76)>>24)>200&&(pixel(14,88)>>24)<(pixel(14,76)>>24)&&(pixel(14,88)>>24)>0,"bars above, fainter reflection below");
        SavePreview(bitmap,L"pulse");
        std::fill(levels.begin(),levels.end(),0.f);
        Expect(canvas.Begin(880,120,96),"canvas");
        lume::platform::windows::DrawSpectrumPulse(canvas,880,120,levels,{8,2});
        Expect(canvas.Finish(bitmap),"render silent pulse");
        Expect((pixel(14,83)>>24)>200&&(pixel(14,77)>>24)==0&&(pixel(14,88)>>24)==0,"silence leaves only the line");
        SavePreview(bitmap,L"pulse-silent");
        // 样式三: rainbow spindles about the middle line, mirrored above and below.
        std::fill(levels.begin(),levels.end(),.6f);
        Expect(canvas.Begin(880,300,96),"canvas");
        lume::platform::windows::DrawSpectrumWaves(canvas,880,300,levels,spectrum::Preferences{}.waves);
        Expect(canvas.Finish(bitmap),"render waves");
        unsigned waveX=0;while(waveX<880&&(pixel(waveX,150)>>24)<200)++waveX;
        Expect(waveX<880&&(pixel(waveX+2,100)>>24)>200&&(pixel(waveX+2,200)>>24)>200&&(pixel(waveX+2,10)>>24)==0,"样式三 is mirrored about the middle");
        const auto green=[](std::uint32_t p){return (p>>8)&255;};
        Expect(green(pixel(waveX+2,150))>red(pixel(waveX+2,150)),"样式三 starts green");
        SavePreview(bitmap,L"waves");
        // LUME_SPECTRUM_SONG names raw stereo (interleaved) 48 kHz float samples: played through
        // the real analysis at 60 frames a second, a few moments are written as
        // <prefix>-song-<second>.png on a dark backdrop for comparing by eye.
        wchar_t song[MAX_PATH]{};
        if(GetEnvironmentVariableW(L"LUME_SPECTRUM_SONG",song,MAX_PATH)){
            std::vector<float> samples;
            if(FILE* file=_wfopen(song,L"rb")){
                std::fseek(file,0,SEEK_END);samples.resize(static_cast<std::size_t>(std::ftell(file))/sizeof(float));
                std::fseek(file,0,SEEK_SET);samples.resize(std::fread(samples.data(),sizeof(float),samples.size(),file));
                std::fclose(file);
            }
            Expect(samples.size()>48000*2*10,"song samples");
            std::vector<float> leftSamples(samples.size()/2),rightSamples(samples.size()/2);
            for(std::size_t i=0;i<leftSamples.size();++i){leftSamples[i]=samples[2*i];rightSamples[i]=samples[2*i+1];}
            const spectrum::BarStyle style=spectrum::Preferences{}.pulse;
            const auto width=static_cast<float>(spectrum::WindowLimits.defaultWidth),height=static_cast<float>(spectrum::WindowLimits.defaultHeight);
            spectrum::Motion motion;std::vector<float> shown;std::vector<long> heights,rises,falls,sampled;double total=0,neighbour=0,bars=0,spikes=0,thirds[3]{};
            // LUME_SPECTRUM_BARS overrides the bar count, to match a recorded widget.
            wchar_t barsText[16]{};const auto barsOverride=GetEnvironmentVariableW(L"LUME_SPECTRUM_BARS",barsText,16)?std::wcstoul(barsText,nullptr,10):0ul;
            const std::size_t count=barsOverride?barsOverride:spectrum::BarCount(width,style.barWidth,style.barGap),hop=800;
            // LUME_SPECTRUM_DUMP: the whole song, every second frame's heights (px) on a line.
            wchar_t dumpPath[MAX_PATH]{};const bool dump=GetEnvironmentVariableW(L"LUME_SPECTRUM_DUMP",dumpPath,MAX_PATH)>0;
            FILE* dumpFile=dump?_wfopen(dumpPath,L"w"):nullptr;const std::size_t limit=dump?SIZE_MAX:60*100;
            for(std::size_t end=spectrum::FrameSize,step=0;end<=leftSamples.size()&&step<=limit;end+=hop,++step){
                const auto window=[&](const std::vector<float>& channel){
                    return std::vector<float>(channel.begin()+static_cast<std::ptrdiff_t>(end-spectrum::FrameSize),channel.begin()+static_cast<std::ptrdiff_t>(end));
                };
                motion.Step(shown,spectrum::StereoBands(spectrum::Magnitudes(window(leftSamples)),spectrum::Magnitudes(window(rightSamples)),48000,count),1.f/60);
                // Height in pixels as drawn (205 px of room), for comparing with
                // what the reference recording measures.
                if(step>300&&step%2==0){
                    for(std::size_t i=0;i<shown.size();++i){
                        const long px=std::lround(shown[i]*205);heights.push_back(px);
                        if(sampled.size()==shown.size()&&px!=sampled[i])(px>sampled[i]?rises:falls).push_back(std::labs(px-sampled[i]));
                    }
                    sampled.assign(shown.size(),0);for(std::size_t i=0;i<shown.size();++i)sampled[i]=std::lround(shown[i]*205);
                    if(dumpFile){for(const long px:sampled)std::fprintf(dumpFile,"%ld ",px);std::fputc('\n',dumpFile);}
                    // Neighbour contrast, as measured on the recording (ref-tools/contrast.js).
                    for(std::size_t i=0;i<sampled.size();++i){
                        total+=static_cast<double>(sampled[i]);++bars;thirds[std::min<std::size_t>(2,i*3/sampled.size())]+=static_cast<double>(sampled[i]);
                        if(i+1<sampled.size())neighbour+=static_cast<double>(std::labs(sampled[i]-sampled[i+1]));
                        if(i&&i+1<sampled.size()&&sampled[i]>=20&&sampled[i]>=sampled[i-1]+sampled[i+1])++spikes;
                    }
                }
                if(step%(60*20)||!step)continue;
                Expect(canvas.Begin(width,height,96),"song canvas");
                canvas.Rounded({0,0,width,height},0,0x3a4436,1);
                lume::platform::windows::DrawSpectrumPulse(canvas,width,height,shown,style);
                Expect(canvas.Finish(bitmap),"render song");
                SavePreview(bitmap,(L"song-"+std::to_wstring(step/60)).c_str());
                Expect(canvas.Begin(width,height,96),"song canvas");
                canvas.Rounded({0,0,width,height},0,0x3a4436,1);
                lume::platform::windows::DrawSpectrumWaves(canvas,width,height,shown,spectrum::Preferences{}.waves);
                Expect(canvas.Finish(bitmap),"render song waves");
                SavePreview(bitmap,(L"song-waves-"+std::to_wstring(step/60)).c_str());
            }
            if(dumpFile)std::fclose(dumpFile);
            std::sort(heights.begin(),heights.end());
            const auto at=[&](double p){return heights[static_cast<std::size_t>(p*static_cast<double>(heights.size()-1))];};
            std::cout<<"song height px p10/p50/p90/p99: "<<at(.1)<<' '<<at(.5)<<' '<<at(.9)<<' '<<at(.99)<<'\n';
            std::sort(rises.begin(),rises.end());std::sort(falls.begin(),falls.end());
            std::cout<<"song per 30 fps frame: rise median "<<rises[rises.size()/2]<<" p90 "<<rises[rises.size()*9/10]
                     <<", fall median "<<falls[falls.size()/2]<<" p90 "<<falls[falls.size()*9/10]<<'\n';
            std::cout<<"song neighbour diff / mean "<<neighbour/(bars-bars/static_cast<double>(count))/(total/bars)
                     <<", spikes % "<<spikes*100/bars<<", thirds / mean "<<thirds[0]*3/total<<' '<<thirds[1]*3/total<<' '<<thirds[2]*3/total<<'\n';
        }
    }
    Files files;
    {
        lume::shell::SpectrumSession session(GetModuleHandleW(nullptr),"spectrum.json",{},[]{},&files);
        Expect(session.Restore()&&!session.Running(),"disabled restore opens nothing");
        auto reply=session.HandlePage(PageCommand::SpectrumEnable);
        Expect(reply&&session.Running()&&session.Handle(),"real spectrum window and capture start");
        Expect(reply->find(L"\"type\":\"spectrum-state\"")!=std::wstring::npos&&reply->find(L"\"material\":\"none\"")!=std::wstring::npos,"state message");
        {
            // It stays at the bottom of the desktop layer: a click does not
            // raise it over the Dock, and it sits right above the desktop host.
            const HWND spectrumWindow=session.Handle(),host=lume::platform::windows::desktop_level::Host();
            Expect(lume::platform::windows::desktop_level::AtBottom(spectrumWindow),"spectrum stays at the bottom of the desktop layer");
            const HWND before=GetWindow(spectrumWindow,GW_HWNDPREV);
            SendMessageW(spectrumWindow,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(spectrumWindow),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
            Expect(GetWindow(spectrumWindow,GW_HWNDPREV)==before,"a click does not raise it");
            if(host)Expect(lume::platform::windows::desktop_level::Above(spectrumWindow,host),"it sits above the desktop host");
        }
        reply=session.HandlePage(static_cast<PageCommand>(static_cast<int>(PageCommand::SpectrumBarWidthFirst)+10));
        Expect(reply&&reply->find(L"\"barWidth\":12")!=std::wstring::npos&&reply->find(L"\"saved\":true")!=std::wstring::npos,"bar width applied and saved");
        Expect(files.values["spectrum.json"].find("\"barWidth\":12")!=std::string::npos,"bar width persisted");
        Expect(!session.HandlePage(PageCommand::PerformanceEnable),"spectrum ignores other widgets");
        files.fail=true;
        reply=session.HandlePage(PageCommand::SpectrumBarGapFirst);
        Expect(reply&&reply->find(L"\"error\":\"save\"")!=std::wstring::npos&&session.Snapshot().blocks.barGap==1,"failed save rolls back");
        files.fail=false;
        Expect(session.HandlePage(PageCommand::SpectrumDisable)&&!session.Running()&&!session.Handle(),"disable stops capture and window");
    }
    {
        lume::shell::SpectrumSession session(GetModuleHandleW(nullptr),"spectrum.json",{},[]{},&files);
        Expect(session.Snapshot().blocks.barWidth==12&&!session.Snapshot().window.enabled,"settings reload");
        auto changed=session.HandlePage(PageCommand::SpectrumStyleBlocks);
        Expect(changed&&changed->find(L"\"style\":\"blocks\"")!=std::wstring::npos&&session.Snapshot().style==spectrum::Style::Blocks,"style switch");
        changed=session.HandlePage(static_cast<PageCommand>(static_cast<int>(PageCommand::SpectrumSizeFirst)+100));
        Expect(changed&&changed->find(L"\"scale\":150")!=std::wstring::npos&&session.Snapshot().window.width==1320,"size command scales the widget");
        auto reply=session.HandlePage(PageCommand::SpectrumReset);
        Expect(reply&&session.Snapshot().blocks.barWidth==4&&session.Snapshot().blocks.barGap==1&&session.Snapshot().blocks.color==0xffffff,"reset restores 4 px, 1 px and white");
    }
    CoUninitialize();
    return 0;
}
