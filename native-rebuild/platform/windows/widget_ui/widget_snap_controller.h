#pragma once
#include "core/widget/snap_geometry.h"
#include "platform/windows/widget_ui/widget_snap_protocol.h"
#include <algorithm>
#include <utility>

namespace lume::platform::windows {
// Event-driven geometry/highlight coordination. Owns no HWND, worker, timer,
// widget preferences or graphics resource.
class WidgetSnapController {
    struct Rect { float x,y,width,height; };
    struct Neighbors { HWND owner;std::vector<Rect> bounds;std::vector<HWND> windows; };
    static Rect Geometry(const RECT& r) {
        return {static_cast<float>(r.left),static_cast<float>(r.top),
                static_cast<float>(r.right-r.left),static_cast<float>(r.bottom-r.top)};
    }
    static BOOL CALLBACK Collect(HWND window,LPARAM parameter) {
        auto& neighbors=*reinterpret_cast<Neighbors*>(parameter);
        DWORD process=0;GetWindowThreadProcessId(window,&process);
        if(window==neighbors.owner||process!=GetCurrentProcessId()||!IsWindowVisible(window)||
           IsIconic(window)||!widget_snap::Compatible(window))return TRUE;
        RECT bounds{};
        if(GetWindowRect(window,&bounds)){neighbors.bounds.push_back(Geometry(bounds));neighbors.windows.push_back(window);}
        return TRUE;
    }
    void Publish(const std::vector<HWND>& windows) {
        std::vector<std::pair<HWND,unsigned>> next;
        edges_=0;
        for(const auto* match:{&state_.horizontal,&state_.vertical}) {
            if(!match->active)continue;
            edges_|=match->edge;
            if(match->target<0||static_cast<std::size_t>(match->target)>=windows.size())continue;
            const auto target=windows[static_cast<std::size_t>(match->target)];
            const auto entry=std::find_if(next.begin(),next.end(),[&](const auto& value){return value.first==target;});
            if(entry==next.end())next.emplace_back(target,match->targetEdge);else entry->second|=match->targetEdge;
        }
        for(const auto& target:targets_)
            if(std::none_of(next.begin(),next.end(),[&](const auto& value){return value.first==target.first;}))
                widget_snap::Notify(target.first,owner_,0);
        for(const auto& target:next)
            if(std::find(targets_.begin(),targets_.end(),target)==targets_.end())
                widget_snap::Notify(target.first,owner_,target.second);
        targets_=std::move(next);
    }
public:
    void Bind(HWND owner){owner_=owner;}
    void Clear(bool linger=false) {
        for(const auto& target:targets_)widget_snap::Notify(target.first,owner_,linger?target.second:0,linger);
        state_={};
        if(!linger){targets_.clear();neighbors_.clear();edges_=0;}
    }
    unsigned Edges()const{return edges_;}
    void Move(RECT& bounds) {
        Neighbors neighbors{owner_,{},{}};
        EnumWindows(Collect,reinterpret_cast<LPARAM>(&neighbors));
        if(neighbors_!=neighbors.windows)state_={};
        neighbors_=neighbors.windows;
        MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        if(!GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&monitor))return;
        const auto result=widget::ResolveWidgetMove(Geometry(bounds),neighbors.bounds,Geometry(monitor.rcWork),state_);
        const LONG width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        bounds.left=static_cast<LONG>(std::lround(result.x));bounds.top=static_cast<LONG>(std::lround(result.y));
        bounds.right=bounds.left+width;bounds.bottom=bounds.top+height;
        Publish(neighbors.windows);
    }
    void Resize(const RECT& bounds,WPARAM sizing) {
        unsigned edges=0;
        switch(sizing) {
            case WMSZ_LEFT:edges=1;break;case WMSZ_RIGHT:edges=2;break;
            case WMSZ_TOP:edges=4;break;case WMSZ_BOTTOM:edges=8;break;
            case WMSZ_TOPLEFT:edges=5;break;case WMSZ_TOPRIGHT:edges=6;break;
            case WMSZ_BOTTOMLEFT:edges=9;break;case WMSZ_BOTTOMRIGHT:edges=10;break;
        }
        Neighbors neighbors{owner_,{},{}};
        EnumWindows(Collect,reinterpret_cast<LPARAM>(&neighbors));
        state_=widget::DetectWidgetResize(Geometry(bounds),edges,neighbors.bounds);
        Publish(neighbors.windows);
    }
private:
    HWND owner_=nullptr;
    widget::SnapState state_;
    unsigned edges_=0;
    std::vector<HWND> neighbors_;
    std::vector<std::pair<HWND,unsigned>> targets_;
};
}
