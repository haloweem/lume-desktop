#pragma once
#include <windows.h>

namespace lume::platform::windows {
// DWM composes coverage alpha; the caller continues to own layout and content.
// fullClient is for transparent WebView content; native controls use only a rim.
void ConfigureRoundedWindow(HWND window,float radiusPixels,bool fullClient=false);

// One temporary physical-pixel DIB per paint; no retained surface or timer.
// Normal GDI text/child controls remain usable in the opaque native interior.
class RoundedWindowPaint {
public:
    RoundedWindowPaint(HDC target,int width,int height);
    ~RoundedWindowPaint();
    RoundedWindowPaint(const RoundedWindowPaint&)=delete;
    RoundedWindowPaint& operator=(const RoundedWindowPaint&)=delete;
    HDC Dc() const {return memory_?memory_:target_;}
    void Present(float radiusPixels);
private:
    HDC target_=nullptr,memory_=nullptr;
    HBITMAP bitmap_=nullptr;
    HGDIOBJ old_=nullptr;
    unsigned* pixels_=nullptr;
    int width_=0,height_=0;
};
}
