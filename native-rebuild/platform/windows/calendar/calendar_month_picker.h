#pragma once
#include "platform/windows/calendar/calendar_renderer.h"
#include <windows.h>
#include <functional>
namespace lume::platform::windows {
class CalendarMonthPicker final {
public:
    CalendarMonthPicker()=default;
    ~CalendarMonthPicker();
    CalendarMonthPicker(const CalendarMonthPicker&)=delete;
    CalendarMonthPicker& operator=(const CalendarMonthPicker&)=delete;
    bool Open(HINSTANCE,HWND owner,CalendarRect screenRect,unsigned dpi,const calendar::Preferences&,calendar::Date selected,calendar::Date today,std::function<void(int,int)> select,std::function<void(bool)> closed,bool materialReady=false);
    void Close(bool restoreFocus=false);
    void SetMaterialReady(bool ready);
    HWND Handle() const { return window_; }
    bool IsOpen() const { return window_!=nullptr; }
    int DraftYear() const { return view_.year; }
    int FocusedMonth() const { return view_.month; }
    bool MaterialReady() const { return view_.materialReady; }
private:
    static LRESULT CALLBACK WindowProc(HWND,UINT,WPARAM,LPARAM);
    void Paint();
    void Key(UINT key);
    void ReadYear();
    void EditYear();
    void Select();
    int Hit(float,float) const;
    HWND window_=nullptr,owner_=nullptr;
    unsigned dpi_=96;
    float width_=304,height_=231;
    calendar::Preferences preferences_;
    CalendarPickerRenderState view_;
    std::unique_ptr<CalendarRenderer> renderer_;
    std::function<void(int,int)> select_;
    std::function<void(bool)> closed_;
};
}
