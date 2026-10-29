#include "platform/windows/radial/radial_window.h"
#include <objbase.h>
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace lume::platform::windows;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Pump(DWORD milliseconds){const auto end=GetTickCount64()+milliseconds;do{MSG m{};
    while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);
}while(GetTickCount64()<end);}
LRESULT CALLBACK Background(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(hwnd,&paint);RECT r{};GetClientRect(hwnd,&r);
        for(LONG y=0;y<r.bottom;y+=64)for(LONG x=0;x<r.right;x+=64){
            const COLORREF colors[]{RGB(71,120,141),RGB(113,139,102),RGB(133,101,130),RGB(116,128,153)};
            HBRUSH brush=CreateSolidBrush(colors[(x/64+y/64)%4]);RECT cell{x,y,std::min(x+64,r.right),std::min(y+64,r.bottom)};
            FillRect(dc,&cell,brush);DeleteObject(brush);}
        EndPaint(hwnd,&paint);return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
std::vector<unsigned> Capture(RECT region){
    const int width=region.right-region.left,height=region.bottom-region.top;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    Require(bitmap&&memory,"capture allocation");HGDIOBJ old=SelectObject(memory,bitmap);
    Require(BitBlt(memory,0,0,width,height,screen,region.left,region.top,SRCCOPY|CAPTUREBLT)!=FALSE,"isolated capture");
    std::vector<unsigned> result(static_cast<unsigned*>(pixels),static_cast<unsigned*>(pixels)+width*height);
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return result;
}
void Save(const std::filesystem::path& path,const std::vector<unsigned>& pixels,RECT region){
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size()*4);
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=region.right-region.left;
    info.biHeight=region.top-region.bottom;info.biPlanes=1;info.biBitCount=32;
    std::ofstream stream(path,std::ios::binary);stream.write(reinterpret_cast<const char*>(&file),sizeof(file));
    stream.write(reinterpret_cast<const char*>(&info),sizeof(info));stream.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()*4));
    Require(stream.good(),"save screenshot");
}
}
int main(int argc,char** argv){
    HWND background=nullptr;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{
        Require(argc==2,"output directory argument");std::filesystem::path output=std::filesystem::u8path(argv[1]);
        std::filesystem::create_directories(output);
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Background;wc.lpszClassName=L"LumeRadialIsolatedPreview";
        RegisterClassW(&wc);
        background=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,wc.lpszClassName,L"Lume radial isolated preview",
            WS_POPUP,64,64,760,620,nullptr,nullptr,wc.hInstance,nullptr);
        Require(background!=nullptr,"isolated background created");ShowWindow(background,SW_SHOWNOACTIVATE);Pump(100);
        RECT bounds{};GetWindowRect(background,&bounds);
        const auto baseline=Capture(bounds);
        Save(output/"baseline.bmp",baseline,bounds);
        POINT point{(bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2};
        RadialWindow window(wc.hInstance,[](UINT){},[]{});
        for(const bool frosted:{false,true}){
            Require(window.Show(point,frosted),"radial opens");
            const auto deadline=GetTickCount64()+5000;
            while(window.MaterialState()!=WidgetMaterialState::Ready&&GetTickCount64()<deadline)Pump(25);
            Require(window.MaterialState()==WidgetMaterialState::Ready,"real glass frame ready");Pump(100);DwmFlush();
            const auto pixels=Capture(bounds);
            RECT ring{};GetWindowRect(window.Handle(),&ring);
            const POINT center{(ring.left+ring.right)/2,(ring.top+ring.bottom)/2};
            const auto index=static_cast<std::size_t>(center.y-bounds.top)*(bounds.right-bounds.left)+center.x-bounds.left;
            Require((pixels.at(index)&0xffffff)==(baseline.at(index)&0xffffff),"center exactly matches underlying desktop pixels");
            Require(WindowFromPoint(center)==background,"center mouse hit passes to underlying application");
            unsigned changed=0;for(std::size_t i=0;i<pixels.size();++i)if((pixels[i]&0xffffff)!=(baseline[i]&0xffffff))++changed;
            Require(changed>2000,"captured glass ring and labels are nonblank");
            Save(output/(frosted?"radial-frosted.bmp":"radial-liquid.bmp"),pixels,bounds);
            // Refraction lives in a thin band at the ring's rims; the body
            // between icons stays clear instead of swirling the desktop.
            const auto dpi=GetDpiForWindow(window.Handle());
            const auto difference=[&](int radius,float angle){
                const int x=center.x+static_cast<int>(std::sin(angle)*radius*static_cast<int>(dpi)/96);
                const int y=center.y-static_cast<int>(std::cos(angle)*radius*static_cast<int>(dpi)/96);
                const auto at=static_cast<std::size_t>(y-bounds.top)*(bounds.right-bounds.left)+x-bounds.left;
                unsigned value=0;
                for(unsigned shift:{0u,8u,16u})value+=static_cast<unsigned>(std::abs(static_cast<int>((pixels.at(at)>>shift)&255)-static_cast<int>((baseline.at(at)>>shift)&255)));
                return value;
            };
            unsigned rimChanged=0,bodyClear=0;
            for(unsigned i=0;i<10;++i){
                const float angle=(static_cast<float>(i)*36+18)*3.14159265f/180;
                unsigned rim=0;for(int radius=78;radius<=86;radius+=2)rim=std::max(rim,difference(radius,angle));
                if(rim>12)++rimChanged;
                if(difference(118,angle)<=60)++bodyClear;
            }
            std::cout<<"ring rim changed samples="<<rimChanged<<" of 10, clear body samples="<<bodyClear<<" of 10\n";
            Require(rimChanged>=5,"glass refracts at the ring's inner rim");
            Require(frosted||bodyClear>=8,"the liquid ring body between icons is not swirled");
            for(const int slot:{0,1,5}){
                const auto position=lume::radial::SlotPosition(slot);
                SendMessageW(window.Handle(),WM_MOUSEMOVE,0,MAKELPARAM(
                    static_cast<int>(std::lround(position.x*dpi/96)),static_cast<int>(std::lround(position.y*dpi/96))));
                // Synthetic hover is test-local. Do not pump a real-cursor
                // WM_MOUSELEAVE before capturing this deterministic frame.
                DwmFlush();
                const auto hovered=Capture(bounds);
                Require((hovered.at(index)&0xffffff)==(baseline.at(index)&0xffffff),"hover keeps center pixel identical to background");
                unsigned bluer=0;
                for(std::size_t i=0;i<hovered.size();++i){
                    const int beforeBlue=static_cast<int>(pixels[i]&255)-static_cast<int>((pixels[i]>>16)&255);
                    const int afterBlue=static_cast<int>(hovered[i]&255)-static_cast<int>((hovered[i]>>16)&255);
                    if(afterBlue>beforeBlue+20)++bluer;
                }
                std::cout<<"blue sector "<<slot<<" material="<<frosted<<" pixels="<<bluer<<'\n';
                Save(output/(std::string(frosted?"radial-frosted-sector-":"radial-liquid-sector-")+std::to_string(slot)+".bmp"),hovered,bounds);
                Require(bluer>1500,"real blue highlight spans a sector rather than a small icon circle");
            }
            window.Hide();Pump(50);
        }
        window.Stop();DestroyWindow(background);background=nullptr;
        CoUninitialize();std::cout<<"PASS real radial liquid/frosted pixels, transparent center and native click-through\n";return 0;
    }catch(const std::exception& error){if(background)DestroyWindow(background);CoUninitialize();std::cerr<<error.what()<<'\n';return 1;}
}
