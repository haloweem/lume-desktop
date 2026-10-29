#include "platform/windows/screenshot/screenshot_editor.h"
#include "platform/windows/screenshot/screenshot_render.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/widget_ui/direct_write_edit.h"
#include "platform/windows/widget_ui/direct_write_tooltip.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <array>
#include <memory>
#include <utility>
#include <cstring>

namespace lume::platform::windows::capture {
namespace {
using screenshot::Point;using screenshot::Rect;using screenshot::Tool;using screenshot::Annotation;
constexpr wchar_t EditorClass[]=L"LumeScreenshotEditor",PinClass[]=L"LumeScreenshotPin";
constexpr std::uint32_t Colors[]{0x000000,0x808080,0x800000,0xff0000,0xff8000,0xffff00,0x008000,0x00a0e9,0x0055aa,0x4444aa,0x804080,0xeeeeee,
    0xffffff,0xc0c0c0,0xc08080,0xff8080,0xffc080,0xffff80,0x80c080,0x80d8ee,0x80a8d0,0xaaaadd,0xc080c0,0x333333};
Gdiplus::Color Color(std::uint32_t rgb,BYTE alpha=255){return Gdiplus::Color(alpha,static_cast<BYTE>(rgb>>16),static_cast<BYTE>(rgb>>8),static_cast<BYTE>(rgb));}
Gdiplus::RectF Box(Rect r){return {static_cast<float>(r.left),static_cast<float>(r.top),static_cast<float>(r.Width()),static_cast<float>(r.Height())};}
void Fill(Gdiplus::Graphics& g,Rect r,std::uint32_t rgb,BYTE alpha=255){
    const auto smoothing=g.GetSmoothingMode();g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    Gdiplus::SolidBrush b(Color(rgb,alpha));g.FillRectangle(&b,Box(r));g.SetSmoothingMode(smoothing);
}
void Text(Gdiplus::Graphics& g,const std::wstring& text,Rect r,float size,std::uint32_t rgb){
    direct_write::Style style; style.size=size; style.family=L"Microsoft YaHei UI";style.flags=DT_WORDBREAK|DT_NOPREFIX;
    direct_write::Draw(g,text,Box(r),Color(rgb),style);
}
void DrawImage(Gdiplus::Graphics& g,const Image& image,Rect destination){
    if(!image.Valid())return;
    Gdiplus::Bitmap bitmap(image.width,image.height,image.width*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data())));
    g.DrawImage(&bitmap,Box(destination),0.f,0.f,static_cast<float>(image.width),static_cast<float>(image.height),Gdiplus::UnitPixel);
}
}
struct ScreenshotEditor::Impl {
    HINSTANCE instance;EditorPorts ports;ULONG_PTR gdiplus=0;HWND window=nullptr,edit=nullptr,tooltip=nullptr;HFONT editFont=nullptr;HBRUSH editBackground=nullptr;
    int editBackgroundWidth=0,editBackgroundHeight=0;
    Image source,composed,frame,dimmed;POINT origin{};Rect bounds,selection,startRect,paintedPending{};
    screenshot::History history;Annotation pending;Tool tool=Tool::Select;Point anchor{},pointer{};
    int drag=0,variant=0,width=3,mosaicStrength=3,brushSize=39,fontSize=20,hover=-1;std::uint32_t color=0xff0000;bool drawing=false,selecting=false,closing=false,busy=false,active=false,exportBlocked=false;
    bool bold=false,italic=false,outline=false,sizeOpen=false,updatingControls=false,latestAdjustable=false;
    Tool latestTool=Tool::Select;
    std::wstring fontFamily=L"Microsoft YaHei UI";
    HWND fontChoice=nullptr,fontSizeChoice=nullptr,strengthSlider=nullptr,strokeSlider=nullptr,strokeValue=nullptr,arrowChoice=nullptr;
    HFONT controlsFont=nullptr;int controlsDpi=0;HCURSOR brushCursor=nullptr;int cursorDiameter=0;
    Rect fontRect{},fontSizeRect{},strengthRect{},arrowRect{},sizePopup{};
    float scale=1,uiScale=1;Rect toolbar{},options{};std::array<Rect,CommandCount> buttons{};std::vector<Rect> optionButtons;std::vector<int> optionIds;
    std::wstring error;
    int OptionId(std::size_t index)const{return optionIds[index];}
    const wchar_t* OptionName(int id)const{
        if(id==0)return L"\u5927\u5c0f";
        if(id>=2&&id<=5){constexpr const wchar_t* names[]{L"\u77e9\u5f62",L"\u692d\u5706",L"\u5b9e\u5fc3\u77e9\u5f62",L"\u5b9e\u5fc3\u692d\u5706"};return names[id-2];}
        if(id==7)return L"\u7c97\u4f53";if(id==8)return L"\u659c\u4f53";if(id==9)return L"\u63cf\u8fb9\u6587\u5b57";
        if(id>=10&&id<=13){constexpr const wchar_t* names[]{L"\u6d82\u62b9\u9a6c\u8d5b\u514b",L"\u6d82\u62b9\u6a21\u7cca",L"\u77e9\u5f62\u9a6c\u8d5b\u514b",L"\u77e9\u5f62\u6a21\u7cca"};return names[id-10];}
        if(id==44)return L"\u81ea\u5b9a\u4e49\u989c\u8272";
        return L"\u989c\u8272";
    }
    struct Pin {
        Impl* owner=nullptr;HWND window=nullptr;Image image;float zoom=1;POINT offset{};
        ~Pin(){if(window)DestroyWindow(window);}
        void Save(){if(!owner->Save(window,image)&&GetLastError()!=ERROR_CANCELLED)MessageBoxW(window,L"\u4fdd\u5b58\u5931\u8d25\uff0c\u8bf7\u91cd\u8bd5\u6216\u66f4\u6362\u76ee\u5f55",L"Lume",MB_OK|MB_ICONWARNING);}
        static LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM w,LPARAM l){
            auto* self=reinterpret_cast<Pin*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
            if(message==WM_NCCREATE){self=static_cast<Pin*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
            if(!self)return DefWindowProcW(hwnd,message,w,l);
            switch(message){
            case WM_ERASEBKGND:return 1;
            case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);
                {Gdiplus::Graphics g(dc);g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);DrawImage(g,self->image,{0,0,r.right,r.bottom});}
                EndPaint(hwnd,&ps);return 0;}
            case WM_LBUTTONDOWN:self->offset={GET_X_LPARAM(l),GET_Y_LPARAM(l)};SetCapture(hwnd);return 0;
            case WM_MOUSEMOVE:if(GetCapture()==hwnd){POINT p{};GetCursorPos(&p);SetWindowPos(hwnd,nullptr,p.x-self->offset.x,p.y-self->offset.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}return 0;
            case WM_LBUTTONUP:if(GetCapture()==hwnd)ReleaseCapture();return 0;
            case WM_MOUSEWHEEL:{
                self->zoom=std::clamp(self->zoom*(GET_WHEEL_DELTA_WPARAM(w)>0?1.1f:1/1.1f),.1f,4.f);
                SetWindowPos(hwnd,nullptr,0,0,std::max(16,static_cast<int>(self->image.width*self->zoom)),std::max(16,static_cast<int>(self->image.height*self->zoom)),SWP_NOMOVE|SWP_NOZORDER);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            case WM_LBUTTONDBLCLK:case WM_CLOSE:DestroyWindow(hwnd);return 0;
            case WM_KEYDOWN:
                if(w==VK_ESCAPE)DestroyWindow(hwnd);
                else if((GetKeyState(VK_CONTROL)&0x8000)&&w=='C'){if(!self->owner->Copy(hwnd,self->image))MessageBeep(MB_ICONWARNING);}
                else if((GetKeyState(VK_CONTROL)&0x8000)&&w=='S')self->Save();
                return 0;
            case WM_CONTEXTMENU:{
                POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(p.x==-1&&p.y==-1)GetCursorPos(&p);
                const auto command=LumeWidgetMenu::ShowItems(hwnd,p,{{L"\u590d\u5236",1},{L"\u4fdd\u5b58",2},{L"100%",3},{L"\u5173\u95ed",4}});
                if(command==1){if(!self->owner->Copy(hwnd,self->image))MessageBeep(MB_ICONWARNING);}
                else if(command==2)self->Save();
                else if(command==3){self->zoom=1;SetWindowPos(hwnd,nullptr,0,0,self->image.width,self->image.height,SWP_NOMOVE|SWP_NOZORDER);InvalidateRect(hwnd,nullptr,FALSE);}
                else if(command==4)DestroyWindow(hwnd);return 0;}
            case WM_NCDESTROY:self->image={};self->window=nullptr;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);break;
            }
            return DefWindowProcW(hwnd,message,w,l);
        }
    };
    std::vector<std::unique_ptr<Pin>> pins;
    Impl(HINSTANCE i,EditorPorts p):instance(i),ports(std::move(p)){
        Gdiplus::GdiplusStartupInput input;if(Gdiplus::GdiplusStartup(&gdiplus,&input,nullptr)!=Gdiplus::Ok)gdiplus=0;
    }
    ~Impl(){Stop();pins.clear();if(gdiplus)Gdiplus::GdiplusShutdown(gdiplus);}
    bool Copy(HWND owner,const Image& image){return ports.copy?ports.copy(owner,image):CopyImage(owner,image);}
    bool Save(HWND owner,const Image& image){return ports.save?ports.save(owner,image):SaveImage(owner,image);}
    bool Start(){
        if(active||busy)return true;
        if(!gdiplus)return false;
        Stop();Image grabbed;POINT offset{};
        if(!ports.capture)DwmFlush();
        if(!(ports.capture?ports.capture(grabbed,offset):GrabDesktop(grabbed,offset))||!grabbed.Valid())return false;
        source=std::move(grabbed);composed=source;origin=offset;bounds={0,0,source.width,source.height};
        dimmed=source;for(auto& pixel:dimmed.pixels){
            const auto r=((pixel>>16)&255)*145/255,g=((pixel>>8)&255)*145/255,b=(pixel&255)*145/255;
            pixel=0xff000000u|(r<<16)|(g<<8)|b;
        }
        frame=dimmed;paintedPending={};
        selection={};history.Clear();tool=Tool::Select;variant=0;hover=-1;exportBlocked=false;error.clear();
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=Procedure;wc.hCursor=LoadCursorW(nullptr,IDC_CROSS);wc.lpszClassName=EditorClass;wc.style=CS_DBLCLKS;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){Stop();return false;}
        window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,EditorClass,L"Lume \u622a\u56fe",WS_POPUP|WS_CLIPCHILDREN,origin.x,origin.y,source.width,source.height,nullptr,nullptr,instance,this);
        if(!window){Stop();return false;}
        scale=static_cast<float>(GetDpiForWindow(window))/96;if(scale<=0)scale=1;
        SetWindowDisplayAffinity(window,WDA_EXCLUDEFROMCAPTURE);
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&common);
        const auto child=[&](LPCWSTR type,DWORD style,int id){return CreateWindowExW(0,type,L"",WS_CHILD|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);};
        constexpr DWORD comboStyle=CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL;
        fontChoice=child(L"COMBOBOX",comboStyle,4510);fontSizeChoice=child(L"COMBOBOX",comboStyle,4511);
        strengthSlider=child(TRACKBAR_CLASSW,TBS_HORZ|TBS_NOTICKS,4512);strokeSlider=child(TRACKBAR_CLASSW,TBS_HORZ|TBS_NOTICKS,4513);
        strokeValue=child(L"EDIT",WS_BORDER|ES_NUMBER|ES_CENTER,4514);arrowChoice=child(L"COMBOBOX",comboStyle,4515);
        if(!fontChoice||!fontSizeChoice||!strengthSlider||!strokeSlider||!strokeValue||!arrowChoice){Stop();return false;}
        if(!direct_write_edit::Attach(strokeValue)){Stop();return false;}
        for(const auto name:{L"Microsoft YaHei UI",L"Microsoft YaHei",L"SimSun",L"SimHei",L"KaiTi",L"Arial",L"Segoe UI",L"Times New Roman"})SendMessageW(fontChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        for(int size:{10,12,14,16,18,20,24,28,32,36,48,64,72,96,128,160,200}){const auto text=std::to_wstring(size);SendMessageW(fontSizeChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
        for(const auto name:{L"\u7bad\u5934",L"\u76f4\u7ebf",L"\u53cc\u5411\u7bad\u5934",L"\u6e10\u7c97\u7bad\u5934"})SendMessageW(arrowChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(strengthSlider,TBM_SETRANGE,TRUE,MAKELONG(2,20));SendMessageW(strokeSlider,TBM_SETRANGE,TRUE,MAKELONG(1,100));
        SendMessageW(strokeValue,EM_SETLIMITTEXT,3,0);SetWindowSubclass(strokeValue,SizeProcedure,1,reinterpret_cast<DWORD_PTR>(this));
        for(HWND control:{fontChoice,fontSizeChoice,arrowChoice,strengthSlider,strokeSlider})SetWindowSubclass(control,ControlProcedure,1,reinterpret_cast<DWORD_PTR>(this));
        sizeOpen=false;
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,instance,nullptr);
        if(tooltip){
            direct_write_tooltip::Attach(tooltip);
            for(int i=0;i<CommandCount;++i){TOOLINFOW ti{};ti.cbSize=TTTOOLINFOW_V2_SIZE;ti.uFlags=TTF_SUBCLASS;ti.hwnd=window;ti.uId=static_cast<UINT_PTR>(i+1);ti.lpszText=const_cast<wchar_t*>(CommandName(static_cast<Command>(i)));SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&ti));}
            for(int i=0;i<64;++i){TOOLINFOW ti{};ti.cbSize=TTTOOLINFOW_V2_SIZE;ti.uFlags=TTF_SUBCLASS;ti.hwnd=window;ti.uId=static_cast<UINT_PTR>(100+i);ti.lpszText=const_cast<wchar_t*>(L"");SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&ti));}
        }
        active=true;ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(window);Repaint();return true;
    }
    void Stop(){
        if(closing)return;closing=true;active=false;drawing=selecting=false;drag=0;busy=false;
        if(window&&GetCapture()==window)ReleaseCapture();
        EndText(false);
        if(tooltip){DestroyWindow(tooltip);tooltip=nullptr;}
        if(window){auto hwnd=window;window=nullptr;DestroyWindow(hwnd);}
        fontChoice=fontSizeChoice=strengthSlider=strokeSlider=strokeValue=arrowChoice=nullptr;
        if(controlsFont){DeleteObject(controlsFont);controlsFont=nullptr;}controlsDpi=0;
        if(brushCursor){DestroyCursor(brushCursor);brushCursor=nullptr;}cursorDiameter=0;
        source={};composed={};frame={};dimmed={};paintedPending={};pending={};selection={};history.Clear();latestAdjustable=false;latestTool=Tool::Select;closing=false;
    }
    bool Commit(Annotation a){
        const auto committedTool=a.tool;
        if(history.Add(std::move(a))){latestAdjustable=true;latestTool=committedTool;return true;}
        exportBlocked=true;error=L"\u5df2\u8fbe\u7f16\u8f91\u4e0a\u9650\uff0c\u8bf7\u64a4\u9500\u540e\u91cd\u65b0\u6807\u6ce8";return false;
    }
    void FreezeLatest(){latestAdjustable=false;}
    void Style(Annotation& a)const{
        a.color=color;a.width=tool==Tool::Mosaic?mosaicStrength:width;a.variant=variant;a.brushSize=brushSize;
        a.fontFamily=fontFamily;a.fontSize=fontSize;a.bold=bold;a.italic=italic;a.outline=outline;
    }
    void UpdateLatestStyle(){
        if(!latestAdjustable||latestTool!=tool||history.Items().empty())return;
        auto updated=history.Items().back();Style(updated);
        if(history.UpdateLast(std::move(updated)))Refresh();
    }
    void UpdateTextStyle(){
        if(!edit)return;Style(pending);
        const HFONT next=CreateFontW(-fontSize,0,0,0,bold?FW_BOLD:FW_NORMAL,italic?TRUE:FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,fontFamily.c_str());
        if(next){SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(editFont)DeleteObject(editFont);editFont=next;}
    }
    int StrokeSize()const{return tool==Tool::Mosaic||tool==Tool::Eraser?brushSize:width;}
    void SetStrokeSize(int value){if(tool==Tool::Mosaic||tool==Tool::Eraser)brushSize=std::clamp(value,1,100);else width=std::clamp(value,1,100);}
    void DrawChoice(const DRAWITEMSTRUCT& item){
        if(item.CtlType!=ODT_COMBOBOX)return;
        const bool selected=(item.itemState&ODS_SELECTED)!=0;
        FillRect(item.hDC,&item.rcItem,GetSysColorBrush(selected?COLOR_HIGHLIGHT:COLOR_WINDOW));
        const auto index=item.itemID==static_cast<UINT>(-1)?SendMessageW(item.hwndItem,CB_GETCURSEL,0,0):item.itemID;
        if(index!=CB_ERR){
            const auto length=SendMessageW(item.hwndItem,CB_GETLBTEXTLEN,index,0);
            if(length>=0){std::wstring label(static_cast<std::size_t>(length)+1,L'\0');
                SendMessageW(item.hwndItem,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(label.data()));label.resize(static_cast<std::size_t>(length));
                RECT rect=item.rcItem;rect.left+=3;rect.right-=2;
                direct_write::Style style;style.size=std::max(10.f,12*uiScale);style.flags|=DT_END_ELLIPSIS;
                direct_write::Draw(item.hDC,label,rect,GetSysColor(selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT),style);
            }
        }
        if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&item.rcItem);
    }
    static LRESULT CALLBACK ControlProcedure(HWND hwnd,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        auto* self=reinterpret_cast<Impl*>(data);
        if(message==WM_KEYDOWN){
            const bool combo=hwnd==self->fontChoice||hwnd==self->fontSizeChoice||hwnd==self->arrowChoice;
            const bool dropped=combo&&SendMessageW(hwnd,CB_GETDROPPEDSTATE,0,0);
            const bool shortcut=(GetKeyState(VK_CONTROL)&0x8000)&&(w=='Z'||w=='Y'||w=='C'||w=='S');
            if(!dropped&&(w==VK_ESCAPE||w==VK_RETURN||shortcut)){self->Key(w);return 0;}
        }
        return DefSubclassProc(hwnd,message,w,l);
    }
    static LRESULT CALLBACK SizeProcedure(HWND hwnd,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        auto* self=reinterpret_cast<Impl*>(data);
        if(message==WM_KEYDOWN&&(w==VK_RETURN||w==VK_ESCAPE)){self->sizeOpen=false;SetFocus(self->window);self->Repaint();return 0;}
        return DefSubclassProc(hwnd,message,w,l);
    }
    void EndText(bool commit){
        if(!edit)return;
        if(commit){
            // Keep the edit control's visual line breaks, including soft wrapping,
            // so the committed annotation matches the text the user just saw.
            std::wstring value;
            const int lines=static_cast<int>(SendMessageW(edit,EM_GETLINECOUNT,0,0));
            for(int i=0;i<lines;++i){
                const auto lineStart=SendMessageW(edit,EM_LINEINDEX,static_cast<WPARAM>(i),0);
                const int length=static_cast<int>(SendMessageW(edit,EM_LINELENGTH,static_cast<WPARAM>(lineStart),0));
                std::vector<wchar_t> line(static_cast<std::size_t>(length)+2,L'\0');
                *reinterpret_cast<WORD*>(line.data())=static_cast<WORD>(std::min(length+1,4097));
                const auto count=SendMessageW(edit,EM_GETLINE,static_cast<WPARAM>(i),reinterpret_cast<LPARAM>(line.data()));
                if(i)value+=L'\n';value.append(line.data(),static_cast<std::size_t>(count));
            }
            if(!value.empty()){pending.text=std::move(value);Commit(pending);}
        }
        DestroyWindow(edit);edit=nullptr;if(editFont){DeleteObject(editFont);editFont=nullptr;}
        if(editBackground){DeleteObject(editBackground);editBackground=nullptr;}
        editBackgroundWidth=editBackgroundHeight=0;pending={};
        if(commit)Refresh();
    }
    static LRESULT CALLBACK EditProcedure(HWND hwnd,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        auto* self=reinterpret_cast<Impl*>(data);
        if(message==WM_KEYDOWN&&w==VK_ESCAPE){self->EndText(false);SetFocus(self->window);self->Repaint();return 0;}
        if(message==WM_KEYDOWN&&w==VK_RETURN&&!(GetKeyState(VK_SHIFT)&0x8000)){self->EndText(true);SetFocus(self->window);return 0;}
        return DefSubclassProc(hwnd,message,w,l);
    }
    bool UpdateTextBackground(int x,int y,int backgroundWidth,int backgroundHeight){
        if(!composed.Valid()||x<0||y<0||backgroundWidth<=0||backgroundHeight<=0||x+backgroundWidth>composed.width||y+backgroundHeight>composed.height)return false;
        if(editBackground&&editBackgroundWidth==backgroundWidth&&editBackgroundHeight==backgroundHeight)return true;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=backgroundWidth;
        info.bmiHeader.biHeight=-backgroundHeight;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);return false;}
        for(int row=0;row<backgroundHeight;++row)
            std::memcpy(static_cast<std::uint32_t*>(pixels)+static_cast<std::size_t>(row)*backgroundWidth,
                composed.pixels.data()+static_cast<std::size_t>(y+row)*composed.width+x,static_cast<std::size_t>(backgroundWidth)*4);
        HBRUSH background=CreatePatternBrush(bitmap);DeleteObject(bitmap);
        if(!background)return false;
        if(editBackground)DeleteObject(editBackground);
        editBackground=background;editBackgroundWidth=backgroundWidth;editBackgroundHeight=backgroundHeight;return true;
    }
    void BeginText(Point p){
        EndText(true);pending={};pending.tool=Tool::Text;pending.points={p};Style(pending);
        const int w=std::min(static_cast<int>(120*scale),selection.right-p.x),h=std::min(static_cast<int>((fontSize+14)*scale),selection.bottom-p.y);
        if(w<20||h<20)return;
        edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_MULTILINE|ES_AUTOVSCROLL,p.x,p.y,w,h,window,reinterpret_cast<HMENU>(4516),instance,nullptr);
        if(!edit)return;
        if(!UpdateTextBackground(p.x,p.y,w,h)){
            const auto pixel=composed.pixels[static_cast<std::size_t>(p.y)*composed.width+p.x];
            editBackground=CreateSolidBrush(RGB((pixel>>16)&255,(pixel>>8)&255,pixel&255));
        }
        SendMessageW(edit,EM_SETLIMITTEXT,4096,0);
        if(!direct_write_edit::Attach(edit)){DestroyWindow(edit);edit=nullptr;if(editBackground){DeleteObject(editBackground);editBackground=nullptr;}pending={};return;}
        UpdateTextStyle();
        SetWindowSubclass(edit,EditProcedure,1,reinterpret_cast<DWORD_PTR>(this));ShowWindow(edit,SW_SHOW);SetFocus(edit);
    }
    void ResizeTextEditor(){
        if(!edit||!selection.Valid())return;
        const int length=GetWindowTextLengthW(edit);std::wstring value(static_cast<std::size_t>(length)+1,L'\0');
        if(length>0)GetWindowTextW(edit,value.data(),length+1);value.resize(static_cast<std::size_t>(length));
        direct_write::Style style;style.size=static_cast<float>(fontSize);style.family=fontFamily.c_str();
        style.weight=bold?FW_BOLD:FW_NORMAL;style.italic=italic;style.flags=DT_NOPREFIX;
        const auto measured=direct_write::Measure(value,0,style);
        RECT editRect{};GetWindowRect(edit,&editRect);POINT topLeft{editRect.left,editRect.top};ScreenToClient(window,&topLeft);
        const int desiredWidth=std::max(static_cast<int>(80*scale),static_cast<int>(std::ceil(measured.Width+18*scale)));
        const int availableWidth=selection.right-static_cast<int>(topLeft.x),availableHeight=selection.bottom-static_cast<int>(topLeft.y);
        const int w=std::clamp(desiredWidth,static_cast<int>(40*scale),std::max(40,availableWidth));
        style.flags|=DT_WORDBREAK;
        const auto wrapped=direct_write::Measure(value,std::max(1.f,w-4*scale),style);
        const int desiredHeight=std::max(static_cast<int>((fontSize+14)*scale),static_cast<int>(std::ceil(wrapped.Height+8*scale)));
        const int h=std::clamp(desiredHeight,static_cast<int>(24*scale),std::max(24,availableHeight));
        UpdateTextBackground(topLeft.x,topLeft.y,w,h);
        SetWindowPos(edit,nullptr,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void Refresh(){composed=Composite(source,history.Items(),selection);Repaint();}
    void Repaint(){if(window){Layout();InvalidateRect(window,nullptr,FALSE);}}
    static Rect Unite(Rect a,Rect b){
        if(!a.Valid())return b;if(!b.Valid())return a;
        return {std::min(a.left,b.left),std::min(a.top,b.top),std::max(a.right,b.right),std::max(a.bottom,b.bottom)};
    }
    void Invalidate(Rect area){
        area=screenshot::Constrain(area,bounds);if(!window||!area.Valid())return;
        const RECT r{area.left,area.top,area.right,area.bottom};InvalidateRect(window,&r,FALSE);
    }
    void RepaintStroke(){
        const auto next=AnnotationBounds(pending,selection);
        Invalidate(Unite(paintedPending,next));paintedPending=next;
    }
    Rect MonitorBounds()const{
        POINT middle{origin.x+(selection.left+selection.right)/2,origin.y+(selection.top+selection.bottom)/2};
        MONITORINFO info{sizeof(info)};
        if(!GetMonitorInfoW(MonitorFromPoint(middle,MONITOR_DEFAULTTONEAREST),&info))return bounds;
        const Rect work{info.rcWork.left-origin.x,info.rcWork.top-origin.y,info.rcWork.right-origin.x,info.rcWork.bottom-origin.y};
        const Rect clipped{std::max(bounds.left,work.left),std::max(bounds.top,work.top),std::min(bounds.right,work.right),std::min(bounds.bottom,work.bottom)};
        return clipped.Valid()?clipped:bounds;
    }
    void Layout(){
        if(!selection.Valid()){
            toolbar={};options={};sizePopup={};sizeOpen=false;
            for(HWND control:{fontChoice,fontSizeChoice,strengthSlider,strokeSlider,strokeValue,arrowChoice})if(control)ShowWindow(control,SW_HIDE);
            return;
        }
        const Rect area=MonitorBounds();
        uiScale=std::min(scale,std::max(.65f,area.Width()/500.f));
        const int margin=1;
        const int cell=std::max(20,std::min(static_cast<int>(32*uiScale),area.Width()-2*margin));
        const int groupGap=std::max(2,static_cast<int>(4*uiScale));
        const bool singleRow=area.Width()>=CommandCount*cell+2*margin+groupGap*2;
        const int columns=singleRow?CommandCount:std::clamp((area.Width()-2*margin)/cell,1,CommandCount-1),rows=(CommandCount+columns-1)/columns;
        const bool settings=tool!=Tool::Select;
        const int optionCell=cell,optionGap=std::max(2,static_cast<int>(3*uiScale));
        const int paletteSwatch=std::max(9,static_cast<int>(12*uiScale)),paletteWidth=optionCell+12*paletteSwatch+4;
        const int barWidth=columns*cell+2*margin+(singleRow?groupGap*2:0),height=rows*cell+2;
        optionButtons.clear();optionIds.clear();fontRect=fontSizeRect=strengthRect=arrowRect={};
        int x=1,y=1,maxX=1;
        const auto reserve=[&](int requested){const int w=std::min(requested,area.Width()-2);
            if(x+w>area.Width()-1&&x>1){maxX=std::max(maxX,x);x=1;y+=optionCell;}
            const Rect r{x,y,x+w,y+optionCell};x+=w;maxX=std::max(maxX,x);return r;};
        const auto add=[&](int id,int w){const auto r=reserve(w);optionButtons.push_back(r);optionIds.push_back(id);};
        if(settings){
            if(tool!=Tool::Text)add(0,optionCell);
            if(tool==Tool::Shape)for(int v=0;v<4;++v)add(2+v,optionCell);
            if(tool==Tool::Arrow)arrowRect=reserve(std::max(88,static_cast<int>(100*uiScale)));
            if(tool==Tool::Mosaic){for(int v=0;v<4;++v)add(10+v,optionCell);strengthRect=reserve(std::max(100,static_cast<int>(120*uiScale)));}
            if(tool==Tool::Text){
                add(7,optionCell);add(8,optionCell);add(9,optionCell);
                fontRect=reserve(std::max(120,static_cast<int>(150*uiScale)));fontSizeRect=reserve(std::max(54,static_cast<int>(60*uiScale)));
            }
            if(tool!=Tool::Mosaic&&tool!=Tool::Eraser){
                const auto p=reserve(paletteWidth);optionButtons.push_back({p.left+2,p.top+3,p.left+optionCell-2,p.bottom-3});optionIds.push_back(44);
                for(int i=0;i<24;++i){const int px=p.left+optionCell+(i%12)*paletteSwatch,py=p.top+(i/12)*(optionCell/2);
                    optionButtons.push_back({px+1,py+2,px+paletteSwatch-1,py+optionCell/2-1});optionIds.push_back(20+i);}
            }
        }
        const int optionsWidth=settings?std::min(area.Width(),maxX+1):0,optionsHeight=settings?y+optionCell+1:0;
        const int combinedWidth=std::max(barWidth,optionsWidth),total=height+(settings?optionGap+optionsHeight:0);
        const auto combined=screenshot::ToolbarBounds(selection,area,combinedWidth,total,std::max(2,static_cast<int>(4*scale)));
        const int toolbarX=std::clamp(selection.right-barWidth,area.left,area.right-barWidth);
        toolbar={toolbarX,combined.top,toolbarX+barWidth,combined.top+height};
        for(int i=0;i<CommandCount;++i){const int extra=singleRow?((i>=7?groupGap:0)+(i>=9?groupGap:0)):0;
            const int bx=toolbar.left+margin+(i%columns)*cell+extra,by=toolbar.top+margin+(i/columns)*cell;buttons[i]={bx,by,bx+cell,by+cell};}
        const int optionX=std::clamp(toolbar.left,area.left,area.right-optionsWidth);
        options=settings?Rect{optionX,toolbar.bottom+optionGap,optionX+optionsWidth,toolbar.bottom+optionGap+optionsHeight}:Rect{};
        const auto place=[&](Rect& r){if(r.Valid())r={r.left+options.left,r.top+options.top,r.right+options.left,r.bottom+options.top};};
        for(auto& r:optionButtons)place(r);place(fontRect);place(fontSizeRect);place(strengthRect);place(arrowRect);
        if(controlsDpi!=static_cast<int>(scale*96)){
            const HFONT next=CreateFontW(-std::max(10,static_cast<int>(12*uiScale)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            if(next){for(HWND control:{fontChoice,fontSizeChoice,strengthSlider,strokeSlider,strokeValue,arrowChoice})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
                if(controlsFont)DeleteObject(controlsFont);controlsFont=next;}controlsDpi=static_cast<int>(scale*96);
            for(HWND control:{fontChoice,fontSizeChoice,arrowChoice}){
                const auto itemHeight=static_cast<LPARAM>(std::max(16,static_cast<int>(18*uiScale)));
                SendMessageW(control,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),itemHeight);SendMessageW(control,CB_SETITEMHEIGHT,0,itemHeight);
            }
        }
        const bool visible=settings&&!selecting&&!drag;
        const auto position=[&](HWND control,Rect r,bool show,bool combo){
            if(!control)return;
            if(show&&r.Valid())SetWindowPos(control,nullptr,r.left+2,r.top+std::max(1,(optionCell-std::max(18,static_cast<int>(22*uiScale)))/2),std::max(1,r.Width()-4),combo?std::max(180,static_cast<int>(220*uiScale)):std::max(18,static_cast<int>(22*uiScale)),SWP_NOZORDER|SWP_NOACTIVATE);
            ShowWindow(control,show&&r.Valid()?SW_SHOWNA:SW_HIDE);
        };
        updatingControls=true;
        SendMessageW(fontChoice,CB_SELECTSTRING,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(fontFamily.c_str()));
        const auto sizeText=std::to_wstring(fontSize);SendMessageW(fontSizeChoice,CB_SELECTSTRING,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(sizeText.c_str()));
        SendMessageW(arrowChoice,CB_SETCURSEL,variant,0);SendMessageW(strengthSlider,TBM_SETPOS,TRUE,mosaicStrength);
        position(fontChoice,fontRect,visible&&tool==Tool::Text,true);position(fontSizeChoice,fontSizeRect,visible&&tool==Tool::Text,true);
        position(arrowChoice,arrowRect,visible&&tool==Tool::Arrow,true);
        Rect sliderRect=strengthRect;sliderRect.right-=std::max(25,static_cast<int>(28*uiScale));position(strengthSlider,sliderRect,visible&&tool==Tool::Mosaic,false);
        sizePopup={};
        if(sizeOpen&&visible&&tool!=Tool::Text){
            const int pw=std::min(area.Width(),std::max(180,static_cast<int>(200*uiScale))),ph=optionCell+8;
            const int px=std::clamp(options.left,area.left,area.right-pw);
            int py=options.bottom+3;if(py+ph>area.bottom)py=std::max(area.top,toolbar.top-ph-3);
            sizePopup={px,py,px+pw,py+ph};
        }
        Rect sr=sizePopup;sr.right-=50;Rect vr=sizePopup;vr.left=vr.right-48;
        position(strokeSlider,sr,sizePopup.Valid(),false);position(strokeValue,vr,sizePopup.Valid(),false);
        SendMessageW(strokeSlider,TBM_SETPOS,TRUE,StrokeSize());const auto strokeText=std::to_wstring(StrokeSize());SetWindowTextW(strokeValue,strokeText.c_str());
        updatingControls=false;
        if(tooltip)for(int i=0;i<CommandCount;++i){TOOLINFOW ti{};ti.cbSize=TTTOOLINFOW_V2_SIZE;ti.hwnd=window;ti.uId=static_cast<UINT_PTR>(i+1);ti.rect={buttons[i].left,buttons[i].top,buttons[i].right,buttons[i].bottom};SendMessageW(tooltip,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&ti));}
        if(tooltip)for(std::size_t i=0;i<64;++i){TOOLINFOW ti{};ti.cbSize=TTTOOLINFOW_V2_SIZE;ti.hwnd=window;ti.uId=100+i;
            if(i<optionButtons.size()){const auto r=optionButtons[i];ti.rect={r.left,r.top,r.right,r.bottom};ti.lpszText=const_cast<wchar_t*>(OptionName(OptionId(i)));}
            else ti.lpszText=const_cast<wchar_t*>(L"");
            SendMessageW(tooltip,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&ti));SendMessageW(tooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&ti));}
    }
    int Button(Point p)const{if(!selection.Valid())return -1;for(int i=0;i<CommandCount;++i)if(buttons[i].Contains(p))return i;return -1;}
    bool Option(Point p){
        for(std::size_t i=0;i<optionButtons.size();++i)if(optionButtons[i].Contains(p)){
            const int id=OptionId(i);
            if(id==0)sizeOpen=!sizeOpen;
            else if(id>=2&&id<=5)variant=id-2;
            else if(id==7)bold=!bold;else if(id==8)italic=!italic;else if(id==9)outline=!outline;
            else if(id>=10&&id<=13){constexpr int variants[]{2,3,0,1};variant=variants[id-10];}
            else if(id>=20&&id<44)color=Colors[id-20];
            else if(id==44){
                static COLORREF custom[16]{};CHOOSECOLORW choice{sizeof(choice)};choice.hwndOwner=window;choice.lpCustColors=custom;choice.Flags=CC_FULLOPEN|CC_RGBINIT;
                choice.rgbResult=RGB((color>>16)&255,(color>>8)&255,color&255);busy=true;
                if(ChooseColorW(&choice))color=(static_cast<std::uint32_t>(GetRValue(choice.rgbResult))<<16)|(static_cast<std::uint32_t>(GetGValue(choice.rgbResult))<<8)|GetBValue(choice.rgbResult);
                busy=false;
            }
            UpdateTextStyle();UpdateLatestStyle();
            Repaint();return true;
        }
        return false;
    }
    Image Result()const{return selection.Valid()&&!drawing&&!selecting&&!drag&&!exportBlocked&&!edit?Crop(composed,selection):Image{};}
    void PinImage(Image image){
        pins.erase(std::remove_if(pins.begin(),pins.end(),[](const auto& p){return !p->window;}),pins.end());
        std::size_t pixels=image.pixels.size();for(const auto& pin:pins)pixels+=pin->image.pixels.size();
        if(pins.size()>=12||pixels>64u*1024u*1024u){error=L"\u8bf7\u5148\u5173\u95ed\u4e00\u5f20\u8d34\u56fe";Repaint();return;}
        auto pin=std::make_unique<Pin>();pin->owner=this;pin->image=std::move(image);
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpszClassName=PinClass;wc.lpfnWndProc=Pin::Proc;wc.hCursor=LoadCursorW(nullptr,IDC_SIZEALL);wc.style=CS_DBLCLKS;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
        HWND hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,PinClass,L"Lume \u8d34\u56fe",WS_POPUP|WS_BORDER,origin.x+selection.left,origin.y+selection.top,pin->image.width,pin->image.height,nullptr,nullptr,instance,pin.get());
        if(!hwnd){error=L"\u8d34\u56fe\u521b\u5efa\u5931\u8d25";Repaint();return;}
        pins.push_back(std::move(pin));Stop();ShowWindow(hwnd,SW_SHOW);SetForegroundWindow(hwnd);
    }
    void Action(Command command){
        if(busy)return;
        if(command==Command::Close){Stop();return;}
        if(drawing||selecting||drag)return;
        error.clear();EndText(true);
        if(exportBlocked&&command!=Command::Undo&&command!=Command::Redo){error=L"\u6807\u6ce8\u672a\u4fdd\u5b58\uff0c\u8bf7\u64a4\u9500\u540e\u91cd\u65b0\u6807\u6ce8";Repaint();return;}
        const int i=static_cast<int>(command);
        if(i<=static_cast<int>(Command::Eraser)){
            const Tool tools[]{Tool::Shape,Tool::Arrow,Tool::Pen,Tool::Marker,Tool::Mosaic,Tool::Text,Tool::Eraser};
            FreezeLatest();tool=tool==tools[i]?Tool::Select:tools[i];sizeOpen=false;variant=tool==Tool::Mosaic?2:0;if(tool==Tool::Marker)color=0xffff00;
            Repaint();return;
        }
        if(command==Command::Undo){FreezeLatest();exportBlocked=false;history.Undo();Refresh();return;}
        if(command==Command::Redo){FreezeLatest();if(exportBlocked){Repaint();return;}history.Redo();Refresh();return;}
        auto image=Result();if(!image.Valid())return;
        if(command==Command::Pin){PinImage(std::move(image));return;}
        busy=true;bool ok=false;
        if(command==Command::Copy)ok=Copy(window,image);
        else if(command==Command::Save)ok=Save(window,image);
        busy=false;
        if(ok){FreezeLatest();Stop();}else{
            if(command==Command::Copy)error=L"\u526a\u8d34\u677f\u6682\u4e0d\u53ef\u7528\uff0c\u8bf7\u91cd\u8bd5";
            else if(GetLastError()!=ERROR_CANCELLED)error=L"\u4fdd\u5b58\u5931\u8d25\uff0c\u8bf7\u91cd\u8bd5\u6216\u66f4\u6362\u76ee\u5f55";
            Repaint();}
    }
    void Down(Point p){
        if(busy)return;SetFocus(window);const int button=Button(p);
        if(button>=0){Action(static_cast<Command>(button));return;}
        if(options.Contains(p)){Option(p);return;}
        if(sizePopup.Contains(p))return;
        if(sizeOpen){sizeOpen=false;Repaint();}
        FreezeLatest();EndText(true);error.clear();pointer=p;anchor=screenshot::Clamp(p,bounds);
        if(selection.Valid()){
            const int handle=screenshot::HitHandle(selection,p,static_cast<int>(7*scale));
            if(handle!=screenshot::None&&handle!=screenshot::Move){drag=handle;startRect=selection;SetCapture(window);return;}
            if(selection.Contains(p)&&tool!=Tool::Select){
                if(tool==Tool::Text){BeginText(p);return;}
                pending={};pending.tool=tool;pending.points={p};Style(pending);
                drawing=true;paintedPending={};SetCapture(window);RepaintStroke();return;
            }
            if(handle==screenshot::Move){drag=handle;startRect=selection;SetCapture(window);return;}
        }
        selecting=true;selection={};history.Clear();exportBlocked=false;composed=source;SetCapture(window);Repaint();
    }
    void Motion(Point p){
        pointer=p;
        if(selecting){selection=screenshot::Normalize(anchor,screenshot::Clamp(p,bounds));Repaint();return;}
        if(drag){selection=screenshot::Drag(startRect,drag,{p.x-anchor.x,p.y-anchor.y},bounds);Repaint();return;}
        if(drawing){
            p=screenshot::Clamp(p,selection);
            // Keep one overflow point so History::Add rejects the whole stroke.
            // Silently truncating here could export an incomplete redaction.
            if(pending.points.size()<=20000){
                if(tool==Tool::Pen||tool==Tool::Marker||tool==Tool::Eraser||(tool==Tool::Mosaic&&variant>=2)){const auto last=pending.points.back();if(std::hypot(p.x-last.x,p.y-last.y)>=1)pending.points.push_back(p);}
                else if(pending.points.size()==1)pending.points.push_back(p);else pending.points.back()=p;
            }
            RepaintStroke();return;
        }
        const int next=Button(p);if(next!=hover){hover=next;Invalidate(toolbar);}
    }
    void Up(Point p){
        if(!selecting&&!drawing&&!drag)return;
        Motion(p);const bool wasDrawing=drawing,wasDrag=drag!=0;
        selecting=drawing=false;drag=0;if(GetCapture()==window)ReleaseCapture();
        if(wasDrawing){Commit(pending);pending={};paintedPending={};Refresh();}
        else if(wasDrag)Refresh();else Repaint();
    }
    void Paint(HDC dc,Rect dirty){
        if(!source.Valid())return;
        dirty=screenshot::Constrain(dirty,bounds);if(!dirty.Valid())return;
        // Restore only the damaged pixels. The frozen background and committed
        // annotations survive paint events; only the active stroke is replayed.
        if(drawing)dirty=Unite(dirty,AnnotationBounds(pending,selection));
        for(int y=dirty.top;y<dirty.bottom;++y){
            const auto offset=static_cast<std::size_t>(y)*source.width;
            std::memcpy(frame.pixels.data()+offset+dirty.left,dimmed.pixels.data()+offset+dirty.left,static_cast<std::size_t>(dirty.Width())*4);
            if(selection.Valid()&&y>=selection.top&&y<selection.bottom){
                const int left=std::max(dirty.left,selection.left),right=std::min(dirty.right,selection.right);
                if(right>left)std::memcpy(frame.pixels.data()+offset+left,composed.pixels.data()+offset+left,static_cast<std::size_t>(right-left)*4);
            }
        }
        if(drawing)RenderAnnotation(frame,source,pending,selection);
        Gdiplus::Bitmap buffer(frame.width,frame.height,frame.width*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(frame.pixels.data()));
        {Gdiplus::Graphics g(&buffer);
        g.SetClip(Gdiplus::Rect(dirty.left,dirty.top,dirty.Width(),dirty.Height()));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const auto intersects=[&](Rect rect){return rect.Valid()&&rect.left<dirty.right&&rect.right>dirty.left&&rect.top<dirty.bottom&&rect.bottom>dirty.top;};
        if(selection.Valid()){
            Gdiplus::Pen blue(Color(0x20a9ef),std::max(1.5f,1.5f*scale));g.DrawRectangle(&blue,Box(selection));
            const int r=std::max(3,static_cast<int>(4*scale));
            for(auto p:std::array<Point,8>{{{selection.left,selection.top},{(selection.left+selection.right)/2,selection.top},{selection.right,selection.top},{selection.right,(selection.top+selection.bottom)/2},{selection.right,selection.bottom},{(selection.left+selection.right)/2,selection.bottom},{selection.left,selection.bottom},{selection.left,(selection.top+selection.bottom)/2}}}){
                Gdiplus::SolidBrush b(Color(0x20a9ef));Gdiplus::Pen white(Color(0xffffff),1.5f*scale);
                g.FillEllipse(&b,static_cast<float>(p.x-r),static_cast<float>(p.y-r),static_cast<float>(r*2),static_cast<float>(r*2));g.DrawEllipse(&white,static_cast<float>(p.x-r),static_cast<float>(p.y-r),static_cast<float>(r*2),static_cast<float>(r*2));
            }
            const auto size=std::to_wstring(selection.Width())+L" x "+std::to_wstring(selection.Height())+L" px";
            const int labelW=std::min(bounds.Width(),static_cast<int>((size.size()*8.f+12)*scale)),labelH=static_cast<int>(27*scale);
            const int lx=std::clamp(selection.left,0,bounds.right-labelW),ly=selection.top>=labelH+8?selection.top-labelH-8:selection.top+8;
            Fill(g,{lx,ly,lx+labelW,ly+labelH},0x191b1e,225);Text(g,size,{lx+5,ly+2,lx+labelW,ly+labelH},14*scale,0xffffff);
            if(!selecting&&!drag){
                Gdiplus::Pen edge(Color(0x6f9abe),1);
                if(intersects(toolbar)){
                Fill(g,toolbar,0xfafafa);
                g.DrawRectangle(&edge,Box(toolbar));
                for(const int next:{7,9})if(buttons[next].top==buttons[next-1].top&&buttons[next].left>buttons[next-1].right){
                    const float x=(buttons[next].left+buttons[next-1].right)*.5f;
                    Gdiplus::Pen divider(Color(0xc2c2c2),1);g.DrawLine(&divider,x,buttons[next].top+8*uiScale,x,buttons[next].bottom-8*uiScale);
                }
                const Tool tools[]{Tool::Shape,Tool::Arrow,Tool::Pen,Tool::Marker,Tool::Mosaic,Tool::Text,Tool::Eraser};
                for(int i=0;i<CommandCount;++i){auto rct=buttons[i];const bool selected=i<7&&tool==tools[i];
                    if(selected||hover==i)Fill(g,rct,selected?0xdcecfb:0xe3effb);
                    const bool disabled=(i==7&&!history.CanUndo())||(i==8&&!history.CanRedo());
                    const float icon=std::min(20.f*scale,rct.Width()*.60f),x=rct.left+(rct.Width()-icon)/2,y=rct.top+(rct.Height()-icon)/2;
                    DrawIcon(g,static_cast<Command>(i),x,y,icon,Color(0x111111,disabled?100:255));
                }
                }
                if(intersects(options)){
                    Fill(g,options,0xfafafa);g.DrawRectangle(&edge,Box(options));
                    for(std::size_t index=0;index<optionButtons.size();++index){const int i=OptionId(index);const auto rct=optionButtons[index];const float cx=(rct.left+rct.right)*.5f,cy=(rct.top+rct.bottom)*.5f;
                        if(i>=20&&i<44){Fill(g,rct,Colors[i-20]);Gdiplus::Pen border(Color(color==Colors[i-20]?0x2184ee:0x999999),1);g.DrawRectangle(&border,Box(rct));continue;}
                        if(i==44){Fill(g,rct,color);Gdiplus::Pen border(Color(0x555555),1);g.DrawRectangle(&border,Box(rct));continue;}
                        if(i==0){Fill(g,rct,0xdcecfb);const int value=StrokeSize();
                            if(tool==Tool::Mosaic||tool==Tool::Eraser||value>8)Text(g,std::to_wstring(value),{rct.left+3,rct.top+5,rct.right,rct.bottom},12*uiScale,0x111111);
                            else {Gdiplus::SolidBrush ink(Color(0x111111));const float radius=std::max(1.5f,value*.5f);g.FillEllipse(&ink,cx-radius,cy-radius,2*radius,2*radius);}continue;}
                        if(i>=7&&i<=9){const bool selected=i==7?bold:i==8?italic:outline;if(selected)Fill(g,rct,0xdcecfb);
                            direct_write::Style style;style.size=18*uiScale;style.family=L"Times New Roman";
                            style.weight=i==7?FW_BOLD:FW_NORMAL;style.italic=i==8;style.flags=DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_NOPREFIX;
                            const wchar_t label[]{i==7?L'B':i==8?L'I':L'A',0};
                            direct_write::Draw(g,label,Box(rct),Color(0x111111),style);continue;}
                        int v=i-2;if(i>=10){constexpr int variants[]{2,3,0,1};v=variants[i-10];}
                        const bool selected=variant==v;if(selected)Fill(g,rct,0xdcecfb);
                        Gdiplus::Pen pen(Color(0x111111),1*uiScale);Gdiplus::SolidBrush ink(Color(0x111111));const float s=7*uiScale;
                        if(i>=2&&i<=5){if(v==0)g.DrawRectangle(&pen,cx-s,cy-s,s*2,s*2);else if(v==1)g.DrawEllipse(&pen,cx-s,cy-s,s*2,s*2);else if(v==2)g.FillRectangle(&ink,cx-s,cy-s,s*2,s*2);else g.FillEllipse(&ink,cx-s,cy-s,s*2,s*2);}
                        else if(i>=10&&i<=13){
                            if(v>=2)g.DrawEllipse(&pen,cx-s-2,cy-s-2,s*2+4,s*2+4);
                            if(v%2==0)DrawIcon(g,Command::Mosaic,cx-s,cy-s,s*2,Color(0x111111));
                            else for(int j=0;j<4;++j)g.DrawLine(&pen,cx-s,cy-s+j*4*uiScale,cx+s,cy-s+j*4*uiScale);
                        }
                    }
                    if(tool==Tool::Mosaic&&strengthRect.Valid())Text(g,std::to_wstring(mosaicStrength),{strengthRect.right-static_cast<int>(26*uiScale),strengthRect.top+5,strengthRect.right,strengthRect.bottom},12*uiScale,0x333333);
                }
                if(intersects(sizePopup)){Fill(g,sizePopup,0xfafafa);g.DrawRectangle(&edge,Box(sizePopup));}
            }
        }
        if(!error.empty()){const int y=selection.Valid()?std::min(bounds.bottom-35,toolbar.bottom+8):20;Fill(g,{20,y,std::min(bounds.right,420),y+30},0x222222);Text(g,error,{25,y+3,std::min(bounds.right,420),y+30},14,0xffffff);}
        }
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=frame.width;info.bmiHeader.biHeight=-dirty.Height();
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        // Present a top-down view of just these rows, retaining the full stride.
        // Source Y is always zero, avoiding partial-DIB Y-origin ambiguity that
        // previously copied unrelated rows over toolbar icons and live strokes.
        const auto* rows=frame.pixels.data()+static_cast<std::size_t>(dirty.top)*frame.width;
        StretchDIBits(dc,dirty.left,dirty.top,dirty.Width(),dirty.Height(),dirty.left,0,dirty.Width(),dirty.Height(),rows,&info,DIB_RGB_COLORS,SRCCOPY);
    }
    void Key(WPARAM key){
        if(busy)return;
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        if(key==VK_ESCAPE){Stop();return;}
        if(ctrl&&key=='Z'){Action((GetKeyState(VK_SHIFT)&0x8000)?Command::Redo:Command::Undo);return;}
        if(ctrl&&key=='Y'){Action(Command::Redo);return;}
        if(ctrl&&key=='C'){Action(Command::Copy);return;}
        if(ctrl&&key=='S'){Action(Command::Save);return;}
        if(key==VK_RETURN&&selection.Valid()){Action(Command::Copy);return;}
        if(key==VK_SPACE){EndText(true);tool=Tool::Select;Repaint();return;}
        if(!selection.Valid()||tool!=Tool::Select)return;
        const int step=(GetKeyState(VK_SHIFT)&0x8000)?10:1;
        Point delta{};if(key==VK_LEFT)delta.x=-step;else if(key==VK_RIGHT)delta.x=step;else if(key==VK_UP)delta.y=-step;else if(key==VK_DOWN)delta.y=step;else return;
        selection=screenshot::Drag(selection,screenshot::Move,delta,bounds);Refresh();
    }
    void ControlCommand(HWND control,int notification){
        if(updatingControls||busy)return;
        if((notification==CBN_SELCHANGE||notification==CBN_SELENDOK)&&(control==fontChoice||control==fontSizeChoice||control==arrowChoice)){
            const bool dropped=SendMessageW(control,CB_GETDROPPEDSTATE,0,0)!=0;
            // An open list is still a tentative choice; Escape must cancel it.
            if(notification==CBN_SELCHANGE&&dropped)return;
            const auto index=SendMessageW(control,CB_GETCURSEL,0,0);if(index==CB_ERR)return;
            if(control==arrowChoice)variant=static_cast<int>(index);
            else {wchar_t value[160]{};SendMessageW(control,CB_GETLBTEXT,static_cast<WPARAM>(index),reinterpret_cast<LPARAM>(value));
                if(control==fontChoice)fontFamily=value;else fontSize=std::clamp(_wtoi(value),10,200);}
            UpdateTextStyle();UpdateLatestStyle();if(edit)ResizeTextEditor();if(!dropped)SetFocus(edit?edit:window);Repaint();return;
        }
        if(control==edit&&notification==EN_CHANGE){ResizeTextEditor();return;}
        if(control==strokeValue&&notification==EN_CHANGE){
            wchar_t value[8]{};GetWindowTextW(control,value,8);if(!value[0])return;
            SetStrokeSize(_wtoi(value));SendMessageW(strokeSlider,TBM_SETPOS,TRUE,StrokeSize());Invalidate(options);
        }
    }
    HCURSOR BrushCursor(){
        const int diameter=std::clamp(brushSize,3,100);
        if(brushCursor&&cursorDiameter==diameter)return brushCursor;
        if(brushCursor){DestroyCursor(brushCursor);brushCursor=nullptr;}
        const int size=(diameter+7+15)/16*16,stride=(size+15)/16*2;
        std::vector<BYTE> andMask(static_cast<std::size_t>(stride)*size,0xff),xorMask(andMask.size(),0);
        const double center=size/2.,radius=diameter/2.;
        for(int y=0;y<size;++y)for(int x=0;x<size;++x){const double distance=std::hypot(x-center,y-center);
            if(std::abs(distance-radius)>.85&&!(x==size/2&&std::abs(y-size/2)<=2)&&!(y==size/2&&std::abs(x-size/2)<=2))continue;
            const auto index=static_cast<std::size_t>(y)*stride+x/8;const auto bit=static_cast<BYTE>(0x80>>(x%8));andMask[index]&=static_cast<BYTE>(~bit);
            if(distance<=radius)xorMask[index]|=bit;
        }
        brushCursor=CreateCursor(instance,size/2,size/2,size,size,andMask.data(),xorMask.data());cursorDiameter=diameter;
        return brushCursor?brushCursor:LoadCursorW(nullptr,IDC_CROSS);
    }
    static LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,w,l);
        try{
            switch(message){
            case WM_ERASEBKGND:return 1;
            case WM_MEASUREITEM:{auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(l);if(item&&item->CtlType==ODT_COMBOBOX){item->itemHeight=static_cast<UINT>(std::max(16,static_cast<int>(18*self->uiScale)));return TRUE;}break;}
            case WM_DRAWITEM:{auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item&&item->CtlType==ODT_COMBOBOX){self->DrawChoice(*item);return TRUE;}break;}
            case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);self->Paint(dc,{ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right,ps.rcPaint.bottom});EndPaint(hwnd,&ps);return 0;}
            case WM_PRINTCLIENT:self->Paint(reinterpret_cast<HDC>(w),self->bounds);return 0;
            case WM_LBUTTONDOWN:self->Down({GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;
            case WM_LBUTTONUP:self->Up({GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;
            case WM_MOUSEMOVE:self->Motion({GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;
            case WM_RBUTTONUP:self->EndText(false);if(self->tool!=Tool::Select){self->tool=Tool::Select;self->Repaint();}else self->Stop();return 0;
            case WM_KEYDOWN:self->Key(w);return 0;
            case WM_COMMAND:
                if(l)self->ControlCommand(reinterpret_cast<HWND>(l),HIWORD(w));
                else if(LOWORD(w)>=ScreenshotEditor::CommandFirst&&LOWORD(w)<ScreenshotEditor::CommandFirst+CommandCount&&self->selection.Valid())self->Action(static_cast<Command>(LOWORD(w)-ScreenshotEditor::CommandFirst));return 0;
            case WM_HSCROLL:
                if(!self->updatingControls&&l){const HWND control=reinterpret_cast<HWND>(l);
                    if(control==self->strengthSlider)self->mosaicStrength=static_cast<int>(SendMessageW(control,TBM_GETPOS,0,0));
                    else if(control==self->strokeSlider){self->SetStrokeSize(static_cast<int>(SendMessageW(control,TBM_GETPOS,0,0)));self->updatingControls=true;
                        const auto value=std::to_wstring(self->StrokeSize());SetWindowTextW(self->strokeValue,value.c_str());self->updatingControls=false;}
                    self->UpdateLatestStyle();self->Invalidate(self->options);
                }return 0;
            case WM_MOUSEWHEEL:
                if(self->selection.Valid()&&!self->drawing&&self->tool!=Tool::Text&&self->tool!=Tool::Select){self->SetStrokeSize(self->StrokeSize()+(GET_WHEEL_DELTA_WPARAM(w)>0?1:-1));self->UpdateLatestStyle();self->Repaint();}return 0;
            case WM_CTLCOLOREDIT:
                if(reinterpret_cast<HWND>(l)==self->edit){SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);
                    POINT origin{};LPtoDP(reinterpret_cast<HDC>(w),&origin,1);
                    SetBrushOrgEx(reinterpret_cast<HDC>(w),origin.x,origin.y,nullptr);
                    SetTextColor(reinterpret_cast<HDC>(w),RGB((self->color>>16)&255,(self->color>>8)&255,self->color&255));
                    return reinterpret_cast<LRESULT>(self->editBackground?self->editBackground:GetStockObject(WHITE_BRUSH));}
                [[fallthrough]];
            case WM_CTLCOLORSTATIC:SetBkColor(reinterpret_cast<HDC>(w),RGB(250,250,250));SetTextColor(reinterpret_cast<HDC>(w),RGB(50,50,50));return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
            case WM_LBUTTONDBLCLK:if(self->tool==Tool::Select&&self->selection.Contains({GET_X_LPARAM(l),GET_Y_LPARAM(l)}))self->Action(Command::Copy);return 0;
            case WM_CAPTURECHANGED:if(!self->closing&&(self->selecting||self->drawing||self->drag)){
                if(self->drawing)self->Commit(self->pending);
                self->selecting=self->drawing=false;self->drag=0;self->pending={};self->Refresh();}return 0;
            case WM_DISPLAYCHANGE:case WM_CLOSE:self->Stop();return 0;
            case WM_DPICHANGED:self->scale=static_cast<float>(HIWORD(w))/96;self->Repaint();return 0;
            case WM_SETCURSOR:{
                POINT p{};GetCursorPos(&p);ScreenToClient(hwnd,&p);LPCWSTR cursor=IDC_CROSS;
                const Point point{p.x,p.y};const int hit=self->selection.Valid()?screenshot::HitHandle(self->selection,point,static_cast<int>(7*self->scale)):0;
                if(self->Button(point)>=0||self->options.Contains(point))cursor=IDC_ARROW;
                else if(hit==(screenshot::Left|screenshot::Top)||hit==(screenshot::Right|screenshot::Bottom))cursor=IDC_SIZENWSE;
                else if(hit==(screenshot::Right|screenshot::Top)||hit==(screenshot::Left|screenshot::Bottom))cursor=IDC_SIZENESW;
                else if(hit==screenshot::Left||hit==screenshot::Right)cursor=IDC_SIZEWE;
                else if(hit==screenshot::Top||hit==screenshot::Bottom)cursor=IDC_SIZENS;
                else if(hit==screenshot::Move&&self->tool==Tool::Select)cursor=IDC_SIZEALL;
                else if(self->tool==Tool::Text)cursor=IDC_IBEAM;
                else if(self->selection.Contains(point)&&(self->tool==Tool::Eraser||(self->tool==Tool::Mosaic&&self->variant>=2))){SetCursor(self->BrushCursor());return TRUE;}
                SetCursor(LoadCursorW(nullptr,cursor));return TRUE;}
            case WM_NCDESTROY:if(self->window==hwnd){self->window=nullptr;self->active=false;}SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);break;
            }
        }catch(...){self->Stop();return 0;}
        return DefWindowProcW(hwnd,message,w,l);
    }
};
ScreenshotEditor::ScreenshotEditor(HINSTANCE i,EditorPorts p):impl_(std::make_unique<Impl>(i,std::move(p))){}
ScreenshotEditor::~ScreenshotEditor()=default;
bool ScreenshotEditor::Start(){try{return impl_->Start();}catch(...){impl_->Stop();return false;}}
void ScreenshotEditor::Stop(){impl_->Stop();}
bool ScreenshotEditor::Active()const{return impl_->active;}
HWND ScreenshotEditor::Handle()const{return impl_->window;}
Image ScreenshotEditor::Result()const{return impl_->Result();}
}
