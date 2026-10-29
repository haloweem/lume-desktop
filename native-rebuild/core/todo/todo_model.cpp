#include "core/todo/todo_model.h"
#include "core/todo/todo_time.h"
#include <algorithm>

namespace lume::todo {
namespace {
bool ReadCodepoint(const std::string &text, std::size_t &at, std::uint32_t &cp) noexcept {
    const auto first = static_cast<unsigned char>(text[at++]);
    if (first < 0x80) {
        cp = first;
        return true;
    }
    int count = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
        count = 1;
        minimum = 0x80;
        cp = first & 0x1f;
    } else if (first >= 0xe0 && first <= 0xef) {
        count = 2;
        minimum = 0x800;
        cp = first & 0x0f;
    } else if (first >= 0xf0 && first <= 0xf4) {
        count = 3;
        minimum = 0x10000;
        cp = first & 7;
    } else
        return false;
    if (text.size() - at < static_cast<std::size_t>(count))
        return false;
    while (count--) {
        const auto c = static_cast<unsigned char>(text[at++]);
        if ((c & 0xc0) != 0x80)
            return false;
        cp = (cp << 6) | (c & 0x3f);
    }
    return cp >= minimum && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
}
bool Space(std::uint32_t cp) noexcept {
    return (cp >= 9 && cp <= 13) || cp == 32 || cp == 0xa0 || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 || cp == 0x202f || cp == 0x205f ||
           cp == 0x3000 || cp == 0xfeff;
}
constexpr const char *schedulePrefix = "lume:calendar-schedule:";
constexpr const char *colorPrefix = "lume:calendar-color:";
const char *ColorName(Color value) {
    switch (value) {
    case Color::None:
        return "none";
    case Color::Red:
        return "red";
    case Color::Orange:
        return "orange";
    case Color::Yellow:
        return "yellow";
    case Color::Green:
        return "green";
    case Color::Blue:
        return "blue";
    }
    return nullptr;
}
void ReplaceTag(std::vector<std::string> &tags, const char *prefix, const std::string &value) {
    tags.erase(
        std::remove_if(tags.begin(), tags.end(), [&](const auto &tag) { return tag.rfind(prefix, 0) == 0; }),
        tags.end());
    if (!value.empty())
        tags.push_back(std::string(prefix) + value);
}
void ScheduleTag(TaskRecord &task) {
    const auto schedule = *task.view.schedule;
    ReplaceTag(task.tags, schedulePrefix,
               "[" + std::to_string(schedule.startAt) + "," + std::to_string(schedule.endAt) + "]");
}
} // namespace
bool ValidUtf8Length(const std::string &text, std::size_t maximum) noexcept {
    std::size_t at = 0, units = 0;
    std::uint32_t cp = 0;
    while (at < text.size()) {
        if (!ReadCodepoint(text, at, cp))
            return false;
        units += cp > 0xffff ? 2 : 1;
        if (units > maximum)
            return false;
    }
    return true;
}
std::string Trim(const std::string &text) {
    std::size_t at = 0, start = text.size(), end = 0;
    std::uint32_t cp = 0;
    while (at < text.size()) {
        const auto before = at;
        if (!ReadCodepoint(text, at, cp))
            return text;
        if (!Space(cp)) {
            start = std::min(start, before);
            end = at;
        }
    }
    return start == text.size() ? std::string{} : text.substr(start, end - start);
}
void RefreshView(TaskRecord &task, const ILocalTimePort &port) {
    task.view.day = GroupDay(task.view, port);
    task.view.reminder = ReminderFor(task.view);
    task.view.recurrence = task.recurrence;
}
std::optional<TaskView> NextInstance(const TaskView &task, const Recurrence &recurrence, const ILocalTimePort &port) {
    if (!task.dueAt)
        return {};
    auto plan = *task.dueAt;
    const auto local = port.ToLocal(plan);
    if (!local)
        return {};
    const bool midnight = !local->hour && !local->minute && !local->second && !local->millisecond;
    if (midnight) {
        const auto before = CheckedAdd(plan, -1);
        if (!before)
            return {};
        plan = *before;
    }
    const auto due = NextOccurrence(plan, recurrence, port);
    if (!due)
        return {};
    const auto adjusted = CheckedAdd(*due, midnight ? 1 : 0);
    if (!adjusted || !ValidTimestamp(*adjusted, port))
        return {};
    TaskView next = task;
    next.dueAt = adjusted;
    if (task.reminderAt) {
        next.reminderAt = NextOccurrence(*task.reminderAt, recurrence, port);
        if (!next.reminderAt)
            return {};
    }
    if (task.schedule) {
        const auto delta = *adjusted - *task.dueAt;
        const auto start = CheckedAdd(task.schedule->startAt, delta), end = CheckedAdd(task.schedule->endAt, delta);
        if (!start || !end || !ValidSchedule({*start, *end}, port))
            return {};
        next.schedule = Schedule{*start, *end};
    }
    next.day = GroupDay(next, port);
    next.reminder = ReminderFor(next);
    return next;
}
namespace {
bool Repeating(const TaskView &task) {
    return task.recurrence && task.dueAt && !task.completed && !task.deletedAt && !task.projected;
}
} // namespace
std::vector<TaskView> ProjectOccurrences(const TaskView &task, Millis until, std::size_t limit,
                                         const ILocalTimePort &port) {
    std::vector<TaskView> out;
    if (!Repeating(task))
        return out;
    auto current = task;
    while (out.size() < limit) {
        auto next = NextInstance(current, *task.recurrence, port);
        if (!next || (next->schedule ? next->schedule->startAt : *next->dueAt) > until)
            break;
        next->projected = true;
        out.push_back(*next);
        current = std::move(*next);
    }
    return out;
}
void AppendReminderOccurrences(std::vector<TaskView> &tasks, Millis now, const ILocalTimePort &port) {
    const auto count = tasks.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (!Repeating(tasks[i]) || !tasks[i].reminderAt || *tasks[i].reminderAt > now)
            continue;
        auto current = tasks[i];
        const auto recurrence = *current.recurrence;
        // Bounded walk: a daily series left open for decades stops here.
        for (int step = 0; step < 40000; ++step) {
            auto next = NextInstance(current, recurrence, port);
            if (!next || !next->reminderAt)
                break;
            if (*next->reminderAt >= now - 86400000) {
                next->projected = true;
                tasks.push_back(*next);
                if (*next->reminderAt > now)
                    break;
            }
            current = std::move(*next);
        }
    }
}
Error ApplyPatch(TaskRecord &target, const TaskPatch &patch, bool creating, const ILocalTimePort &port) {
    TaskRecord task = target;
    const auto oldReminder = ReminderFor(task.view);
    if (patch.title)
        task.view.title = Trim(*patch.title);
    if (patch.notes) {
        if (!ValidUtf8Length(*patch.notes, 10000))
            return Error::InvalidTask;
        task.view.notes = Trim(*patch.notes);
    }
    if (task.view.title.empty() || !ValidUtf8Length(task.view.title, 500))
        return Error::InvalidTask;
    if (patch.schedule) {
        if (!ValidSchedule(*patch.schedule, port))
            return Error::InvalidTime;
        task.view.schedule = patch.schedule;
        task.view.dueAt = patch.schedule->endAt;
        ScheduleTag(task);
        if (!patch.reminder && oldReminder.kind == ReminderKind::Relative) {
            const auto next = CheckedAdd(patch.schedule->startAt, -oldReminder.leadMilliseconds);
            if (!next || !ValidTimestamp(*next, port))
                return Error::InvalidTime;
            task.view.reminderAt = next;
        }
    }
    if (creating && !task.view.schedule)
        return Error::InvalidTime;
    if (patch.color) {
        const auto name = ColorName(*patch.color);
        if (!name)
            return Error::InvalidTask;
        task.view.color = *patch.color;
        ReplaceTag(task.tags, colorPrefix, *patch.color == Color::None ? "" : name);
    }
    if (patch.reminder) {
        const auto reminder = *patch.reminder;
        if (reminder.kind == ReminderKind::None)
            task.view.reminderAt.reset();
        else if (reminder.kind == ReminderKind::Relative) {
            if (!task.view.schedule || reminder.leadMilliseconds < 0 ||
                reminder.leadMilliseconds > 525600LL * 60000 || reminder.leadMilliseconds % 60000)
                return Error::InvalidReminder;
            const auto at = CheckedAdd(task.view.schedule->startAt, -reminder.leadMilliseconds);
            if (!at || !ValidTimestamp(*at, port))
                return Error::InvalidTime;
            task.view.reminderAt = at;
        } else if (reminder.kind == ReminderKind::LegacyAbsolute) {
            if (creating || oldReminder.kind != ReminderKind::LegacyAbsolute ||
                reminder.absoluteAt != oldReminder.absoluteAt)
                return Error::InvalidReminder;
            task.view.reminderAt = oldReminder.absoluteAt;
        } else
            return Error::InvalidReminder;
    }
    if (patch.recurrence) {
        const auto &recurrence = *patch.recurrence;
        if (recurrence && (recurrence->interval < 1 || recurrence->interval > 365 || !task.view.schedule))
            return Error::InvalidTask;
        task.recurrence = recurrence;
        task.recurrenceChanged = true;
    }
    if (task.tags.size() > 30)
        return Error::LimitExceeded;
    RefreshView(task, port);
    target = std::move(task);
    return Error::None;
}
Error CompleteTask(TaskRecord &target, Millis now, const std::string &nextId, std::optional<TaskRecord> &next,
                   const ILocalTimePort &port) {
    if (target.view.completed) {
        next.reset();
        return Error::None;
    }
    TaskRecord task = target;
    std::optional<TaskRecord> following;
    if (task.recurrence && task.view.dueAt) {
        auto view = NextInstance(task.view, *task.recurrence, port);
        // A calendar schedule completed late continues from its first
        // occurrence that has not ended, not from one already in the past.
        for (int step = 0; view && task.view.schedule && *view->dueAt <= now && step < 40000; ++step)
            view = NextInstance(*view, *task.recurrence, port);
        if (!view)
            return Error::InvalidTime;
        following = task;
        following->view.id = nextId;
        following->view.dueAt = view->dueAt;
        following->view.reminderAt = view->reminderAt;
        following->view.schedule = view->schedule;
        following->view.createdAt = now;
        following->view.updatedAt = now;
        following->view.revision = 0;
        following->view.completed = false;
        following->completedAt.reset();
        following->view.deletedAt.reset();
        following->resetSubtasks = true;
        if (following->view.schedule)
            ScheduleTag(*following);
        RefreshView(*following, port);
    }
    task.view.completed = true;
    task.view.updatedAt = now;
    task.completedAt = now;
    RefreshView(task, port);
    target = std::move(task);
    next = std::move(following);
    return Error::None;
}
} // namespace lume::todo
