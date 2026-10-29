#pragma once
// Opt-in screen evidence for the production NativeWidgetWindow and LumeGlass.
// Only our isolated background rectangle is captured; no user state is loaded.
#include "vendor/nlohmann/json.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <algorithm>
#include <cmath>

namespace widget_presentation_capture {
using namespace lume::platform::windows;
using Json=nlohmann::json;
inline void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
inline void Pump(DWORD milliseconds){
    const auto end=GetTickCount64()+milliseconds;
    do{
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        Sleep(2);
    }while(GetTickCount64()<end);
}
struct Window {
    HWND value=nullptr;
    ~Window(){if(value)DestroyWindow(value);}
};
struct Module {
    HMODULE value=nullptr;
    ~Module(){if(value)FreeLibrary(value);}
};
template<class T>T Function(HMODULE module,const char* name){
    const auto address=GetProcAddress(module,name);T result=nullptr;
    static_assert(sizeof(result)==sizeof(address));std::memcpy(&result,&address,sizeof(result));return result;
}
inline LRESULT CALLBACK Background(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    if(message==WM_PAINT){
        PAINTSTRUCT paint{};const auto dc=BeginPaint(hwnd,&paint);RECT bounds{};GetClientRect(hwnd,&bounds);
        const COLORREF colors[]={RGB(113,177,211),RGB(222,168,117),RGB(151,193,127),RGB(196,153,211)};
        for(LONG y=0;y<bounds.bottom;y+=48)for(LONG x=0;x<bounds.right;x+=48){
            RECT cell{x,y,std::min(x+48,bounds.right),std::min(y+48,bounds.bottom)};
            const auto brush=CreateSolidBrush(colors[(x/48+y/48)%4]);
            FillRect(dc,&cell,brush);DeleteObject(brush);
        }
        EndPaint(hwnd,&paint);return 0;
    }
    if(message==WM_ERASEBKGND)return 1;
    return DefWindowProcW(hwnd,message,w,l);
}
inline void SaveBmp(const std::filesystem::path& path,const std::vector<std::uint32_t>& pixels,int width,int height){
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size()*4);
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-height;
    info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream stream(path,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&file),sizeof(file));
    stream.write(reinterpret_cast<const char*>(&info),sizeof(info));
    stream.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()*4));
    Require(stream.good(),"could not save screen frame");
}
// A separate capture thread also samples while Start/Stop run synchronously.
struct Recorder {
    std::atomic<bool> stop{false};
    std::atomic<unsigned> phase{0}; // background, starting, ready, closing, closed
    std::atomic<unsigned> count{0};
    std::thread thread;
    Json frames=Json::array();
    std::string error;
    std::vector<std::uint32_t> baseline;
    const std::filesystem::path output;
    const RECT region;
    ULONGLONG started=GetTickCount64();
    Recorder(std::filesystem::path directory,RECT rectangle):output(std::move(directory)),region(rectangle){
        thread=std::thread([this]{Record();});
    }
    ~Recorder(){Finish();}
    void Finish(){stop=true;if(thread.joinable())thread.join();}
    void Record(){
        HDC screen=nullptr,memory=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;
        try{
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            const int width=region.right-region.left,height=region.bottom-region.top;
            screen=GetDC(nullptr);memory=CreateCompatibleDC(screen);
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            void* bits=nullptr;bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            Require(screen&&memory&&bitmap&&bits,"capture surface unavailable");
            old=SelectObject(memory,bitmap);
            Module dwm{LoadLibraryW(L"dwmapi.dll")};
            const auto flush=dwm.value?Function<HRESULT(WINAPI*)()>(dwm.value,"DwmFlush"):nullptr;
            std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width)*height);
            while(!stop&&frames.size()<600){
                if(flush)flush();else Sleep(16);
                const unsigned beforePhase=phase.load();
                const auto before=GetTickCount64();
                Require(BitBlt(memory,0,0,width,height,screen,region.left,region.top,SRCCOPY|CAPTUREBLT)!=FALSE,"screen copy failed");
                GdiFlush();std::memcpy(pixels.data(),bits,pixels.size()*4);
                for(auto& pixel:pixels)pixel|=0xff000000u;
                if(baseline.empty())baseline=pixels;
                unsigned nearBlack=0,changed=0;
                for(std::size_t i=0;i<pixels.size();++i){
                    const auto p=pixels[i],b=baseline[i];
                    const int red=(p>>16)&255,green=(p>>8)&255,blue=p&255;
                    if(red<16&&green<16&&blue<16)++nearBlack;
                    if(std::max({std::abs(red-static_cast<int>((b>>16)&255)),
                        std::abs(green-static_cast<int>((b>>8)&255)),std::abs(blue-static_cast<int>(b&255))})>8)++changed;
                }
                const auto name="frame-"+std::to_string(frames.size())+".bmp";
                SaveBmp(output/name,pixels,width,height);
                frames.push_back({{"file",name},{"startMs",before-started},{"endMs",GetTickCount64()-started},
                    {"phaseBefore",beforePhase},{"phaseAfter",phase.load()},
                    {"nearBlackRatio",static_cast<double>(nearBlack)/pixels.size()},
                    {"changedFromBackgroundRatio",static_cast<double>(changed)/pixels.size()}});
                count=static_cast<unsigned>(frames.size());
            }
            Require(frames.size()<600,"capture exceeded bounded frame count");
        }catch(const std::exception& exception){error=exception.what();}
        if(old)SelectObject(memory,old);
        if(bitmap)DeleteObject(bitmap);
        if(memory)DeleteDC(memory);
        if(screen)ReleaseDC(nullptr,screen);
    }
};
inline int Run(const char* directory){
    try{
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const auto output=std::filesystem::absolute(std::filesystem::u8path(directory));
        std::filesystem::create_directories(output);
        Module material{LoadLibraryW(L"LumeGlass.dll")};Require(material.value!=nullptr,"LumeGlass.dll unavailable");
        const auto statistics=Function<LumeMaterialStatisticsFn>(material.value,"LumeMaterialStatistics");
        Require(statistics!=nullptr,"material statistics unavailable");
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=Background;wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"LumeIsolatedPresentationBackground";
        Require(RegisterClassExW(&wc)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"background registration failed");
        Window background{CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Lume isolated visual verification",
            WS_POPUP,40,40,960,680,nullptr,nullptr,wc.hInstance,nullptr)};
        Require(background.value!=nullptr,"background window unavailable");
        ShowWindow(background.value,SW_SHOWNOACTIVATE);UpdateWindow(background.value);
        SetWindowPos(background.value,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        Pump(80);
        const unsigned dpi=GetDpiForWindow(background.value);
        Json modes=Json::array();
        for(const bool frosted:{false,true}){
            const auto mode=frosted?"frosted":"liquid",modeOutput=mode;
            const auto target=output/modeOutput;std::filesystem::create_directories(target);
            const lume::widget::Limits limits{340,216,320,204,720,720};
            WidgetWindowPorts ports;ports.showCloseButton=false;
            ports.draw=[](WidgetCanvas& canvas,float,float){
                canvas.Text(L"Native material first frame",{24,20,292,26},18,0xffffff,1,true);
                canvas.Rounded({24,66,112,4},2,0x58b3ff);
                canvas.Text(L"Close restores this patterned background",{24,160,292,20},12);
            };
            NativeWidgetWindow window(wc.hInstance,L"Isolated production widget",limits,ports);
            auto preferences=lume::widget::Defaults(limits);preferences.placed=true;
            preferences.x=120;preferences.y=120;preferences.frosted=frosted;preferences.locked=true;
            Require(window.Configure(preferences),"test widget configure failed");
            const int width=MulDiv(340,static_cast<int>(dpi),96),height=MulDiv(216,static_cast<int>(dpi),96);
            const RECT region{104,104,120+width+16,120+height+16};
            Recorder recorder(target,region);
            while(recorder.count<5){Pump(4);Require(GetTickCount64()-recorder.started<3000,"background capture timed out");}
            recorder.phase=1;
            Require(window.Start(),"production widget Start failed");
            // Raise only the two isolated test windows; user windows/settings are untouched.
            SetWindowPos(window.Handle(),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
            const auto timeout=GetTickCount64()+6000;
            while(window.MaterialState()!=WidgetMaterialState::Ready&&GetTickCount64()<timeout)Pump(4);
            const bool ready=window.MaterialState()==WidgetMaterialState::Ready;
            recorder.phase=2;Pump(250);
            LumeMaterialStatisticsData active{};active.size=sizeof(active);Require(statistics(&active)!=0,"active statistics failed");
            recorder.phase=3;const auto closingAt=GetTickCount64()-recorder.started;
            window.Stop();const auto closedAt=GetTickCount64()-recorder.started;
            recorder.phase=4;Pump(250);recorder.Finish();
            Require(recorder.error.empty(),recorder.error.c_str());
            LumeMaterialStatisticsData closed{};closed.size=sizeof(closed);Require(statistics(&closed)!=0,"closed statistics failed");
            double maximumBlack=0,maximumReadyChange=0,finalChange=1;
            unsigned baselineFrames=0,startingFrames=0,readyFrames=0,closedFrames=0;
            for(const auto& frame:recorder.frames){
                maximumBlack=std::max(maximumBlack,frame.at("nearBlackRatio").get<double>());
                const unsigned phase=frame.at("phaseBefore"),after=frame.at("phaseAfter");
                if(phase==0&&after==0)++baselineFrames;
                if(phase==1||after==1)++startingFrames;
                if(phase==2&&after==2){++readyFrames;maximumReadyChange=std::max(maximumReadyChange,frame.at("changedFromBackgroundRatio").get<double>());}
                if(phase==4&&after==4){++closedFrames;finalChange=frame.at("changedFromBackgroundRatio").get<double>();}
            }
            const bool pass=ready&&baselineFrames>=3&&startingFrames&&readyFrames>=3&&closedFrames>=3&&
                maximumBlack<.01&&maximumReadyChange>.03&&finalChange<.001&&
                active.users==1&&active.devices==1&&closed.users==0&&closed.devices==0&&closed.captures==0;
            modes.push_back({{"mode",mode},{"dpi",dpi},{"width",region.right-region.left},{"height",region.bottom-region.top},
                {"ready",ready},{"passed",pass},{"maximumNearBlackRatio",maximumBlack},{"maximumReadyChange",maximumReadyChange},
                {"finalBackgroundDifferenceRatio",finalChange},{"closingStartedMs",closingAt},{"closeReturnedMs",closedAt},
                {"baselineFrames",baselineFrames},{"startingFrames",startingFrames},{"readyFrames",readyFrames},{"closedFrames",closedFrames},
                {"active",{{"users",active.users},{"devices",active.devices},{"captures",active.captures}}},
                {"closed",{{"users",closed.users},{"devices",closed.devices},{"captures",closed.captures}}},
                {"frames",recorder.frames}});
            std::cout<<mode<<": "<<recorder.frames.size()<<" captured frames; ready="<<ready
                <<" maxBlack="<<maximumBlack<<" finalBackgroundDelta="<<finalChange<<" pass="<<pass<<'\n';
        }
        bool passed=true;for(const auto& mode:modes)passed=passed&&mode.at("passed").get<bool>();
        Json report{{"version",1},{"passed",passed},
            {"scope","Production NativeWidgetWindow Start/Stop and production LumeGlass.dll, on an isolated patterned Win32 background. Compositor-paced screen samples include synchronous Start/Stop."},
            {"limits","Actual monitor DPI only. Sampled frames cannot prove every scanout or all machines. Fixed foreground; no live feature data or Explorer/wallpaper integration acceptance."},
            {"phases",{"background","starting","ready","closing","closed"}},{"modes",modes}};
        std::ofstream stream(output/"presentation-report.json");stream<<report.dump(2);
        Require(stream.good(),"presentation report failed");return passed?0:1;
    }catch(const std::exception& error){std::cerr<<"Presentation capture failed: "<<error.what()<<'\n';return 1;}
}
}
