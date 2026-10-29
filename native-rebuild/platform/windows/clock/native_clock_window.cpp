#include "platform/windows/clock/native_clock_window.h"
#include "platform/windows/clock/clock_window_bounds.h"
#include "platform/windows/clock/clock_renderer.h"
#include "platform/windows/widget_ui/StdAfx.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/widget_ui/LumeWidgetColorPicker.h"
#include "platform/windows/widget_ui/LumeWidgetPositionEditor.h"
#include "platform/windows/widget_ui/resource.h"
#include "platform/windows/widget_ui/widget_snap_protocol.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include <algorithm>
#include <vector>
#include <cstring>
#include <utility>

namespace lume::platform::windows {
namespace {
constexpr wchar_t kClass[]=L"LumeNativeDesktopClock";
constexpr UINT_PTR kMinuteTimer=1;
constexpr UINT_PTR kSnapTimer=2;
constexpr UINT_PTR kDragSnapTimer=3;
constexpr UINT kRefreshDpi=WM_APP+0x471;
// HWND_BOTTOM alone can put the clock behind Explorer's opaque desktop host.
// Insert immediately above that host, below ordinary application windows.
void ShowAtDesktopLevel(HWND window) { desktop_level::Place(window,true); }
class MenuGraphics {
public:
    MenuGraphics() { Gdiplus::GdiplusStartupInput input; valid=Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok; }
    ~MenuGraphics() { if(valid) Gdiplus::GdiplusShutdown(token); }
    bool valid=false;
private:
    ULONG_PTR token=0;
};
}

NativeClockWindow::NativeClockWindow(HINSTANCE instance,std::function<void(clock::Action)> handler)
    : instance_(instance),handler_(std::move(handler)) {}
NativeClockWindow::~NativeClockWindow() { *alive_=false; Dispose(); }

bool NativeClockWindow::Configure(clock::Style style,const clock::Appearance& appearance) {
    if (disposed_ || clock::Index(style)>2 || !clock::ValidAppearance(appearance)) return false;
    const auto oldAppearance=appearance_;
    const auto oldStyle=style_;
    appearance_=appearance;
    style_=style;
    if(window_&&appearance_.locked){
        dragging_=false;
        if(GetCapture()==window_)ReleaseCapture();
        ClearSnapGuides();
    }
    if (window_ && !suspended_ && !Paint()) {
        appearance_=oldAppearance; style_=oldStyle;
        return false;
    }
    return true;
}

bool NativeClockWindow::Start() {
    if(disposed_) return false;
    if(window_) { if(suspended_) Resume(); return IsRunning(); }
    renderer_=std::make_unique<ClockRenderer>();
    if(!renderer_->Initialize()) { renderer_.reset(); return false; }
    WNDCLASSEXW cls{};
    cls.cbSize=sizeof(cls); cls.hInstance=instance_; cls.lpfnWndProc=WindowProc;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.lpszClassName=kClass;
    if(!RegisterClassExW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) { renderer_.reset(); return false; }
    suspended_=false;
    window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,kClass,L"Lume Clock",WS_POPUP,
        appearance_.x,appearance_.y,1,1,nullptr,nullptr,instance_,this);
    if(!window_) { renderer_.reset(); return false; }
    SetPropW(window_,widget_snap::Capability,reinterpret_cast<HANDLE>(1));
    dragSnap_.Bind(window_);
    dpi_=GetDpiForWindow(window_);
    if(!dpi_) dpi_=96;
    if(!Paint()) { Stop(); return false; }
    ShowAtDesktopLevel(window_);
    ArmMinuteTimer();
    return true;
}

void NativeClockWindow::Stop() {
    ClearSnapGuides();
    if(window_) {
        const HWND closing=window_;
        KillTimer(closing,kMinuteTimer);
        KillTimer(closing,kSnapTimer);
        KillTimer(closing,kDragSnapTimer);
        dragging_=false;
        if(GetCapture()==closing) ReleaseCapture();
        DestroyWindow(closing);
        window_=nullptr;
    }
    renderer_.reset();
    snapEdges_=0;snapSource_=nullptr;
    suspended_=false;
}
void NativeClockWindow::ClearSnapGuides() {
    dragSnap_.Clear();snapEdges_=0;snapSource_=nullptr;
    if(window_){KillTimer(window_,kSnapTimer);KillTimer(window_,kDragSnapTimer);}
}
void NativeClockWindow::Suspend() {
    if(!window_ || suspended_) return;
    dragging_=false;if(GetCapture()==window_)ReleaseCapture();
    ClearSnapGuides();widget_snap::SetEdges(window_,0);
    suspended_=true;
    KillTimer(window_,kMinuteTimer);
    ShowWindow(window_,SW_HIDE);
    renderer_.reset();
}
void NativeClockWindow::Resume() {
    if(!window_ || !suspended_ || disposed_) return;
    renderer_=std::make_unique<ClockRenderer>();
    if(!renderer_->Initialize()) { renderer_.reset(); return; }
    suspended_=false;
    if(!Paint()) { suspended_=true; renderer_.reset(); return; }
    ShowAtDesktopLevel(window_);
    ArmMinuteTimer();
}
void NativeClockWindow::Dispose() { Stop(); disposed_=true; handler_={}; }

POINT NativeClockWindow::Constrain(POINT position,SIZE size) const {
    position.x=std::clamp(position.x,-32768L,32767L);
    position.y=std::clamp(position.y,-32768L,32767L);
    RECT bounds{position.x,position.y,position.x+size.cx,position.y+size.cy};
    // A transparent canvas can cross monitor edges. Only recover positions
    // without a usable grip on any connected display, including negative origins.
    struct Visibility {RECT bounds;SIZE grip;bool reachable=false;};
    Visibility visibility{bounds,ClockGrip(size,dpi_),false};
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL {
        auto& view=*reinterpret_cast<Visibility*>(data);
        MONITORINFO info{};info.cbSize=sizeof(info);
        if(GetMonitorInfoW(monitor,&info)&&ClockHasGrip(view.bounds,info.rcWork,view.grip)){
            view.reachable=true;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&visibility));
    if(visibility.reachable)return position;
    MONITORINFO info{}; info.cbSize=sizeof(info);
    if(GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&info)) {
        position=RecoverClockPosition(position,size,info.rcWork,visibility.grip);
    }
    position.x=std::clamp(position.x,-32768L,32767L);
    position.y=std::clamp(position.y,-32768L,32767L);
    return position;
}

bool NativeClockWindow::Paint() {
    if(!window_ || !renderer_ || suspended_) return false;
    SYSTEMTIME time{}; GetLocalTime(&time);
    ClockBitmap bitmap;
    if(!renderer_->Render(style_,appearance_,time,dpi_,bitmap)) return false;
    const auto guides=snapEdges_|dragSnap_.Edges();
    widget_snap::DrawBitmapEdges(bitmap.pixels,bitmap.width,bitmap.height,guides,static_cast<unsigned>(std::max(1,MulDiv(2,static_cast<int>(dpi_),96))));
    widget_snap::SetEdges(window_,guides);
    size_={static_cast<LONG>(bitmap.width),static_cast<LONG>(bitmap.height)};
    contentOffset_={bitmap.originX,bitmap.originY};
    POINT position=Constrain({appearance_.x-contentOffset_.x,appearance_.y-contentOffset_.y},size_);
    if(dragging_) { RECT bounds{}; GetWindowRect(window_,&bounds); position={bounds.left,bounds.top}; }
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=size_.cx; info.bmiHeader.biHeight=-size_.cy;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr);
    HDC memory=CreateCompatibleDC(screen);
    void* pixels=nullptr;
    HBITMAP dib=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    bool success=false;
    if(memory && dib && pixels) {
        std::memcpy(pixels,bitmap.pixels.data(),bitmap.pixels.size()*4);
        // A nearly transparent hit surface lets dragging/right-click work between
        // the glyphs, but only close to them: the rest of the window's box stays
        // fully clear, so the Dock or desktop icons under it keep their clicks.
        // A user-selected alpha of zero remains completely invisible/click-through.
        if (!appearance_.customColor || appearance_.alpha != 0) {
            auto* hitPixels=static_cast<std::uint32_t*>(pixels);
            const int w=size_.cx,h=size_.cy,reach=std::max(1,MulDiv(8,static_cast<int>(dpi_),96));
            // Box dilation of the drawn pixels: rows first, then columns.
            std::vector<unsigned char> drawn(static_cast<std::size_t>(w)*h),close(drawn.size());
            for (std::size_t index=0;index<drawn.size();++index) drawn[index]=(hitPixels[index]>>24)!=0;
            for (int y=0;y<h;++y) {
                int last=-reach-1;
                for (int x=0;x<w;++x) if (drawn[static_cast<std::size_t>(y)*w+x]) last=x;
                    else if (x-last<=reach) close[static_cast<std::size_t>(y)*w+x]=1;
                last=w+reach+1;
                for (int x=w-1;x>=0;--x) if (drawn[static_cast<std::size_t>(y)*w+x]) last=x;
                    else if (last-x<=reach) close[static_cast<std::size_t>(y)*w+x]=1;
            }
            for (std::size_t index=0;index<drawn.size();++index) drawn[index]=drawn[index]||close[index];
            std::fill(close.begin(),close.end(),static_cast<unsigned char>(0));
            for (int x=0;x<w;++x) {
                int last=-reach-1;
                for (int y=0;y<h;++y) if (drawn[static_cast<std::size_t>(y)*w+x]) last=y;
                    else if (y-last<=reach) close[static_cast<std::size_t>(y)*w+x]=1;
                last=h+reach+1;
                for (int y=h-1;y>=0;--y) if (drawn[static_cast<std::size_t>(y)*w+x]) last=y;
                    else if (last-y<=reach) close[static_cast<std::size_t>(y)*w+x]=1;
            }
            for (std::size_t index=0;index<drawn.size();++index)
                if ((hitPixels[index]>>24)==0 && (drawn[index]||close[index])) hitPixels[index]=0x01000000u;
        }
        const HGDIOBJ previous=SelectObject(memory,dib);
        POINT source{};
        BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        success=UpdateLayeredWindow(window_,screen,&position,&size_,memory,&source,0,&blend,ULW_ALPHA)!=FALSE;
        SelectObject(memory,previous);
    }
    if(dib) DeleteObject(dib);
    if(memory) DeleteDC(memory);
    if(screen) ReleaseDC(nullptr,screen);
    return success;
}

void NativeClockWindow::ArmMinuteTimer() {
    if(!window_ || suspended_) return;
    SYSTEMTIME time{}; GetLocalTime(&time);
    const UINT delay=(60u-time.wSecond)*1000u-time.wMilliseconds;
    SetTimer(window_,kMinuteTimer,std::max(1u,delay),nullptr);
}

void NativeClockWindow::Dispatch(clock::Action action) {
    // Copy before invoking: accepted actions may synchronously stop/dispose this window.
    const auto handler=handler_;
    if(handler) handler(action);
}

void NativeClockWindow::Center() {
    const POINT current{appearance_.x,appearance_.y};
    const HMONITOR monitor=window_ ? MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST) : MonitorFromPoint(current,MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{}; info.cbSize=sizeof(info);
    if(!GetMonitorInfoW(monitor,&info)) return;
    const POINT position=Constrain({info.rcWork.left+(info.rcWork.right-info.rcWork.left-size_.cx)/2,
                                  info.rcWork.top+(info.rcWork.bottom-info.rcWork.top-size_.cy)/2},size_);
    Dispatch({clock::ActionType::Position,static_cast<int>(std::clamp(position.x+contentOffset_.x,-32768L,32767L)),
              static_cast<int>(std::clamp(position.y+contentOffset_.y,-32768L,32767L))});
}

void NativeClockWindow::ShowMenu(POINT position) {
    if(menuOpen_ || !window_) return;
    MenuGraphics graphics;
    if(!graphics.valid) return;
    menuOpen_=true;
    const auto alive=alive_;
    const HWND owner=window_;
    const UINT command=LumeWidgetMenu::Show(owner,position,appearance_.x,appearance_.y,appearance_.scale,
                                          appearance_.locked,static_cast<int>(style_));
    if(!*alive) return;
    if(window_!=owner || !IsWindow(owner)) { menuOpen_=false; return; }
    clock::Action action{};
    bool accepted=true;
    if(command>=15 && command<=17) action={clock::ActionType::SelectStyle,static_cast<int>(command)-15};
    else if(command>=IDM_LUME_WIDGET_SIZE_FIRST && command<=IDM_LUME_WIDGET_SIZE_LAST)
        action={clock::ActionType::Scale,50+(static_cast<int>(command)-IDM_LUME_WIDGET_SIZE_FIRST)*10};
    else if(command==IDM_CLOSESKIN) action={clock::ActionType::Enable,0};
    else if(command==IDM_LUME_WIDGET_LOCK) action={clock::ActionType::Lock,appearance_.locked ? 0 : 1};
    else if(command==11) action={clock::ActionType::OpenSettings};
    else if(command==IDM_LUME_WIDGET_POSITION) {
        int x=appearance_.x,y=appearance_.y;
        accepted=LumeWidgetPositionEditor::Show(owner,x,y);
        action={clock::ActionType::Position,x,y};
    } else if(command==IDM_LUME_WIDGET_COLOR) {
        LumeWidgetColor color{static_cast<BYTE>((appearance_.color>>16)&255),static_cast<BYTE>((appearance_.color>>8)&255),
                              static_cast<BYTE>(appearance_.color&255),static_cast<BYTE>(appearance_.alpha)};
        accepted=LumeWidgetColorPicker::Show(owner,color);
        action={clock::ActionType::Color,(color.red<<16)|(color.green<<8)|color.blue,color.alpha};
    } else if(command==IDM_LUME_WIDGET_CENTER) {
        menuOpen_=false; Center(); return;
    } else accepted=false;
    if(!*alive) return;
    menuOpen_=false;
    if(accepted && window_==owner && IsWindow(owner)) Dispatch(action);
}

LRESULT CALLBACK NativeClockWindow::WindowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto* self=reinterpret_cast<NativeClockWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        self=static_cast<NativeClockWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->window_=window;
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    if(!self) return DefWindowProcW(window,message,wparam,lparam);
    if(message==WM_WINDOWPOSCHANGING) desktop_level::Changing(lparam);  // stays at desktop level when activated
    if(message==desktop_level::Message()) {
        if(!self->suspended_&&IsWindowVisible(window)) desktop_level::Place(window,false);
        return 0;
    }
    switch(message) {
    case widget_snap::CancelMessage: {
        const auto edges=self->snapEdges_|self->dragSnap_.Edges();
        self->ClearSnapGuides();
        if(edges)self->Paint();
        return 0;
    }
    case widget_snap::Message: {
        const auto source=reinterpret_cast<HWND>(lparam);
        const auto edges=static_cast<unsigned>(wparam)&15u;
        if(!edges&&self->snapSource_!=source)return 0;
        const bool changed=self->snapEdges_!=edges;
        self->snapEdges_=edges;self->snapSource_=edges?source:nullptr;KillTimer(window,kSnapTimer);
        if(wparam&widget_snap::Linger)SetTimer(window,kSnapTimer,260,nullptr);
        if(changed)self->Paint();
        return 0;
    }
    case WM_MOUSEACTIVATE: desktop_level::Select(window);return MA_ACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        if(wparam==kDragSnapTimer){KillTimer(window,kDragSnapTimer);self->dragSnap_.Clear();self->Paint();return 0;}
        if(wparam==kSnapTimer){KillTimer(window,kSnapTimer);self->snapEdges_=0;self->snapSource_=nullptr;self->Paint();return 0;}
        if(wparam==kMinuteTimer) { KillTimer(window,kMinuteTimer); self->Paint(); self->ArmMinuteTimer(); }
        return 0;
    case WM_TIMECHANGE: self->Paint(); self->ArmMinuteTimer(); return 0;
    case WM_DISPLAYCHANGE: self->Paint(); return 0;
    case WM_DPICHANGED: self->dpi_=HIWORD(wparam); PostMessageW(window,kRefreshDpi,0,0); return 0;
    case kRefreshDpi: self->Paint(); return 0;
    case WM_LBUTTONDOWN:
        if(!self->appearance_.locked && !self->menuOpen_) {
            widget_snap::BeginInteraction(window);
            RECT bounds{}; GetWindowRect(window,&bounds);
            GetCursorPos(&self->dragCursor_); self->dragOrigin_={bounds.left,bounds.top};
            self->dragging_=true; SetCapture(window);
        }
        return 0;
    case WM_MOUSEMOVE:
        if(self->dragging_&&!self->appearance_.locked&&!self->suspended_&&GetCapture()==window) {
            POINT cursor{}; GetCursorPos(&cursor);
            const auto position=self->Constrain({self->dragOrigin_.x+cursor.x-self->dragCursor_.x,
                                                self->dragOrigin_.y+cursor.y-self->dragCursor_.y},self->size_);
            RECT moving{position.x,position.y,position.x+self->size_.cx,position.y+self->size_.cy};
            SendMessageW(window,WM_MOVING,0,reinterpret_cast<LPARAM>(&moving));
            SetWindowPos(window,nullptr,moving.left,moving.top,0,0,SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOZORDER);
        }
        return 0;
    case WM_MOVING:
        if(!self->appearance_.locked&&!self->suspended_) {
            const auto edges=self->dragSnap_.Edges();
            self->dragSnap_.Move(*reinterpret_cast<RECT*>(lparam));
            if(edges!=self->dragSnap_.Edges())self->Paint();
            return TRUE;
        }
        break;
    case WM_LBUTTONUP:
        if(self->dragging_) {
            self->dragging_=false; ReleaseCapture();
            self->dragSnap_.Clear(true);
            if(self->dragSnap_.Edges())SetTimer(window,kDragSnapTimer,260,nullptr);
            RECT bounds{}; GetWindowRect(window,&bounds);
            self->Dispatch({clock::ActionType::Position,static_cast<int>(std::clamp(bounds.left+self->contentOffset_.x,-32768L,32767L)),
                           static_cast<int>(std::clamp(bounds.top+self->contentOffset_.y,-32768L,32767L))});
        }
        return 0;
    case WM_CAPTURECHANGED:
        if(self->dragging_) { self->dragging_=false;self->dragSnap_.Clear();self->Paint(); }
        return 0;
    case WM_CONTEXTMENU: {
        POINT position{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        if(position.x==-1 && position.y==-1) GetCursorPos(&position);
        self->ShowMenu(position); return 0;
    }
    case WM_CLOSE: self->Dispatch({clock::ActionType::Enable,0}); return 0;
    case WM_NCDESTROY:
        desktop_level::Forget(window);
        KillTimer(window,kMinuteTimer);
        KillTimer(window,kSnapTimer);
        KillTimer(window,kDragSnapTimer);self->dragSnap_.Clear();
        RemovePropW(window,widget_snap::Capability);
        RemovePropW(window,widget_snap::ActiveEdges);
        SetWindowLongPtrW(window,GWLP_USERDATA,0);
        self->window_=nullptr;
        break;
    default: break;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
} // namespace lume::platform::windows
