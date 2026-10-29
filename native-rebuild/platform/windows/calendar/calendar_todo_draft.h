#pragma once
#include "core/todo/todo_time.h"
namespace lume::platform::windows {
class CalendarTodoDraft final {
  public:
    CalendarTodoDraft(const todo::ILocalTimePort &, todo::Command, todo::Millis,
                      const std::optional<todo::TaskView> &);
    std::string title, notes;
    todo::Color color = todo::Color::None;
    std::optional<todo::LocalDateTime> start, end;
    todo::ReminderDraft reminder;
    std::optional<todo::Recurrence> recurrence;
    bool SetStart(todo::LocalDateTime);
    void SetEnd(todo::LocalDateTime value) { end=value; }
    void SetReminder(todo::ReminderDraft value) { reminder=value;reminderChanged_=true; }
    void SetRecurrence(std::optional<todo::Recurrence> value) { recurrence=value; }
    todo::Error Build(todo::Command &) const;
  private:
    const todo::ILocalTimePort &time_;
    todo::Command seed_;
    std::optional<todo::Schedule> previous_;
    bool reminderChanged_ = false;
    std::optional<todo::Recurrence> originalRecurrence_;
    std::optional<todo::Millis> Resolve(const todo::LocalDateTime &, bool start) const;
};
}
