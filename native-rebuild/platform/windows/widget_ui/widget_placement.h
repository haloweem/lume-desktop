#pragma once
#include "core/window/screen_fit.h"
#include "platform/windows/lume_window_ids.h"
#include <windows.h>
#include <shellscalingapi.h>
#include <string>
#include <vector>
// Screen-size adaptation shared by desktop widgets: default sizes shrink on
// small displays, sizes fit the work area, and first placements avoid the
// widgets already on the desktop.
namespace lume::platform::windows::widget_placement {
inline window::PhysicalRect ToRect(const RECT& r) {return {r.left,r.top,r.right-r.left,r.bottom-r.top};}
inline RECT ToRECT(const window::PhysicalRect& r) {return {r.x,r.y,r.x+r.width,r.y+r.height};}
// FitScale for the monitor's size in DIP at its effective DPI.
inline double MonitorFitScale(HMONITOR monitor) {
    MONITORINFO info{};info.cbSize=sizeof(info);
    if(!monitor||!GetMonitorInfoW(monitor,&info))return 1.0;
    UINT dpiX=96,dpiY=96;
    if(FAILED(GetDpiForMonitor(monitor,MDT_EFFECTIVE_DPI,&dpiX,&dpiY))||!dpiX||!dpiY)dpiX=dpiY=96;
    return window::FitScale((info.rcMonitor.right-info.rcMonitor.left)*96.0/dpiX,
                            (info.rcMonitor.bottom-info.rcMonitor.top)*96.0/dpiY);
}
// Visible Lume desktop windows (widgets, clock, calendar, Dock, island) on the
// monitor, excluding `own`.
inline std::vector<window::PhysicalRect> Occupied(HWND own,HMONITOR monitor) {
    struct Data {HWND own;HMONITOR monitor;std::vector<window::PhysicalRect> rects;} data{own,monitor,{}};
    EnumWindows([](HWND w,LPARAM l)->BOOL{
        auto& d=*reinterpret_cast<Data*>(l);DWORD pid=0;GetWindowThreadProcessId(w,&pid);
        if(w==d.own||pid!=GetCurrentProcessId()||!IsWindowVisible(w))return TRUE;
        wchar_t cls[96]{};GetClassNameW(w,cls,96);
        if(!lume::window_ids::HasDesktopClassPrefix(cls)&&!GetPropW(w,lume::window_ids::DesktopWidgetProp))return TRUE;
        RECT r{};
        if(GetWindowRect(w,&r)&&r.right>r.left&&r.bottom>r.top&&MonitorFromRect(&r,MONITOR_DEFAULTTONULL)==d.monitor)
            d.rects.push_back(ToRect(r));
        return TRUE;
    },reinterpret_cast<LPARAM>(&data));
    return data.rects;
}
// Where a widget shown for the first time goes: top-right of the work area,
// clear of the other Lume desktop windows. Margin and gap are in DIP.
inline POINT FirstPosition(HWND own,HMONITOR monitor,const RECT& work,SIZE size,unsigned dpi,int marginDip=32,int gapDip=16) {
    const auto rect=window::FreeSlot(ToRect(work),size.cx,size.cy,MulDiv(marginDip,static_cast<int>(dpi),96),
                                     MulDiv(gapDip,static_cast<int>(dpi),96),Occupied(own,monitor));
    return {rect.x,rect.y};
}
}
