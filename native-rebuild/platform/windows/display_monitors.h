#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace lume::platform::windows {
struct DisplayMonitor {
    std::wstring device;
    RECT bounds{}, work{};
    bool primary=false;
};
inline bool operator==(const DisplayMonitor& a,const DisplayMonitor& b) {
    return a.device==b.device&&a.primary==b.primary&&EqualRect(&a.bounds,&b.bounds)&&EqualRect(&a.work,&b.work);
}

inline std::vector<DisplayMonitor> ConnectedDisplayMonitors() {
    std::vector<DisplayMonitor> monitors;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL{
        MONITORINFOEXW info{};info.cbSize=sizeof(info);
        if(GetMonitorInfoW(monitor,&info)&&!IsRectEmpty(&info.rcMonitor)&&!IsRectEmpty(&info.rcWork))
            reinterpret_cast<std::vector<DisplayMonitor>*>(data)->push_back({info.szDevice,info.rcMonitor,info.rcWork,(info.dwFlags&MONITORINFOF_PRIMARY)!=0});
        return TRUE;
    },reinterpret_cast<LPARAM>(&monitors));
    // Keep numbering deterministic when Windows changes its enumeration order.
    std::sort(monitors.begin(),monitors.end(),[](const auto& a,const auto& b){return a.device<b.device;});
    if(monitors.size()>64)monitors.resize(64);
    return monitors;
}
}  // namespace lume::platform::windows
