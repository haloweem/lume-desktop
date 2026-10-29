#pragma once
#include "core/todo/todo_types.h"
#include "platform/windows/calendar/calendar_layout.h"
#include <functional>
#include <map>

namespace lume::platform::windows {
// The native surface sees value snapshots and commands, never persistence/JSON.
struct CalendarTodoPort {
    const todo::ILocalTimePort* time=nullptr;
    std::function<todo::RepositorySnapshot()> read;
    std::function<todo::CommandResult(const todo::Command&)> dispatch;
    std::function<todo::Millis()> now;
    std::function<void()> refreshTime;
};
class CalendarTodoView {
public:
    // With a time port, open repeating schedules are also listed on their later
    // days (two years ahead) as projected copies appended after stored tasks.
    void Update(todo::RepositorySnapshot snapshot,const todo::ILocalTimePort* time=nullptr,todo::Millis now=0,std::optional<todo::Millis> projectionUntil={});
    // The stored task a row stands for: projected copies map to their series.
    std::size_t Stored(std::size_t index) const;
    const todo::RepositorySnapshot& Snapshot() const {return snapshot_;}
    const std::vector<std::size_t>& ForDay(calendar::Date) const;
private:
    todo::RepositorySnapshot snapshot_;
    std::map<int,std::vector<std::size_t>> groups_;
};
struct CalendarAgendaRow {
    std::size_t task=0;
    CalendarRect row,check,title,time,notes,edit,remove;
};
struct CalendarAgendaLayout {
    CalendarRect area,heading,list,status;
    std::vector<CalendarAgendaRow> rows;
    float totalHeight=0,maxScroll=0;
    float scroll=0;  // The scroll actually drawn (day/week views resolve an automatic start).
    float minZoom=.25f;  // 日/周: the zoom at which the whole day fits.
    float dayMaxScroll=0,upcomingMaxScroll=0;  // 全 view lists.
    std::array<float,42> cellMaxScroll{};  // 月 view cells.
};
CalendarAgendaLayout CalculateAgendaLayout(const CalendarLayout&,float width);
std::wstring TodoWide(const std::string&);
// RGB of a todo's colour label, as the calendar agenda draws it; 0 for none.
inline std::uint32_t TodoColorRgb(todo::Color color) {
    constexpr std::uint32_t colors[]={0,0xff6961,0xffa348,0xf5d65c,0x6bd89b,0x65b4ff};
    const auto i=static_cast<unsigned>(color);
    return i<6?colors[i]:0;
}
std::wstring TodoDateText(const todo::LocalDateTime&,bool withDate=true);
std::wstring TodoScheduleText(const todo::TaskView&,const todo::ILocalTimePort&,todo::Millis now);
std::wstring TodoRepeatText(const std::optional<todo::Recurrence>&);
std::wstring TodoResultText(todo::Error,todo::Warning=todo::Warning::None);
} // namespace lume::platform::windows
