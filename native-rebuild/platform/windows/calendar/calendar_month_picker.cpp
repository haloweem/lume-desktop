#include "platform/windows/calendar/calendar_month_picker.h"
#include <windowsx.h>
#include <algorithm>
#include <cstring>
#include <utility>
namespace lume::platform::windows {
namespace { constexpr wchar_t kClass[]=L"LumeNativeCalendarMonthPicker"; }
CalendarMonthPicker::~CalendarMonthPicker() { closed_={}; Close(); }
bool CalendarMonthPicker::Open(HINSTANCE instance,HWND owner,CalendarRect screen,unsigned dpi,const calendar::Preferences& p,calendar::Date selected,calendar::Date today,std::function<void(int,int)> select,std::function<void(bool)> closed,bool materialReady) {
    Close(); owner_=owner; preferences_=p; dpi_=dpi; width_=screen.width; height_=screen.height;
    select_=std::move(select); closed_=std::move(closed);
    view_={selected.year,selected.month,selected.month+2,false,{},selected,today,0,false};
    view_.materialReady=materialReady;
    renderer_=std::make_unique<CalendarRenderer>();
    if(!renderer_->Initialize()) {renderer_.reset();return false;}
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=WindowProc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=kClass;
    if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){renderer_.reset();return false;}
    window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,kClass,L"选择月份",WS_POPUP,static_cast<int>(screen.x),static_cast<int>(screen.y),MulDiv(static_cast<int>(width_),static_cast<int>(dpi),96),MulDiv(static_cast<int>(height_),static_cast<int>(dpi),96),owner,nullptr,instance,this);
    if(!window_){renderer_.reset();return false;}
    Paint(); ShowWindow(window_,SW_SHOW); SetFocus(window_); return true;
}
void CalendarMonthPicker::Close(bool restoreFocus) {
    if(!window_) {renderer_.reset();return;}
    const auto notify=closed_; const auto owner=owner_; const auto old=window_;
    window_=nullptr; DestroyWindow(old);renderer_.reset();
    if(restoreFocus&&IsWindow(owner)) SetFocus(owner);
    if(notify) notify(restoreFocus);
}
void CalendarMonthPicker::Paint() {
    if(!window_) return;
    CalendarBitmap frame; if(!renderer_||!renderer_->RenderPicker(preferences_,view_,width_,height_,dpi_,frame)) return;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=static_cast<LONG>(frame.width);info.bmiHeader.biHeight=-static_cast<LONG>(frame.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);void* pixels=nullptr;HBITMAP dib=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(dc&&dib&&pixels) { std::memcpy(pixels,frame.pixels.data(),frame.pixels.size()*4);auto old=SelectObject(dc,dib);POINT source{};SIZE size{static_cast<LONG>(frame.width),static_cast<LONG>(frame.height)};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(window_,screen,nullptr,&size,dc,&source,0,&blend,ULW_ALPHA);SelectObject(dc,old); }
    if(dib) DeleteObject(dib);
    if(dc) DeleteDC(dc);
    if(screen) ReleaseDC(nullptr,screen);
}
void CalendarMonthPicker::SetMaterialReady(bool ready) {
    if(view_.materialReady==ready)return;
    view_.materialReady=ready;
    Paint();
}
void CalendarMonthPicker::ReadYear() {
    if(!view_.editing) return;
    int year=0;bool valid=!view_.draft.empty()&&view_.draft.size()<=4;
    for(wchar_t ch:view_.draft) { if(ch<L'0'||ch>L'9') {valid=false;break;}year=year*10+ch-L'0'; }
    if(valid&&year>=1&&year<=9999) view_.year=year;
    view_.editing=false; view_.draft.clear();view_.selectAll=false;
}
void CalendarMonthPicker::EditYear() {
    if(view_.editing)return;
    view_.editing=true;view_.draft=std::to_wstring(view_.year);view_.caret=view_.draft.size();
}
void CalendarMonthPicker::Select() { ReadYear(); const auto select=select_;const int year=view_.year,month=view_.month;Close(true);if(select) select(year,month); }
void CalendarMonthPicker::Key(UINT key) {
    if(key==VK_ESCAPE) {Close(true);return;}
    if(view_.focus==1&&(key==VK_HOME||key==VK_END||key==VK_LEFT||key==VK_RIGHT||key==VK_DELETE||(key==L'A'&&GetKeyState(VK_CONTROL)<0))) {
        EditYear();
        if(key==L'A')view_.selectAll=true;
        else {
            if(key==VK_HOME)view_.caret=0;
            if(key==VK_END)view_.caret=view_.draft.size();
            if(key==VK_LEFT&&view_.caret>0)--view_.caret;
            if(key==VK_RIGHT&&view_.caret<view_.draft.size())++view_.caret;
            if(key==VK_DELETE){if(view_.selectAll){view_.draft.clear();view_.caret=0;}else if(view_.caret<view_.draft.size())view_.draft.erase(view_.caret,1);}
            view_.selectAll=false;
        }
        Paint();return;
    }
    if(key==VK_TAB) {
        ReadYear();const bool back=GetKeyState(VK_SHIFT)<0;
        int next=view_.focus+(back?-1:1);
        if(view_.focus>=3&&view_.focus<=14) next=back?2:15;
        if(next==3) next=view_.month+2;
        if(next<0||next>15) {Close(false);return;}
        view_.focus=next;Paint();return;
    }
    if(view_.focus==1&&(key==VK_UP||key==VK_DOWN)) {ReadYear();view_.year=std::clamp(view_.year+(key==VK_UP?1:-1),1,9999);Paint();return;}
    if(key==VK_PRIOR||key==VK_NEXT) {ReadYear();view_.year=std::clamp(view_.year+(key==VK_PRIOR?-1:1),1,9999);Paint();return;}
    if(key==VK_RETURN||key==VK_SPACE) {
        if(view_.focus==0||view_.focus==2) {ReadYear();view_.year=std::clamp(view_.year+(view_.focus==0?-1:1),1,9999);Paint();return;}
        if(view_.focus==1) {ReadYear();view_.month=view_.selected.month;view_.focus=view_.month+2;Paint();return;}
        if(view_.focus==15) {view_.year=view_.today.year;view_.month=view_.today.month;Select();return;}
        Select();return;
    }
    if(view_.focus>=3&&view_.focus<=14) {
        int delta=key==VK_LEFT?-1:key==VK_RIGHT?1:key==VK_UP?-4:key==VK_DOWN?4:0;
        if(delta) view_.month=(view_.month-1+delta+12)%12+1;
        if(key==VK_HOME) view_.month=1;
        if(key==VK_END) view_.month=12;
        view_.focus=view_.month+2;Paint();
    }
}
int CalendarMonthPicker::Hit(float x,float y) const {
    const bool compact=height_<=203;const float p=compact?11.0f:15.0f,gap=compact?8.0f:12.0f,mg=compact?4.0f:6.0f,mh=compact?30.0f:32.0f;
    if(CalendarRect{p,p,30,30}.Contains(x,y)) return 0;
    if(CalendarRect{width_-p-30,p,30,30}.Contains(x,y)) return 2;
    if(CalendarRect{p+40,p,width_-2*p-80,30}.Contains(x,y)) return 1;
    const float top=p+30+gap,cw=(width_-2*p-3*mg)/4;
    for(int i=0;i<12;++i) if(CalendarRect{p+static_cast<float>(i%4)*(cw+mg),top+static_cast<float>(i/4)*(mh+mg),cw,mh}.Contains(x,y)) return i+3;
    if(CalendarRect{(width_-96)/2,top+3*mh+2*mg+gap+(compact?8.0f:10.0f),96,28}.Contains(x,y)) return 15;
    return -1;
}
LRESULT CALLBACK CalendarMonthPicker::WindowProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    auto* self=reinterpret_cast<CalendarMonthPicker*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(m==WM_NCCREATE) {self=static_cast<CalendarMonthPicker*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window_=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self) return DefWindowProcW(w,m,wp,lp);
    switch(m) {
    case WM_ERASEBKGND:return 1;
    case WM_KEYDOWN:self->Key(static_cast<UINT>(wp));return 0;
    case WM_CHAR:
        if(self->view_.focus==1) {
            auto& v=self->view_;
            if(wp==1){self->EditYear();v.selectAll=true;self->Paint();return 0;}
            if(wp!=VK_BACK&&(wp<L'0'||wp>L'9'))return 0;
            self->EditYear();
            if(v.selectAll){v.draft.clear();v.caret=0;v.selectAll=false;}
            if(wp==VK_BACK) {if(v.caret>0){v.draft.erase(v.caret-1,1);--v.caret;}}
            else if(v.draft.size()<4) {v.draft.insert(v.caret,1,static_cast<wchar_t>(wp));++v.caret;}
            self->Paint();
        }return 0;
    case WM_LBUTTONDOWN: {
        const int hit=self->Hit(static_cast<float>(GET_X_LPARAM(lp))*96/self->dpi_,static_cast<float>(GET_Y_LPARAM(lp))*96/self->dpi_);
        if(hit>=0) {self->ReadYear();self->view_.focus=hit;if(hit>=3&&hit<=14)self->view_.month=hit-2;if(hit!=1)self->Key(VK_RETURN);else {self->view_.selectAll=true;self->Paint();}}return 0;
    }
    case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE) self->Close(false);return 0;
    case WM_KILLFOCUS:if(reinterpret_cast<HWND>(wp)!=self->owner_) self->Close(false);return 0;
    case WM_CLOSE:self->Close(false);return 0;
    case WM_NCDESTROY:{
        SetWindowLongPtrW(w,GWLP_USERDATA,0);
        if(self->window_==w){self->window_=nullptr;self->renderer_.reset();const auto callback=self->closed_;if(callback)callback(false);}
        return DefWindowProcW(w,m,wp,lp);
    }
    default:break;
    }
    return DefWindowProcW(w,m,wp,lp);
}
}
