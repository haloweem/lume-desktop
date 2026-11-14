#pragma once
#include "core/todo/todo_types.h"
#include "vendor/nlohmann/json.hpp"
#include <string>

namespace lume::todo {
// The task sheet's field protocol, shared by the to-do board pages and the
// task editor popup so every surface reads and writes tasks the same way.
//
// Row: {id,title,notes,completed,color,repeat,frequency,reminder,date,time,end,
//       allDay,days,endDate,subtasks:[{id,title,completed}]}
// Fields (all optional except as noted): title, notes, color, date ("YYYY-MM-DD"),
//   time/end ("HH:MM"), allDay+days (a complete schedule), reminder (minutes
//   before, -1 none), frequency (none|daily|weekdays|weekly|monthly), subtasks.
std::string SheetDate(calendar::Date);
nlohmann::json SheetRow(const TaskView &, const ILocalTimePort &);
// Fills `out` from fields; creating requires title and date. Returns "" on
// success, otherwise "invalid" or "invalid-time".
std::string ReadSheetPatch(const nlohmann::json &fields, const TaskView *existing, bool creating,
                           const ILocalTimePort &, Millis now, TaskPatch &out);
const char *SheetErrorName(Error);
}  // namespace lume::todo
