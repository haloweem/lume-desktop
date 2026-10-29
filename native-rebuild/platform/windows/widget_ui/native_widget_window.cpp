#include "platform/windows/widget_ui/native_widget_window.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/widget_ui/widget_snap_controller.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "platform/windows/widget_ui/widget_placement.h"
#include "platform/windows/widget_ui/layered_surface.h"
#include <windowsx.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
namespace lume::platform::windows {
namespace {
constexpr wchar_t ClassName[]=L"LumeNativeDesktopWidget";
// Deliberately outside the desktop class prefix: it is a surface, not a widget.
constexpr wchar_t DisplayClassName[]=L"LumeWidgetDisplaySurface";
constexpr UINT_PTR SnapTimer=0x6a41,RemoteSnapTimer=0x6a42;
void DesktopLevel(HWND window){desktop_level::Place(window,true);}
}
struct NativeWidgetWindow::State {
    State(HINSTANCE instance,std::wstring title,widget::Limits limits,WidgetWindowPorts ports):
        instance(instance),title(std::move(title)),limits(limits),ports(std::move(ports)),prefs(widget::Defaults(limits)){}
    ~State(){Stop();}
    widget::Preferences Bounds()const{
        auto p=prefs;RECT r{};
        if(window&&GetWindowRect(window,&r)){p.placed=true;p.x=r.left;p.y=r.top;p.width=MulDiv(r.right-r.left,96,static_cast<int>(dpi));p.height=MulDiv(r.bottom-r.top,96,static_cast<int>(dpi));}
        return p;
    }
    void Action(WidgetAction action){const auto callback=ports.action;const auto bounds=Bounds();if(callback)callback(action,bounds);}
    bool Place(){
        if(!window)return false;
        auto p=prefs;POINT point{p.x,p.y};MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        const auto selected=p.placed?MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST):MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY);
        if(!GetMonitorInfoW(selected,&monitor))return false;
        if(!p.placed&&limits.fitToScreen){
            const double fit=widget_placement::MonitorFitScale(selected);
            p.width=window::FitLength(p.width,fit,limits.minWidth);p.height=window::FitLength(p.height,fit,limits.minHeight);
        }
        const int width=MulDiv(p.width,static_cast<int>(dpi),96),height=MulDiv(p.height,static_cast<int>(dpi),96);
        if(!p.placed){const auto at=widget_placement::FirstPosition(window,selected,monitor.rcWork,{width,height},dpi);p.x=at.x;p.y=at.y;}
        // A size saved on a larger display shrinks to this work area.
        const auto fitted=window::FitInside({p.x,p.y,width,height},widget_placement::ToRect(monitor.rcWork),
            MulDiv(limits.minWidth,static_cast<int>(dpi),96),MulDiv(limits.minHeight,static_cast<int>(dpi),96));
        return SetWindowPos(window,nullptr,fitted.x,fitted.y,fitted.width,fitted.height,SWP_NOACTIVATE|SWP_NOZORDER)!=FALSE;
    }
    // Taskbar resized, moved or set to auto-hide: keep the widget in the new work area.
    void WorkAreaChanged(){
        if(!window||configuring||stopping)return;
        RECT before{};GetWindowRect(window,&before);
        configuring=true;Place();configuring=false;
        RECT after{};GetWindowRect(window,&after);
        if(!EqualRect(&before,&after)){Paint();Action(WidgetAction::Bounds);}
    }
    bool Configure(const widget::Preferences& next){
        placementFailed=false;
        if(!widget::Valid(next,limits))return false;
        const auto before=prefs;prefs=next;
        if(!window)return true;
        if(prefs.locked){moveAnchored=false;ClearSnapGuides();}
        // A new configuration may lay the widget out differently.
        hitDirty=true;
        configuring=true;const bool ok=Place()&&Paint();configuring=false;
        if(!ok){prefs=before;configuring=true;Place();Paint();configuring=false;}
        return ok;
    }
    bool Start(){
        placementFailed=false;
        if(window)return true;
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=Procedure;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=ClassName;wc.style=CS_DBLCLKS;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        configuring=true;
        window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|(ports.bottom?WS_EX_NOACTIVATE:0),ClassName,title.c_str(),WS_POPUP,
            prefs.x,prefs.y,1,1,nullptr,nullptr,instance,this);
        if(!window){configuring=false;return false;}
        if(ports.canDropFiles)DragAcceptFiles(window,TRUE);
        SetPropW(window,lume::window_ids::DesktopWidgetProp,reinterpret_cast<HANDLE>(1));dpi=GetDpiForWindow(window);if(!dpi)dpi=96;
        if(ports.bottom)SetPropW(window,lume::window_ids::DesktopBottomProp,reinterpret_cast<HANDLE>(1));
        if(ports.shelf)SetPropW(window,lume::window_ids::DesktopShelfProp,reinterpret_cast<HANDLE>(1));
        SetPropW(window,widget_snap::Capability,reinterpret_cast<HANDLE>(1));snap.Bind(window);
        if(ports.hitShape&&!CreateDisplay()){configuring=false;Stop();return false;}
        canvas=std::make_unique<WidgetCanvas>();hitDirty=true;
        const bool ok=Place()&&Paint();configuring=false;
        if(!ok){Stop();return false;}
        DesktopLevel(window);
        // Placement already falls back to a show without a z-order request; a
        // still hidden window means the desktop layer refused it outright.
        if(!IsWindowVisible(window)){placementFailed=true;Stop();return false;}
        return true;
    }
    void Stop(){
        stopping=true;moveAnchored=false;ClearSnapGuides();material.Detach();
        if(window){auto old=window;window=nullptr;if(GetCapture()==old)ReleaseCapture();DestroyWindow(old);}
        if(display){RemovePropW(display,lume::window_ids::DesktopCompanionOwnerProp);DestroyWindow(display);display=nullptr;}
        surface.Release();displaySurface.Release();canvas.reset();bitmap={};hitBitmap={};hover=false;pressed=false;stopping=false;
    }
    // The click-through window that shows a hitShape widget's frames. It is
    // not owned (an owned window would stay above the widget): the desktop
    // layer keeps it directly below, and it never takes or passes on input.
    bool CreateDisplay(){
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=DisplayClassName;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        // Not WS_EX_NOACTIVATE: glass above samples a transparent window only
        // when it is not also no-activate, so the Dock still refracts the bars.
        display=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW,DisplayClassName,title.c_str(),WS_POPUP,
            prefs.x,prefs.y,1,1,nullptr,nullptr,instance,nullptr);
        if(!display)return false;
        SetPropW(display,lume::window_ids::DesktopCompanionOwnerProp,window);
        return true;
    }
    // Follows the widget's position and visibility, directly below it.
    void SyncDisplay(){
        if(!display||!window)return;
        if(!IsWindowVisible(window)){if(IsWindowVisible(display))ShowWindow(display,SW_HIDE);return;}
        RECT r{};GetWindowRect(window,&r);
        UINT flags=SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER;
        if(GetWindow(window,GW_HWNDNEXT)==display)flags|=SWP_NOZORDER;
        if(!IsWindowVisible(display)){if(!displaySurface.Width())return;flags|=SWP_SHOWWINDOW;}
        SetWindowPos(display,window,r.left,r.top,0,0,flags);
    }
    void ClearSnapGuides(){
        snap.Clear();remoteEdges=0;remoteSource=nullptr;
        if(window){KillTimer(window,SnapTimer);KillTimer(window,RemoteSnapTimer);}
    }
    float Width()const{RECT r{};GetClientRect(window,&r);return static_cast<float>(r.right)*96/dpi;}
    float Height()const{RECT r{};GetClientRect(window,&r);return static_cast<float>(r.bottom)*96/dpi;}
    bool CloseAt(float x,float y)const{return ports.showCloseButton&&x>=Width()-26&&x<Width()-6&&y>=4&&y<24;}
    bool Paint(){
        if(!window||!canvas||stopping)return false;
        const auto w=Width(),h=Height();
        if(!canvas->Begin(w,h,dpi))return false;
        // Where a layered window takes the mouse is decided here, by alpha:
        // a widget catches clicks on its visible body and nowhere else.
        // A tiny transparent hit surface permits dragging between foreground
        // glyphs in a layered HWND; it is not a black/white fallback material.
        // It covers the glass body only: a strip reserved below the glass is
        // clear, so the desktop icons or Dock under it keep their clicks.
        const float radius=std::max(22.f,std::min(w*.064705882353f,h*.061111111111f));
        const float body=ports.materialBottomInset?std::max(1.f,h-static_cast<float>(ports.materialBottomInset(w,h))):h;
        if(prefs.solid){canvas->Rounded({0,0,w,h},radius,static_cast<std::uint32_t>(prefs.panelColor),1);}
        // A bottom widget (the spectrum) has no hit surface at all: only what it
        // draws catches the mouse, and clicks elsewhere reach the Dock or icons.
        // With a hitShape its frames go to the display window instead.
        else if(!ports.bottom&&!display)canvas->Rounded({0,0,w,body},radius,0xffffff,!ports.glass&&ports.hoverPane&&(hover||pressed)?.08f:1.f/255);
        if(ports.draw)ports.draw(*canvas,w,h);
        if(ports.showCloseButton&&(hover||pressed)){
            const WidgetRect close{w-26,4,20,20};
            if(closeHover||pressed)canvas->Rounded(close,4,0xffffff,pressed?.15f:.12f);
            canvas->Line(w-20,10,w-12,18,0xffffff,.9f,1.2f);
            canvas->Line(w-12,10,w-20,18,0xffffff,.9f,1.2f);
        }
        const unsigned edges=snap.Edges()|remoteEdges;
        widget_snap::SetEdges(window,edges);
        const float inset=std::max(22.f,std::min(w*.064705882353f,h*.061111111111f));
        if(edges&widget_snap::Left)canvas->Line(1,inset,1,h-inset,0x0a84ff,.9f,2);
        if(edges&widget_snap::Right)canvas->Line(w-1,inset,w-1,h-inset,0x0a84ff,.9f,2);
        if(edges&widget_snap::Top)canvas->Line(inset,1,w-inset,1,0x0a84ff,.9f,2);
        if(edges&widget_snap::Bottom)canvas->Line(inset,h-1,w-inset,h-1,0x0a84ff,.9f,2);
        if(!canvas->Finish(bitmap))return false;
        if(display)return PaintSplit(w,h);
        const bool ok=surface.Present(window,bitmap);
        if(ok)UpdateMaterial();
        return ok;
    }
    // A hitShape widget: the frame shows through the click-through display
    // window; the widget window itself carries only the fixed hit region,
    // redrawn when the layout changes rather than with every frame.
    bool PaintSplit(float w,float h){
        RECT r{};GetWindowRect(window,&r);const POINT at{r.left,r.top};
        if(!displaySurface.Present(display,bitmap,&at))return false;
        if(hitDirty){
            if(!canvas->Begin(w,h,dpi))return false;
            for(const auto& rect:ports.hitShape(w,h))canvas->Rounded(rect,0,0xffffff,1.f/255);
            if(!canvas->Finish(hitBitmap)||!surface.Present(window,hitBitmap))return false;
            hitDirty=false;
        }
        SyncDisplay();
        return true;
    }
    void UpdateMaterial(){
        if(!window||stopping)return;
        if(prefs.solid||!ports.glass){if(material.State()!=WidgetMaterialState::Detached)material.Detach();return;}
        LumeMaterialParameters p{};p.size=sizeof(p);p.owner=window;p.frosted=prefs.frosted;p.opticalProfile=1;p.lightForeground=1;
        p.cornerRadius=std::max(22.f,std::min(Width()*.064705882353f,Height()*.061111111111f));
        if(ports.materialBottomInset)
            p.bottomInset=std::min(ports.materialBottomInset(Width(),Height()),
                                  static_cast<unsigned>(std::max(0.f,Height()-1.f)));
        BOOL animate=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animate,0);p.reducedMotion=!animate;
        POINT cursor{};GetCursorPos(&cursor);ScreenToClient(window,&cursor);
        const bool inside=cursor.x>=0&&cursor.y>=0&&cursor.x<Width()*dpi/96&&cursor.y<Height()*dpi/96;
        p.contactX=std::clamp(static_cast<float>(cursor.x)*96/dpi/std::max(1.f,Width())*2-1,-1.f,1.f);
        p.contactY=std::clamp(static_cast<float>(cursor.y)*96/dpi/std::max(1.f,Height())*2-1,-1.f,1.f);
        p.pressed=pressed;
        // Away from the pane the highlight rests at the material's top-left key.
        if(!inside&&!p.pressed){p.contactX=-.62f;p.contactY=-.85f;}
        if(ports.panels){
            const auto panels=ports.panels(Width(),Height());
            p.panelCount=static_cast<std::uint32_t>(std::min<std::size_t>(panels.size(),8));
            std::copy_n(panels.begin(),p.panelCount,p.panels);
        }
        if(material.State()==WidgetMaterialState::Detached)material.Attach(p);else material.Update(p);
    }
    LRESULT Hit(LPARAM pos)const{
        POINT pt{GET_X_LPARAM(pos),GET_Y_LPARAM(pos)};ScreenToClient(window,&pt);
        const float x=static_cast<float>(pt.x)*96/dpi,y=static_cast<float>(pt.y)*96/dpi,w=Width(),h=Height();
        if(CloseAt(x,y)||(ports.interactiveAt&&ports.interactiveAt(x,y)))return HTCLIENT;
        if(!prefs.locked){
            const bool left=x<8,right=x>=w-8,top=y<8,bottom=y>=h-8;
            if(top&&left)return HTTOPLEFT;
            if(top&&right)return HTTOPRIGHT;
            if(bottom&&left)return HTBOTTOMLEFT;
            if(bottom&&right)return HTBOTTOMRIGHT;
            if(left)return HTLEFT;
            if(right)return HTRIGHT;
            if(top)return HTTOP;
            if(bottom)return HTBOTTOM;
        }
        return HTCLIENT;
    }
    void Menu(POINT point){
        if(menu)return;
        Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
        if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return;
        menu=true;
        const auto result=ports.menu?ports.menu(window,point,Bounds()):
            LumeWidgetMenu::ShowActions(window,point,ports.canRefresh,false,false,false,prefs.frosted,ports.glass,prefs.locked);
        menu=false;
        Gdiplus::GdiplusShutdown(token);
        if(stopping||!window)return;
        switch(result){
            case 2:Action(WidgetAction::Refresh);break;
            case 1:Action(WidgetAction::Close);break;case 8:Action(WidgetAction::Liquid);break;case 9:Action(WidgetAction::Frosted);break;
            case 10:Action(WidgetAction::Reset);break;case 11:Action(WidgetAction::Settings);break;case 12:Action(WidgetAction::Lock);break;
            default:if(result&&ports.menu&&ports.command){const auto callback=ports.command;callback(result,Bounds());}break;
        }
    }
    static LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);s->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
        if(!s)return DefWindowProcW(hwnd,message,w,l);
        // Internal guide timers are consumed before a module's WM_TIMER port.
        if(message==WM_TIMER&&(w==SnapTimer||w==RemoteSnapTimer)) {
            KillTimer(hwnd,w);
            const auto edges=s->snap.Edges()|s->remoteEdges;
            if(w==SnapTimer)s->snap.Clear();else {s->remoteEdges=0;s->remoteSource=nullptr;}
            if(edges!=(s->snap.Edges()|s->remoteEdges))s->Paint();
            return 0;
        }
        if(message==widget_snap::CancelMessage) {
            const auto edges=s->snap.Edges()|s->remoteEdges;
            s->ClearSnapGuides();
            if(edges)s->Paint();
            return 0;
        }
        if(message==widget_snap::Message) {
            const auto source=reinterpret_cast<HWND>(l);const unsigned edges=static_cast<unsigned>(w)&15u;
            if(!edges&&s->remoteSource!=source)return 0;
            const bool changed=s->remoteEdges!=edges;
            s->remoteSource=edges?source:nullptr;s->remoteEdges=edges;KillTimer(hwnd,RemoteSnapTimer);
            if(w&widget_snap::Linger)SetTimer(hwnd,RemoteSnapTimer,260,nullptr);
            if(changed)s->Paint();
            return 0;
        }
        if(message==WM_WINDOWPOSCHANGING)desktop_level::Changing(l);  // stays at desktop level when activated
        // A bottom widget (the spectrum) never activates: activation, including
        // the drag move loop, would lift it over the Dock and take its clicks.
        if(message==WM_MOUSEACTIVATE&&s->ports.bottom)return MA_NOACTIVATE;
        if(message==WM_MOUSEACTIVATE)desktop_level::Select(hwnd);
        // Anything that still lifts it (activation, a move) puts it back at once.
        // A raise on activation can arrive with SWP_NOZORDER (Changing() set it)
        // yet still lift the window, so check every change and activation.
        if((message==WM_WINDOWPOSCHANGED||message==WM_ACTIVATE||message==WM_EXITSIZEMOVE)&&s->ports.bottom&&!s->stopping&&desktop_level::placingWindow!=hwnd&&
           IsWindowVisible(hwnd))
            PostMessageW(hwnd,desktop_level::Message(),0,0);
        if(message==WM_WINDOWPOSCHANGED&&!s->stopping)s->SyncDisplay();
        if(message==WM_NCDESTROY)desktop_level::Forget(hwnd);
        if(message==desktop_level::Message()){
            if(!s->stopping&&IsWindowVisible(hwnd))desktop_level::Place(hwnd,false);
            return 0;
        }
        if(message==WM_SETTINGCHANGE&&w==SPI_SETWORKAREA)s->WorkAreaChanged();
        if(!s->stopping&&s->ports.event){
            switch(message){
                case WM_TIMER:case WM_TIMECHANGE:case WM_SETTINGCHANGE:case WM_POWERBROADCAST:
                case WM_MOUSEWHEEL:case WM_MOUSEHWHEEL:case WM_KEYDOWN:case WM_SYSKEYDOWN:
                case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:case WM_SETFOCUS:case WM_KILLFOCUS:
                case WM_MOUSEMOVE:case WM_MOUSELEAVE:case WM_LBUTTONUP:case WM_CAPTURECHANGED:
                case WM_DROPFILES:case WM_COMMAND:case WM_CONTEXTMENU:case WM_GETMINMAXINFO:case WM_CTLCOLOREDIT:
                    if(const auto result=s->ports.event(hwnd,message,w,l))return *result;
                    break;
            }
        }
        switch(message){
            case WM_ERASEBKGND:return 1;
            case WM_PAINT:{PAINTSTRUCT paint{};BeginPaint(hwnd,&paint);EndPaint(hwnd,&paint);return 0;}
            case WM_NCHITTEST:return s->Hit(l);
            case WM_GETMINMAXINFO:{auto* mm=reinterpret_cast<MINMAXINFO*>(l);
                mm->ptMinTrackSize={MulDiv(s->limits.minWidth,static_cast<int>(s->dpi),96),MulDiv(s->limits.minHeight,static_cast<int>(s->dpi),96)};
                mm->ptMaxTrackSize={MulDiv(s->limits.maxWidth,static_cast<int>(s->dpi),96),MulDiv(s->limits.maxHeight,static_cast<int>(s->dpi),96)};return 0;}
            case WM_MOUSEMOVE:{
                const bool over=s->CloseAt(static_cast<float>(GET_X_LPARAM(l))*96/s->dpi,static_cast<float>(GET_Y_LPARAM(l))*96/s->dpi);
                if(!s->hover){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);}
                const bool changed=!s->hover||s->closeHover!=over;s->hover=true;s->closeHover=over;
                if(changed)s->Paint();else s->UpdateMaterial();return 0;}
            case WM_MOUSELEAVE:s->hover=false;s->closeHover=false;s->Paint();return 0;
            case WM_LBUTTONDOWN:{
                const float x=static_cast<float>(GET_X_LPARAM(l))*96/s->dpi,y=static_cast<float>(GET_Y_LPARAM(l))*96/s->dpi;
                if(s->CloseAt(x,y)){s->pressed=true;SetCapture(hwnd);s->Paint();}
                else if(!s->prefs.locked){
                    POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
                    if(ClientToScreen(hwnd,&point)){
                        ReleaseCapture();
                        SendMessageW(hwnd,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(point.x,point.y));
                    }
                }
                return 0;}
            case WM_LBUTTONUP:{
                const bool close=s->pressed&&s->CloseAt(static_cast<float>(GET_X_LPARAM(l))*96/s->dpi,static_cast<float>(GET_Y_LPARAM(l))*96/s->dpi);
                s->pressed=false;if(GetCapture()==hwnd)ReleaseCapture();if(close)s->Action(WidgetAction::Close);else s->Paint();return 0;}
            case WM_CAPTURECHANGED:
                s->moveAnchored=false;
                if(s->pressed){s->pressed=false;s->Paint();}return 0;
            case WM_CANCELMODE:
                s->moveAnchored=false;s->ClearSnapGuides();s->Paint();
                break;
            case WM_CONTEXTMENU:{POINT pt{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(pt.x==-1&&pt.y==-1)GetCursorPos(&pt);s->Menu(pt);return 0;}
            case WM_SIZE:
                s->hitDirty=true;
                if(!s->configuring)s->Paint();
                if(!s->stopping&&s->ports.event)s->ports.event(hwnd,message,w,l);
                return 0;
            case WM_MOVE:
                if(!s->configuring)s->UpdateMaterial();
                if(!s->stopping&&s->ports.event)s->ports.event(hwnd,message,w,l);
                return 0;
            case WM_ENTERSIZEMOVE:
                s->resizing=false;widget_snap::BeginInteraction(hwnd);
                s->moveAnchored=GetWindowRect(hwnd,&s->moveOrigin)&&GetCursorPos(&s->moveCursor);
                return 0;
            case WM_MOVING:
                if(!s->prefs.locked){
                    auto& bounds=*reinterpret_cast<RECT*>(l);
                    POINT cursor{};
                    if(s->moveAnchored&&GetCursorPos(&cursor)){
                        // WM_MOVING can feed the snapped window position back
                        // into the next proposal. Keep raw cursor displacement
                        // relative to the fixed origin, as clock/calendar do,
                        // so slow one-pixel moves can cross the release margin.
                        const auto width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
                        bounds.left=s->moveOrigin.left+cursor.x-s->moveCursor.x;
                        bounds.top=s->moveOrigin.top+cursor.y-s->moveCursor.y;
                        bounds.right=bounds.left+width;bounds.bottom=bounds.top+height;
                    }
                    const auto edges=s->snap.Edges();s->snap.Move(bounds);
                    if(edges!=s->snap.Edges())s->Paint();
                    return TRUE;
                }
                break;
            case WM_SIZING:
                if(!s->prefs.locked){
                    const auto edges=s->snap.Edges();s->resizing=true;s->moveAnchored=false;
                    s->snap.Resize(*reinterpret_cast<RECT*>(l),w);
                    if(edges!=s->snap.Edges())s->Paint();
                }
                break;
            case WM_EXITSIZEMOVE:{
                s->moveAnchored=false;
                const auto edges=s->snap.Edges();
                s->snap.Clear(!s->resizing);if(s->snap.Edges())SetTimer(hwnd,SnapTimer,260,nullptr);
                if(edges!=s->snap.Edges())s->Paint();
                s->Action(WidgetAction::Bounds);return 0;
            }
            case WM_DPICHANGED:{
                s->dpi=HIWORD(w);s->hitDirty=true;const auto* rect=reinterpret_cast<RECT*>(l);
                SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
                if(s->moveAnchored){
                    // A monitor transition can change the accepted origin as
                    // well as the size. Continue dragging from that new frame.
                    s->moveAnchored=GetWindowRect(hwnd,&s->moveOrigin)&&GetCursorPos(&s->moveCursor);
                    s->snap.Clear();
                }
                if(!s->configuring){s->Paint();s->Action(WidgetAction::Bounds);}
                if(!s->stopping&&s->ports.event)s->ports.event(hwnd,message,w,l);
                return 0;}
            case WM_DISPLAYCHANGE:if(!s->configuring){s->Configure(s->Bounds());s->Action(WidgetAction::Bounds);}return 0;
            case LumeMaterialStatusChangedMessage:
                if(!s->stopping){
                    // The first layered foreground may have been submitted
                    // before DWM showed the owner. Refresh it when glass is
                    // ready so a transparent hit surface cannot linger alone.
                    s->Paint();
                    if(s->ports.materialChanged)s->ports.materialChanged();
                }
                return 0;
            case NativeWidgetWindow::UpdateMessage:if(!s->stopping&&s->ports.update)s->ports.update();return 0;
            case WM_CLOSE:s->Action(WidgetAction::Close);return 0;
            case WM_DROPFILES:DragFinish(reinterpret_cast<HDROP>(w));return 0;
            case WM_NCDESTROY:s->snap.Clear();RemovePropW(hwnd,lume::window_ids::DesktopWidgetProp);RemovePropW(hwnd,lume::window_ids::DesktopBottomProp);
                RemovePropW(hwnd,lume::window_ids::DesktopShelfProp);RemovePropW(hwnd,widget_snap::Capability);RemovePropW(hwnd,widget_snap::ActiveEdges);
                SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);if(s->window==hwnd)s->window=nullptr;break;
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
    HINSTANCE instance;std::wstring title;widget::Limits limits;WidgetWindowPorts ports;widget::Preferences prefs;
    HWND window=nullptr;unsigned dpi=96;std::unique_ptr<WidgetCanvas> canvas;WidgetBitmap bitmap;WidgetMaterialRuntime material;
    WidgetSnapController snap;unsigned remoteEdges=0;HWND remoteSource=nullptr;
    RECT moveOrigin{};POINT moveCursor{};bool moveAnchored=false;
    LayeredSurface surface;
    // hitShape widgets only: the display window, its surface and the hit mask.
    HWND display=nullptr;LayeredSurface displaySurface;WidgetBitmap hitBitmap;bool hitDirty=true;
    bool configuring=false,stopping=false,hover=false,closeHover=false,pressed=false,menu=false,resizing=false;
    bool placementFailed=false;
};
NativeWidgetWindow::NativeWidgetWindow(HINSTANCE instance,std::wstring title,widget::Limits limits,WidgetWindowPorts ports):
    state_(std::make_unique<State>(instance,std::move(title),limits,std::move(ports))){}
NativeWidgetWindow::~NativeWidgetWindow()=default;
bool NativeWidgetWindow::Configure(const widget::Preferences& p){return state_->Configure(p);}
bool NativeWidgetWindow::Start(){return state_->Start();}
void NativeWidgetWindow::Stop(){state_->Stop();}
bool NativeWidgetWindow::Paint(){return state_->Paint();}
bool NativeWidgetWindow::PlacementFailed()const{return state_->placementFailed;}
HWND NativeWidgetWindow::Handle()const{return state_->window;}
widget::Preferences NativeWidgetWindow::Bounds()const{return state_->Bounds();}
WidgetMaterialState NativeWidgetWindow::MaterialState()const{return state_->material.State();}
}
