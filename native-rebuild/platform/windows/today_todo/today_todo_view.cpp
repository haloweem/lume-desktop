#include "platform/windows/today_todo/today_todo_view.h"
#include <algorithm>
#include <cmath>
namespace lume::platform::windows {
void TodayTodoView::Update(todo::RepositorySnapshot snapshot,const todo::ILocalTimePort& time,todo::Millis now){
    const auto local=time.ToLocal(now);const auto date=local?local->date:calendar::Date{};
    if(date!=date_||snapshot.scope.accountKey!=Scope().accountKey||snapshot.scope.generation!=Scope().generation)scroll_=0;
    date_=date;
    // Only project as far as the current local day, retaining calendar semantics.
    auto end=local; if(end){end->hour=23;end->minute=59;end->second=59;end->millisecond=999;}
    const auto until=end?time.FromLocal(*end):std::optional<todo::Millis>{};
    todos_.Update(std::move(snapshot),&time,now,until.value_or(now));
    rows_=local?todos_.ForDay(date_):std::vector<std::size_t>{};
    scroll_=std::clamp(scroll_,0.f,MaximumScroll());
}
std::size_t TodayTodoView::Completed()const{
    return std::count_if(rows_.begin(),rows_.end(),[this](auto i){return todos_.Snapshot().tasks[i].completed;});
}
void TodayTodoView::Draw(WidgetCanvas& c,float w,float h){
    width_=w;height_=h;scroll_=std::clamp(scroll_,0.f,MaximumScroll());
    c.Text(L"今日待办",{18,10,w-128,28},17,0xffffff,1,true);
    c.Text(std::to_wstring(Completed())+L"/"+std::to_wstring(Count()),{w-96,12,72,24},12,0xffffff,.8f,false,WidgetTextAlign::Right);
    if(rows_.empty()){
        c.Text(Writable()?L"今天没有待办":L"待办数据暂不可用",{24,53,w-48,25},14,0xffffff,.85f);
        c.Text(L"在日历中添加今天的待办",{24,83,w-48,20},11,0xffffff,.65f);
    }
    const auto first=static_cast<std::size_t>(scroll_/28),last=std::min(rows_.size(),static_cast<std::size_t>(std::ceil((scroll_+VisibleHeight())/28)));
    const bool clipped=c.PushClip({20,44,w-40,VisibleHeight()});
    for(auto i=first;i<last;++i){
        const auto& task=todos_.Snapshot().tasks[rows_[i]];const float y=44+static_cast<float>(i)*28-scroll_;
        const float alpha=task.completed?.68f:1.f;
        // The editor's colour marks importance like the calendar agenda: a tinted row with a bar.
        const auto tint=TodoColorRgb(task.color);
        if(tint){c.Rounded({18,y,w-36,25},7,tint,task.completed?.2f:.34f);c.Rounded({18,y+3,3,19},1.5f,tint,task.completed?.55f:1);}
        if(hover_==static_cast<int>(i))c.Rounded({18,y,w-36,25},7,0xffffff,.1f);
        if(task.completed){
            c.Rounded({24,y+5,15,15},7.5f,0x34c759,1);
            c.Line(28,y+12,30,y+15,0xffffff,1,1.6f);c.Line(30,y+15,35,y+9,0xffffff,1,1.6f);
        }else c.AnnularSector(31.5f,y+12.5f,6.3f,7.5f,0,360,0xffffff,task.projected?.5f:.9f);
        auto title=TodoWide(task.title)+(task.projected?L" ↻":L"");
        const float available=w-76;
        if(c.MeasureText(title,14)>available){
            std::size_t low=0,high=title.size();
            while(low<high){const auto mid=(low+high+1)/2;
                if(c.MeasureText(title.substr(0,mid)+L"…",14)<=available)low=mid;else high=mid-1;}
            // Never split a UTF-16 surrogate pair at the truncation boundary.
            if(low&&title[low-1]>=0xd800&&title[low-1]<=0xdbff)--low;
            title=title.substr(0,low)+L"…";
        }
        c.Text(title,{48,y,available,25},14,0xffffff,alpha);
        if(task.completed)c.Line(48,y+13,48+std::min(available,c.MeasureText(title,14)),y+13,0xffffff,.5f,1);

    }
    if(clipped)c.PopClip();
    if(!error_.empty())c.Text(error_,{24,h-20,w-48,16},10,0xffd7bd,1);
}
int TodayTodoView::Hit(float x,float y)const{
    if(x<23||x>=width_-24||y<44||y>=44+VisibleHeight())return -1;
    const auto index=static_cast<std::size_t>((y-44+scroll_)/28);
    return index<rows_.size()?static_cast<int>(index):-1;
}
TodayTodoAction TodayTodoView::ActionAt(float x,float y)const{
    if(Hit(x,y)<0)return TodayTodoAction::None;
    if(x<44)return TodayTodoAction::Toggle;
    return TodayTodoAction::None;
}
float TodayTodoView::MaximumScroll()const{return std::max(0.f,static_cast<float>(rows_.size())*28-VisibleHeight());}
bool TodayTodoView::Scroll(float delta,bool absolute){
    if(!std::isfinite(delta))return false;
    const float next=std::clamp(absolute?delta:scroll_+delta,0.f,MaximumScroll());
    if(next==scroll_)return false;scroll_=next;return true;
}
std::optional<todo::TaskView> TodayTodoView::TaskAt(int hit)const{
    if(hit<0||static_cast<std::size_t>(hit)>=rows_.size())return {};
    return todos_.Snapshot().tasks[rows_[static_cast<std::size_t>(hit)]];
}
}
