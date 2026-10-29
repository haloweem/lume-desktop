#pragma once
#include "todo_test_support.h"
#include "core/todo/todo_json_codec.h"
#include "core/todo/todo_model.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

namespace todo_test {
class BlockingFiles final : public lume::settings::IFileStore {
  public:
    Files files;
    bool block = false, entered = false, released = false;
    std::mutex mutex;
    std::condition_variable changed;
    bool Read(const std::string &p, std::string &out) const override {
        return files.Read(p, out);
    }
    bool Exists(const std::string &p) const override {
        return files.Exists(p);
    }
    bool Write(const std::string &p, const std::string &value) override {
        if (block && p == "data/local/todo-data.json.tmp") {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return released; });
        }
        return files.Write(p, value);
    }
    bool Rename(const std::string &a, const std::string &b) override {
        return files.Rename(a, b);
    }
    bool Remove(const std::string &p) override {
        return files.Remove(p);
    }
};
inline void SerializationCase() {
    using namespace lume::todo;
    BlockingFiles files;
    Utc utc;
    int id = 0;
    TodoRepository repo(files, utc, "data", [&] { return "serial-" + std::to_string(++id); });
    const auto scope = repo.SwitchAccount("local").snapshot.scope;
    files.block = true;
    auto commit = std::async(std::launch::async, [&] { return repo.Dispatch(Add(scope), 0); });
    {
        std::unique_lock<std::mutex> lock(files.mutex);
        Expect(files.changed.wait_for(lock, std::chrono::seconds(3), [&] { return files.entered; }),
               "commit entered blocked file write");
    }
    std::promise<void> switching;
    auto started = switching.get_future();
    auto change = std::async(std::launch::async, [&] {
        switching.set_value();
        return repo.SwitchAccount(std::string(64, 'b'));
    });
    started.wait();
    Expect(change.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
           "account switch waits for in-flight primary commit");
    {
        std::lock_guard<std::mutex> lock(files.mutex);
        files.released = true;
        files.changed.notify_all();
    }
    Expect(commit.get().ok, "in-flight commit completes original account");
    auto switched = change.get();
    Expect(switched.snapshot.scope.accountKey == std::string(64, 'b') && switched.snapshot.tasks.empty() &&
               files.files.data.count("data/local/todo-data.json"),
           "serialized switch sees isolated next account");
    Expect(repo.Dispatch(Add(scope), 0).error == Error::StaleScope,
           "late original command rejected after switch");
}
inline std::string Seed(const Utc &utc, const std::string &extra = {}) {
    using namespace lume::todo;
    const auto start = At(2024, 1, 31, 9), end = At(2024, 1, 31, 10);
    return "{\"version\":1,\"revision\":77,\"customRoot\":{\"a\":[1,true]},\"reminded\":{\"one\":0},"
           "\"tasks\":[{\"id\":\"one\",\"title\":\"recurring\",\"revision\":8,\"createdAt\":0,\"updatedAt\":"
           "1,\"dueAt\":" +
           std::to_string(end) + ",\"reminderAt\":" + std::to_string(start - 60001) +
           ",\"tags\":[\"lume:calendar-schedule:[" + std::to_string(start) + "," + std::to_string(end) +
           "]\",\"lume:calendar-color:red\",\"private\"],\"recurrence\":{\"frequency\":\"monthly\","
           "\"interval\":1,\"metadata\":{\"keep\":true}},\"subtasks\":[{\"id\":\"s\",\"title\":\"sub\","
           "\"completed\":true,\"unknown\":3}],\"unknownTask\":{\"a\":[1,2]}" +
           extra + "}],\"outbox\":[]}" + (utc.ToLocal(0) ? "" : "");
}
inline void CalendarRepeatCases() {
    using namespace lume::todo;
    Utc utc;
    Files files;
    int id = 0;
    TodoRepository repo(files, utc, "data", [&] { return "repeat-" + std::to_string(++id); });
    auto scope = repo.SwitchAccount("local").snapshot.scope;
    Command add{scope, CommandType::Add, "", {}};
    add.patch.title = "lunch";
    add.patch.schedule = Schedule{At(2026, 10, 11, 9), At(2026, 10, 11, 10)};
    add.patch.reminder = ReminderDraft{ReminderKind::Relative, 600000, 0, false};
    add.patch.recurrence = std::optional<Recurrence>(Recurrence{Frequency::Daily, 1});
    auto added = repo.Dispatch(add, At(2026, 10, 11, 8));
    Expect(added.ok && added.snapshot.tasks.size() == 1 && added.snapshot.tasks[0].recurrence &&
               added.snapshot.tasks[0].recurrence->frequency == Frequency::Daily,
           "a calendar schedule is created repeating daily");
    Expect(files.data.at("data/local/todo-data.json").find("\"recurrence\":{\"frequency\":\"daily\",\"interval\":1}") !=
               std::string::npos,
           "the daily rule is stored in the shared todo format");
    const auto head = added.snapshot.tasks[0];
    const auto projected = ProjectOccurrences(head, At(2026, 10, 18, 9), 100, utc);
    Expect(projected.size() == 7 && projected[0].projected && projected[0].id == head.id &&
               projected[0].schedule->startAt == At(2026, 10, 12, 9) && projected[0].day &&
               projected[0].day->day == 12 && projected[6].schedule->startAt == At(2026, 10, 18, 9) &&
               projected[0].reminderAt == At(2026, 10, 12, 9) - 600000,
           "later days list the schedule and its reminder until the horizon");
    Expect(ProjectOccurrences(head, At(2026, 10, 18, 9), 3, utc).size() == 3, "projection is bounded");
    std::vector<TaskView> reminders{head};
    AppendReminderOccurrences(reminders, At(2026, 10, 13, 9, 30), utc);
    Expect(reminders.size() == 3 && reminders[1].reminderAt == At(2026, 10, 13, 9) - 600000 &&
               reminders[2].reminderAt == At(2026, 10, 14, 9) - 600000,
           "each day's reminder is due on its own, followed by the next upcoming one");
    Command complete{scope, CommandType::Complete, head.id, {}};
    auto done = repo.Dispatch(complete, At(2026, 10, 13, 9, 30));
    Expect(done.ok && done.snapshot.tasks.size() == 2 && done.snapshot.tasks[1].schedule->startAt == At(2026, 10, 13, 9),
           "completing a past day continues from the first occurrence that has not ended");
    Command stop{scope, CommandType::Update, done.snapshot.tasks[1].id, {}};
    stop.patch.recurrence = std::optional<Recurrence>{};
    auto stopped = repo.Dispatch(stop, At(2026, 10, 13, 9, 31));
    Expect(stopped.ok && !stopped.snapshot.tasks[1].recurrence &&
               ProjectOccurrences(stopped.snapshot.tasks[1], At(2027, 1, 1), 100, utc).empty(),
           "choosing no repeat stops later occurrences");
    Command weekly{scope, CommandType::Update, done.snapshot.tasks[1].id, {}};
    weekly.patch.recurrence = std::optional<Recurrence>(Recurrence{Frequency::Weekly, 1});
    weekly.patch.schedule = std::nullopt;
    auto noSchedule = Command{scope, CommandType::Add, "", {}};
    noSchedule.patch.title = "plain";
    noSchedule.patch.recurrence = weekly.patch.recurrence;
    Expect(!repo.Dispatch(noSchedule, 0).ok, "a repeat needs a schedule");
    Files kept;
    kept.data["data/local/todo-data.json"] = Seed(utc);
    TodoRepository legacy(kept, utc, "data", [&] { return "legacy-" + std::to_string(++id); });
    auto legacyScope = legacy.SwitchAccount("local").snapshot.scope;
    Command rename{legacyScope, CommandType::Update, "one", {}};
    rename.patch.title = "renamed";
    Expect(legacy.Dispatch(rename, At(2024, 1, 31, 8)).ok &&
               kept.data.at("data/local/todo-data.json").find("\"metadata\":{\"keep\":true}") != std::string::npos,
           "an unedited rule keeps its stored form and unknown fields");
}
inline void RecurrenceCases() {
    using namespace lume::todo;
    Utc utc;
    const auto midnight = [&](Millis due, Frequency frequency, Millis expected) {
        TaskRecord task;
        task.view.id = "midnight-original";
        task.view.title = "midnight deadline";
        task.view.dueAt = due;
        task.recurrence = Recurrence{frequency, 1};
        std::optional<TaskRecord> next;
        Expect(CompleteTask(task, 0, "midnight-next", next, utc) == Error::None && task.view.completed &&
                   next && next->view.dueAt == expected && !next->view.completed,
               "midnight completion applies prior plan-day minus one plus one millisecond semantics");
        Expect(NextOccurrence(due - 1, {frequency, 1}, utc) == expected - 1,
               "midnight recurrence advances preceding day 23:59:59.999 exactly");
        const auto local = utc.ToLocal(*next->view.dueAt);
        Expect(local && local->hour == 0 && local->minute == 0 && local->second == 0 &&
                   local->millisecond == 0,
               "midnight recurrence restores the exact midnight boundary");
    };
    midnight(At(2026, 9, 19), Frequency::Weekdays, At(2026, 9, 22));
    midnight(At(2026, 3, 1), Frequency::Monthly, At(2026, 3, 29));
    std::cout << "midnight weekdays September 19 to 22 and monthly March 1 to 29 checks passed\n";
    Files files;
    files.data["data/local/todo-data.json"] = Seed(utc);
    int id = 0;
    TodoRepository repo(files, utc, "data", [&] { return "generated-" + std::to_string(++id); });
    auto load = repo.SwitchAccount("local");
    Command command{load.snapshot.scope, CommandType::Complete, "one", {}};
    auto saved = repo.Dispatch(command, At(2024, 1, 31, 11));
    Expect(saved.ok && saved.snapshot.tasks.size() == 2 && saved.snapshot.outboxCount == 2,
           "recurrence completion commits two tasks and two operations together");
    Expect(saved.snapshot.revision == 77 && saved.snapshot.tasks[0].revision == 8 &&
               saved.snapshot.tasks[1].revision == 0,
           "sync revisions preserved; next revision zero");
    const auto &next = saved.snapshot.tasks[1];
    Expect(next.dueAt == At(2024, 2, 29, 10) && next.schedule->startAt == At(2024, 2, 29, 9) &&
               next.schedule->endAt == next.dueAt,
           "monthly schedule shifts by next dueAt delta");
    Expect(next.reminderAt == At(2024, 2, 29, 9) - 60001 && !next.completed &&
               next.createdAt == At(2024, 1, 31, 11),
           "next reminder milliseconds retained and creation reset");
    const auto committed = files.data.at("data/local/todo-data.json");
    TodoJsonCodec codec;
    TodoState state;
    Expect(codec.Decode(committed, state, utc) == Error::None, "decode recurrence persisted state");
    Expect(state.tasks[0].preservedJson.find("\"completed\":true") != std::string::npos &&
               state.tasks[1].preservedJson.find("\"completed\":false") != std::string::npos,
           "only next subtasks reset");
    Expect(state.tasks[1].preservedJson.find("\"unknown\":3") != std::string::npos &&
               state.tasks[1].preservedJson.find("\"metadata\":{\"keep\":true}") != std::string::npos,
           "next recurrence and subtask unknown metadata retained");
    command.type = CommandType::Restore;
    Expect(repo.Dispatch(command, 0).ok && repo.Read().tasks.size() == 2,
           "restore does not retract generated occurrence");
    const auto idBefore = id;
    command.type = CommandType::Complete;
    // A new completion after restore intentionally starts another occurrence.
    Expect(repo.Dispatch(command, 0).ok && repo.Read().tasks.size() == 3 && id > idBefore,
           "recompletion after explicit restore makes new occurrence");
    Files failed;
    failed.data["data/local/todo-data.json"] = Seed(utc);
    id = 0;
    TodoRepository atomic(failed, utc, "data", [&] { return "a-" + std::to_string(++id); });
    auto scope = atomic.SwitchAccount("local").snapshot.scope;
    failed.fail = "rename:data/local/todo-data.json.tmp:data/local/todo-data.json";
    command = {scope, CommandType::Complete, "one", {}};
    Expect(!atomic.Dispatch(command, 0).ok && atomic.Read().tasks.size() == 1 &&
               !atomic.Read().tasks[0].completed && atomic.Read().outboxCount == 0,
           "recurrence primary failure rolls back both tasks and both operations");
    Files limit;
    TodoState ending;
    Expect(codec.Decode(Seed(utc), ending, utc) == Error::None, "decode upper fixture");
    ending.tasks[0].view.schedule = Schedule{At(9999, 12, 30, 9), At(9999, 12, 30, 10)};
    ending.tasks[0].view.dueAt = At(9999, 12, 30, 10);
    ending.tasks[0].tags = {"lume:calendar-schedule:[" + std::to_string(At(9999, 12, 30, 9)) + "," +
                            std::to_string(At(9999, 12, 30, 10)) + "]"};
    std::string encoded;
    Expect(codec.Encode(ending, encoded) == Error::None, "encode upper fixture");
    limit.data["data/local/todo-data.json"] = encoded;
    TodoRepository edge(limit, utc, "data", [&] { return "edge-" + std::to_string(++id); });
    command = {edge.SwitchAccount("local").snapshot.scope, CommandType::Complete, "one", {}};
    Expect(edge.Dispatch(command, 0).error == Error::InvalidTime && !edge.Read().tasks[0].completed &&
               edge.Read().outboxCount == 0,
           "next year overflow refuses whole completion");
}
inline void IoFailureCases() {
    using namespace lume::todo;
    Utc utc;
    const auto seed = Seed(utc);
    int id = 0;
    for (const auto &failure : std::vector<std::pair<std::string, Warning>>{
             {"write:data/local/todo-data.json.bak.tmp", Warning::BackupWriteFailed},
             {"rename:data/local/todo-data.json.bak.tmp:data/local/todo-data.json.bak",
              Warning::BackupReplaceFailed}}) {
        Files files;
        files.data["data/local/todo-data.json"] = seed;
        files.data["data/local/todo-data.json.bak"] = seed;
        TodoRepository repo(files, utc, "data", [&] { return "io-" + std::to_string(++id); });
        auto scope = repo.SwitchAccount("local").snapshot.scope;
        Command edit{scope, CommandType::Update, "one", {}};
        edit.patch.notes = "new";
        files.fail = failure.first;
        auto result = repo.Dispatch(edit, 0);
        Expect(result.ok && result.committed && result.warning == failure.second &&
                   files.data.at("data/local/todo-data.json.bak") == seed,
               "post-commit backup failure preserves old backup and reports successful primary");
        files.fail.clear();
        TodoRepository restart(files, utc, "data", [] { return "restart"; });
        Expect(restart.SwitchAccount("local").snapshot.tasks[0].notes == "new",
               "restart recovers successful primary despite backup failure");
    }
    for (const auto &failure : std::vector<std::pair<std::string, Error>>{
             {"read:data/local/todo-data.json", Error::ReadFailed},
             {"rename:data/local/todo-data.json:data/local/todo-data.json.corrupt", Error::IsolationFailed},
             {"read:data/local/todo-data.json.bak", Error::ReadFailed}}) {
        Files files;
        files.data["data/local/todo-data.json"] = "bad";
        files.data["data/local/todo-data.json.bak"] = seed;
        files.fail = failure.first;
        TodoRepository repo(files, utc, "data", [] { return "io"; });
        auto loaded = repo.SwitchAccount("local");
        Expect(!loaded.snapshot.writable && loaded.error == failure.second, "load I/O failure fails closed");
        Expect(files.data.at("data/local/todo-data.json.bak") == seed, "load failure never changes backup");
    }
    Files allBad;
    allBad.data["data/local/todo-data.json"] = "bad";
    allBad.data["data/local/todo-data.json.bak"] = "also bad";
    TodoRepository bad(allBad, utc, "data", [] { return "bad"; });
    auto b = bad.SwitchAccount("local");
    Expect(!b.snapshot.writable && bad.Dispatch(Add(b.snapshot.scope), 0).error == Error::ReadOnly,
           "all corrupt snapshots refuse writes");
    Files recovered;
    recovered.data["data/local/todo-data.json"] = "bad";
    recovered.data["data/local/todo-data.json.bak"] = seed;
    TodoRepository recovery(recovered, utc, "data", [&] { return "r-" + std::to_string(++id); });
    auto load = recovery.SwitchAccount("local");
    Command edit{load.snapshot.scope, CommandType::Update, "one", {}};
    edit.patch.notes = "recovered";
    Expect(recovery.Dispatch(edit, 0).ok, "backup recovered state accepts valid commit");
    TodoRepository again(recovered, utc, "data", [] { return "again"; });
    Expect(again.SwitchAccount("local").source == LoadSource::Primary &&
               again.Read().tasks[0].notes == "recovered",
           "recovery survives second restart");
    Files side;
    side.data["data/local/todo-data.json"] = "{\"version\":1,\"tasks\":[]}";
    side.data["data/local/todo-outbox.json"] =
        "{\"version\":1,\"operations\":[{\"id\":\"legacy-op\",\"unknown\":true}]}";
    TodoRepository migrated(side, utc, "data", [&] { return "m-" + std::to_string(++id); });
    auto migration = migrated.SwitchAccount("local");
    Expect(migration.snapshot.outboxCount == 1, "inline missing imports legacy sidecar operations");
    Expect(migrated.Dispatch(Add(migration.snapshot.scope), 0).ok && migrated.Read().outboxCount == 2,
           "migration persists same-file outbox");
    side.data["data/local/todo-outbox.json"] = "corrupt";
    TodoRepository migratedAgain(side, utc, "data", [] { return "again"; });
    Expect(migratedAgain.SwitchAccount("local").snapshot.outboxCount == 2,
           "inline outbox ignores corrupted stale sidecar");
    side.data["data/local/todo-data.json"] = "{\"version\":1,\"tasks\":[],\"outbox\":[]}";
    TodoRepository empty(side, utc, "data", [] { return "empty"; });
    Expect(empty.SwitchAccount("local").snapshot.writable && empty.Read().outboxCount == 0,
           "inline empty never rehydrates sidecar");
    side.data["data/local/todo-data.json"] = "{\"version\":1,\"tasks\":[]}";
    side.fail = "read:data/local/todo-outbox.json";
    TodoRepository sideFail(side, utc, "data", [] { return "bad"; });
    Expect(sideFail.SwitchAccount("local").error == Error::ReadFailed && !sideFail.Read().writable,
           "missing inline plus unreadable sidecar fails closed");
    Files cleanup;
    cleanup.data["data/local/todo-data.json"] = seed;
    TodoRepository clean(cleanup, utc, "data", [&] { return "cleanup-" + std::to_string(++id); });
    auto c = clean.SwitchAccount("local");
    cleanup.fail = "rename:data/local/todo-data.json.tmp:data/local/todo-data.json";
    cleanup.failAlso = "remove:data/local/todo-data.json.tmp";
    Command badEdit{c.snapshot.scope, CommandType::Update, "one", {}};
    badEdit.patch.notes = "never committed";
    const auto cleanupFailure = clean.Dispatch(badEdit, 0);
    Expect(cleanupFailure.error == Error::ReplaceFailed && cleanupFailure.warning == Warning::CleanupFailed &&
               cleanup.data.at("data/local/todo-data.json") == seed,
           "cleanup remove failure reports original failed commit without changing primary");
}
inline void LargeSnapshotCase() {
    using namespace lume::todo;
    Utc utc;
    TodoJsonCodec codec;
    TodoState state;
    for (int i = 0; i < 1000; ++i) {
        TaskRecord task;
        task.view.id = "task-" + std::to_string(i);
        task.view.title = "title";
        task.view.notes = std::string(300, 'n');
        task.view.dueAt = At(2026, 9, 25, 10);
        state.tasks.push_back(std::move(task));
    }
    std::string text;
    Expect(codec.Encode(state, text) == Error::None, "encode 1000 task fixture");
    Files files;
    files.data["data/local/todo-data.json"] = text;
    int id = 0;
    TodoRepository repo(files, utc, "data", [&] { return "perf-" + std::to_string(++id); });
    auto begin = std::chrono::steady_clock::now();
    const auto load = repo.SwitchAccount("local");
    auto loaded = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i)
        Expect(repo.Read().tasks.size() == 1000, "UI snapshot uses precomputed values");
    auto read = std::chrono::steady_clock::now();
    Command edit{load.snapshot.scope, CommandType::Update, "task-0", {}};
    edit.patch.notes = "updated";
    Expect(repo.Dispatch(edit, 0).ok, "1000 task repository commits edit");
    auto end = std::chrono::steady_clock::now();
    const auto ms = [](auto first, auto last) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(last - first).count();
    };
    std::cout << "1000 tasks bytes=" << text.size() << " load_ms=" << ms(begin, loaded)
              << " read100_ms=" << ms(loaded, read) << " dispatch_ms=" << ms(read, end) << '\n';
}
} // namespace todo_test
