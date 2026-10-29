#pragma once
#include <windows.h>
#include <algorithm>

namespace lume::platform::windows {
// Physical screen coordinates; independent of monitor origin and logical DPI.
inline SIZE ClockGrip(SIZE size,unsigned dpi) {
    const LONG minimum=std::max(1,MulDiv(32,static_cast<int>(dpi),96));
    return {std::min(size.cx,minimum),std::min(size.cy,minimum)};
}
inline bool ClockHasGrip(RECT bounds,RECT work,SIZE grip) {
    return std::min(bounds.right,work.right)-std::max(bounds.left,work.left)>=grip.cx&&
           std::min(bounds.bottom,work.bottom)-std::max(bounds.top,work.top)>=grip.cy;
}
inline POINT RecoverClockPosition(POINT position,SIZE size,RECT work,SIZE grip) {
    const LONG gx=std::min(grip.cx,work.right-work.left),gy=std::min(grip.cy,work.bottom-work.top);
    return {std::clamp(position.x,work.left-size.cx+gx,work.right-gx),
            std::clamp(position.y,work.top-size.cy+gy,work.bottom-gy)};
}
}
