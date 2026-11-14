#include "core/todo/todo_sheet.h"
#include "core/todo/todo_time.h"
#include <algorithm>
#include <array>
#include <cstdio>

namespace lume::todo {
namespace {
using Json = nlohmann::json;
constexpr std::array<const char*, 6> ColorNames{"none", "red", "orange", "yellow", "green", "blue"};

std::string DateText(calendar::Date d) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d", d.year, d.month, d.day);
    return text;
}
std::string ClockText(const LocalDateTime& t) {
    char text[8]{};
    std::snprintf(text, sizeof(text), "%02d:%02d", t.hour, t.minute);
    return text;
}
std::optional<calendar::Date> ParseDate(const Json& value) {
    if (!value.is_string()) return std::nullopt;
    const auto text = value.get<std::string>();
    calendar::Date d{};
    char tail = 0;
    if (text.size() != 10 || std::sscanf(text.c_str(), "%4d-%2d-%2d%c", &d.year, &d.month, &d.day, &tail) != 3) return std::nullopt;
    if (!calendar::IsValidDate(d) || d.year < 1970 || d.year > 2999) return std::nullopt;
    return d;
}
// "" clears the time of day (the task keeps the default morning slot).
std::optional<std::pair<int, int>> ParseClock(const Json& value) {
    if (!value.is_string()) return std::nullopt;
    const auto text = value.get<std::string>();
    int hour = 0, minute = 0;
    char tail = 0;
    if (text.size() != 5 || std::sscanf(text.c_str(), "%2d:%2d%c", &hour, &minute, &tail) != 2) return std::nullopt;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return std::nullopt;
    return std::pair{hour, minute};
}
std::optional<Color> ParseColor(const Json& value) {
    if (!value.is_string()) return std::nullopt;
    const auto text = value.get<std::string>();
    for (std::size_t i = 0; i < ColorNames.size(); ++i)
        if (text == ColorNames[i]) return static_cast<Color>(i);
    return std::nullopt;
}
std::optional<std::string> ParseText(const Json& value, std::size_t limit) {
    if (!value.is_string()) return std::nullopt;
    auto text = value.get<std::string>();
    if (text.size() > limit * 4) return std::nullopt;
    return text;
}
}  // namespace
const char *SheetErrorName(Error error) {
    switch (error) {
    case Error::None: return "";
    case Error::ReadOnly: case Error::NotLoaded: return "read-only";
    case Error::InvalidTask: return "invalid-task";
    case Error::InvalidTime: return "invalid-time";
    case Error::NotFound: case Error::Deleted: return "not-found";
    case Error::StaleScope: return "stale";
    case Error::LimitExceeded: return "limit";
    default: return "save";
    }
}
namespace {
// A start on the given day at the given (or preserved, or default) time,
// keeping the previous duration when there is one.
std::optional<Schedule> Reschedule(std::optional<Schedule> previous, std::optional<calendar::Date> date,
    std::optional<std::optional<std::pair<int, int>>> clock, const ILocalTimePort& time, Millis now) {
    std::optional<LocalDateTime> start;
    if (previous) start = time.ToLocal(previous->startAt);
    const auto day = date ? *date : start ? start->date : calendar::Date{};
    if (!date && !start) return std::nullopt;
    if (!clock || !*clock) {
        if (!start || (clock && !*clock)) {
            const auto fallback = DefaultSchedule(day, now, time);
            if (!fallback) return std::nullopt;
            if (!previous) return fallback;
            const auto local = time.ToLocal(fallback->startAt);
            if (!local) return std::nullopt;
            start = local;
        }
    } else {
        if (!start) start = LocalDateTime{};
        start->hour = (*clock)->first;start->minute = (*clock)->second;start->second = start->millisecond = 0;
    }
    start->date = day;
    const auto from = ResolveLocal(*start, time);
    if (!from) return std::nullopt;
    const Millis duration = previous ? std::max<Millis>(0, previous->endAt - previous->startAt) : 3600000;
    const auto to = CheckedAdd(*from, duration);
    if (!to) return std::nullopt;
    const Schedule result{*from, *to};
    return ValidSchedule(result, time) ? std::optional(result) : std::nullopt;
}
constexpr std::array<const char*, 5> RepeatNames{"daily", "weekdays", "weekly", "monthly", "none"};
bool Midnight(const LocalDateTime& t) { return !t.hour && !t.minute && !t.second && !t.millisecond; }
long long DayNumber(calendar::Date d) {
    const int y = d.year - (d.month <= 2 ? 1 : 0), era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    const int doy = (153 * (d.month + (d.month > 2 ? -3 : 9)) + 2) / 5 + d.day - 1;
    return static_cast<long long>(era) * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy;
}
// A complete schedule from the sheet: an all-day span of whole days, or a
// start and end time over one or more days (an end not after the start on a
// single day rolls over to the next morning).
std::optional<Schedule> FullSchedule(calendar::Date date, bool allDay, int days, std::optional<std::pair<int, int>> start,
    std::optional<std::pair<int, int>> end, const ILocalTimePort& time) {
    if (days < 1 || days > 366) return std::nullopt;
    LocalDateTime from{date, 0, 0, 0, 0}, to{};
    if (allDay) {
        const auto last = calendar::AddDays(date, days);
        if (!last) return std::nullopt;
        to = {*last, 0, 0, 0, 0};
    } else {
        if (!start) return std::nullopt;
        from.hour = start->first;from.minute = start->second;
        const auto endClock = end ? *end : std::pair{std::min(23, start->first + 1), start->first >= 23 ? 59 : start->second};
        auto last = calendar::AddDays(date, days - 1);
        if (!last) return std::nullopt;
        if (days == 1 && endClock <= *start) last = calendar::AddDays(date, 1);
        if (!last) return std::nullopt;
        to = {*last, endClock.first, endClock.second, 0, 0};
    }
    const auto a = ResolveLocal(from, time), b = ResolveLocal(to, time);
    if (!a || !b) return std::nullopt;
    const Schedule result{*a, *b};
    return ValidSchedule(result, time) ? std::optional(result) : std::nullopt;
}
}  // namespace

std::string SheetDate(calendar::Date d) { return DateText(d); }

Json SheetRow(const TaskView &task, const ILocalTimePort &time) {
    Json row{{"id", task.id}, {"title", task.title}, {"notes", task.notes}, {"completed", task.completed},
        {"color", ColorNames[std::min<std::size_t>(static_cast<std::size_t>(task.color), ColorNames.size() - 1)]},
        {"repeat", task.recurrence.has_value()}, {"date", ""}, {"time", ""}, {"end", ""},
        {"allDay", false}, {"days", 1}, {"endDate", ""}, {"subtasks", Json::array()},
        {"frequency", task.recurrence ? RepeatNames[std::min<std::size_t>(static_cast<std::size_t>(task.recurrence->frequency), 3)] : "none"},
        {"reminder", task.reminder.kind == ReminderKind::Relative ? static_cast<long long>(task.reminder.leadMilliseconds / 60000) :
            task.reminder.kind == ReminderKind::None ? -1LL : -2LL}};
    for (const auto& step : task.subtasks)
        row["subtasks"].push_back({{"id", step.id}, {"title", step.title}, {"completed", step.completed}});
    if (task.schedule) {
        if (const auto start = time.ToLocal(task.schedule->startAt)) {
            row["date"] = DateText(start->date);row["time"] = ClockText(*start);
        }
        const auto start = time.ToLocal(task.schedule->startAt), end = time.ToLocal(task.schedule->endAt);
        if (end) {
            row["end"] = ClockText(*end);
            if (start) {
                const bool allDay = Midnight(*start) && Midnight(*end) && DayNumber(end->date) > DayNumber(start->date);
                const auto span = DayNumber(end->date) - DayNumber(start->date);
                row["allDay"] = allDay;
                row["days"] = std::clamp<long long>(allDay ? span : span + (Midnight(*end) && span > 0 ? 0 : 1), 1, 366);
                row["endDate"] = DateText(allDay ? calendar::AddDays(end->date, -1).value_or(end->date) : end->date);
                if (allDay) row["time"] = row["end"] = "";
            }
        }
    } else if (task.day) row["date"] = DateText(*task.day);
    return row;
}

std::string ReadSheetPatch(const Json &fields, const TaskView *existing, bool creating, const ILocalTimePort &time,
                           Millis now, TaskPatch &out) {
    if (const auto field = fields.find("title"); field != fields.end()) {
        const auto title = ParseText(*field, 500);
        if (!title) return "invalid";
        out.title = *title;
    } else if (creating) return "invalid";
    if (const auto field = fields.find("notes"); field != fields.end()) {
        const auto notes = ParseText(*field, 10000);
        if (!notes) return "invalid";
        out.notes = *notes;
    }
    if (const auto field = fields.find("color"); field != fields.end()) {
        const auto color = ParseColor(*field);
        if (!color) return "invalid";
        out.color = *color;
    }
    std::optional<calendar::Date> date;
    std::optional<std::optional<std::pair<int, int>>> clock;
    if (const auto field = fields.find("date"); field != fields.end()) {
        date = ParseDate(*field);
        if (!date) return "invalid";
    } else if (creating) return "invalid";
    if (const auto field = fields.find("time"); field != fields.end()) {
        if (field->is_string() && field->get<std::string>().empty()) clock = std::optional<std::pair<int, int>>{};
        else {
            const auto parsed = ParseClock(*field);
            if (!parsed) return "invalid";
            clock = parsed;
        }
    }
    if (const auto field = fields.find("allDay"); field != fields.end()) {
        // The sheet sends a complete schedule.
        const auto daysField = fields.find("days");
        if (!field->is_boolean() || !date || daysField == fields.end() || !daysField->is_number_integer()) return "invalid";
        const auto days = daysField->get<int>();
        std::optional<std::pair<int, int>> start, end;
        if (!field->get<bool>()) {
            start = ParseClock(fields.value("time", Json{}));
            if (!start) return "invalid";
            if (const auto stop = fields.find("end"); stop != fields.end()) {
                end = ParseClock(*stop);
                if (!end) return "invalid";
            }
        }
        const auto schedule = FullSchedule(*date, field->get<bool>(), days, start, end, time);
        if (!schedule) return "invalid-time";
        out.schedule = *schedule;
    } else if (date || clock) {
        const auto schedule = Reschedule(existing ? existing->schedule : std::nullopt, date, clock, time, now);
        if (!schedule) return "invalid-time";
        out.schedule = *schedule;
    }
    if (const auto field = fields.find("reminder"); field != fields.end()) {
        if (!field->is_number_integer()) return "invalid";
        const auto minutes = field->get<long long>();
        if (minutes == -1) out.reminder = ReminderDraft{};
        else if (minutes >= 0 && minutes <= 525600) {
            ReminderDraft reminder;reminder.kind = ReminderKind::Relative;reminder.leadMilliseconds = minutes * 60000;
            out.reminder = reminder;
        } else return "invalid";
    }
    if (const auto field = fields.find("subtasks"); field != fields.end()) {
        if (!field->is_array() || field->size() > 100) return "invalid";
        std::vector<Subtask> steps;
        for (const auto& item : *field) {
            if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || !item.contains("title") ||
                !item["title"].is_string() || !item.contains("completed") || !item["completed"].is_boolean()) return "invalid";
            steps.push_back({item["id"].get<std::string>(), item["title"].get<std::string>(), item["completed"].get<bool>()});
        }
        out.subtasks = std::move(steps);
    }
    if (const auto field = fields.find("frequency"); field != fields.end()) {
        if (!field->is_string()) return "invalid";
        const auto name = field->get<std::string>();
        const auto found = std::find(RepeatNames.begin(), RepeatNames.end(), name);
        if (found == RepeatNames.end()) return "invalid";
        if (name == "none") out.recurrence = std::optional<Recurrence>{};
        else out.recurrence = std::optional<Recurrence>{Recurrence{static_cast<Frequency>(found - RepeatNames.begin()), 1}};
    }
    return {};
}
}  // namespace lume::todo
