#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace corner_test {
// Independent 64x64 subpixel area integration; no production clip/shader
// formula is reused. Coordinates are mirrored into the top-left corner.
inline double CirclePixelCoverage(int x,int y,double radius) {
    if(x>=radius||y>=radius)return 1.0;
    const auto square=[](double value){return value*value;};
    if(square(radius-x)+square(radius-y)<=square(radius))return 1.0;
    if(square(std::max(0.0,radius-x-1))+square(std::max(0.0,radius-y-1))>=square(radius))return 0.0;
    unsigned covered=0;
    for(unsigned sy=0;sy<64;++sy)for(unsigned sx=0;sx<64;++sx)
        if(square(x+(sx+.5)/64-radius)+square(y+(sy+.5)/64-radius)<=square(radius))++covered;
    return covered/4096.0;
}
inline bool Unoccluded(HWND window,const RECT& bounds) {
    for(HWND above=GetWindow(window,GW_HWNDPREV);above;above=GetWindow(above,GW_HWNDPREV)) {
        RECT other{},overlap{};DWORD cloaked=0;
        if(!IsWindowVisible(above)||IsIconic(above)||!GetWindowRect(above,&other)||
           !IntersectRect(&overlap,&other,&bounds))continue;
        if(SUCCEEDED(DwmGetWindowAttribute(above,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked)continue;
        wchar_t name[128]{};GetClassNameW(above,name,128);
        std::wcerr<<L"corner fixture is occluded by "<<name<<L", hwnd="<<above<<L'\n';
        return false;
    }
    return true;
}
inline void Diagnose(HWND window, HWND behind, const RECT& bounds, int pass,const char* filename=nullptr) {
    const char* directory=std::getenv("LUME_CORNER_DIAGNOSTICS");
    if(!directory)return;
    const auto path=std::filesystem::path(directory);
    std::ofstream log(path/"samples.log",std::ios::app);
    log<<"tick="<<GetTickCount64()<<" pass="<<pass<<" target="<<window<<" backdrop="<<behind
       <<" rect="<<bounds.left<<','<<bounds.top<<','<<bounds.right<<','<<bounds.bottom
       <<" foreground="<<GetForegroundWindow()<<'\n';
    for(HWND above=GetWindow(window,GW_HWNDPREV);above;above=GetWindow(above,GW_HWNDPREV)) {
        RECT r{},overlap{};
        if(!IsWindowVisible(above)||!GetWindowRect(above,&r)||!IntersectRect(&overlap,&r,&bounds))continue;
        char name[128]{};GetClassNameA(above,name,sizeof(name));DWORD pid=0;GetWindowThreadProcessId(above,&pid);
        log<<"above="<<above<<" class="<<name<<" pid="<<pid
           <<" topmost="<<((GetWindowLongPtrW(above,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0)
           <<" rect="<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom<<'\n';
    }
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(bitmap&&dc) {
        auto old=SelectObject(dc,bitmap);
        BitBlt(dc,0,0,width,height,screen,bounds.left,bounds.top,SRCCOPY);GdiFlush();
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);
        header.bfSize=header.bfOffBits+static_cast<DWORD>(width*height*4);
        std::ofstream out(path/(filename?std::string(filename):std::string("pass-")+std::to_string(pass)+".bmp"),std::ios::binary);
        out.write(reinterpret_cast<const char*>(&header),sizeof(header));
        out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
        out.write(static_cast<const char*>(bits),width*height*4);
        SelectObject(dc,old);
    }
    if(bitmap)DeleteObject(bitmap);
    if(dc)DeleteDC(dc);
    ReleaseDC(nullptr,screen);
}
}

// Sample the real compositor over two known backgrounds. PrintWindow alone
// cannot detect a later hard window-region clip or an opaque WebView backing.
inline bool CheckComposedCorners(HWND window,double radius,HWND occluder=nullptr) {
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumeCornerTestBackdrop";
    wc.lpfnWndProc=[](HWND w,UINT m,WPARAM wp,LPARAM lp)->LRESULT {
        if(m==WM_ERASEBKGND){RECT r{};GetClientRect(w,&r);
            FillRect(reinterpret_cast<HDC>(wp),&r,static_cast<HBRUSH>(GetStockObject(GetWindowLongPtrW(w,GWLP_USERDATA)?WHITE_BRUSH:BLACK_BRUSH)));return 1;}
        return DefWindowProcW(w,m,wp,lp);
    };
    RegisterClassW(&wc);
    RECT r{};GetWindowRect(window,&r);
    HWND behind=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_POPUP,
        r.left,r.top,r.right-r.left,r.bottom-r.top,occluder,nullptr,wc.hInstance,nullptr);
    if(!behind)return false;
    // Establish actual owned-window ordering, not only a best-effort TOPMOST
    // request from a background process. The chain is target > backdrop >
    // test occluder; restore ownership before destroying either fixture.
    const auto originalOwner=GetWindow(window,GW_OWNER);
    const bool wasTopmost=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    SetWindowLongPtrW(window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(behind));
    bool okay=GetWindow(window,GW_OWNER)==behind;
    okay=SetWindowPos(behind,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE&&okay;
    okay=SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE&&okay;
    const int side=std::max(2,static_cast<int>(std::ceil(radius))+2);
    std::vector<COLORREF> black;
    std::ofstream coverage;
    if(const char* directory=std::getenv("LUME_CORNER_DIAGNOSTICS")) {
        coverage.open(std::filesystem::path(directory)/"coverage.csv");
        coverage<<"corner,x,y,radius,reference64x64,composedAlpha,error\n";
    }
    for(int pass=0;pass<2;++pass) {
        SetWindowLongPtrW(behind,GWLP_USERDATA,pass);
        RedrawWindow(behind,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
        DwmFlush();Sleep(80);DwmFlush();
        corner_test::Diagnose(window,behind,r,pass);
        if(!corner_test::Unoccluded(window,r))okay=false;
        HDC screen=GetDC(nullptr);
        auto memory=CreateCompatibleDC(screen);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=side;info.bmiHeader.biHeight=-side;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        auto old=SelectObject(memory,bitmap);
        for(int corner=0;corner<4;++corner) {
            BitBlt(memory,0,0,side,side,screen,corner%2?r.right-side:r.left,corner/2?r.bottom-side:r.top,SRCCOPY);
            GdiFlush();
            int blended=0,clear=0,edgePixels=0,unstable=0;
            double errorSum=0,opaqueBias=0;
            for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
                const auto pixel=static_cast<const unsigned*>(bits)[y*side+x];
                if(!pass)black.push_back(pixel);
                else {
                    const auto before=black[static_cast<std::size_t>(corner*side*side+y*side+x)];
                    const int delta=static_cast<int>(GetRValue(pixel))-static_cast<int>(GetRValue(before));
                    if(delta>3&&delta<252)++blended;
                    if(delta>=252)++clear;
                    const int localX=corner%2?side-1-x:x,localY=corner/2?side-1-y:y;
                    const auto reference=corner_test::CirclePixelCoverage(localX,localY,radius);
                    if(reference>.01&&reference<.99) {
                        const int green=static_cast<int>(GetGValue(pixel))-static_cast<int>(GetGValue(before));
                        const int blue=static_cast<int>(GetBValue(pixel))-static_cast<int>(GetBValue(before));
                        const double alpha=std::clamp(1.0-(delta+green+blue)/765.0,0.0,1.0);
                        const double error=alpha-reference;
                        errorSum+=std::abs(error);opaqueBias+=error;++edgePixels;
                        if(std::max({delta,green,blue})-std::min({delta,green,blue})>8||
                            std::min({delta,green,blue})< -3)++unstable;
                        if(coverage)coverage<<corner<<','<<localX<<','<<localY<<','<<radius<<','
                            <<reference<<','<<alpha<<','<<error<<'\n';
                    }
                }
            }
            if(pass&&(blended<3||clear<1)) {
                std::cerr<<"composited corner "<<corner<<" lacks alpha coverage: blended="<<blended<<" clear="<<clear<<'\n';okay=false;
            }
            if(pass) {
                const double meanError=edgePixels?errorSum/edgePixels:1.0;
                std::cout<<"composited corner "<<corner<<" radius="<<radius<<" blended="<<blended<<" clear="<<clear
                    <<" referenceEdgePixels="<<edgePixels<<" meanAreaError="<<meanError
                    <<" meanOpaqueBias="<<(edgePixels?opaqueBias/edgePixels:1.0)<<" unstable="<<unstable<<'\n';
                if(edgePixels<3||meanError>.10||unstable) {
                    std::cerr<<"composited corner "<<corner<<" differs from independent pixel area coverage\n";okay=false;
                }
            }
        }
        SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
        ReleaseDC(nullptr,screen);
    }
    SetWindowLongPtrW(window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(originalOwner));
    if(!wasTopmost)SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    if(GetWindow(window,GW_OWNER)!=originalOwner)okay=false;
    DestroyWindow(behind);
    return okay;
}
