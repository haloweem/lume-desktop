#include "platform/windows/calendar/calendar_todo_ui.h"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
using namespace lume::platform::windows;
namespace {
struct Dib {
    int width, height;
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=nullptr;
    HGDIOBJ previous=nullptr;
    std::uint32_t* pixels=nullptr;
    Dib(int w,int h):width(w),height(h) {
        BITMAPINFO info{};
        info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};
        void* bits=nullptr;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(bitmap){previous=SelectObject(dc,bitmap);pixels=static_cast<std::uint32_t*>(bits);}
    }
    ~Dib(){if(previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    void Clear(bool white){PatBlt(dc,0,0,width,height,white?WHITENESS:BLACKNESS);}
};
bool whiteMatte=false;
LRESULT CALLBACK Parent(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_CTLCOLOREDIT||message==WM_CTLCOLORSTATIC) {
        const auto dc=reinterpret_cast<HDC>(w);
        SetTextColor(dc,RGB(255,255,255));SetBkColor(dc,whiteMatte?RGB(255,255,255):RGB(0,0,0));
        return reinterpret_cast<LRESULT>(GetStockObject(whiteMatte?WHITE_BRUSH:BLACK_BRUSH));
    }
    return DefWindowProcW(window,message,w,l);
}
int failures=0;
void Check(bool result,const char* message){if(!result){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
// Run the same black/white foreground reconstruction as the glass surface,
// then composite white text over neutral light/dark backgrounds. A neutral
// glyph may have alpha edges, but must never acquire red/blue subpixel fringes.
void CheckText(const Dib& black,const Dib& white,const char* label,unsigned dpi){
    unsigned ink=0,fringes=0,antialiased=0;
    for(int i=0;i<black.width*black.height;++i){
        const auto b=black.pixels[i],w=white.pixels[i];int alpha=0;
        for(const int shift:{0,8,16})alpha=std::max(alpha,255-static_cast<int>((w>>shift)&255)+static_cast<int>((b>>shift)&255));
        alpha=std::clamp(alpha,0,255);
        if(alpha>0)++ink;
        if(alpha>0&&alpha<255)++antialiased;
        for(const int background:{32,192}){
            int channels[3]{};
            for(int channel=0;channel<3;++channel)
                channels[channel]=std::min(static_cast<int>((b>>(channel*8))&255),alpha)+(background*(255-alpha)+127)/255;
            if(*std::max_element(channels,channels+3)-*std::min_element(channels,channels+3)>2)++fringes;
        }
    }
    Check(ink>20,"visible text survives transparent reconstruction");
    Check(antialiased>10,"text retains smooth grayscale coverage");
    if(fringes){++failures;std::cerr<<"FAIL colored text edges: "<<label<<" dpi="<<dpi<<" pixels="<<fringes<<'\n';}
}
}
int main(){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc{};wc.lpfnWndProc=Parent;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumeTodoTextFixture";
    if(!RegisterClassW(&wc))return 2;
    // Native EDIT skips painting beneath a hidden ancestor. Keep this fixture
    // visible but offscreen and nonactivating; never capture the user's desktop.
    const auto owner=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_POPUP|WS_VISIBLE,
        -32000,-32000,900,200,nullptr,nullptr,wc.hInstance,nullptr);
    if(!owner)return 2;
    for(const unsigned dpi:{96u,120u,144u,192u}){
        lume::calendar::Preferences prefs;prefs.textColor.rgb=0xffffff;
        todo_ui::Surface surface(dpi,prefs);
        // Neutral test matte: isolate glyph coverage from intended glass colors.
        DeleteObject(surface.background);surface.background=CreateSolidBrush(RGB(0,0,0));
        const int width=MulDiv(400,static_cast<int>(dpi),96),height=MulDiv(42,static_cast<int>(dpi),96);
        Dib black(width,height),white(width,height);
        if(!black.pixels||!white.pixels)return 2;
        for(const int size:{12,13,20}){
            black.Clear(false);white.Clear(true);
            surface.Label(black.dc,L"新建日程 阅读文字 2026/10/17",{0,0,400,42},DT_LEFT,size);
            surface.Label(white.dc,L"新建日程 阅读文字 2026/10/17",{0,0,400,42},DT_LEFT,size);
            GdiFlush();CheckText(black,white,"label/title",dpi);
        }
        const auto button=surface.Add(owner,owner,L"BUTTON",L"2026/10/17 09:00",102,BS_OWNERDRAW);
        DRAWITEMSTRUCT item{};item.hwndItem=button;item.rcItem={0,0,width,height};
        for(const auto text:{L"2026/10/17 09:00",L"10 分钟前",L"添加"}){
            SetWindowTextW(button,text);
            for(const int size:{11,13}){
                black.Clear(false);white.Clear(true);
                item.hDC=black.dc;surface.Button(item,false,CLR_INVALID,todo_ui::Surface::Appearance::Plain,false,false,size);
                DeleteObject(surface.background);surface.background=CreateSolidBrush(RGB(255,255,255));
                item.hDC=white.dc;surface.Button(item,false,CLR_INVALID,todo_ui::Surface::Appearance::Plain,false,false,size);
                DeleteObject(surface.background);surface.background=CreateSolidBrush(RGB(0,0,0));
                GdiFlush();CheckText(black,white,"button/date",dpi);
            }
        }
        const auto edit=surface.Add(owner,owner,L"EDIT",L"中文内容 ABC 123",101,ES_AUTOHSCROLL);
        MoveWindow(edit,0,0,width,height,FALSE);
        black.Clear(false);white.Clear(true);
        whiteMatte=false;SendMessageW(edit,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(black.dc),PRF_CLIENT|PRF_ERASEBKGND);
        whiteMatte=true;SendMessageW(edit,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(white.dc),PRF_CLIENT|PRF_ERASEBKGND);
        GdiFlush();CheckText(black,white,"native edit",dpi);
        SendMessageW(edit,EM_SETSEL,5,8);SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"XYZ"));
        Check(todo_ui::Text(edit)==L"中文内容 XYZ 123","native selection/replacement remains intact");
        DestroyWindow(edit);DestroyWindow(button);
    }
    DestroyWindow(owner);
    if(failures)return 1;
    std::cout<<"PASS neutral calendar labels/buttons/native inputs on transparent mattes at 96/120/144/192 DPI\n";
}
