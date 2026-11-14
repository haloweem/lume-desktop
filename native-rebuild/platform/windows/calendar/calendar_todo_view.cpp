#include "platform/windows/calendar/calendar_todo_view.h"
#include "core/todo/todo_model.h"
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <limits>
#include <queue>
namespace lume::platform::windows {
namespace {int Key(calendar::Date d){return (d.year*12+d.month)*32+d.day;}}
void CalendarTodoView::Update(todo::RepositorySnapshot snapshot,const todo::ILocalTimePort* time){
    snapshot_=std::move(snapshot);time_=time;storedCount_=snapshot_.tasks.size();days_.clear();storedRows_.clear();Group();
    if(!storedCount_)storedRows_=std::vector<std::size_t>{};
}
void CalendarTodoView::PrepareDays(std::vector<calendar::Date> days) const{
    std::sort(days.begin(),days.end());days.erase(std::unique(days.begin(),days.end()),days.end());
    if(days==days_)return;
    days_=std::move(days);snapshot_.tasks.resize(storedCount_);storedRows_.clear();
    if(time_)for(std::size_t i=0;i<storedCount_;++i){
        auto projected=todo::ProjectOccurrencesForDays(snapshot_.tasks[i],days_,*time_);
        for(auto& next:projected){snapshot_.tasks.push_back(std::move(next));storedRows_.push_back(i);}
    }
    Group();
}
void CalendarTodoView::Group() const{
    groups_.clear();sortedGroups_.clear();timeBlocks_.clear();blockTime_=nullptr;
    for(std::size_t i=0;i<snapshot_.tasks.size();++i){const auto& t=snapshot_.tasks[i];if(!t.deletedAt&&t.day)groups_[Key(*t.day)].push_back(i);}
    for(auto& entry:groups_)std::stable_sort(entry.second.begin(),entry.second.end(),[this](auto a,auto b){
        return snapshot_.tasks[a].completed<snapshot_.tasks[b].completed;
    });
}
std::size_t CalendarTodoView::Stored(std::size_t index)const{
    return index>=storedCount_&&index-storedCount_<storedRows_.size()?storedRows_[index-storedCount_]:index;
}
const std::vector<std::size_t>& CalendarTodoView::ForDay(calendar::Date date)const{
    static const std::vector<std::size_t> empty;
    const auto found=groups_.find(Key(date));return found==groups_.end()?empty:found->second;
}
const std::vector<std::size_t>& CalendarTodoView::SortedForDay(calendar::Date date)const{
    const auto& source=ForDay(date);if(source.empty())return source;
    const auto key=Key(date);const auto found=sortedGroups_.find(key);if(found!=sortedGroups_.end())return found->second;
    auto list=source;
    std::stable_sort(list.begin(),list.end(),[this](auto x,auto y){const auto& p=snapshot_.tasks[x];const auto& q=snapshot_.tasks[y];
        if(p.completed!=q.completed)return p.completed<q.completed;
        return (p.schedule?p.schedule->startAt:INT64_MAX)<(q.schedule?q.schedule->startAt:INT64_MAX);});
    return sortedGroups_.emplace(key,std::move(list)).first->second;
}
const std::vector<CalendarTimeBlock>& CalendarTodoView::TimeBlocksForDay(calendar::Date date,const todo::ILocalTimePort& time)const{
    static const std::vector<CalendarTimeBlock> empty;
    if(ForDay(date).empty())return empty;
    if(blockTime_!=&time){timeBlocks_.clear();blockTime_=&time;}
    const auto key=Key(date);const auto found=timeBlocks_.find(key);if(found!=timeBlocks_.end())return found->second;
    std::vector<CalendarTimeBlock> blocks;
    for(const auto i:SortedForDay(date)){
        const auto& task=snapshot_.tasks[i];if(!task.schedule)continue;
        const auto start=time.ToLocal(task.schedule->startAt),end=time.ToLocal(task.schedule->endAt);if(!start)continue;
        const float first=start->date==date?start->hour*60.f+start->minute:0.f;
        const float last=end&&end->date==date?end->hour*60.f+end->minute:1440.f;
        blocks.push_back({i,first,std::max(last,first+15),0,1,start,end});
    }
    std::sort(blocks.begin(),blocks.end(),[](const auto& p,const auto& q){return p.start<q.start;});
    for(std::size_t b=0;b<blocks.size();){
        std::size_t e=b;float end=blocks[b].end;int lanes=0;
        using Active=std::pair<float,int>;
        std::priority_queue<Active,std::vector<Active>,std::greater<Active>> active;
        std::priority_queue<int,std::vector<int>,std::greater<int>> free;
        for(;e<blocks.size()&&(e==b||blocks[e].start<end);++e){
            auto& block=blocks[e];
            while(!active.empty()&&active.top().first<=block.start){free.push(active.top().second);active.pop();}
            if(free.empty())block.lane=lanes++;else {block.lane=free.top();free.pop();}
            active.emplace(block.end,block.lane);end=std::max(end,block.end);
        }
        for(std::size_t m=b;m<e;++m)blocks[m].lanes=lanes;
        b=e;
    }
    return timeBlocks_.emplace(key,std::move(blocks)).first->second;
}
CalendarAgendaLayout CalculateAgendaLayout(const CalendarLayout& l,float width){
    CalendarAgendaLayout a;a.area=l.agenda;
    if(a.area.width<=0)return a;
    const bool small=width<=600;const float left=small?9.0f:19.0f;
    a.heading={a.area.x+left,a.area.y+(small?6.0f:8.0f),a.area.width-left,0};
    const float listTop=a.heading.Bottom()+4+26;
    a.list={a.heading.x,listTop,a.heading.width,std::max(0.0f,a.area.Bottom()-listTop)};
    a.status={a.list.x,a.area.Bottom()-28,a.list.width,28};return a;
}
std::wstring TodoWide(const std::string& s){
    if(s.empty())return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(count<=0)return {};
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),result.data(),count);return result;
}
std::wstring TodoDateText(const todo::LocalDateTime& t,bool date){
    wchar_t value[40]{};
    if(date)swprintf(value,40,L"%04d-%02d-%02d %02d:%02d",t.date.year,t.date.month,t.date.day,t.hour,t.minute);
    else swprintf(value,40,L"%02d:%02d",t.hour,t.minute);
    return value;
}
std::wstring TodoScheduleText(const todo::TaskView& task,const todo::ILocalTimePort& port,todo::Millis now){
    if(!task.schedule)return {};
    const auto start=port.ToLocal(task.schedule->startAt),end=port.ToLocal(task.schedule->endAt);
    if(!start||!end)return L"时间不可用";
    std::wstring status;
    if(task.completed)status=L"已完成";
    else if(now>=task.schedule->endAt)status=L"已结束";
    else if(now>=task.schedule->startAt)status=L"进行中";
    else {const auto minutes=(task.schedule->startAt-now+59999)/60000;if(minutes<=60)status=std::to_wstring(minutes)+L" 分钟后开始";}
    const auto repeat=TodoRepeatText(task.recurrence);
    return TodoDateText(*start,false)+L" - "+TodoDateText(*end,start->date!=end->date)+(status.empty()?L"":L" · "+status)+
        (task.recurrence?L" · "+repeat:L"");
}
std::wstring TodoRepeatText(const std::optional<todo::Recurrence>& value){
    if(!value)return L"不重复";
    const int n=value->interval;
    switch(value->frequency){
    case todo::Frequency::Daily:return n==1?L"每天":L"每 "+std::to_wstring(n)+L" 天";
    case todo::Frequency::Weekly:return n==1?L"每周":L"每 "+std::to_wstring(n)+L" 周";
    case todo::Frequency::Monthly:return n==1?L"每月":L"每 "+std::to_wstring(n)+L" 个月";
    case todo::Frequency::Weekdays:return n==1?L"工作日":L"每 "+std::to_wstring(n)+L" 个工作日";
    }
    return L"重复";
}
std::wstring TodoResultText(todo::Error error,todo::Warning warning){
    if(error!=todo::Error::None){switch(error){
    case todo::Error::InvalidTask:return L"请填写 1～500 字符的标题，备注不超过 10000 字符";
    case todo::Error::InvalidTime:return L"时间无效、超过范围或位于夏令时跳时，请调整";
    case todo::Error::InvalidReminder:return L"提前分钟数需为 0～525600 的整数";
    case todo::Error::StaleScope:return L"数据会话已切换，请重新打开编辑器";
    case todo::Error::ReadOnly:case todo::Error::NotLoaded:case todo::Error::UnsupportedVersion:return L"日程数据暂不可写，请检查本地文件并重新打开";
    case todo::Error::NotFound:case todo::Error::Deleted:return L"日程已不存在，请重新打开清单";
    case todo::Error::LimitExceeded:return L"日程数据已达安全上限，未保存更改";
    default:return L"保存失败，请重试；输入已保留";
    }}
    if(warning==todo::Warning::BackupRecovered)return L"已从本地备份恢复";
    if(warning==todo::Warning::BackupWriteFailed||warning==todo::Warning::BackupReplaceFailed)return L"本机已保存；备份更新失败";
    if(warning==todo::Warning::CleanupFailed)return L"临时文件清理失败";
    return {};
}
}
