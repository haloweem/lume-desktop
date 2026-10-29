#include "platform/windows/organizer/organizer_motion.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace lume::platform::windows {
namespace {
bool Inside(WidgetRect r,OrganizerPoint p){
    return p.x>=r.x&&p.x<r.x+r.width&&p.y>=r.y&&p.y<r.y+r.height;
}
OrganizerPoint IconCenter(const OrganizerLayout& layout,const organizer::Collection& collection,WidgetRect r){
    if(layout.collapsed)return {r.x+26,r.y+26};
    const float scale=static_cast<float>(std::clamp(collection.iconScale,50,120))/100;
    if(collection.layout==organizer::Layout::List)return {r.x+9+12*scale,r.y+r.height/2};
    return {r.x+r.width/2,r.y+30.5f*scale};
}
}
bool OrganizerMotion::Step(Spring& spring,float target,float dt,bool reducedMotion,float epsilon){
    if(reducedMotion){spring={target,0};return false;}
    // x'' + 2*zeta*omega*x' + omega^2*(x-target) = 0 的欠阻尼闭式解。
    // 更换目标不清除速度，连续拖动预览和取消均可在任意一帧中断。
    constexpr float omega=26.f,zeta=.78f,decay=zeta*omega;
    const float frequency=omega*std::sqrt(1-zeta*zeta);
    const float displacement=spring.value-target;
    const float attenuation=std::exp(-decay*dt),cosine=std::cos(frequency*dt),sine=std::sin(frequency*dt);
    const float velocity=spring.velocity;
    spring.value=target+attenuation*(displacement*cosine+(velocity+decay*displacement)*sine/frequency);
    spring.velocity=attenuation*(velocity*cosine-(decay*velocity+omega*omega*displacement)*sine/frequency);
    if(std::abs(spring.value-target)<=epsilon&&std::abs(spring.velocity)<=epsilon*omega){
        spring={target,0};return false;
    }
    return true;
}
const OrganizerMotionFrame& OrganizerMotion::Update(const organizer::Collection& collection,const OrganizerLayout& layout,
    const std::optional<OrganizerPoint>& pointer,const std::vector<std::string>& dragged,
    std::string_view primary,OrganizerPoint offset,float dt,bool reducedMotion,bool external,std::size_t externalCount){
    if(collectionId_!=collection.id){Reset();collectionId_=collection.id;}
    dt=std::isfinite(dt)?std::clamp(dt,0.f,.05f):0.f;
    const bool hasPointer=pointer&&std::isfinite(pointer->x)&&std::isfinite(pointer->y)&&
        std::isfinite(offset.x)&&std::isfinite(offset.y);
    if(external&&hasPointer)externalPointer_=pointer;
    std::unordered_set<std::string> live;
    for(const auto& cell:layout.cells)if(cell.index<collection.items.size())live.insert(collection.items[cell.index].id);
    for(auto it=items_.begin();it!=items_.end();){
        if(live.count(it->first)==0)it=items_.erase(it);else ++it;
    }
    std::vector<std::string> group;
    std::unordered_set<std::string> unique;
    if(hasPointer&&!external)for(const auto& id:dragged)
        if(live.count(id)&&unique.insert(id).second)group.push_back(id);
    if(!group.empty()){
        const auto found=std::find(group.begin(),group.end(),primary);
        if(found!=group.end())std::rotate(group.begin(),found,found+1);
    }
    const bool dragging=!group.empty();
    const bool wasExternal=frame_.external;
    const auto previousCount=frame_.count;
    auto previousSlots=std::move(frame_.slots);
    frame_={};active_=false;
    const bool feedbackActive=Step(feedback_,external||dragging?1.f:0.f,dt,reducedMotion,.001f);
    frame_.feedback=std::clamp(feedback_.value,0.f,1.f);
    frame_.external=external||(!dragging&&wasExternal&&(feedbackActive||frame_.feedback>0));
    frame_.count=dragging?group.size():external?externalCount:frame_.external?previousCount:0;
    if(!dragging&&!external&&!wasExternal&&(feedbackActive||frame_.feedback>0))frame_.slots=std::move(previousSlots);
    active_=feedbackActive;
    std::vector<OrganizerMotionCell> floating;
    for(const auto& cell:layout.cells){
        if(cell.index>=collection.items.size())continue;
        const auto& id=collection.items[cell.index].id;
        const auto r=cell.bounds;
        auto found=items_.find(id);
        if(found==items_.end()){
            ItemMotion initial;initial.x.value=r.x;initial.y.value=r.y;
            initial.width.value=r.width;initial.height.value=r.height;
            // 仅新加入的 ID 入场；搜索重新显示的旧项目不重播动画。
            // OLE Leave 可能先清空指针，保留上一帧的外部位置完成这次落下。
            if(!reducedMotion&&(external||wasExternal)&&externalPointer_&&knownIds_.count(id)==0&&
                r.x<layout.viewport.x+layout.viewport.width&&r.x+r.width>layout.viewport.x&&
                r.y<layout.viewport.y+layout.viewport.height&&r.y+r.height>layout.viewport.y){
                const auto center=IconCenter(layout,collection,r);
                const bool list=!layout.collapsed&&collection.layout==organizer::Layout::List;
                const float horizontal=list?8.f:24.f,vertical=list?10.f:16.f;
                initial.x.value=r.x+std::clamp(externalPointer_->x-center.x,-horizontal,horizontal);
                initial.y.value=r.y+std::clamp(externalPointer_->y-center.y,-vertical,vertical);
                initial.scale.value=.9f;
            }
            found=items_.emplace(id,initial).first;
        }
        auto& item=found->second;
        const auto member=std::find(group.begin(),group.end(),id);
        const bool held=member!=group.end();
        float influence=0;
        if(!reducedMotion&&hasPointer&&!dragging&&!external&&Inside(layout.viewport,*pointer)){
            const auto center=IconCenter(layout,collection,r);
            const float radius=layout.collapsed?100.f:collection.layout==organizer::Layout::List?80.f:150.f;
            const float distance=std::hypot(pointer->x-center.x,pointer->y-center.y);
            const float t=std::clamp(1-distance/radius,0.f,1.f);
            influence=t*t*(3-2*t);
        }
        bool moving=false;
        if(held){
            frame_.slots.push_back(r);
            const auto rank=static_cast<std::size_t>(member-group.begin());
            const float depth=static_cast<float>(std::min<std::size_t>(rank,2));
            const auto track=[&](Spring& spring,float target){
                if(!item.dragging||reducedMotion)spring.velocity=0;
                else if(dt>0)spring.velocity=std::clamp((target-spring.value)/dt,-2400.f,2400.f);
                spring.value=target;
            };
            track(item.x,pointer->x-offset.x+depth*8);
            track(item.y,pointer->y-offset.y+depth*6);
            item.width={r.width,0};item.height={r.height,0};
            item.floating=rank<3;
        }else{
            moving|=Step(item.x,r.x,dt,reducedMotion,.02f);
            moving|=Step(item.y,r.y,dt,reducedMotion,.02f);
            moving|=Step(item.width,r.width,dt,reducedMotion,.02f);
            moving|=Step(item.height,r.height,dt,reducedMotion,.02f);
            if(!moving)item.floating=false;
        }
        item.dragging=held;
        moving|=Step(item.scale,reducedMotion?1.f:held?1.12f:1+.28f*influence,dt,reducedMotion,.0002f);
        moving|=Step(item.lift,reducedMotion?0.f:held?5.f:5*influence,dt,reducedMotion,.01f);
        active_|=moving;
        const OrganizerMotionCell rendered{cell.index,{item.x.value,item.y.value,item.width.value,item.height.value},
            item.scale.value,item.lift.value,item.floating};
        if(held&&!item.floating)continue;
        if(item.floating)floating.push_back(rendered);else frame_.cells.push_back(rendered);
    }
    if(dragging)std::sort(floating.begin(),floating.end(),[&](const auto& a,const auto& b){
        const auto rank=[&](std::size_t index){return std::find(group.begin(),group.end(),collection.items[index].id)-group.begin();};
        return rank(a.index)>rank(b.index);
    });
    frame_.cells.insert(frame_.cells.end(),floating.begin(),floating.end());
    knownIds_.clear();
    for(const auto& item:collection.items)knownIds_.insert(item.id);
    if(!frame_.external)externalPointer_.reset();
    return frame_;
}
bool OrganizerMotion::Active()const{return active_;}
void OrganizerMotion::Reset(){
    items_.clear();knownIds_.clear();collectionId_.clear();externalPointer_.reset();frame_={};feedback_={};active_=false;
}
}
