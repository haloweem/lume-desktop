#include "core/reminder/reminder_ledger.h"
#include "todo_test_support.h"
#include <limits>
using namespace lume::reminder;
using todo_test::Expect;
using todo_test::Files;
using lume::todo::Millis;
static lume::todo::TaskView Task(std::string id, Millis at) {
    lume::todo::TaskView task;
    task.id = std::move(id);
    task.reminderAt = at;
    return task;
}
int main() {
    Files files;
    ReminderLedger ledger(files, "reminders.json");
    lume::todo::RepositorySnapshot snapshot;
    snapshot.tasks = {Task("b", 100), Task("a", 100), Task("old", 99), Task("future", 200)};
    Expect(ledger.Evaluate(snapshot, 100).due.empty(), "unloaded ledger suppresses delivery");
    Expect(ledger.Load() && ledger.Ready(), "fresh ledger ready");
    auto plan = ledger.Evaluate(snapshot, 100);
    Expect(plan.due.size() == 3 && plan.due[0].id == "old" && plan.due[1].id == "a" &&
               plan.due[2].id == "b" && plan.nextAt == 200, "at-time delivery ordered by time and id");
    snapshot.tasks = {Task("expired", -1), Task("boundary", 0), Task("recent", 1)};
    plan = ledger.Evaluate(snapshot, 86400000);
    Expect(plan.due.size() == 2 && plan.due[0].id == "boundary" && plan.due[1].id == "recent" &&
               plan.expired.size() == 1 && plan.expired[0].taskId == "expired",
           "exactly 24 hours is delivered; only older reminders expire");
    snapshot.tasks = {Task("completed", 0), Task("deleted", 0), Task("none", 0)};
    snapshot.tasks[0].completed = true;
    snapshot.tasks[1].deletedAt = 0;
    snapshot.tasks[2].reminderAt.reset();
    plan = ledger.Evaluate(snapshot, 100);
    Expect(plan.due.empty() && plan.expired.empty() && !plan.nextAt, "completed deleted and missing ignored");
    Expect(ledger.MarkHandled({{"a", 100}, {"a", 100}}), "mark handled duplicate keys");
    ReminderLedger reopened(files, "reminders.json");
    Expect(reopened.Load() && reopened.Handled({"a", 100}), "restart remembers handled key");
    snapshot.tasks = {Task("a", 100), Task("a", 101)};
    plan = reopened.Evaluate(snapshot, 101);
    Expect(plan.due.size() == 1 && plan.due[0].reminderAt == 101, "changed reminder time is a new key");
    const auto original = files.data.at("reminders.json");
    const std::vector<Key> changedKeys{{"a", 101}};
    const std::vector<Key> invalidLedgerKeys{{"x", 0}};
    files.fail = "write:reminders.json.tmp";
    Expect(!reopened.MarkHandled(changedKeys) && !reopened.Handled({"a", 101}) &&
               files.data.at("reminders.json") == original, "write failure preserves disk and memory");
    files.fail = "rename:reminders.json.tmp:reminders.json";
    Expect(!reopened.MarkHandled(changedKeys) && !reopened.Handled({"a", 101}) &&
               files.data.at("reminders.json") == original, "replace failure preserves disk and memory");
    files.fail.clear();
    files.data["reminders.json"] = "broken";
    ReminderLedger recovered(files, "reminders.json");
    Expect(recovered.Load() && recovered.Handled({"a", 100}) &&
               recovered.Error() == ErrorCode::BackupRecovered, "valid backup recovers handled keys");
    files.data["reminders.json"] = R"({"version":2,"handled":[]})";
    ReminderLedger future(files, "reminders.json");
    Expect(!future.Load() && !future.Ready() && !future.MarkHandled(invalidLedgerKeys) &&
               future.Evaluate(snapshot, 101).due.empty(), "unknown version refuses delivery and writes");
    files.data["reminders.json"] = "broken";
    files.data["reminders.json.bak"] = "broken too";
    ReminderLedger broken(files, "reminders.json");
    Expect(!broken.Load() && !broken.MarkHandled(invalidLedgerKeys), "fully corrupt ledger refuses writes");
    Files degraded;
    ReminderLedger degradedLedger(degraded, "r");
    Expect(degradedLedger.Load(), "fresh backup failure ledger");
    degraded.fail = "write:r.bak.tmp";
    Expect(degradedLedger.MarkHandled(changedKeys) && degradedLedger.Handled({"a", 101}) &&
               degradedLedger.Error() == ErrorCode::BackupWriteFailed,
           "backup write failure retains committed primary and memory");
    degraded.fail.clear();
    ReminderLedger degradedRestart(degraded, "r");
    Expect(degradedRestart.Load() && degradedRestart.Handled({"a", 101}),
           "backup failure restart still deduplicates");
    degraded.fail = "rename:r.bak.tmp:r.bak";
    Expect(degradedRestart.MarkHandled(invalidLedgerKeys) && degradedRestart.Handled({"x", 0}) &&
               degradedRestart.Error() == ErrorCode::BackupReplaceFailed,
           "backup replace failure reports committed state");
    Files malformed;
    malformed.data["r"] = R"({"version":1,"handled":[{"taskId":"x","at":18446744073709551615}]})";
    ReminderLedger invalidNumber(malformed, "r");
    Expect(!invalidNumber.Load(), "out of range timestamps rejected instead of wrapping");
    Files extremes;
    ReminderLedger extremeLedger(extremes, "r");
    Expect(extremeLedger.Load(), "load for extreme times");
    snapshot.tasks = {Task("ancient", std::numeric_limits<Millis>::min())};
    Expect(extremeLedger.Evaluate(snapshot, std::numeric_limits<Millis>::max()).expired.size() == 1,
           "signed timestamp difference cannot overflow");
    std::cout << "reminder_ledger_test passed\n";
}
