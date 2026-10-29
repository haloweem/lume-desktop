#include "todo_test_support.h"
#include "core/todo/todo_json_codec.h"
#include "todo_repository_cases.h"
using namespace lume::todo;
using namespace todo_test;
int main() {
    Files files;
    Utc utc;
    int id = 0;
    TodoRepository repo(files, utc, "data", [&] { return "id-" + std::to_string(++id); });
    Expect(!TodoRepository::ValidAccountKey("../local") &&
               !TodoRepository::ValidAccountKey(std::string(64, 'A')) &&
               TodoRepository::ValidAccountKey(std::string(64, 'a')),
           "strict account keys");
    auto load = repo.SwitchAccount("local");
    Expect(load.source == LoadSource::Fresh && load.snapshot.writable, "fresh writable repository");
    auto add = Add(load.snapshot.scope);
    auto saved = repo.Dispatch(add, At(2026, 9, 25));
    Expect(saved.ok && saved.committed && saved.changed && saved.snapshot.tasks.size() == 1 &&
               saved.snapshot.outboxCount == 1,
           "add commits task and outbox");
    Expect(saved.snapshot.tasks[0].title == "task" && saved.snapshot.tasks[0].revision == 0 &&
               saved.snapshot.revision == 0,
           "title trimmed and sync revision unchanged");
    auto original = files.data.at("data/local/todo-data.json");
    Expect(files.data.at("data/local/todo-data.json.bak") == original, "backup is latest committed snapshot");
    Command update{saved.snapshot.scope, CommandType::Update, saved.snapshot.tasks[0].id, {}};
    update.patch.notes = "changed";
    files.fail = "write:data/local/todo-data.json.tmp";
    auto failed = repo.Dispatch(update, 0);
    Expect(!failed.ok && failed.error == Error::WriteFailed && repo.Read().tasks[0].notes.empty() &&
               files.data.at("data/local/todo-data.json") == original,
           "temporary write failure leaves primary and memory");
    files.fail = "rename:data/local/todo-data.json.tmp:data/local/todo-data.json";
    failed = repo.Dispatch(update, 0);
    Expect(!failed.ok && failed.error == Error::ReplaceFailed && repo.Read().outboxCount == 1 &&
               files.data.at("data/local/todo-data.json") == original,
           "primary replace failure rolls back task outbox and timestamp");
    files.fail = "write:data/local/todo-data.json.bak.tmp";
    auto degraded = repo.Dispatch(update, 0);
    Expect(degraded.ok && degraded.committed && degraded.warning == Warning::BackupWriteFailed &&
               degraded.snapshot.tasks[0].updatedAt == saved.snapshot.tasks[0].updatedAt + 1,
           "backup write failure reports committed and monotonic timestamp");
    files.fail.clear();
    TodoRepository reopened(files, utc, "data", [&] { return "next-" + std::to_string(++id); });
    auto reload = reopened.SwitchAccount("local");
    Expect(reload.snapshot.tasks[0].notes == "changed" && reload.snapshot.outboxCount == 2,
           "restart reads committed degraded primary");
    auto stale = update.scope;
    repo.SwitchAccount(std::string(64, 'a'));
    repo.SwitchAccount("local");
    update.scope = stale;
    Expect(repo.Dispatch(update, 0).error == Error::StaleScope, "ABA account generation rejects old form");
    Command complete{repo.Read().scope, CommandType::Complete, repo.Read().tasks[0].id, {}};
    Expect(repo.Dispatch(complete, 0).ok, "complete existing task");
    auto n = repo.Read().outboxCount;
    auto noop = repo.Dispatch(complete, 0);
    Expect(noop.ok && !noop.changed && !noop.committed && noop.snapshot.outboxCount == n,
           "repeat completion no-op");
    complete.type = CommandType::Restore;
    Expect(repo.Dispatch(complete, 0).ok && !repo.Read().tasks[0].completed, "restore completed task");
    complete.type = CommandType::Delete;
    Expect(repo.Dispatch(complete, 0).ok && repo.Read().tasks[0].deletedAt.has_value(),
           "delete keeps tombstone");
    Files corrupt;
    corrupt.data["data/local/todo-data.json"] = "broken";
    corrupt.data["data/local/todo-data.json.corrupt"] = "earlier";
    corrupt.data["data/local/todo-data.json.bak"] = original;
    TodoRepository recovery(corrupt, utc, "data", [] { return "recovered"; });
    auto recovered = recovery.SwitchAccount("local");
    Expect(recovered.source == LoadSource::Backup && recovered.snapshot.writable &&
               recovered.error == Error::InvalidJson,
           "corrupt primary backup recovery error retained");
    Expect(corrupt.data.at("data/local/todo-data.json.corrupt") == "earlier" &&
               corrupt.data.at("data/local/todo-data.json.corrupt.1") == "broken",
           "isolation never overwrites prior evidence");
    Files unknown;
    unknown.data["data/local/todo-data.json"] = "{\"version\":2,\"tasks\":[]}";
    unknown.data["data/local/todo-data.json.bak"] = original;
    TodoRepository future(unknown, utc, "data", [] { return "x"; });
    auto f = future.SwitchAccount("local");
    Expect(!f.snapshot.writable && f.error == Error::UnsupportedVersion &&
               unknown.data.count("data/local/todo-data.json"),
           "future version preserved and read only even with backup");
    Files onlyBad;
    onlyBad.data["data/local/todo-data.json"] = "broken";
    TodoRepository broken(onlyBad, utc, "data", [] { return "x"; });
    Expect(!broken.SwitchAccount("local").snapshot.writable, "single corrupt primary read only");
    TodoRepository brokenRestart(onlyBad, utc, "data", [] { return "x"; });
    Expect(!brokenRestart.SwitchAccount("local").snapshot.writable,
           "isolated corrupt primary does not become fresh on restart");
    Files collision;
    collision.data["data/local/todo-data.json"] =
        R"({"version":1,"tasks":[],"outbox":[{"id":"a\"b","type":"upsert","task":{}}]})";
    TodoRepository collisionRepo(collision, utc, "data", [] { return "a\"b"; });
    auto collisionLoad = collisionRepo.SwitchAccount("local");
    Expect(collisionRepo.Dispatch(Add(collisionLoad.snapshot.scope), 0).error == Error::IdFailure,
           "escaped existing operation IDs cannot collide");
    RecurrenceCases();
    CalendarRepeatCases();
    IoFailureCases();
    SerializationCase();
    LargeSnapshotCase();
    std::cout << "todo repository checks passed\n";
}
