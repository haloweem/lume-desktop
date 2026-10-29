#include "platform/windows/webview_page_window.h"
#include "core/ui/original_window_layout.h"
#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <string>
#include "composed_corners_test.h"

namespace {
lume::platform::windows::WebViewPageWindow* verificationWindow = nullptr;
bool smooth=false;
bool handshakeOnly=false;
unsigned verificationRasterDpi=0;
int readyMessages=0,earlyShows=0;
HHOOK showHook=nullptr;
ULONGLONG firstShowTick=0,readyTick=0,fixtureReadyTick=0;
bool firstVisibleHomepage=false;
bool startupWasTopmost=false,startupFixtureReady=false,fixturePreparedDuringCreate=false;
bool firstShowPositionComplete=false,firstShowOrderValid=false;
constexpr UINT_PTR kFixtureOrderSubclass=0x53544152;
HWND startupBackdrop=nullptr,startupOwner=nullptr;
UINT_PTR firstSampleTimer=0,closeTimer=0;
UINT verificationHoldMs=500;
unsigned precedingDesktopFrames=0;
std::vector<COLORREF> beforeShowPixels,firstVisiblePixels;
struct CursorIsolation {
    POINT original{};
    POINT moved{10,10};
    bool active=false;
    explicit CursorIsolation(bool enabled) {
        // Clear taskbar hover overlays before creating the screen fixture;
        // never restore over a pointer movement made by the user.
        if (enabled && GetCursorPos(&original) && SetCursorPos(moved.x,moved.y)) {
            active=true;
            Sleep(600);
        }
    }
    ~CursorIsolation() {
        POINT current{};
        if (active && GetCursorPos(&current) &&
            current.x==moved.x && current.y==moved.y)
            SetCursorPos(original.x,original.y);
    }
};
void PrepareStartupFixture(HWND target);
void CALLBACK CloseVerificationWindow(HWND,UINT,UINT_PTR,DWORD);
LRESULT CALLBACK PreserveFixtureCreationOrder(HWND target,UINT message,WPARAM wp,LPARAM lp,
    UINT_PTR id,DWORD_PTR) {
    if(message==WM_WINDOWPOSCHANGING&&!firstShowPositionComplete) {
        auto* position=reinterpret_cast<WINDOWPOS*>(lp);
        // The initial CreateWindowEx position is finalized after WM_CREATE.
        // WM_SHOWWINDOW is only notification: keep this rule through the
        // first completed SWP_SHOWWINDOW position, even when notification
        // has already been observed. This changes fixture Z order only.
        position->hwndInsertAfter=HWND_TOPMOST;
        position->flags&=~SWP_NOZORDER;
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(target,PreserveFixtureCreationOrder,id);
    const auto result=DefSubclassProc(target,message,wp,lp);
    if(message==WM_WINDOWPOSCHANGED&&!firstShowPositionComplete&&
        (reinterpret_cast<const WINDOWPOS*>(lp)->flags&SWP_SHOWWINDOW)) {
        firstShowOrderValid=IsWindowVisible(target)&&
            (GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0&&
            GetWindow(target,GW_OWNER)==startupBackdrop;
        firstShowPositionComplete=true;
    }
    return result;
}
std::vector<COLORREF> SampleHomepage(HWND target) {
    RECT bounds{};GetWindowRect(target,&bounds);
    std::vector<COLORREF> pixels;
    auto screen=GetDC(nullptr);
    auto memory=CreateCompatibleDC(screen);
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!memory||!bitmap) {
        if(bitmap)DeleteObject(bitmap);
        if(memory)DeleteDC(memory);
        ReleaseDC(nullptr,screen);return pixels;
    }
    auto old=SelectObject(memory,bitmap);
    BitBlt(memory,0,0,width,height,screen,bounds.left,bounds.top,SRCCOPY);GdiFlush();
    // Interior grid excludes the changing test backdrops and corner edges.
    for(int y=1;y<36;++y)for(int x=1;x<64;++x)
        pixels.push_back(static_cast<const COLORREF*>(bits)[height*y/36*width+width*x/64]);
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
    ReleaseDC(nullptr,screen);return pixels;
}
double ChangedFraction(const std::vector<COLORREF>& a,const std::vector<COLORREF>& b) {
    if(a.empty()||a.size()!=b.size())return 1;
    unsigned changed=0;
    for(std::size_t i=0;i<a.size();++i)
        if(std::max({std::abs(static_cast<int>(GetRValue(a[i]))-GetRValue(b[i])),
            std::abs(static_cast<int>(GetGValue(a[i]))-GetGValue(b[i])),
            std::abs(static_cast<int>(GetBValue(a[i]))-GetBValue(b[i]))})>3)++changed;
    return changed/static_cast<double>(a.size());
}
void CALLBACK CaptureFirstVisible(HWND,UINT,UINT_PTR id,DWORD) {
    if(!verificationWindow)return;
    const auto target=verificationWindow->Handle();
    RECT bounds{};GetWindowRect(target,&bounds);
    DwmFlush();
    const auto sampleTick=GetTickCount64();
    const auto sample=SampleHomepage(target);
    // WM_SHOWWINDOW precedes DWM's presentation. An unchanged desktop frame
    // is not a displayed loading frame; keep observing until this HWND first
    // changes the scene, then retain that first frame even if it is blank.
    if(ChangedFraction(sample,beforeShowPixels)<.002&&sampleTick-firstShowTick<400) {
        ++precedingDesktopFrames;return;
    }
    KillTimer(nullptr,id);firstSampleTimer=0;
    firstVisiblePixels=sample;
    unsigned colored=0;
    for(const auto pixel:firstVisiblePixels) {
        const int low=std::min({GetRValue(pixel),GetGValue(pixel),GetBValue(pixel)});
        const int high=std::max({GetRValue(pixel),GetGValue(pixel),GetBValue(pixel)});
        if(pixel!=CLR_INVALID&&high>24&&high-low>12)++colored;
    }
    const double coloredFraction=firstVisiblePixels.empty()?0:colored/static_cast<double>(firstVisiblePixels.size());
    firstVisibleHomepage=IsWindowVisible(target)&&corner_test::Unoccluded(target,bounds)&&coloredFraction>.60;
    corner_test::Diagnose(target,nullptr,bounds,-1,"first-visible.bmp");
    std::cout<<"first visible compositor sample delayMs="<<(sampleTick-firstShowTick)
        <<" precedingDesktopFrames="<<precedingDesktopFrames
        <<" coloredFraction="<<coloredFraction<<" ready="<<readyMessages
        <<" homepage="<<firstVisibleHomepage<<'\n';
}
LRESULT CALLBACK ObserveFirstShow(int code,WPARAM wp,LPARAM lp) {
    if(code>=0) {
        const auto& message=*reinterpret_cast<const CWPSTRUCT*>(lp);
        if(message.message==WM_CREATE||(message.message==WM_SHOWWINDOW&&message.wParam)) {
            wchar_t name[64]{};GetClassNameW(message.hwnd,name,64);
            if(std::wstring(name)==L"LumeOriginalPageWindow") {
                if(message.message==WM_CREATE) {
                    // Run initializes WebView2 only after CreateWindowEx
                    // returns. Prepare the fixture before any page loading,
                    // never in the production Ready -> ShowWindow path.
                    if(!handshakeOnly)PrepareStartupFixture(message.hwnd);
                } else if(!firstShowTick) {
                    if(!readyMessages)++earlyShows;
                    firstShowTick=GetTickCount64();
                    if(!handshakeOnly)firstSampleTimer=SetTimer(nullptr,0,1,CaptureFirstVisible);
                    closeTimer=SetTimer(nullptr,0,verificationHoldMs,CloseVerificationWindow);
                    if(!closeTimer)PostMessageW(message.hwnd,WM_CLOSE,0,0);
                }
            }
        }
    }
    return CallNextHookEx(showHook,code,wp,lp);
}
HWND CreateOccluder(HWND target) {
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumeCornerTestOccluder";
    wc.lpfnWndProc=[](HWND window,UINT message,WPARAM wp,LPARAM lp)->LRESULT {
        if(message==WM_ERASEBKGND) {
            RECT r{};GetClientRect(window,&r);auto brush=CreateSolidBrush(RGB(214,30,160));
            FillRect(reinterpret_cast<HDC>(wp),&r,brush);DeleteObject(brush);return 1;
        }
        return DefWindowProcW(window,message,wp,lp);
    };
    RegisterClassW(&wc);
    RECT r{};GetWindowRect(target,&r);
    auto window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
        wc.lpszClassName,L"",WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,
        nullptr,nullptr,wc.hInstance,nullptr);
    ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
    return window;
}
void PrepareStartupFixture(HWND target) {
    fixturePreparedDuringCreate=true;
    startupWasTopmost=(GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    startupOwner=GetWindow(target,GW_OWNER);
    startupBackdrop=CreateOccluder(target);
    if(!startupBackdrop)return;
    const bool orderHook=SetWindowSubclass(target,PreserveFixtureCreationOrder,kFixtureOrderSubclass,0)!=FALSE;
    SetWindowLongPtrW(target,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(startupBackdrop));
    startupFixtureReady=SetWindowPos(target,HWND_TOPMOST,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE&&orderHook&&
        GetWindow(target,GW_OWNER)==startupBackdrop;
    // Present the fixed test backdrop while the target is still being
    // created. WebView2 does not exist yet, so this cannot give its renderer
    // extra time after ready or alter the production first-show ordering.
    bool backdropPresented=false;
    for(int attempt=0;attempt<8&&!backdropPresented;++attempt) {
        DwmFlush();
        beforeShowPixels=SampleHomepage(target);
        backdropPresented=!beforeShowPixels.empty()&&std::all_of(
            beforeShowPixels.begin(),beforeShowPixels.end(),
            [](COLORREF color){return (color&0xffffff)==0xd61ea0;});
    }
    startupFixtureReady=startupFixtureReady&&backdropPresented&&!IsWindowVisible(target);
    std::cout<<"startup fixture ready="<<startupFixtureReady<<" backdropPresented="<<backdropPresented
        <<" targetVisible="<<IsWindowVisible(target)<<" orderHook="<<orderHook<<'\n';
    fixtureReadyTick=GetTickCount64();
    RECT bounds{};GetWindowRect(target,&bounds);
    corner_test::Diagnose(target,nullptr,bounds,-2,"before-show.bmp");
}
void CALLBACK CloseVerificationWindow(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);closeTimer=0;
    if(firstSampleTimer){KillTimer(nullptr,firstSampleTimer);firstSampleTimer=0;}
    if (verificationWindow) {
        if(handshakeOnly) {
            verificationWindow->RequestClose();
            return;
        }
        const auto target=verificationWindow->Handle();
        DwmFlush();
        const auto settled=SampleHomepage(target);
        const double changedFraction=ChangedFraction(settled,firstVisiblePixels);
        if(changedFraction>.02)firstVisibleHomepage=false;
        std::cout<<"first visible versus settled homepage changedFraction="<<changedFraction<<'\n';
        // A normal HWND_TOP cannot establish a screen-sampling fixture over
        // a topmost notification/tool window. Keep that regression reproducible.
        auto occluder=CreateOccluder(target);
        const auto originalTopmost=GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST;
        RECT client{};GetClientRect(target,&client);
        const double scale=(verificationRasterDpi?verificationRasterDpi:GetDpiForWindow(target))/96.0;
        const auto zoom=lume::ui::CalculateOriginalZoom(static_cast<int>(std::lround(client.right/scale)),
            static_cast<int>(std::lround(client.bottom/scale)));
        smooth=CheckComposedCorners(target,34.0*zoom*scale,occluder);
        std::cout<<"corner render DPI="<<(verificationRasterDpi?verificationRasterDpi:GetDpiForWindow(target))
            <<" physical monitor DPI="<<GetDpiForWindow(target)<<" client="<<client.right<<'x'<<client.bottom<<'\n';
        if(!occluder||(GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)!=originalTopmost) {
            std::cerr<<"compositor fixture must create its occluder and restore the target's topmost state\n";
            smooth=false;
        }
        if(occluder)DestroyWindow(occluder);
        RemoveWindowSubclass(target,PreserveFixtureCreationOrder,kFixtureOrderSubclass);
        SetWindowLongPtrW(target,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(startupOwner));
        SetWindowPos(target,startupWasTopmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        if(GetWindow(target,GW_OWNER)!=startupOwner||
            ((GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0)!=startupWasTopmost)
            smooth=false;
        if(startupBackdrop){DestroyWindow(startupBackdrop);startupBackdrop=nullptr;}
        verificationWindow->RequestClose();
    }
}
}

int main(int argc,char** argv) {
    using namespace lume::platform::windows;
    if(argc==2&&std::string(argv[1])=="--handshake-only")handshakeOnly=true;
    else if(argc==3&&std::string(argv[1])=="--dpi"&&
        (std::string(argv[2])=="96"||std::string(argv[2])=="120"||std::string(argv[2])=="144"||std::string(argv[2])=="192"))
        verificationRasterDpi=static_cast<unsigned>(std::atoi(argv[2]));
    else if(argc!=1) {
        std::cerr<<"Usage: lume-webview-page-window-test [--handshake-only | --dpi 96|120|144|192]\n";
        return 2;
    }
    if(handshakeOnly)
        std::cout<<"visual-corner-screen-sampling=NOT_VERIFIED; SKIPPED by explicit --handshake-only mode\n";
    const char* requestedHold=std::getenv("LUME_VERIFY_HOLD_MS");
    verificationHoldMs=requestedHold?static_cast<UINT>(std::clamp(std::atoi(requestedHold),500,10000)):500;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CursorIsolation cursorIsolation(!handshakeOnly);
    WebViewPageWindow window;
    verificationWindow = &window;
    showHook=SetWindowsHookExW(WH_CALLWNDPROC,ObserveFirstShow,nullptr,GetCurrentThreadId());
    if(!showHook){std::cerr<<"first-show observation hook failed\n";return 1;}
    int readyCount = 0;
    const int result = window.Run(GetModuleHandleW(nullptr), [&](lume::ui::PageCommand command) {
        if (command == lume::ui::PageCommand::Ready) {
            ++readyCount;
            ++readyMessages;
            // Observation only: no compositor waits, diagnostics I/O,
            // fixture changes or timers before production ShowWindow.
            readyTick=GetTickCount64();
        }
        return std::wstring(L"{\"version\":1,\"type\":\"settings\",\"clock\":{\"enabled\":false,\"style\":\"mond\"},\"saved\":true}");
    },true,{},verificationRasterDpi);
    UnhookWindowsHookEx(showHook);showHook=nullptr;
    if(firstSampleTimer){KillTimer(nullptr,firstSampleTimer);firstSampleTimer=0;}
    if(closeTimer){KillTimer(nullptr,closeTimer);closeTimer=0;}
    if(startupBackdrop){DestroyWindow(startupBackdrop);startupBackdrop=nullptr;}
    verificationWindow = nullptr;
    std::cout<<"startup ordering fixtureAtCreate="<<fixturePreparedDuringCreate
        <<" fixtureTick="<<fixtureReadyTick<<" readyTick="<<readyTick
        <<" firstShowTick="<<firstShowTick<<" readyToShowMs="<<(firstShowTick>=readyTick?firstShowTick-readyTick:0)
        <<" showPositionComplete="<<firstShowPositionComplete<<" showOrderValid="<<firstShowOrderValid<<'\n';
    if (result != 0 || readyCount != 1 || earlyShows ||
        (!handshakeOnly&&(!smooth||!firstVisibleHomepage||!startupFixtureReady||
            !fixturePreparedDuringCreate||fixtureReadyTick>readyTick||
            !firstShowPositionComplete||!firstShowOrderValid))) {
        std::cerr << "WebView page must first show its ready homepage: result=" << result << " ready=" << readyCount
                  << " earlyShows="<<earlyShows<<" firstVisibleHomepage="<<firstVisibleHomepage
                  << " smooth="<<smooth<<" startupFixtureReady="<<startupFixtureReady<<'\n';
        return 1;
    }
    std::cout << "Real WebView2 page ready and clean close passed\n";
}
