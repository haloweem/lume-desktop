#include "core/todo/todo_time.h"
#include <algorithm>
#include <limits>

namespace lume::todo {
bool operator==(const LocalDateTime &a, const LocalDateTime &b) noexcept {
    return a.date == b.date && a.hour == b.hour && a.minute == b.minute && a.second == b.second &&
           a.millisecond == b.millisecond;
}
bool ValidLocal(const LocalDateTime &x) noexcept {
    return calendar::IsValidDate(x.date) && x.hour >= 0 && x.hour < 24 && x.minute >= 0 && x.minute < 60 &&
           x.second >= 0 && x.second < 60 && x.millisecond >= 0 && x.millisecond < 1000;
}
std::optional<Millis> CheckedAdd(Millis a, Millis b) noexcept {
    if ((b > 0 && a > std::numeric_limits<Millis>::max() - b) ||
        (b < 0 && a < std::numeric_limits<Millis>::min() - b))
        return {};
    return a + b;
}
std::optional<Millis> ResolveLocal(const LocalDateTime &x, const ILocalTimePort &port) {
    if (!ValidLocal(x))
        return {};
    const auto instant = port.FromLocal(x);
    if (!instant)
        return {};
    const auto roundtrip = port.ToLocal(*instant);
    return roundtrip && *roundtrip == x ? instant : std::nullopt;
}
bool ValidTimestamp(Millis x, const ILocalTimePort &port) {
    const auto local = port.ToLocal(x);
    return local && ValidLocal(*local);
}
bool ValidSchedule(Schedule x, const ILocalTimePort &port) {
    if (x.endAt <= x.startAt)
        return false;
    const auto start = port.ToLocal(x.startAt), end = port.ToLocal(x.endAt);
    if (!start || !end || !ValidLocal(*start) || !ValidLocal(*end))
        return false;
    if (start->date > calendar::Date{9999, 12, 30})
        return false;
    // datetime-local controls have minute precision at their maximum.
    if (start->date == calendar::Date{9999, 12, 30} && start->hour == 23 && start->minute == 59 &&
        (start->second || start->millisecond))
        return false;
    if (end->date == calendar::Date{9999, 12, 31} && end->hour == 23 && end->minute == 59 &&
        (end->second || end->millisecond))
        return false;
    return true;
}
std::optional<Schedule> DefaultSchedule(calendar::Date date, Millis now, const ILocalTimePort &port) {
    const auto today = port.ToLocal(now);
    if (!today || !ValidLocal(*today))
        return {};
    LocalDateTime value{date, 9, 0, 0, 0};
    if (date == today->date) {
        const int minutes = std::min(1439, today->hour * 60 + today->minute + 15);
        value.hour = minutes / 60;
        value.minute = minutes % 60;
    }
    const auto start = ResolveLocal(value, port);
    if (!start)
        return {};
    const auto end = CheckedAdd(*start, 3600000);
    if (!end)
        return {};
    const Schedule result{*start, *end};
    return ValidSchedule(result, port) ? std::optional<Schedule>(result) : std::nullopt;
}
std::optional<Schedule> MoveScheduleStart(Schedule value, Millis start, const ILocalTimePort &port) {
    if (!ValidSchedule(value, port))
        return {};
    const auto end = CheckedAdd(start, value.endAt - value.startAt);
    if (!end)
        return {};
    const Schedule result{start, *end};
    return ValidSchedule(result, port) ? std::optional<Schedule>(result) : std::nullopt;
}
std::optional<calendar::Date> GroupDay(const TaskView &task, const ILocalTimePort &port) {
    if (task.deletedAt)
        return {};
    const auto instant = task.schedule ? std::optional<Millis>(task.schedule->startAt) : task.dueAt;
    if (!instant)
        return {};
    const auto local = port.ToLocal(*instant);
    if (!local || !ValidLocal(*local))
        return {};
    if (!task.schedule && !local->hour && !local->minute && !local->second && !local->millisecond)
        return calendar::AddDays(local->date, -1);
    return local->date;
}
ReminderDraft ReminderFor(const TaskView &task) {
    if (!task.reminderAt)
        return {};
    if (task.schedule && *task.reminderAt <= task.schedule->startAt) {
        const auto difference =
            static_cast<long double>(task.schedule->startAt) - static_cast<long double>(*task.reminderAt);
        if (difference <= static_cast<long double>(std::numeric_limits<Millis>::max())) {
            const auto lead = static_cast<Millis>(difference);
            return {ReminderKind::Relative, lead, 0, lead % 60000 != 0 || lead > 525600LL * 60000};
        }
    }
    return {ReminderKind::LegacyAbsolute, 0, *task.reminderAt, false};
}
std::optional<Millis> NextOccurrence(Millis value, Recurrence recurrence, const ILocalTimePort &port) {
    if (recurrence.interval < 1 || recurrence.interval > 365 || !ValidTimestamp(value, port))
        return {};
    if (recurrence.frequency == Frequency::Daily || recurrence.frequency == Frequency::Weekly) {
        const auto next = CheckedAdd(value, 86400000LL * recurrence.interval *
                                                (recurrence.frequency == Frequency::Weekly ? 7 : 1));
        return next && ValidTimestamp(*next, port) ? next : std::nullopt;
    }
    auto local = port.ToLocal(value);
    if (!local)
        return {};
    if (recurrence.frequency == Frequency::Monthly) {
        const int month = (local->date.year - 1) * 12 + local->date.month - 1 + recurrence.interval;
        if (month >= 9999 * 12)
            return {};
        local->date.year = month / 12 + 1;
        local->date.month = month % 12 + 1;
        local->date.day =
            std::min(local->date.day, calendar::DaysInMonth(local->date.year, local->date.month));
    } else if (recurrence.frequency == Frequency::Weekdays) {
        int remaining = recurrence.interval;
        while (remaining) {
            const auto next = calendar::AddDays(local->date, 1);
            if (!next)
                return {};
            local->date = *next;
            const int weekday = calendar::Weekday(*next);
            if (weekday != 0 && weekday != 6)
                --remaining;
        }
    } else
        return {};
    return ResolveLocal(*local, port);
}
} // namespace lume::todo
