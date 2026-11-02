#include "apps/shell/island_adapter.h"
#include "platform/windows/weather/weather_view.h"
#include <algorithm>
#include <cmath>

namespace lume::shell {
using platform::windows::WindowsLocalTime;

std::vector<platform::windows::IslandTodo> IslandTodoAdapter::ForDay(calendar::Date day) const {
    std::vector<platform::windows::IslandTodo> rows;
    if(!todos_)return rows;
    platform::windows::CalendarTodoView view;
    const auto& time=todos_->Time();
    auto end=todo::LocalDateTime{day,23,59,59,999};
    const auto until=time.FromLocal(end);
    const auto now=WindowsLocalTime::NowMillis();
    view.Update(todos_->Read(),&time,now,until.value_or(now));
    for(const auto index:view.ForDay(day)){
        const auto& task=view.Snapshot().tasks[index];
        platform::windows::IslandTodo row;row.id=task.id;row.title=platform::windows::TodoWide(task.title);
        row.completed=task.completed;row.projected=task.projected;row.color=platform::windows::TodoColorRgb(task.color);
        if(task.schedule)if(const auto start=time.ToLocal(task.schedule->startAt))row.time=platform::windows::TodoDateText(*start,false);
        rows.push_back(std::move(row));
    }
    return rows;
}
std::optional<platform::windows::IslandTodo> IslandTodoAdapter::Find(const std::string& id) const {
    if(!todos_)return std::nullopt;
    const auto snapshot=todos_->Read();
    const auto found=std::find_if(snapshot.tasks.begin(),snapshot.tasks.end(),[&](const auto& t){return t.id==id&&!t.deletedAt;});
    if(found==snapshot.tasks.end())return std::nullopt;
    platform::windows::IslandTodo row;row.id=found->id;row.title=platform::windows::TodoWide(found->title);
    row.completed=found->completed;row.color=platform::windows::TodoColorRgb(found->color);
    row.notes=platform::windows::TodoWide(found->notes);
    const auto& time=todos_->Time();
    if(found->schedule)if(const auto start=time.ToLocal(found->schedule->startAt))row.time=platform::windows::TodoDateText(*start,true);
    return row;
}
bool IslandTodoAdapter::HasTodos(calendar::Date day) {
    if(!todos_)return false;
    // Rebuilt only after the store changed, so drawing a month never copies it.
    if(dirty_) {
        const auto snapshot=todos_->Read();
        dirty_=false;days_.clear();
        const auto& time=todos_->Time();
        for(const auto& task:snapshot.tasks){
            if(task.deletedAt)continue;
            if(task.day)days_.insert(task.day->year*10000+task.day->month*100+task.day->day);
            else if(task.schedule)if(const auto start=time.ToLocal(task.schedule->startAt))
                days_.insert(start->date.year*10000+start->date.month*100+start->date.day);
        }
    }
    return days_.count(day.year*10000+day.month*100+day.day)!=0;
}
void IslandTodoAdapter::Toggle(const platform::windows::IslandTodo& row) const {
    if(!todos_)return;
    const auto snapshot=todos_->Read();
    const auto found=std::find_if(snapshot.tasks.begin(),snapshot.tasks.end(),[&](const auto& t){return t.id==row.id&&!t.deletedAt;});
    if(!snapshot.writable||found==snapshot.tasks.end()||found->completed!=row.completed)return;
    todo::Command command;command.scope=snapshot.scope;command.id=found->id;
    command.type=found->completed?todo::CommandType::Restore:todo::CommandType::Complete;
    todos_->Dispatch(command);
}
void IslandTodoAdapter::Remove(const platform::windows::IslandTodo& row) const {
    if(!todos_)return;
    const auto snapshot=todos_->Read();
    const auto found=std::find_if(snapshot.tasks.begin(),snapshot.tasks.end(),[&](const auto& t){return t.id==row.id&&!t.deletedAt;});
    if(!snapshot.writable||found==snapshot.tasks.end())return;
    todo::Command command;command.scope=snapshot.scope;command.id=found->id;command.type=todo::CommandType::Delete;
    todos_->Dispatch(command);
}

dynamic_island::Weather IslandWeather(const platform::windows::WeatherReading& reading) {
    dynamic_island::Weather next;
    next.enabled=true;
    next.failed=reading.error!=weather::ProviderError::None;
    if(!reading.snapshot)return next;
    const auto& snapshot=*reading.snapshot;
    next.available=true;
    next.city=platform::windows::WeatherText(snapshot.cityName);
    next.condition=platform::windows::WeatherText(snapshot.condition);
    next.temperature=static_cast<int>(std::floor(snapshot.temperature+.5));
    next.code=snapshot.weatherCode;
    next.isDay=snapshot.isDay;
    if(!snapshot.forecast.empty()){
        next.hasRange=true;
        next.high=static_cast<int>(std::floor(snapshot.forecast.front().high+.5));
        next.low=static_cast<int>(std::floor(snapshot.forecast.front().low+.5));
    }
    next.hours=std::min(next.hourly.size(),snapshot.hourly.size());
    for(std::size_t i=0;i<next.hours;++i){
        const auto& hour=snapshot.hourly[i];auto& projected=next.hourly[i];
        projected.label=platform::windows::WeatherHourLabel(hour.time);
        projected.temperature=static_cast<int>(std::floor(hour.temperature+.5));
        projected.code=hour.weatherCode;
        projected.isDay=hour.isDay.value_or(hour.time.size()>=13&&hour.time.substr(11,2)>="06"&&hour.time.substr(11,2)<"18");
    }
    return next;
}

}  // namespace lume::shell
