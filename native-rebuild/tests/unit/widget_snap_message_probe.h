#pragma once
#include "platform/windows/widget_ui/widget_snap_protocol.h"

namespace lume::test {
// Observe delivery to an owned real window without replacing its behavior.
class WidgetSnapMessageProbe final {
public:
    explicit WidgetSnapMessageProbe(HWND window):window_(window) {
        if(!SetPropW(window_,Property,reinterpret_cast<HANDLE>(this)))return;
        original_=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window_,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Procedure)));
        if(!original_)RemovePropW(window_,Property);
    }
    ~WidgetSnapMessageProbe(){
        if(original_&&IsWindow(window_)){
            SetWindowLongPtrW(window_,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original_));
            RemovePropW(window_,Property);
        }
    }
    WidgetSnapMessageProbe(const WidgetSnapMessageProbe&)=delete;
    WidgetSnapMessageProbe& operator=(const WidgetSnapMessageProbe&)=delete;
    bool Attached()const{return original_!=nullptr;}
    unsigned Count()const{return messages_;}
private:
    static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<WidgetSnapMessageProbe*>(GetPropW(window,Property));
        if(!self)return DefWindowProcW(window,message,w,l);
        if(message==platform::windows::widget_snap::Message)++self->messages_;
        return CallWindowProcW(self->original_,window,message,w,l);
    }
    static constexpr wchar_t Property[]=L"LumeSnapRegressionMessageProbe";
    HWND window_=nullptr;WNDPROC original_=nullptr;unsigned messages_=0;
};
}
