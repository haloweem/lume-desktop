#include "platform/windows/calendar/calendar_todo_draft.h"
#include "core/todo/todo_model.h"
namespace lume::platform::windows {
CalendarTodoDraft::CalendarTodoDraft(const todo::ILocalTimePort &time,todo::Command seed,todo::Millis now,
 const std::optional<todo::TaskView> &existing):time_(time),seed_(std::move(seed)) {
    if(existing) {title=existing->title;notes=existing->notes;color=existing->color;reminder=existing->reminder;previous_=existing->schedule;
        recurrence=originalRecurrence_=existing->recurrence;}
    else {reminder={todo::ReminderKind::Relative,600000,0,false};reminderChanged_=true;}
    if(seed_.patch.title)title=*seed_.patch.title;
    if(seed_.patch.notes)notes=*seed_.patch.notes;
    if(seed_.patch.color)color=*seed_.patch.color;
    if(seed_.patch.reminder){reminder=*seed_.patch.reminder;reminderChanged_=true;}
    if(seed_.patch.schedule)previous_=seed_.patch.schedule;
    if(seed_.patch.recurrence)recurrence=*seed_.patch.recurrence;
    if(!previous_) {
        const auto local=time_.ToLocal(now);
        if(local)previous_=todo::DefaultSchedule(existing&&existing->day?*existing->day:local->date,now,time_);
    }
    if(previous_){start=time_.ToLocal(previous_->startAt);end=time_.ToLocal(previous_->endAt);}
}
bool CalendarTodoDraft::SetStart(todo::LocalDateTime value) {
    const auto oldStart=start?Resolve(*start,true):std::nullopt;
    const auto oldEnd=end?Resolve(*end,false):std::nullopt;
    start=value;
    const auto resolved=Resolve(value,true);
    if(!resolved)return false;
    std::optional<todo::Schedule> moved;
    if(oldStart&&oldEnd&&*oldEnd>*oldStart)
        moved=todo::MoveScheduleStart({*oldStart,*oldEnd},*resolved,time_);
    else {
        // The reference editor replaces a missing/nonpositive duration with one hour.
        const auto fallback=todo::CheckedAdd(*resolved,3600000);
        if(fallback&&todo::ValidSchedule({*resolved,*fallback},time_))moved=todo::Schedule{*resolved,*fallback};
    }
    if(!moved)return false;
    previous_=moved;end=time_.ToLocal(moved->endAt);return true;
}
std::optional<todo::Millis> CalendarTodoDraft::Resolve(const todo::LocalDateTime &value,bool isStart) const {
    if(previous_) {
        const auto instant=isStart?previous_->startAt:previous_->endAt;
        const auto local=time_.ToLocal(instant);
        // A repeated DST wall-clock time has two instants. Keep the selected one
        // until that endpoint's local value actually changes.
        if(local&&*local==value)return instant;
    }
    return todo::ResolveLocal(value,time_);
}
todo::Error CalendarTodoDraft::Build(todo::Command &out) const {
    const auto cleanTitle=todo::Trim(title),cleanNotes=todo::Trim(notes);
    if(cleanTitle.empty()||!todo::ValidUtf8Length(cleanTitle,500)||!todo::ValidUtf8Length(cleanNotes,10000))return todo::Error::InvalidTask;
    if(!start||!end)return todo::Error::InvalidTime;
    const auto from=Resolve(*start,true),to=Resolve(*end,false);
    if(!from||!to||!todo::ValidSchedule({*from,*to},time_))return todo::Error::InvalidTime;
    if(reminderChanged_ && reminder.kind==todo::ReminderKind::Relative &&
       (reminder.leadMilliseconds<0||reminder.leadMilliseconds>525600LL*60000||reminder.leadMilliseconds%60000))return todo::Error::InvalidReminder;
    out=seed_;out.patch.title=cleanTitle;out.patch.notes=cleanNotes;out.patch.color=color;out.patch.schedule=todo::Schedule{*from,*to};
    out.patch.reminder=reminderChanged_?std::optional<todo::ReminderDraft>(reminder):std::nullopt;
    // Only an edited rule is written; an unchanged one keeps its stored form.
    if(recurrence!=originalRecurrence_)out.patch.recurrence=recurrence;
    else out.patch.recurrence.reset();
    return todo::Error::None;
}
}
