#include "todo_test_support.h"
#include "platform/windows/local_settings_file_store.h"
#include <windows.h>
#include <filesystem>
using namespace lume::todo;
using namespace todo_test;
int main() {
    const auto root = std::filesystem::u8path(TODO_TEST_ARTIFACT_ROOT) /
                      std::to_wstring(GetCurrentProcessId()) / L"\u65e5\u7a0b\U0001f600";
    std::filesystem::create_directories(root / "local");
    const auto base = root.u8string(), path = (root / "local" / "todo-data.json").u8string();
    lume::platform::windows::LocalSettingsFileStore files;
    Utc utc;
    int id = 0;
    auto ids = [&] { return "real-" + std::to_string(++id); };
    TodoRepository repo(files, utc, base, ids);
    auto scope = repo.SwitchAccount("local").snapshot.scope;
    auto add = Add(scope);
    add.patch.title = u8"中文😀";
    const auto saved = repo.Dispatch(add, At(2026, 9, 25));
    Expect(saved.ok && saved.committed, "real Unicode path atomic save");
    std::string prior;
    Expect(files.Read(path, prior), "read real primary bytes");
    TodoRepository reopened(files, utc, base, ids);
    auto load = reopened.SwitchAccount("local");
    Expect(load.snapshot.tasks[0].title == u8"中文😀" && load.snapshot.outboxCount == 1,
           "real reopen Unicode title and same-file outbox");
    Command edit{load.snapshot.scope, CommandType::Update, load.snapshot.tasks[0].id, {}};
    edit.patch.notes = "changed";
    std::filesystem::create_directory(std::filesystem::u8path(path + ".tmp"));
    Expect(!reopened.Dispatch(edit, 0).ok, "real temporary directory obstruction refuses write");
    std::string text;
    Expect(files.Read(path, text) && text == prior && reopened.Read().tasks[0].notes.empty(),
           "real write failure preserves old bytes and memory");
    std::filesystem::remove(std::filesystem::u8path(path + ".tmp"));
    const auto wide = std::filesystem::u8path(path).wstring();
    HANDLE lock = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(lock != INVALID_HANDLE_VALUE, "lock isolated test primary");
    const auto blocked = reopened.Dispatch(edit, 0);
    CloseHandle(lock);
    Expect(blocked.error == Error::ReplaceFailed && files.Read(path, text) && text == prior,
           "real locked primary refuses replace without losing prior bytes");
    std::filesystem::create_directory(std::filesystem::u8path(path + ".bak.tmp"));
    const auto degraded = reopened.Dispatch(edit, 0);
    Expect(degraded.ok && degraded.committed && degraded.warning == Warning::BackupWriteFailed,
           "real backup write failure is committed degraded success");
    std::filesystem::remove(std::filesystem::u8path(path + ".bak.tmp"));
    TodoRepository latest(files, utc, base, ids);
    Expect(latest.SwitchAccount("local").snapshot.tasks[0].notes == "changed",
           "real restart reads degraded primary");
    edit.scope = latest.Read().scope;
    edit.patch.notes = "latest";
    Expect(latest.Dispatch(edit, 0).ok, "real subsequent backup refresh");
    Expect(files.Write(path, "corrupt") && files.Write(path + ".corrupt", "first evidence"),
           "prepare isolated corruption");
    TodoRepository recovery(files, utc, base, ids);
    auto recovered = recovery.SwitchAccount("local");
    Expect(recovered.source == LoadSource::Backup && recovered.snapshot.tasks[0].notes == "latest",
           "real corruption recovers latest backup");
    Expect(files.Read(path + ".corrupt", text) && text == "first evidence" &&
               files.Read(path + ".corrupt.1", text) && text == "corrupt",
           "real isolation preserves all evidence");
    edit.scope = recovered.snapshot.scope;
    edit.patch.notes = "repaired";
    Expect(recovery.Dispatch(edit, 0).ok, "real repaired commit");
    TodoRepository finalRead(files, utc, base, ids);
    Expect(finalRead.SwitchAccount("local").source == LoadSource::Primary &&
               finalRead.Read().tasks[0].notes == "repaired",
           "real repair survives restart");
    std::cout << "todo file checks passed in " << root.u8string() << '\n';
}
