#include "platform/windows/calendar/calendar_todo_ui.h"
#include "platform/windows/text_util.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/widget_ui/ui_font.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/widget_ui/direct_write_edit.h"
#include "platform/windows/calendar/calendar_css_shadow.h"
#include "platform/windows/rounded_window_surface.h"
#include "platform/windows/material/widget_material_runtime.h"
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <imm.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
namespace lume::platform::windows::todo_ui {
namespace {
constexpr wchar_t kComposing[]=L"LumeTodoImeComposing";
constexpr UINT kPresentSurface=WM_APP+542;
constexpr UINT_PTR kMaterialSubclass=0x544f444f,kCaretTimer=0x544f444f;
constexpr UINT_PTR kFirstFrameTimer=0x544f4450;
LRESULT CALLBACK ControlProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR target) {
    // Request before dispatch: a key handler can synchronously destroy its owner.
    switch(m) {
    case WM_PAINT:case WM_SETTEXT:case WM_SETFONT:case WM_ENABLE:case WM_SHOWWINDOW:
    case WM_SETFOCUS:case WM_KILLFOCUS:case WM_CHAR:case WM_KEYDOWN:case WM_KEYUP:
    case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_MOUSEWHEEL:case WM_HSCROLL:case WM_VSCROLL:
    case EM_SETSEL:case EM_REPLACESEL:case WM_CUT:case WM_PASTE:case WM_CLEAR:case WM_UNDO:
    case BM_SETCHECK:case BM_SETSTATE:case WM_IME_COMPOSITION:case WM_IME_ENDCOMPOSITION:
        SendMessageW(reinterpret_cast<HWND>(target),SurfaceChangedMessage,m,0);break;
    case WM_MOUSEMOVE:
        if(wp&MK_LBUTTON)SendMessageW(reinterpret_cast<HWND>(target),SurfaceChangedMessage,m,0);
        break;
    default:break;
    }
    if(m==WM_IME_STARTCOMPOSITION)SetPropW(w,kComposing,reinterpret_cast<HANDLE>(1));
    if(m==WM_IME_ENDCOMPOSITION)RemovePropW(w,kComposing);
    if(m==WM_MOUSEACTIVATE||m==WM_LBUTTONDOWN)
        SendMessageW(reinterpret_cast<HWND>(target),MouseMessage,m,reinterpret_cast<LPARAM>(w));
    if(m==WM_SETFOCUS)SendMessageW(reinterpret_cast<HWND>(target),FocusMessage,0,reinterpret_cast<LPARAM>(w));
    if(m==WM_GETDLGCODE)return DefSubclassProc(w,m,wp,lp)|DLGC_WANTALLKEYS;
    if(m==WM_KEYDOWN&&!Composing(w)) {
        if(wp=='A'&&GetKeyState(VK_CONTROL)<0){wchar_t kind[16]{};GetClassNameW(w,kind,16);if(lstrcmpiW(kind,L"EDIT")==0){SendMessageW(w,EM_SETSEL,0,-1);return 0;}}
        if(wp==VK_TAB||wp==VK_ESCAPE||wp==VK_RETURN||wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN||wp==VK_HOME||wp==VK_END)
            if(SendMessageW(reinterpret_cast<HWND>(target),KeyMessage,wp,reinterpret_cast<LPARAM>(w)))return 0;
    }
    if(m==WM_CHAR&&!Composing(w)) {
        if(wp==VK_TAB||wp==VK_ESCAPE)return 0;
        if(wp==VK_RETURN && !(GetWindowLongPtrW(w,GWL_STYLE)&ES_MULTILINE))return 0;
    }
    if(m==WM_NCDESTROY){RemovePropW(w,kComposing);RemoveWindowSubclass(w,ControlProc,id);}
    return DefSubclassProc(w,m,wp,lp);
}
COLORREF Rgb(int rgb){return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);}
Gdiplus::Color Color(COLORREF color){return Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color));}
void Rounded(HDC dc,RECT rect,float diameter,COLORREF fill,COLORREF border=CLR_INVALID,BYTE opacity=255,BYTE borderOpacity=255) {
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float inset=border==CLR_INVALID?0.f:.5f;
    const float x=static_cast<float>(rect.left)+inset,y=static_cast<float>(rect.top)+inset;
    const float w=static_cast<float>(rect.right-rect.left)-2*inset,h=static_cast<float>(rect.bottom-rect.top)-2*inset;
    diameter=std::min({diameter,w,h});
    Gdiplus::GraphicsPath path;
    path.AddArc(x,y,diameter,diameter,180,90);path.AddArc(x+w-diameter,y,diameter,diameter,270,90);
    path.AddArc(x+w-diameter,y+h-diameter,diameter,diameter,0,90);path.AddArc(x,y+h-diameter,diameter,diameter,90,90);path.CloseFigure();
    if(fill!=CLR_INVALID){Gdiplus::SolidBrush brush(Gdiplus::Color(opacity,GetRValue(fill),GetGValue(fill),GetBValue(fill)));graphics.FillPath(&brush,&path);}
    if(border!=CLR_INVALID){Gdiplus::Pen pen(Gdiplus::Color(borderOpacity,GetRValue(border),GetGValue(border),GetBValue(border)),1.f);graphics.DrawPath(&pen,&path);}
}
// Original calendar-todos.css and todo.css input material values. EDIT HWNDs
// retain input/selection/IME; the two matte passes carry these CSS alpha values.
struct InputMaterial {COLORREF fill=RGB(0,0,0);BYTE alpha=0,border=0;float radius=0;};
InputMaterial InputStyle(HWND control) {
    switch(GetDlgCtrlID(control)) {
    case 101:case 105:return {RGB(0,0,0),26,56,7.f};
    case 205:case 206:return {RGB(255,255,255),18,64,4.f};
    case 210:return {RGB(255,255,255),20,64,6.f};
    default:return {};
    }
}
}
// The native HWND tree remains the input/IME/accessibility tree. Only its pixels
// are flattened; the independent runtime lease owns the captured glass beneath.
struct Surface::MaterialLayer {
    Surface& surface;
    HWND window=nullptr;
    HWND shadow=nullptr;
    int shadowWidth=0,shadowHeight=0;
    WidgetMaterialRuntime runtime;
    bool pending=false,glass=false,failed=false,caretVisible=true;
    bool showRequested=false;
    HWND initialFocus=nullptr;
    HWND caretWindow=nullptr;
    UINT caretInterval=0;
    explicit MaterialLayer(Surface& value):surface(value){}
    ~MaterialLayer(){Detach();}
    struct Dib {
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;std::uint32_t* pixels=nullptr;
        Dib(int width,int height) {
            dc=CreateCompatibleDC(nullptr);if(!dc)return;
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            void* bits=nullptr;bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            if(bitmap){old=SelectObject(dc,bitmap);pixels=static_cast<std::uint32_t*>(bits);}
        }
        ~Dib(){if(old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    };
    void Attach(HWND owner) {
        window=owner;
        if(!SetWindowSubclass(window,Proc,kMaterialSubclass,reinterpret_cast<DWORD_PTR>(this))){failed=true;return;}
        LumeMaterialParameters p{};p.size=sizeof(p);p.owner=window;
        for(HWND ancestor=GetWindow(window,GW_OWNER);ancestor;ancestor=GetWindow(ancestor,GW_OWNER))
            if(GetPropW(ancestor,lume::window_ids::GlassOwnerProp))p.sceneAnchor=ancestor;
        // Popups use the calendar's own material, liquid or frosted.
        p.frosted=surface.preferences_.material!=calendar::Material::LiquidGlass;  // Solid uses frosted popups.
        p.opticalProfile=1;p.cornerRadius=8.f;
        p.lightForeground=surface.preferences_.textColor.rgb!=0;
        BOOL animation=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0);
        p.reducedMotion=!animation;
        failed=!runtime.Attach(p);
    }
    void Reveal() {
        if(!showRequested)return;
        showRequested=false;KillTimer(window,kFirstFrameTimer);
        const auto target=window,focus=initialFocus;
        Request();
        // Activation may synchronously close this popup and delete the layer.
        // After ShowWindow only use local HWNDs, never this or Surface.
        ShowWindow(target,SW_SHOW);
        if(IsWindow(target)&&IsWindow(focus)&&IsChild(target,focus)&&IsWindowEnabled(focus))SetFocus(focus);
    }
    void Show(HWND focus) {
        initialFocus=focus;showRequested=true;
        // Initialization can publish several non-ready statuses. None is a
        // reason to expose the legacy opaque panel before the glass first frame.
        if(!SetTimer(window,kFirstFrameTimer,3000,nullptr)){failed=true;runtime.Detach();}
        Refresh();
    }
    void Detach() {
        if(shadow){DestroyWindow(shadow);shadow=nullptr;}
        if(window){KillTimer(window,kCaretTimer);KillTimer(window,kFirstFrameTimer);RemovePropW(window,L"LumeTodoGlassActive");RemoveWindowSubclass(window,Proc,kMaterialSubclass);}
        window=nullptr;showRequested=false;initialFocus=nullptr;runtime.Detach();
    }
    void Request(bool resetCaret=false) {
        if(!window||surface.capturing_)return;
        if(resetCaret)caretVisible=true;
        if(!pending){pending=true;PostMessageW(window,kPresentSurface,0,0);}
    }
    void Caret() {
        HWND focus=GetFocus();wchar_t kind[16]{};
        if(focus)GetClassNameW(focus,kind,16);
        const bool editable=glass&&IsWindowVisible(window)&&IsChild(window,focus)&&
            lstrcmpiW(kind,L"EDIT")==0&&!Composing(focus);
        const auto blink=GetCaretBlinkTime();
        const UINT interval=editable&&blink&&blink!=INFINITE?std::max(250u,blink):0;
        if(focus!=caretWindow||interval!=caretInterval) {
            KillTimer(window,kCaretTimer);caretWindow=focus;caretInterval=interval;caretVisible=true;
            if(interval)SetTimer(window,kCaretTimer,interval,nullptr);
        }
    }
    void Fallback() {
        if(shadow)ShowWindow(shadow,SW_HIDE);
        glass=false;KillTimer(window,kCaretTimer);caretInterval=0;
        RemovePropW(window,L"LumeTodoGlassActive");
        const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
        if(style&WS_EX_LAYERED) {
            SetWindowLongPtrW(window,GWL_EXSTYLE,style&~WS_EX_LAYERED);
            SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
            ConfigureRoundedWindow(window,8.f*surface.dpi/96.f);
            RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
        }
    }
    void Print(HWND current,HDC dc) {
        RECT client{};GetClientRect(current,&client);
        const int state=SaveDC(dc);
        wchar_t kind[16]{};GetClassNameW(current,kind,16);
        if(lstrcmpiW(kind,L"EDIT")==0) {
            RECT outer{};GetWindowRect(current,&outer);
            POINT origin{};ClientToScreen(current,&origin);
            RECT frame{0,0,outer.right-outer.left,outer.bottom-outer.top};
            const auto style=InputStyle(current);
            IntersectClipRect(dc,0,0,frame.right,frame.bottom);
            Rounded(dc,frame,std::max(1.f,style.radius*2*surface.dpi/96.f),style.fill,
                    CLR_INVALID,style.alpha);
            const int editState=SaveDC(dc);
            OffsetViewportOrgEx(dc,origin.x-outer.left,origin.y-outer.top,nullptr);
            IntersectClipRect(dc,0,0,client.right,client.bottom);
            SendMessageW(current,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);
            RestoreDC(dc,editState);
            // Editor title/notes frames include padding and are drawn by the
            // form. Picker fields replace the opaque native non-client border.
            if(style.border&&GetDlgCtrlID(current)>=200)
                Rounded(dc,frame,style.radius*2*surface.dpi/96.f,CLR_INVALID,RGB(255,255,255),255,style.border);
        } else {
            IntersectClipRect(dc,0,0,client.right,client.bottom);
            SendMessageW(current,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);
        }
        for(HWND child=GetWindow(current,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))continue;
            RECT rect{};GetWindowRect(child,&rect);
            POINT point{rect.left,rect.top};ScreenToClient(current,&point);
            const int childState=SaveDC(dc);
            OffsetViewportOrgEx(dc,point.x,point.y,nullptr);
            Print(child,dc);RestoreDC(dc,childState);
        }
        RestoreDC(dc,state);
    }
    void DrawCaret(HDC dc) {
        if(!caretVisible)return;
        GUITHREADINFO info{};info.cbSize=sizeof(info);
        if(!GetGUIThreadInfo(GetCurrentThreadId(),&info)||!IsChild(window,info.hwndCaret)||
           info.hwndCaret!=GetFocus()||Composing(info.hwndCaret))return;
        RECT rect=info.rcCaret;
        MapWindowPoints(info.hwndCaret,window,reinterpret_cast<POINT*>(&rect),2);
        // Match the form viewport clip while its native EDIT is partly hidden.
        for(HWND parent=info.hwndCaret;parent&&parent!=window;parent=GetParent(parent)) {
            RECT clip{};GetClientRect(parent,&clip);MapWindowPoints(parent,window,reinterpret_cast<POINT*>(&clip),2);
            if(!IntersectRect(&rect,&rect,&clip))return;
        }
        auto brush=CreateSolidBrush(surface.foreground);FillRect(dc,&rect,brush);DeleteObject(brush);
    }
    bool Present() {
        RECT rect{};GetClientRect(window,&rect);
        const int width=rect.right,height=rect.bottom;if(width<=0||height<=0)return false;
        Dib black(width,height),white(width,height);
        if(!black.pixels||!white.pixels)return false;
        surface.capturing_=true;
        surface.matte_=RGB(0,0,0);PatBlt(black.dc,0,0,width,height,BLACKNESS);Print(window,black.dc);DrawCaret(black.dc);
        surface.matte_=RGB(255,255,255);PatBlt(white.dc,0,0,width,height,WHITENESS);Print(window,white.dc);DrawCaret(white.dc);
        surface.capturing_=false;GdiFlush();
        const float radius=std::min({8.f*surface.dpi/96.f,width*.5f,height*.5f});
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const auto index=static_cast<std::size_t>(y)*width+x;
            const auto b=black.pixels[index],w=white.pixels[index];
            // For each channel: white-black = 255*(1-alpha). Take the largest
            // coverage to absorb rounding differences and keep premultiplied RGB valid.
            int alpha=0;
            for(int shift:{0,8,16})alpha=std::max(alpha,255-static_cast<int>((w>>shift)&255)+static_cast<int>((b>>shift)&255));
            alpha=std::clamp(alpha,0,255);
            const float inset=surface.popoverShadow_?CalendarCssInsetCoverage(x+.5f,y+.5f,
                static_cast<float>(width),static_cast<float>(height),surface.dpi/96.f):0.f;
            const int insetAlpha=static_cast<int>(std::lround(inset*255));
            // The inset belongs below foreground content. Black-pass RGB and
            // alpha already encode the foreground; compose white underneath.
            const int underlying=(insetAlpha*(255-alpha)+127)/255;
            alpha+=underlying;
            const float dx=std::max({radius-(x+.5f),x+.5f-(width-radius),0.f});
            const float dy=std::max({radius-(y+.5f),y+.5f-(height-radius),0.f});
            const float coverage=std::clamp(radius+.5f-std::hypot(dx,dy),0.f,1.f);
            auto pixel=static_cast<std::uint32_t>(std::lround(alpha*coverage))<<24;
            for(int shift:{0,8,16})pixel|=static_cast<std::uint32_t>(std::lround(std::min(static_cast<int>((b>>shift)&255)+underlying,alpha)*coverage))<<shift;
            // A one-bit alpha floor keeps empty interior regions hit-testable;
            // it does not dim text or apply whole-window constant opacity.
            if(coverage==1.f&&alpha==0)pixel=0x01000000u;
            black.pixels[index]=pixel;
        }
        const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
        if(!(style&WS_EX_LAYERED))SetWindowLongPtrW(window,GWL_EXSTYLE,style|WS_EX_LAYERED);
        POINT origin{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        return UpdateLayeredWindow(window,nullptr,nullptr,&size,black.dc,&origin,0,&blend,ULW_ALPHA)!=FALSE;
    }
    static LRESULT CALLBACK ShadowProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        if(m==WM_NCHITTEST)return HTTRANSPARENT;
        if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        return DefWindowProcW(w,m,wp,lp);
    }
    void UpdateShadow() {
        if(!surface.popoverShadow_)return;
        if(!IsWindowVisible(window)){if(shadow)ShowWindow(shadow,SW_HIDE);return;}
        RECT rect{};GetWindowRect(window,&rect);
        const int width=rect.right-rect.left,height=rect.bottom-rect.top;
        if(!shadow) {
            WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);
            wc.lpfnWndProc=ShadowProc;wc.lpszClassName=L"LumeTodoCssShadow";
            RegisterClassW(&wc);
            // Both flags also exclude this helper from GlassScene capture.
            shadow=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
                wc.lpszClassName,L"",WS_POPUP,0,0,0,0,window,nullptr,wc.hInstance,nullptr);
        }
        if(!shadow)return;
        const bool firstPresentation=shadowWidth==0;
        const int padding=static_cast<int>(std::ceil(36.f*surface.dpi/96.f))+
            static_cast<int>(std::ceil(8.f*surface.dpi/96.f))+1;
        if(width!=shadowWidth||height!=shadowHeight) {
            const auto bitmap=MakeCalendarCssShadow(width,height,surface.dpi/96.f);
            Dib dib(bitmap.width,bitmap.height);if(!dib.pixels)return;
            std::copy(bitmap.pixels.begin(),bitmap.pixels.end(),dib.pixels);
            POINT source{},destination{rect.left-padding,rect.top-padding};
            SIZE size{bitmap.width,bitmap.height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            if(!UpdateLayeredWindow(shadow,nullptr,&destination,&size,dib.dc,&source,0,&blend,ULW_ALPHA))return;
            shadowWidth=width;shadowHeight=height;
        }
        SetWindowPos(shadow,window,rect.left-padding,rect.top-padding,0,0,
            SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW|(firstPresentation?0:SWP_NOZORDER));
    }
    void Refresh() {
        pending=false;
        if(!window)return;
        if(failed){Fallback();Reveal();return;}
        if(runtime.State()!=WidgetMaterialState::Ready) {
            if(runtime.Status().failed){failed=true;runtime.Detach();Fallback();Reveal();}
            return;
        }
        glass=true;Caret();
        if(Present()){
            SetPropW(window,L"LumeTodoGlassActive",reinterpret_cast<HANDLE>(1));
            if(IsWindowVisible(window))UpdateShadow();
            Reveal();
        }
        else {failed=true;runtime.Detach();Fallback();Reveal();}
    }
    static LRESULT CALLBACK Proc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
        auto& self=*reinterpret_cast<MaterialLayer*>(data);
        if(m==WM_NCDESTROY){RemoveWindowSubclass(w,Proc,id);self.Detach();return DefSubclassProc(w,m,wp,lp);}
        if(m==kPresentSurface){self.Refresh();return 0;}
        if(m==SurfaceChangedMessage){self.Request(wp!=WM_PAINT);return 0;}
        if(m==LumeMaterialStatusChangedMessage){self.Request();return 0;}
        if(m==WM_TIMER&&wp==kCaretTimer){self.caretVisible=!self.caretVisible;self.Request();return 0;}
        if(m==WM_TIMER&&wp==kFirstFrameTimer){
            KillTimer(w,kFirstFrameTimer);
            if(self.showRequested){self.failed=true;self.runtime.Detach();self.Fallback();self.Reveal();}
            return 0;
        }
        if(!self.surface.capturing_) {
            switch(m) {
            case WM_PAINT:
                if(self.glass){PAINTSTRUCT ps;BeginPaint(w,&ps);EndPaint(w,&ps);self.Request();return 0;}
                break;
            case WM_COMMAND:case WM_SETFOCUS:case WM_KILLFOCUS:case WM_ACTIVATE:
            case WM_WINDOWPOSCHANGED:case WM_SHOWWINDOW:case WM_ENABLE:
                self.Request(m==WM_COMMAND||m==WM_SETFOCUS);break;
            default:break;
            }
        }
        return DefSubclassProc(w,m,wp,lp);
    }
};
int Px(float x,unsigned dpi) noexcept{return static_cast<int>(std::lround(x*static_cast<float>(dpi)/96.f));}
std::wstring Wide(const std::string &x) { return lume::text::Wide(x); }
std::string Utf8(const std::wstring &x) { return lume::text::Utf8(x); }
std::wstring Text(HWND w) {
    const int n=GetWindowTextLengthW(w);
    std::wstring text(static_cast<std::size_t>(n)+1,0);
    GetWindowTextW(w,text.data(),n+1);
    text.resize(static_cast<std::size_t>(n));
    return text;
}
bool Number(HWND w,int minimum,int maximum,int &out) {
    auto text=Text(w);
    if(text.empty()||text.size()>10)return false;
    long long value=0;
    for(auto c:text) {
        if(c<L'0'||c>L'9')return false;
        value=value*10+c-L'0';
        if(value>INT_MAX)return false;
    }
    if(value<minimum||value>maximum)return false;
    out=static_cast<int>(value);
    return true;
}
bool Composing(HWND w) {
    if(GetPropW(w,kComposing))return true;
    auto imc=ImmGetContext(w);
    const bool active=imc&&ImmGetCompositionStringW(imc,GCS_COMPSTR,nullptr,0)>0;
    if(imc)ImmReleaseContext(w,imc);
    return active;
}
void Move(HWND w,CalendarRect r,unsigned dpi){MoveWindow(w,Px(r.x,dpi),Px(r.y,dpi),Px(r.width,dpi),Px(r.height,dpi),TRUE);}
void Fence(const std::vector<HWND> &items,HWND current,bool backwards) {
    std::vector<HWND> enabled;
    for(auto w:items)if(IsWindowEnabled(w)&&IsWindowVisible(w))enabled.push_back(w);
    if(enabled.empty())return;
    auto it=std::find(enabled.begin(),enabled.end(),current);
    auto i=it==enabled.end()?0:static_cast<int>(it-enabled.begin());
    i=(i+(backwards?-1:1)+static_cast<int>(enabled.size()))%static_cast<int>(enabled.size());
    SetFocus(enabled[static_cast<std::size_t>(i)]);
}
Surface::Surface(unsigned d,const calendar::Preferences &p):dpi(d),foreground(Rgb(p.textColor.rgb)),accent(Rgb(p.highlightColor.rgb)),preferences_(p){
    Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&graphicsToken_,&input,nullptr);
    // The native EDIT keeps input semantics and font metrics. Its visible text
    // is painted by the DirectWrite adapter, including the glass matte passes.
    font=CreateFontW(-Px(13,d),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,ui_font::Family(L"日程"));
    background=CreateSolidBrush(RGB(31,33,38));editBackground=CreateSolidBrush(RGB(28,30,34));
}
Surface::~Surface(){material_.reset();if(font)DeleteObject(font);if(background)DeleteObject(background);if(editBackground)DeleteObject(editBackground);if(graphicsToken_)Gdiplus::GdiplusShutdown(graphicsToken_);}
void Surface::Attach(HWND window,bool popoverShadow){popoverShadow_=popoverShadow;material_=std::make_unique<MaterialLayer>(*this);material_->Attach(window);}
void Surface::Show(HWND initialFocus){if(material_)material_->Show(initialFocus);}
void Surface::Refresh(){if(material_)material_->Request();}
void Surface::Clear(HDC dc,RECT rect) const {
    FillRect(dc,&rect,capturing_?static_cast<HBRUSH>(GetStockObject(matte_?WHITE_BRUSH:BLACK_BRUSH)):background);
}
HWND Surface::Add(HWND parent,HWND target,const wchar_t *kind,const wchar_t *text,int id,DWORD style){
    HWND w=CreateWindowExW(0,kind,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,1,1,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    if(w){SendMessageW(w,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        if((lstrcmpiW(kind,L"EDIT")==0 || (lstrcmpiW(kind,L"BUTTON")==0 && (style&BS_TYPEMASK)!=BS_OWNERDRAW))
            && !direct_write_edit::Attach(w)){DestroyWindow(w);return nullptr;}
        SetWindowSubclass(w,ControlProc,1,reinterpret_cast<DWORD_PTR>(target));}
    return w;
}
void Surface::Background(HDC dc,RECT r) const {
    if(capturing_){Clear(dc,r);Rounded(dc,r,16.f*dpi/96.f,CLR_INVALID,RGB(255,255,255),255,61);return;}
    RoundedWindowPaint buffer(dc,r.right,r.bottom);
    FillRect(buffer.Dc(),&r,background);
    Rounded(buffer.Dc(),r,16.f*dpi/96.f,CLR_INVALID,RGB(75,78,84));
    buffer.Present(8.f*dpi/96.f);
}
void Surface::Label(HDC dc,const std::wstring &text,CalendarRect r,unsigned align,int size) const{
    direct_write::Style style;
    style.size=static_cast<float>(Px(static_cast<float>(size),dpi));
    style.weight=size>=18?FW_SEMIBOLD:FW_NORMAL;
    style.flags=align|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX;
    RECT rect{Px(r.x,dpi),Px(r.y,dpi),Px(r.Right(),dpi),Px(r.Bottom(),dpi)};
    direct_write::Draw(dc,text,rect,foreground,style);
}
void Surface::EditFrame(HDC dc,CalendarRect r) const {
    const RECT rect{Px(r.x,dpi),Px(r.y,dpi),Px(r.Right(),dpi),Px(r.Bottom(),dpi)};
    if(capturing_)Rounded(dc,rect,14.f*dpi/96.f,RGB(0,0,0),RGB(255,255,255),26,56);
    else Rounded(dc,rect,14.f*dpi/96.f,RGB(28,30,34),RGB(80,82,86));
}
void Surface::Separator(HDC dc,float y,float width) const {
    const float scale=static_cast<float>(dpi)/96.f;
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    // Original schedule-field border: one DIP, white14%; source-over on both
    // matte passes preserves its alpha in the reconstructed foreground.
    Gdiplus::SolidBrush brush(capturing_?Gdiplus::Color(36,255,255,255):Gdiplus::Color(255,63,64,68));
    graphics.FillRectangle(&brush,0.f,y*scale,width*scale,scale);
}
void Surface::Button(const DRAWITEMSTRUCT &item,bool selected,COLORREF swatch,Appearance appearance,
                     bool today,bool muted,int fontSize) const{
    auto dc=item.hDC;RECT r=item.rcItem;Clear(dc,r);const bool down=(item.itemState&ODS_SELECTED)!=0;
    if(appearance==Appearance::Swatch) {
        Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const float scale=static_cast<float>(dpi)/96.f,diameter=22*scale;
        const float x=r.left+(r.right-r.left-diameter)/2,y=r.top+(r.bottom-r.top-diameter)/2;
        Gdiplus::SolidBrush fill(todo_ui::Color(swatch==CLR_INVALID?RGB(31,33,38):swatch));
        Gdiplus::Pen pen(todo_ui::Color(RGB(132,133,136)),1.f);
        graphics.FillEllipse(&fill,x,y,diameter,diameter);graphics.DrawEllipse(&pen,x,y,diameter,diameter);
        if(swatch==CLR_INVALID) {
            graphics.DrawLine(&pen,x+4*scale,y+18*scale,x+18*scale,y+4*scale);
        }
        if(selected) {
            Gdiplus::Pen ring(todo_ui::Color(foreground),2*scale);
            graphics.DrawEllipse(&ring,x-3*scale,y-3*scale,28*scale,28*scale);
        }
    }
    else {
        if(selected||appearance==Appearance::Filled)
            Rounded(dc,r,5.f*dpi/96.f,accent);
        else if(down)
            // calendar-todos.css: white 15% interaction feedback, never an
            // opaque dark tile. Source-over also works on the fallback panel.
            Rounded(dc,r,5.f*dpi/96.f,RGB(255,255,255),CLR_INVALID,38);
        if(today) {
            Rounded(dc,r,5.f*dpi/96.f,CLR_INVALID,accent);
        }
        const auto text=Text(item.hwndItem);
        direct_write::Style style;
        style.size=static_cast<float>(Px(static_cast<float>(fontSize),dpi));
        style.flags=DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX;
        auto textColor=foreground;
        if(muted)textColor=RGB((GetRValue(foreground)*45+31*55)/100,
                              (GetGValue(foreground)*45+33*55)/100,(GetBValue(foreground)*45+38*55)/100);
        if(item.itemState&ODS_DISABLED)textColor=RGB(110,113,121);
        InflateRect(&r,-Px(2,dpi),0);
        if(appearance==Appearance::Schedule) {
            RECT arrow=r;arrow.left=arrow.right-Px(14,dpi);
            direct_write::Draw(dc,L"›",arrow,textColor,style);r.right-=Px(22,dpi);
        }
        if(appearance==Appearance::Schedule)style.flags=(style.flags&~DT_CENTER)|DT_RIGHT;
        direct_write::Draw(dc,text,r,textColor,style);
    }
    if(item.itemState&ODS_FOCUS){InflateRect(&r,-2,-2);DrawFocusRect(dc,&r);}
}
LRESULT Surface::Color(HDC dc,HWND control) const{
    SetTextColor(dc,foreground);
    if(!capturing_){SetBkColor(dc,RGB(28,30,34));return reinterpret_cast<LRESULT>(editBackground);}
    const auto style=InputStyle(control);
    const auto channel=[&](BYTE fill,BYTE matte){return (fill*style.alpha+matte*(255-style.alpha)+127)/255;};
    const COLORREF color=RGB(channel(GetRValue(style.fill),GetRValue(matte_)),
                             channel(GetGValue(style.fill),GetGValue(matte_)),
                             channel(GetBValue(style.fill),GetBValue(matte_)));
    SetBkColor(dc,color);SetDCBrushColor(dc,color);
    return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
}
}
