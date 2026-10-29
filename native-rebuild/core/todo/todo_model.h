#pragma once
#include "core/todo/todo_state.h"

namespace lume::todo {
Error ApplyPatch(TaskRecord &task, const TaskPatch &patch, bool creating, const ILocalTimePort &port);
Error CompleteTask(TaskRecord &task, Millis now, const std::string &nextId, std::optional<TaskRecord> &next,
                   const ILocalTimePort &port);
void RefreshView(TaskRecord &task, const ILocalTimePort &port);
// The occurrence after task under recurrence, with due, reminder and schedule moved.
std::optional<TaskView> NextInstance(const TaskView &task, const Recurrence &recurrence, const ILocalTimePort &port);
// Later occurrences of an open repeating task that start no later than until.
std::vector<TaskView> ProjectOccurrences(const TaskView &task, Millis until, std::size_t limit,
                                         const ILocalTimePort &port);
// Projected occurrences whose reminder is due within the last day, plus the next upcoming one.
void AppendReminderOccurrences(std::vector<TaskView> &tasks, Millis now, const ILocalTimePort &port);
bool ValidUtf8Length(const std::string &text, std::size_t maximumUtf16Units) noexcept;
std::string Trim(const std::string &text);
} // namespace lume::todo
