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
struct CalendarTimeBlock {
    std::size_t task=0;float start=0,end=0;int lane=0,lanes=1;
    std::optional<todo::LocalDateTime> localStart,localEnd;
};
class CalendarTodoView {
public:
    void Update(todo::RepositorySnapshot snapshot,const todo::ILocalTimePort* time=nullptr);
    // Call once before taking row references. Only the active view's dates are
    // projected; an unchanged date set reuses the existing rows and indexes.
    void PrepareDays(std::vector<calendar::Date> days) const;
    // The stored task a row stands for: projected copies map to their series.
    std::size_t Stored(std::size_t index) const;
    const todo::RepositorySnapshot& Snapshot() const {return snapshot_;}
    const std::vector<std::size_t>& ForDay(calendar::Date) const;
    const std::vector<std::size_t>& SortedForDay(calendar::Date) const;
    const std::vector<CalendarTimeBlock>& TimeBlocksForDay(calendar::Date,const todo::ILocalTimePort&) const;
private:
    void Group() const;
    mutable todo::RepositorySnapshot snapshot_;
    mutable std::map<int,std::vector<std::size_t>> groups_;
    mutable std::vector<calendar::Date> days_;
    mutable std::vector<std::size_t> storedRows_;
    mutable std::map<int,std::vector<std::size_t>> sortedGroups_;
    mutable std::map<int,std::vector<CalendarTimeBlock>> timeBlocks_;
    mutable const todo::ILocalTimePort* blockTime_=nullptr;
    std::size_t storedCount_=0;
    const todo::ILocalTimePort* time_=nullptr;
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
