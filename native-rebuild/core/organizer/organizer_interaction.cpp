#include "core/organizer/organizer_interaction.h"

#include <algorithm>

namespace lume::organizer {
namespace {
bool Contains(const std::vector<std::string>& values,std::string_view value){
    return std::find(values.begin(),values.end(),value)!=values.end();
}
}

bool Interaction::IsSelected(std::string_view id) const { return Contains(selected_,id); }

void Interaction::Select(const std::vector<std::string>& visible,std::string_view id,
    bool control,bool shift,bool preserveGroup){
    const auto found=std::find(visible.begin(),visible.end(),id);
    if(found==visible.end())return;
    if(shift){
        const auto anchor=std::find(visible.begin(),visible.end(),anchor_);
        const auto begin=anchor==visible.end()?found:std::min(anchor,found);
        const auto end=anchor==visible.end()?found:std::max(anchor,found);
        std::vector<std::string> range(begin,end+1);
        if(control){
            for(const auto& value:range)if(!Contains(selected_,value))selected_.push_back(value);
        }else selected_=std::move(range);
    }else if(control){
        if(IsSelected(id))selected_.erase(std::remove(selected_.begin(),selected_.end(),id),selected_.end());
        else selected_.emplace_back(id);
    }else if(preserveGroup&&IsSelected(id)){
        // Keep a multi-selection intact while the pointer moves into a drag.
    }else selected_={std::string(id)};
    if(!shift||!Contains(visible,anchor_))anchor_=std::string(id);
    std::stable_sort(selected_.begin(),selected_.end(),[&](const auto& left,const auto& right){
        return std::find(visible.begin(),visible.end(),left)<std::find(visible.begin(),visible.end(),right);
    });
}

void Interaction::SelectAll(const std::vector<std::string>& visible){
    selected_.clear();
    for(const auto& id:visible)if(!Contains(selected_,id))selected_.push_back(id);
    anchor_=selected_.empty()?std::string{}:selected_.front();
}
void Interaction::Clear(){selected_.clear();anchor_.clear();}

void Interaction::Reconcile(const std::vector<std::string>& visible){
    std::vector<std::string> reconciled;
    for(const auto& id:visible)if(IsSelected(id)&&!Contains(reconciled,id))reconciled.push_back(id);
    selected_=std::move(reconciled);
    if(!Contains(visible,anchor_))anchor_.clear();
    if(dragging_&&visible!=dragOrigin_)EndDrag();
}

std::vector<std::string> Interaction::OrderedSelection(const std::vector<std::string>& order) const{
    std::vector<std::string> result;
    for(const auto& id:order)if(IsSelected(id))result.push_back(id);
    return result;
}

bool Interaction::BeginDrag(const std::vector<std::string>& order,std::string_view grabbed){
    EndDrag();Reconcile(order);
    if(!Contains(order,grabbed)){EndDrag();return false;}
    if(!IsSelected(grabbed))selected_={std::string(grabbed)};
    selected_=OrderedSelection(order);dragged_=selected_;
    preview_=order;dragOrigin_=order;dragging_=true;return true;
}

void Interaction::MoveBefore(std::optional<std::string> before){
    if(!dragging_)return;
    if(before&&Contains(dragged_,*before))return;
    std::vector<std::string> remaining;
    remaining.reserve(preview_.size());
    for(const auto& id:preview_)if(!Contains(dragged_,id))remaining.push_back(id);
    const auto at=before?std::find(remaining.begin(),remaining.end(),*before):remaining.end();
    if(before&&at==remaining.end())return;
    const auto position=at==remaining.end()?remaining.end():at;
    std::size_t offset=static_cast<std::size_t>(position-remaining.begin());
    remaining.insert(remaining.begin()+static_cast<std::ptrdiff_t>(offset),dragged_.begin(),dragged_.end());
    preview_=std::move(remaining);
}

void Interaction::EndDrag(){dragging_=false;dragged_.clear();preview_.clear();dragOrigin_.clear();}
}
