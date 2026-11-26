#pragma once
#include "core/dock/dock_model.h"
#include <windows.h>
#include <algorithm>

namespace lume::platform::windows {
inline RECT SpaceInsideDock(RECT work, const RECT& anchor, dock::Edge edge, LONG gap) {
    if (IsRectEmpty(&anchor)) return work;
    switch (edge) {
    case dock::Edge::Top: work.top = std::clamp(anchor.bottom + gap, work.top, work.bottom); break;
    case dock::Edge::Left: work.left = std::clamp(anchor.right + gap, work.left, work.right); break;
    case dock::Edge::Right: work.right = std::clamp(anchor.left - gap, work.left, work.right); break;
    default: work.bottom = std::clamp(anchor.top - gap, work.top, work.bottom); break;
    }
    return work;
}

inline POINT DockPopupOrigin(const RECT& space, const RECT& anchor, dock::Edge edge, LONG width, LONG height) {
    const LONG centerX = anchor.left + (anchor.right - anchor.left - width) / 2;
    const LONG centerY = anchor.top + (anchor.bottom - anchor.top - height) / 2;
    const LONG x = edge == dock::Edge::Left ? space.left : edge == dock::Edge::Right ? space.right - width : centerX;
    const LONG y = edge == dock::Edge::Top ? space.top : edge == dock::Edge::Bottom ? space.bottom - height : centerY;
    return {std::clamp(x, space.left, std::max(space.left, space.right - width)),
            std::clamp(y, space.top, std::max(space.top, space.bottom - height))};
}
}
