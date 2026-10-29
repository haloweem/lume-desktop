#include "todo_test_support.h"
#include "core/todo/todo_json_codec.h"
#include "core/todo/todo_model.h"
#include "todo_time_cases.h"
#include <limits>
using namespace lume::todo;
using namespace todo_test;

namespace {
void CodecNumberBoundaries(const TodoJsonCodec &codec, const Utc &utc, const std::string &valid) {
    TodoState output;
    Expect(codec.Decode(valid, output, utc) == Error::None, "numeric boundary sentinel state loads");
    std::string before;
    Expect(codec.Encode(output, before) == Error::None, "numeric boundary sentinel state encodes");
    struct InvalidCase {
        std::string json;
        Error error;
        const char *message;
    };
    const auto task = [](const char *field, const char *number) {
        return std::string("{\"version\":1,\"tasks\":[{\"id\":\"numeric\",\"title\":\"numeric\",\"") + field +
               "\":" + number + "}]}";
    };
    const std::vector<InvalidCase> cases = {
        {task("dueAt", "9223372036854775808"), Error::InvalidTime,
         "unsigned timestamp above int64 maximum rejected"},
        {task("dueAt", "-9223372036854775809"), Error::InvalidTime, "timestamp below int64 minimum rejected"},
        {task("dueAt", "1e999"), Error::InvalidJson, "floating point overflow rejected"},
        {task("dueAt", "0.5"), Error::InvalidTime, "fractional millisecond timestamp rejected"},
        {task("revision", "9223372036854775808"), Error::InvalidTask,
         "task revision above int64 maximum rejected"},
        {task("revision", "-1"), Error::InvalidTask, "negative task revision rejected"},
        {task("revision", "0.5"), Error::InvalidTask, "fractional task revision rejected"},
        {"{\"version\":1,\"revision\":9223372036854775808,\"tasks\":[]}", Error::InvalidJson,
         "root revision above int64 maximum rejected"},
        {"{\"version\":1,\"revision\":-1,\"tasks\":[]}", Error::InvalidJson,
         "negative root revision rejected"},
        {"{\"version\":1,\"revision\":0.5,\"tasks\":[]}", Error::InvalidJson,
         "fractional root revision rejected"}};
    for (const auto &entry : cases) {
        Expect(codec.Decode(entry.json, output, utc) == entry.error, entry.message);
        std::string after;
        Expect(codec.Encode(output, after) == Error::None && after == before,
               "rejected numeric input leaves complete decoded output unchanged");
    }
    std::cout << "codec numeric boundary and output preservation checks passed\n";
}
} // namespace

int main() {
    Utc utc;
    Expect(ValidLocal({{1, 1, 1}}) && ValidLocal({{9999, 12, 31}, 23, 59, 59, 999}), "years 1 through 9999");
    Expect(!ValidLocal({{0, 1, 1}}) && !ValidLocal({{2025, 2, 29}}), "reject invalid calendar dates");
    const auto today = DefaultSchedule({2026, 9, 25}, At(2026, 9, 25, 23, 55, 53), utc);
    Expect(today && today->startAt == At(2026, 9, 25, 23, 59) && today->endAt == At(2026, 9, 26, 0, 59),
           "today default capped minute plus one hour");
    Expect(DefaultSchedule({2026, 9, 26}, At(2026, 9, 25), utc)->startAt == At(2026, 9, 26, 9),
           "other day default nine");
    Expect(!DefaultSchedule({9999, 12, 31}, At(9999, 12, 30), utc), "start upper date rejected");
    auto moved = MoveScheduleStart({1000, 31000}, 2000, utc);
    Expect(moved && moved->endAt == 32000, "start movement keeps duration milliseconds");
    Expect(!CheckedAdd(std::numeric_limits<Millis>::max(), 1), "addition overflow rejected");
    TaskView task;
    task.dueAt = At(2026, 9, 26);
    Expect(GroupDay(task, utc) == lume::calendar::Date{2026, 9, 25},
           "legacy midnight belongs to preceding date");
    task.schedule = Schedule{At(2026, 9, 25, 23), At(2026, 9, 26, 1)};
    Expect(GroupDay(task, utc) == lume::calendar::Date{2026, 9, 25}, "schedule grouped by start");
    task.reminderAt = 0;
    Expect(ReminderFor(task).kind == ReminderKind::Relative, "epoch zero reminder retained");
    task.schedule = Schedule{60001, 120000};
    task.reminderAt = 0;
    Expect(ReminderFor(task).leadMilliseconds == 60001 && ReminderFor(task).legacyRelative,
           "legacy fractional-minute lead retained");
    task.reminderAt = 60002;
    Expect(ReminderFor(task).kind == ReminderKind::LegacyAbsolute, "later reminder remains absolute");
    task.schedule.reset();
    Expect(ReminderFor(task).kind == ReminderKind::LegacyAbsolute, "unscheduled reminder remains absolute");
    Expect(ValidUtf8Length(u8"中文😀", 4) && !ValidUtf8Length(u8"中文😀", 3),
           "UTF16 lengths count surrogate pairs");
    Expect(!ValidUtf8Length(std::string("\xc0\xaf", 2), 5), "reject invalid UTF8");
    Expect(Trim(u8"\u3000 title \u00a0") == "title", "Unicode whitespace trim");
    Expect(NextOccurrence(At(2024, 1, 31, 10), {Frequency::Monthly, 1}, utc) == At(2024, 2, 29, 10),
           "monthly leap clamp");
    Expect(NextOccurrence(At(2026, 9, 25, 10), {Frequency::Weekdays, 1}, utc) == At(2026, 9, 28, 10),
           "weekdays skip weekend");
    Expect(!NextOccurrence(At(9999, 12, 31), {Frequency::Daily, 1}, utc),
           "recurrence outside supported years rejected");
    TaskRecord record;
    record.view.id = "one";
    record.view.title = "old";
    record.view.schedule = Schedule{60001, 120001};
    record.view.reminderAt = 0;
    record.tags = {"private"};
    TaskPatch patch;
    patch.schedule = Schedule{120001, 180001};
    patch.title = " updated ";
    Expect(ApplyPatch(record, patch, false, utc) == Error::None && record.view.reminderAt == 60000 &&
               record.view.dueAt == 180001,
           "schedule update shifts untouched fractional reminder and sets dueAt");
    patch.reminder = ReminderDraft{ReminderKind::Relative, 60001, 0, true};
    Expect(ApplyPatch(record, patch, false, utc) == Error::InvalidReminder,
           "explicit forged legacy lead rejected");
    TodoJsonCodec codec;
    TodoState state;
    const std::string json =
        R"({"version":1,"revision":42,"unknown":{"nested":[1,true]},"reminded":{"a":0},"tasks":[{"id":"a","title":"\u4e2d\u6587\ud83d\ude00","createdAt":0,"updatedAt":0,"revision":9,"reminderAt":0,"metadata":{"x":7},"recurrence":{"frequency":"monthly","interval":1,"extra":true},"subtasks":[{"id":"s","title":"sub","completed":true,"extra":7}]}],"outbox":[]})";
    CodecNumberBoundaries(codec, utc, json);
    Expect(codec.Decode(json, state, utc) == Error::None && state.tasks[0].view.title == u8"中文😀" &&
               state.tasks[0].view.reminderAt == 0,
           "decode Chinese surrogate escaped emoji and zero");
    std::string encoded;
    Expect(codec.Encode(state, encoded) == Error::None, "encode loaded state");
    Expect(encoded.find("\"extra\":7") != std::string::npos &&
               encoded.find("\"reminded\"") != std::string::npos &&
               encoded.find("\"unknown\"") != std::string::npos,
           "unknown root task nested metadata preserved");
    Expect(state.preservedJson.find("\"tasks\"") == std::string::npos &&
               state.preservedJson.find("\"outbox\"") == std::string::npos,
           "root metadata does not duplicate owned task and outbox arrays");
    Expect(state.tasks[0].preservedJson.find(u8"中文") == std::string::npos,
           "typed title is not duplicated in task metadata");
    Expect(codec.Decode("{\"version\":2,\"tasks\":[]}", state, utc) == Error::UnsupportedVersion,
           "unknown version distinguished");
    Expect(codec.Decode("{\"version\":1,\"tasks\":[],\"outbox\":[]}", state, utc) == Error::None &&
               state.inlineOutbox,
           "inline empty distinguished");
    Expect(codec.Decode("{\"version\":1,\"tasks\":[]}", state, utc) == Error::None && !state.inlineOutbox,
           "absent inline distinguished");
    Expect(codec.Decode(std::string(TodoJsonCodec::MaxBytes + 1, ' '), state, utc) == Error::LimitExceeded,
           "input size limit");
    const std::string deep = std::string(60, '[') + "0" + std::string(60, ']');
    Expect(codec.Decode(deep, state, utc) == Error::LimitExceeded, "depth limited before parse");
    std::string nestedSchedule =
        "{\"version\":1,\"tasks\":[{\"id\":\"deep\",\"title\":\"deep\",\"tags\":[\"lume:calendar-schedule:" +
        deep + "\"]}]}";
    Expect(codec.Decode(nestedSchedule, state, utc) == Error::LimitExceeded,
           "embedded schedule JSON also obeys depth bound");
    TimeAndValidationCases();
    std::cout << "todo core checks passed\n";
}
