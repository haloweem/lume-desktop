#pragma once
#include "todo_test_support.h"
#include "core/todo/todo_model.h"

namespace todo_test {
class DstPort final : public lume::todo::ILocalTimePort {
  public:
    bool lateFold = false, normalizeGap = false;
    std::optional<lume::todo::LocalDateTime> ToLocal(lume::todo::Millis value) const override {
        const bool summer = value >= At(2026, 3, 8, 7) && value < At(2026, 11, 1, 6);
        return Utc{}.ToLocal(value + (summer ? -4LL : -5LL) * 3600000);
    }
    std::optional<lume::todo::Millis> FromLocal(const lume::todo::LocalDateTime &x) const override {
        const auto naive = Utc{}.FromLocal(x);
        if (!naive)
            return {};
        const auto early = *naive + 4 * 3600000LL, late = *naive + 5 * 3600000LL;
        const auto e = ToLocal(early), l = ToLocal(late);
        if (e && *e == x && l && *l == x)
            return lateFold ? late : early;
        if (e && *e == x)
            return early;
        if (l && *l == x)
            return late;
        return normalizeGap ? std::optional<lume::todo::Millis>(late) : std::nullopt;
    }
};
inline void TimeAndValidationCases() {
    using namespace lume::todo;
    Utc utc;
    DstPort dst;
    Expect(!ResolveLocal({{2026, 3, 8}, 2, 30, 0, 0}, dst), "DST spring gap rejected");
    dst.normalizeGap = true;
    Expect(!ResolveLocal({{2026, 3, 8}, 2, 30, 0, 0}, dst),
           "DST silent normalization rejected by round trip");
    const auto early = ResolveLocal({{2026, 11, 1}, 1, 30, 0, 0}, dst);
    dst.lateFold = true;
    const auto late = ResolveLocal({{2026, 11, 1}, 1, 30, 0, 0}, dst);
    Expect(early && late && *late - *early == 3600000, "DST fold follows injected port choice");
    const auto friday = *ResolveLocal({{2026, 3, 6}, 9, 0, 0, 0}, dst);
    const auto weekdays = NextOccurrence(friday, {Frequency::Weekdays, 1}, dst);
    Expect(weekdays && dst.ToLocal(*weekdays)->hour == 9 && *weekdays - friday == 71LL * 3600000,
           "weekdays keep local hour across DST");
    const auto saturday = *ResolveLocal({{2026, 3, 7}, 9, 0, 0, 0}, dst);
    const auto daily = NextOccurrence(saturday, {Frequency::Daily, 1}, dst),
               weekly = NextOccurrence(friday, {Frequency::Weekly, 1}, dst);
    Expect(daily && *daily - saturday == 86400000 && dst.ToLocal(*daily)->hour == 10,
           "daily fixed milliseconds across DST");
    Expect(weekly && *weekly - friday == 7LL * 86400000 && dst.ToLocal(*weekly)->hour == 10,
           "weekly fixed milliseconds across DST");
    const auto feb = *ResolveLocal({{2026, 2, 8}, 2, 30, 0, 0}, dst);
    Expect(!NextOccurrence(feb, {Frequency::Monthly, 1}, dst), "monthly recurrence landing in gap rejected");
    Expect(ValidSchedule({At(1, 1, 1), At(1, 1, 1, 1)}, utc), "schedule minimum year supported");
    Expect(ValidSchedule({At(9999, 12, 30, 23, 59), At(9999, 12, 31, 23, 59)}, utc),
           "schedule maximum minute supported");
    Expect(!ValidSchedule({At(9999, 12, 30, 23, 59, 0, 1), At(9999, 12, 31, 23, 59)}, utc),
           "start beyond maximum exact minute rejected");
    Expect(!ValidSchedule({At(9999, 12, 30, 23, 59), At(9999, 12, 31, 23, 59, 0, 1)}, utc),
           "end beyond maximum exact minute rejected");
    TaskRecord record;
    record.view.title = "original";
    record.view.id = "fixed";
    record.view.revision = 19;
    record.view.createdAt = 0;
    record.view.schedule = Schedule{At(2026, 9, 25, 9), At(2026, 9, 25, 10)};
    record.view.dueAt = record.view.schedule->endAt;
    TaskPatch patch;
    patch.title = std::string(500, 'a');
    Expect(ApplyPatch(record, patch, false, utc) == Error::None, "500 UTF16 title accepted");
    patch.title = std::string(501, 'a');
    Expect(ApplyPatch(record, patch, false, utc) == Error::InvalidTask, "501 UTF16 title rejected");
    patch.title = u8"  \u3000  ";
    Expect(ApplyPatch(record, patch, false, utc) == Error::InvalidTask,
           "Unicode whitespace-only title rejected");
    patch.title = "valid";
    patch.notes = std::string(10001, 'n');
    Expect(ApplyPatch(record, patch, false, utc) == Error::InvalidTask, "notes cap before trimming");
    patch.notes = std::string(10000, 'n');
    Expect(ApplyPatch(record, patch, false, utc) == Error::None, "10000 UTF16 notes accepted");
    Expect(record.view.id == "fixed" && record.view.revision == 19 && record.view.createdAt == 0,
           "patch leaves immutable identity revision creation unchanged");
    for (const auto color :
         {Color::None, Color::Red, Color::Orange, Color::Yellow, Color::Green, Color::Blue}) {
        patch = {};
        patch.color = color;
        Expect(ApplyPatch(record, patch, false, utc) == Error::None && record.view.color == color,
               "all six colors accepted");
    }
    patch.color = static_cast<Color>(99);
    Expect(ApplyPatch(record, patch, false, utc) == Error::InvalidTask, "invalid color rejected");
    record.tags.assign(30, "tag");
    patch = {};
    patch.color = Color::Red;
    Expect(ApplyPatch(record, patch, false, utc) == Error::LimitExceeded,
           "tag overflow rejects complete patch");
    record.tags.clear();
    record.view.reminderAt = record.view.schedule->startAt - 525601LL * 60000 - 1;
    patch = {};
    patch.schedule = Schedule{record.view.schedule->startAt + 1000, record.view.schedule->endAt + 1000};
    const auto reminder = *record.view.reminderAt;
    Expect(ApplyPatch(record, patch, false, utc) == Error::None && record.view.reminderAt == reminder + 1000,
           "unchanged legacy oversized fractional relative lead moves exactly");
    patch = {};
    patch.reminder = ReminderDraft{ReminderKind::Relative, 525601LL * 60000, 0, true};
    Expect(ApplyPatch(record, patch, false, utc) == Error::InvalidReminder, "new oversized lead rejected");
    patch.reminder = ReminderDraft{ReminderKind::Relative, 525600LL * 60000, 0, false};
    Expect(ApplyPatch(record, patch, false, utc) == Error::None, "new maximum lead accepted");
    patch.reminder = ReminderDraft{ReminderKind::Relative, -60000, 0, false};
    Expect(ApplyPatch(record, patch, false, utc) == Error::InvalidReminder, "new negative lead rejected");
    record.view.reminderAt = record.view.schedule->startAt + 1;
    const auto absolute = *record.view.reminderAt;
    patch = {};
    patch.schedule = Schedule{record.view.schedule->startAt + 60000, record.view.schedule->endAt + 60000};
    Expect(ApplyPatch(record, patch, false, utc) == Error::None && record.view.reminderAt == absolute,
           "legacy after-start reminder retains absolute timestamp when start moves");
    record.view.schedule.reset();
    record.view.reminderAt = 0;
    patch = {};
    patch.schedule = Schedule{100000, 200000};
    Expect(ApplyPatch(record, patch, false, utc) == Error::None && record.view.reminderAt == 0,
           "legacy no-schedule epoch zero reminder preserved when adding schedule");
    patch = {};
    patch.reminder = ReminderDraft{};
    Expect(ApplyPatch(record, patch, false, utc) == Error::None && !record.view.reminderAt,
           "explicit None clears reminder");
}
} // namespace todo_test
