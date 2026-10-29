#include "platform/windows/calendar/calendar_todo_draft.h"
#include "todo_test_support.h"
using namespace lume::platform::windows;
using namespace lume::todo;
using namespace todo_test;
class Gap final : public ILocalTimePort {
 public:
    Utc utc;
    std::optional<LocalDateTime> ToLocal(Millis x) const override { return utc.ToLocal(x); }
    std::optional<Millis> FromLocal(const LocalDateTime &x) const override {
      if(x.date==lume::calendar::Date{2026,3,8} && x.hour==2) return {};
      return utc.FromLocal(x);
    }
};
class Fold final : public ILocalTimePort {
 public:
    Utc utc;
    std::optional<LocalDateTime> ToLocal(Millis x) const override {
      return utc.ToLocal(x>=At(2026,11,1,2)?x-3600000:x);
    }
    std::optional<Millis> FromLocal(const LocalDateTime &x) const override {
      const auto value=utc.FromLocal(x);
      return value&&x.date==lume::calendar::Date{2026,11,1}&&x.hour>=2?
        std::optional<Millis>(*value+3600000):value;
    }
};
int main() {
 Utc time; Command seed=Add({"isolated",4}); CalendarTodoDraft draft(time,seed,At(2026,9,25,12),{});
 draft.title=u8"中文新日程 🗓";draft.notes=u8"中文备注\n第二行";Command command;
 Expect(draft.Build(command)==Error::None,"valid Unicode draft builds");
 Expect(command.patch.title==draft.title && command.patch.notes==draft.notes,"Unicode and multiline draft preserved");
 Expect(command.scope.accountKey=="isolated" && command.scope.generation==4,"command seed scope preserved");
 Expect(command.patch.reminder && command.patch.reminder->leadMilliseconds==600000,"new event defaults ten minutes");
 Expect(draft.SetStart({{2026,9,26},9,30}),"start accepts valid next day");
 Expect(draft.end && draft.end->date.day==26 && draft.end->hour==10 && draft.end->minute==30,"move preserves duration");
 draft.SetEnd({{2026,9,26},9,0});Expect(draft.Build(command)==Error::InvalidTime,"end must be later");
 Expect(draft.SetStart({{2026,9,26},11,0})&&draft.end->hour==12,
        "moving start with negative duration falls back to one hour");
 draft.SetEnd(*draft.start);
 Expect(draft.SetStart({{2026,9,26},13,0})&&draft.end->hour==14,
        "moving start with zero duration falls back to one hour");
 draft.title=std::string(501,'a');Expect(draft.Build(command)==Error::InvalidTask,"title bounded");
 draft.title="  valid  ";draft.notes=std::string(10001,'a');Expect(draft.Build(command)==Error::InvalidTask,"notes bounded");
 TaskView existing;existing.title="legacy";existing.schedule=seed.patch.schedule;existing.reminder={ReminderKind::LegacyAbsolute,0,At(2026,9,24),false};
 seed.type=CommandType::Update;seed.id="old";seed.patch={};CalendarTodoDraft legacy(time,seed,At(2026,9,25),existing);
 Expect(legacy.Build(command)==Error::None && !command.patch.reminder,"untouched legacy reminder omitted");
 legacy.SetReminder({ReminderKind::Relative,525600LL*60000,0,false});
 Expect(legacy.Build(command)==Error::None && command.patch.reminder->leadMilliseconds==525600LL*60000,"arbitrary maximum reminder");
 legacy.SetReminder({ReminderKind::Relative,60001,0,false});Expect(legacy.Build(command)==Error::InvalidReminder,"fractional minutes rejected");
 legacy.SetReminder({ReminderKind::None,0,0,false});Expect(legacy.Build(command)==Error::None && command.patch.reminder->kind==ReminderKind::None,"none explicit");
 Gap gap;seed.patch.schedule=Schedule{At(2026,3,8,1),At(2026,3,8,2)};CalendarTodoDraft skipped(gap,seed,At(2026,3,8),existing);
 skipped.title="gap draft";Expect(!skipped.SetStart({{2026,3,8},2,30}),"gap is rejected");
 Expect(skipped.start->hour==2 && skipped.Build(command)==Error::InvalidTime && skipped.title=="gap draft","invalid local draft retained");
 seed.patch.schedule=Schedule{At(9999,12,31,22),At(9999,12,31,23)};CalendarTodoDraft edge(time,seed,At(9999,12,31),existing);
 Expect(!edge.SetStart({{9999,12,31},23,30}),"duration overflow year boundary rejected");
 Fold fold;seed.patch={};existing.schedule=Schedule{At(2026,11,1,2,15),At(2026,11,1,2,45)};
 CalendarTodoDraft repeated(fold,seed,At(2026,11,1),existing);repeated.title="only title changed";
 Expect(repeated.Build(command)==Error::None&&command.patch.schedule->startAt==existing.schedule->startAt&&
        command.patch.schedule->endAt==existing.schedule->endAt,"unchanged fold endpoints preserve exact instants");
 repeated.SetEnd({{2026,11,1},2,0});
 Expect(repeated.Build(command)==Error::None&&command.patch.schedule->startAt==existing.schedule->startAt&&
        command.patch.schedule->endAt==At(2026,11,1,3),"editing only end preserves ambiguous start instant");
 CalendarTodoDraft movedFold(fold,seed,At(2026,11,1),existing);
 Expect(movedFold.SetStart({{2026,11,1},1,15})&&movedFold.Build(command)==Error::None&&
        command.patch.schedule->startAt==existing.schedule->startAt,"reselecting same fold time preserves instant");
 std::cout<<"calendar todo draft tests passed\n";
}
