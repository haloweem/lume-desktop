#include "platform/windows/rounded_window_surface.h"
#include <dwmapi.h>
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace lume::platform::windows {
namespace {
constexpr UINT_PTR kRoundedSubclass=0x4c756d65;
float Distance(float x,float y,float width,float height,float radius) {
    const float dx=std::max({radius-x,x-(width-radius),0.f});
    const float dy=std::max({radius-y,y-(height-radius),0.f});
    return std::hypot(dx,dy);
}
LRESULT CALLBACK RoundedProc(HWND w,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    if(message==WM_NCDESTROY)RemoveWindowSubclass(w,RoundedProc,id);
    const auto result=DefSubclassProc(w,message,wp,lp);
    if(message==WM_NCHITTEST&&result==HTCLIENT&&data) {
        RECT r{};GetClientRect(w,&r);
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);
        const float radius=static_cast<float>(data)/1024.f;
        if(Distance(static_cast<float>(p.x)+.5f,static_cast<float>(p.y)+.5f,
                    static_cast<float>(r.right),static_cast<float>(r.bottom),radius)>radius+.5f)return HTTRANSPARENT;
    }
    return result;
}
}
void ConfigureRoundedWindow(HWND window,float radius,bool fullClient) {
    const int rim=fullClient?-1:static_cast<int>(std::ceil(radius))+1;
    const MARGINS margins{rim,rim,rim,rim};
    DwmExtendFrameIntoClientArea(window,&margins);
    // Windows 11 must not apply a second, differently sized corner clip.
    const int noSystemRounding=1;
    DwmSetWindowAttribute(window,static_cast<DWMWINDOWATTRIBUTE>(33),&noSystemRounding,sizeof(noSystemRounding));
    SetWindowSubclass(window,RoundedProc,kRoundedSubclass,static_cast<DWORD_PTR>(std::max(0.f,radius)*1024));
}
RoundedWindowPaint::RoundedWindowPaint(HDC target,int width,int height):target_(target),width_(width),height_(height) {
    if(width<=0||height<=0)return;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;
    bitmap_=CreateDIBSection(target,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap_)return;
    memory_=CreateCompatibleDC(target);
    if(!memory_){DeleteObject(bitmap_);bitmap_=nullptr;return;}
    old_=SelectObject(memory_,bitmap_);pixels_=static_cast<unsigned*>(bits);
}
RoundedWindowPaint::~RoundedWindowPaint() {
    if(memory_){SelectObject(memory_,old_);DeleteDC(memory_);}
    if(bitmap_)DeleteObject(bitmap_);
}
void RoundedWindowPaint::Present(float radius) {
    if(!memory_)return;
    GdiFlush();
    // GDI may zero alpha. The content is opaque; only geometric edge coverage
    // is transparent. Premultiply once here, at the composition boundary.
    for(int i=0;i<width_*height_;++i)pixels_[i]|=0xff000000u;
    radius=std::clamp(radius,0.f,static_cast<float>(std::min(width_,height_))/2);
    const int corner=static_cast<int>(std::ceil(radius));
    for(int y=0;y<corner;++y)for(int x=0;x<corner;++x) {
        const auto alpha=static_cast<unsigned>(std::lround(255.f*std::clamp(radius+.5f-
            Distance(x+.5f,y+.5f,static_cast<float>(width_),static_cast<float>(height_),radius),0.f,1.f)));
        for(int row:{y,height_-1-y})for(int col:{x,width_-1-x}) {
            auto &pixel=pixels_[row*width_+col];
            const auto red=(((pixel>>16)&255u)*alpha+127)/255;
            const auto green=(((pixel>>8)&255u)*alpha+127)/255;
            const auto blue=((pixel&255u)*alpha+127)/255;
            pixel=(alpha<<24)|(red<<16)|(green<<8)|blue;
        }
    }
    BitBlt(target_,0,0,width_,height_,memory_,0,0,SRCCOPY);
}
}
