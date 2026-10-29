#pragma once
// Persistence values. Window clients need only todo_repository.h / TaskView.
#include "core/todo/todo_types.h"

namespace lume::todo {
struct TaskRecord {
    TaskView view;
    std::optional<Millis> completedAt;
    std::optional<Recurrence> recurrence;
    std::vector<std::string> tags;
    double sortKey = 0;
    // Untyped properties retained at the JSON-value level, including nested metadata.
    std::string preservedJson = "{}";
    bool resetSubtasks = false;
    // Set once edited, so an unrecognized stored rule survives untouched otherwise.
    bool recurrenceChanged = false;
};
struct TodoState {
    std::int64_t revision = 0;
    std::vector<TaskRecord> tasks;
    std::vector<std::string> outbox;
    std::vector<std::string> outboxIds;
    std::string preservedJson = "{}";
    bool inlineOutbox = true;
};
} // namespace lume::todo
