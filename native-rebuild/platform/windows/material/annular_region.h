#pragma once
#include <windows.h>
namespace lume::platform::windows {
struct AnnularRegionApi {
    decltype(&CreateEllipticRgn) create=&CreateEllipticRgn;
    decltype(&CombineRgn) combine=&CombineRgn;
    decltype(&DeleteObject) remove=&DeleteObject;
    decltype(&SetWindowRgn) publish=&SetWindowRgn;
};
inline HRGN CreateAnnularRegion(RECT outer,RECT inner,const AnnularRegionApi& api={}){
    // This binary HWND envelope must not clip the visual edge's fractional
    // pixel coverage. Three physical pixels also cover GDI's integer ellipse
    // rasterization bias; the actual visible edges remain shader/D2D masked.
    InflateRect(&outer,3,3);InflateRect(&inner,-3,-3);
    const HRGN region=api.create(outer.left,outer.top,outer.right,outer.bottom);
    if(!region)return nullptr;
    const HRGN center=api.create(inner.left,inner.top,inner.right,inner.bottom);
    if(!center){api.remove(region);return nullptr;}
    const bool ok=api.combine(region,region,center,RGN_DIFF)!=ERROR;
    api.remove(center);
    if(!ok){api.remove(region);return nullptr;}
    return region;
}
// A null region is never published: SetWindowRgn(null) would remove the input mask.
inline bool PublishRegion(HWND window,HRGN region,const AnnularRegionApi& api={}){
    if(!region)return false;
    if(api.publish(window,region,FALSE))return true;
    api.remove(region);return false;
}
}
