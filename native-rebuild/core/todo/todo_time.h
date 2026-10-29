#pragma once
#include "core/todo/todo_types.h"

namespace lume::todo {
bool ValidLocal(const LocalDateTime &value) noexcept;
std::optional<Millis> ResolveLocal(const LocalDateTime &value, const ILocalTimePort &port);
bool ValidTimestamp(Millis value, const ILocalTimePort &port);
bool ValidSchedule(Schedule value, const ILocalTimePort &port);
std::optional<Schedule> DefaultSchedule(calendar::Date date, Millis now, const ILocalTimePort &port);
std::optional<Schedule> MoveScheduleStart(Schedule value, Millis start, const ILocalTimePort &port);
std::optional<calendar::Date> GroupDay(const TaskView &task, const ILocalTimePort &port);
ReminderDraft ReminderFor(const TaskView &task);
std::optional<Millis> NextOccurrence(Millis value, Recurrence recurrence, const ILocalTimePort &port);
std::optional<Millis> CheckedAdd(Millis value, Millis delta) noexcept;
} // namespace lume::todo
