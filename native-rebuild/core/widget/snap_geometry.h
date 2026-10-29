#pragma once
#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

namespace lume::widget {
// Original widget-snap.js policy, in physical screen pixels. Geometry is
// generic so presentation modules do not depend on another widget's model.
struct SnapMatch {
    bool active=false;
    float origin=0,delta=0,gap=0,center=0;
    int priority=0;
    unsigned edge=0;
    int target=-1;
    unsigned targetEdge=0;
};
struct SnapState { SnapMatch horizontal,vertical; };
namespace snap_detail {
inline float Gap(float a,float b,float c,float d) { return std::max(0.f,std::max(c-b,a-d)); }
template<class Rect>
SnapMatch Match(Rect source,const std::vector<Rect>& targets,const Rect* area,
                bool horizontal,unsigned edges,float threshold) {
    SnapMatch best;
    const float origin=horizontal?source.x:source.y,length=horizontal?source.width:source.height;
    const unsigned first=horizontal?1u:4u,second=horizontal?2u:8u;
    auto consider=[&](float coordinate,bool trailing,int priority,Rect other,unsigned edge,int target,unsigned targetEdge) {
        const float a=horizontal?source.y:source.x,b=horizontal?source.height:source.width;
        const float c=horizontal?other.y:other.x,d=horizontal?other.height:other.width;
        SnapMatch match{true,coordinate-(trailing?length:0),std::abs(origin+(trailing?length:0)-coordinate),
            Gap(a,a+b,c,c+d),std::abs(a+b/2-c-d/2),priority,edge,target,targetEdge};
        if(match.delta>threshold||(priority!=2&&match.gap>10))return;
        if(!best.active||std::tie(match.delta,match.priority,match.gap,match.center)<
                          std::tie(best.delta,best.priority,best.gap,best.center))best=match;
    };
    for(const auto edge:{first,second}) {
        if(!(edges&edge))continue;
        const bool trailing=edge==second;
        for(std::size_t index=0;index<targets.size();++index) {
            const auto& target=targets[index];
            const float other=horizontal?target.x:target.y,lengthOther=horizontal?target.width:target.height;
            consider(other+(trailing?lengthOther:0),trailing,0,target,edge,static_cast<int>(index),edge);
            consider(trailing?other-8:other+lengthOther+8,trailing,1,target,edge,static_cast<int>(index),trailing?first:second);
        }
        if(area)consider((horizontal?area->x:area->y)+(trailing?(horizontal?area->width:area->height):0),
                         trailing,2,source,edge,-1,0);
    }
    return best;
}
}
template<class Rect>
Rect ResolveWidgetMove(Rect source,const std::vector<Rect>& targets,Rect workArea,SnapState& state) {
    if(!state.horizontal.active||std::abs(source.x-state.horizontal.origin)>12)
        state.horizontal=snap_detail::Match(source,targets,&workArea,true,15,8);
    if(!state.vertical.active||std::abs(source.y-state.vertical.origin)>12)
        state.vertical=snap_detail::Match(source,targets,&workArea,false,15,8);
    if(state.horizontal.active)source.x=state.horizontal.origin;
    if(state.vertical.active)source.y=state.vertical.origin;
    return source;
}
template<class Rect>
SnapState DetectWidgetResize(Rect source,unsigned edges,const std::vector<Rect>& targets) {
    return {snap_detail::Match(source,targets,static_cast<const Rect*>(nullptr),true,edges,2),
            snap_detail::Match(source,targets,static_cast<const Rect*>(nullptr),false,edges,2)};
}
}
