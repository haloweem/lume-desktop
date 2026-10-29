#include "platform/windows/radial/radial_window.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/radial/radial_shortcut.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/material/annular_region.h"
#include "platform/windows/widget_ui/layered_surface.h"
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace lume::platform::windows {
namespace {
constexpr wchar_t ClassName[]=L"LumeRadialMenu";
constexpr float Center=radial::Diameter/2,Inner=radial::InnerRadius,Outer=radial::OuterRadius;
void ScreenshotIcon(WidgetCanvas& canvas,float x,float y){
    constexpr float size=9,tip=5;
    for(const float sign:{-1.f,1.f}){
        canvas.Line(x-size,y+sign*size,x-size+tip,y+sign*size,0xffffff,.95f,1.5f);
        canvas.Line(x+size,y+sign*size,x+size-tip,y+sign*size,0xffffff,.95f,1.5f);
        canvas.Line(x+sign*size,y-size,x+sign*size,y-size+tip,0xffffff,.95f,1.5f);
        canvas.Line(x+sign*size,y+size,x+sign*size,y+size-tip,0xffffff,.95f,1.5f);
    }
}
}
void RadialWindow::Draw(WidgetCanvas& canvas,int hover){
    if(hover>=0&&hover<10)
        canvas.AnnularSector(Center,Center,Inner,Outer,hover*36.f-18,36,0x328dff,.40f);
    for(int i=0;i<10;++i){
        const auto point=radial::SlotPosition(i);
        const auto x=point.x,y=point.y;
        if(i==radial::ScreenshotSlot)ScreenshotIcon(canvas,x,y-6);
        else if(i==1)canvas.Text(L"A",{x-10,y-18,20,22},18,0xffffff,.35f,false,WidgetTextAlign::Center);
        else{
            canvas.Line(x-5,y-6,x+5,y-6,0xffffff,.25f,1.2f);
            canvas.Line(x,y-11,x,y-1,0xffffff,.25f,1.2f);
        }
        const wchar_t* text=i==radial::ScreenshotSlot?L"\u622a\u56fe":i==1?L"\u7ffb\u8bd1":L"\u5f85\u5b9a";
        canvas.Text(text,{x-25,y+7,50,18},11,0xffffff,i==radial::ScreenshotSlot?.95f:.35f,false,WidgetTextAlign::Center);
    }
}
struct RadialWindow::Impl {
    HINSTANCE instance;std::function<void(UINT)> action;std::function<void()> changed;
    HWND window=nullptr;unsigned dpi=96;bool visible=false,frosted=false,menu=false;
    int hover=-1,pressed=-1;
    std::unique_ptr<WidgetCanvas> canvas;WidgetBitmap bitmap;WidgetMaterialRuntime material;
    LayeredSurface surface;
    Impl(HINSTANCE i,std::function<void(UINT)> a,std::function<void()> c):instance(i),action(std::move(a)),changed(std::move(c)){}
    ~Impl(){Stop();}
    void Notify(){if(changed)changed();}
    bool Create(){
        if(window)return true;
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=Procedure;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=ClassName;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,ClassName,L"Lume \u8f6e\u76d8\u83dc\u5355",
            WS_POPUP,0,0,320,320,nullptr,nullptr,instance,this);
        return window!=nullptr;
    }
    bool Region(){
        const int edge=MulDiv(8,static_cast<int>(dpi),96),size=MulDiv(320,static_cast<int>(dpi),96);
        const int hole=MulDiv(84,static_cast<int>(dpi),96),end=MulDiv(236,static_cast<int>(dpi),96);
        return PublishRegion(window,CreateAnnularRegion({edge,edge,size-edge,size-edge},{hole,hole,end,end}));
    }
    bool Show(POINT point,bool nextFrosted){
        if(!Create())return false;
        const auto selected=MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};info.cbSize=sizeof(info);if(!GetMonitorInfoW(selected,&info))return false;
        // First move the hidden HWND onto the pointer monitor, then ask Windows for its DPI.
        SetWindowPos(window,nullptr,point.x,point.y,1,1,SWP_NOACTIVATE|SWP_NOZORDER);
        dpi=GetDpiForWindow(window);if(!dpi)dpi=96;
        const int size=MulDiv(320,static_cast<int>(dpi),96);
        if(size>info.rcWork.right-info.rcWork.left||size>info.rcWork.bottom-info.rcWork.top)return false;
        const int x=std::clamp(static_cast<int>(point.x)-size/2,static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-size);
        const int y=std::clamp(static_cast<int>(point.y)-size/2,static_cast<int>(info.rcWork.top),static_cast<int>(info.rcWork.bottom)-size);
        if(!SetWindowPos(window,HWND_TOPMOST,x,y,size,size,SWP_NOACTIVATE)||!Region())return false;
        if(!canvas)canvas=std::make_unique<WidgetCanvas>();
        frosted=nextFrosted;hover=pressed=-1;visible=true;
        if(!Paint(frosted)){Hide();return false;}
        ShowWindow(window,SW_SHOWNOACTIVATE);Notify();return true;
    }
    bool Paint(bool nextFrosted){
        frosted=nextFrosted;if(!window||!visible)return true;
        if(!canvas||!canvas->Begin(320,320,dpi))return false;
        canvas->AnnularSector(Center,Center,Inner,Outer,0,360,0xffffff,1.f/255);
        RadialWindow::Draw(*canvas,hover);
        if(!canvas->Finish(bitmap))return false;
        if(!surface.Present(window,bitmap))return false;
        LumeMaterialParameters p{};p.size=sizeof(p);p.owner=window;p.frosted=frosted;p.opticalProfile=1;p.lightForeground=1;
        p.cornerRadius=160;p.innerRadius=76;
        BOOL animate=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animate,0);p.reducedMotion=!animate;
        if(material.State()==WidgetMaterialState::Detached)material.Attach(p);else material.Update(p);
        return true;
    }
    void Hide(){
        if(menu&&window){
            // Unwind only this ring's native menu stack, including a material
            // submenu. Hiding the owner alone leaves its modal capture alive.
            EnumThreadWindows(GetCurrentThreadId(),[](HWND popup,LPARAM owner)->BOOL{
                wchar_t name[64]{};
                if(!GetClassNameW(popup,name,64)||wcscmp(name,lume::window_ids::WidgetMenuClass))return TRUE;
                for(HWND parent=GetWindow(popup,GW_OWNER);parent;parent=GetWindow(parent,GW_OWNER)){
                    if(parent==reinterpret_cast<HWND>(owner)){PostMessageW(popup,WM_CLOSE,0,0);break;}
                }
                return TRUE;
            },reinterpret_cast<LPARAM>(window));
        }
        if(window)ShowWindow(window,SW_HIDE);
        visible=false;hover=pressed=-1;material.Detach();surface.Release();canvas.reset();bitmap={};Notify();
    }
    void Stop(){Hide();if(window){auto old=window;window=nullptr;DestroyWindow(old);}}
    void Menu(POINT point){
        if(menu)return;
        Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
        if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return;
        menu=true;SetPropW(window,L"LumeRadialMenuActive",reinterpret_cast<HANDLE>(1));
        const UINT result=LumeWidgetMenu::ShowRadial(window,point,frosted);
        RemovePropW(window,L"LumeRadialMenuActive");menu=false;Gdiplus::GdiplusShutdown(token);
        if(result&&action)action(result);
    }
    int Hit(LPARAM data)const{return radial::HitSlot(static_cast<float>(GET_X_LPARAM(data))*96/dpi,static_cast<float>(GET_Y_LPARAM(data))*96/dpi);}
    static LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,w,l);
        switch(message){
            case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
            case WM_ERASEBKGND:return 1;
            case WM_PAINT:{PAINTSTRUCT paint{};BeginPaint(hwnd,&paint);EndPaint(hwnd,&paint);return 0;}
            case WM_NCHITTEST:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(hwnd,&p);
                return radial::InRing(static_cast<float>(p.x)*96/self->dpi,static_cast<float>(p.y)*96/self->dpi)?HTCLIENT:HTTRANSPARENT;}
            case WM_MOUSEMOVE:{const auto hit=self->Hit(l);if(hit!=self->hover){self->hover=hit;self->Paint(self->frosted);}
                TRACKMOUSEEVENT track{};track.cbSize=sizeof(track);track.dwFlags=TME_LEAVE;track.hwndTrack=hwnd;TrackMouseEvent(&track);return 0;}
            case WM_MOUSELEAVE:self->hover=-1;self->pressed=-1;self->Paint(self->frosted);return 0;
            case WM_LBUTTONDOWN:self->pressed=self->Hit(l);return 0;
            case WM_LBUTTONUP:{const bool screenshot=self->pressed==radial::ScreenshotSlot&&self->Hit(l)==radial::ScreenshotSlot;self->pressed=-1;
                if(screenshot&&self->action)self->action(Screenshot);return 0;}
            case WM_CONTEXTMENU:{POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(point.x==-1&&point.y==-1)GetCursorPos(&point);self->Menu(point);return 0;}
            case WM_KEYDOWN:if(w==VK_ESCAPE&&self->action)self->action(Dismiss);return 0;
            case WM_CLOSE:if(self->action)self->action(Dismiss);return 0;
            case RadialShortcut::InvokeMessage:if(self->action)self->action(Toggle);return 0;
            case RadialShortcut::DismissMessage:if(!self->menu&&self->action)self->action(Dismiss);return 0;
            case RadialShortcut::ReleaseMessage:if(self->action)self->action(Dismiss);return 0;
            case WM_DISPLAYCHANGE:if(self->action)self->action(Dismiss);return 0;
            case WM_TIMER:if(w==101&&self->action)self->action(101);return 0;
            case LumeMaterialStatusChangedMessage:self->Notify();return 0;
            case WM_NCDESTROY:SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);if(self->window==hwnd)self->window=nullptr;break;
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
};
RadialWindow::RadialWindow(HINSTANCE instance,std::function<void(UINT)> action,std::function<void()> changed):impl_(std::make_unique<Impl>(instance,std::move(action),std::move(changed))){}
RadialWindow::~RadialWindow()=default;
bool RadialWindow::Create(){return impl_->Create();}
bool RadialWindow::Show(POINT p,bool frosted){return impl_->Show(p,frosted);}
void RadialWindow::Hide(){impl_->Hide();}
void RadialWindow::Stop(){impl_->Stop();}
bool RadialWindow::Paint(bool frosted){return impl_->Paint(frosted);}
HWND RadialWindow::Handle()const{return impl_->window;}
bool RadialWindow::Visible()const{return impl_->visible;}
WidgetMaterialState RadialWindow::MaterialState()const{return impl_->material.State();}
}
