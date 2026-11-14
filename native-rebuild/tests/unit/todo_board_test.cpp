#include "apps/shell/todo_board.h"
#include "platform/windows/text_util.h"
#include "unique_test_directory.h"
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace lume;
using Json = nlohmann::json;
namespace {
void Check(bool v, const char* m) { if (!v) throw std::runtime_error(m); }
Json Parse(const std::optional<std::wstring>& reply) {
    Check(reply.has_value(), "board command is recognised");
    return Json::parse(text::Utf8(*reply));
}
std::wstring Command(const Json& value) { return text::Wide(value.dump()); }
const Json* Find(const Json& state, const std::string& title) {
    for (const auto& task : state["tasks"]) if (task["title"] == title) return &task;
    return nullptr;
}
}  // namespace
int main() {
    try {
        const auto root = UniqueTestDirectory(std::filesystem::u8path(TODO_TEST_ARTIFACT_ROOT));
        shell::TodoSession session((root / "calendar.json").u8string());
        std::string opened;
        shell::TodoBoard board(&session, [&](const std::string& id, calendar::Date) { opened = id; });
        Check(!board.Handle(L"{\"version\":1,\"scope\":\"music\",\"action\":\"snapshot\"}"), "other scopes are ignored");
        auto state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "snapshot"}})));
        Check(state["type"] == "todo-board-state" && state["writable"] && state["tasks"].empty() && state["today"].get<std::string>().size() == 10,
            "empty writable snapshot");
        const auto today = state["today"].get<std::string>();
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "add"}, {"title", u8"写周报"},
            {"date", today}, {"time", "16:30"}, {"color", "orange"}})));
        Check(state["result"] == "applied", "add applies");
        const auto* task = Find(state, u8"写周报");
        Check(task && (*task)["date"] == today && (*task)["time"] == "16:30" && (*task)["end"] == "17:30" && (*task)["color"] == "orange",
            "added task keeps date, time, one hour slot and colour");
        const auto id = (*task)["id"].get<std::string>();
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "update"}, {"id", id}, {"date", "2030-01-15"}})));
        task = Find(state, u8"写周报");
        Check(state["result"] == "applied" && task && (*task)["date"] == "2030-01-15" && (*task)["time"] == "16:30" && (*task)["end"] == "17:30",
            "moving a day keeps its time and duration");
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "complete"}, {"id", id}})));
        Check(Find(state, u8"写周报") && (*Find(state, u8"写周报"))["completed"] == true, "complete applies");
        Check(session.Read().tasks.front().completed, "the shared store sees the completion");
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "restore"}, {"id", id}})));
        Check((*Find(state, u8"写周报"))["completed"] == false, "restore applies");
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "update"}, {"id", id}, {"time", "25:00"}})));
        Check(state["result"] == "error" && state["error"] == "invalid", "invalid time is rejected");
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "add"}, {"title", "  "}, {"date", today}})));
        Check(state["result"] == "error", "blank titles are rejected");
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "update"}, {"id", id},
            {"date", "2030-01-15"}, {"allDay", true}, {"days", 3}, {"reminder", 1440}, {"frequency", "weekly"}})));
        task = Find(state, u8"写周报");
        Check(state["result"] == "applied" && task && (*task)["allDay"] == true && (*task)["days"] == 3 && (*task)["time"] == "" &&
            (*task)["endDate"] == "2030-01-17" && (*task)["reminder"] == 1440 && (*task)["frequency"] == "weekly" && (*task)["repeat"] == true,
            "all-day span, reminder and repeat round trip");
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "update"}, {"id", id},
            {"date", "2030-01-15"}, {"allDay", false}, {"days", 1}, {"time", "23:00"}, {"end", "01:00"}, {"reminder", -1}, {"frequency", "none"}})));
        task = Find(state, u8"写周报");
        Check(state["result"] == "applied" && (*task)["allDay"] == false && (*task)["time"] == "23:00" && (*task)["end"] == "01:00" &&
            (*task)["endDate"] == "2030-01-16" && (*task)["reminder"] == -1 && (*task)["frequency"] == "none",
            "a timed slot past midnight rolls to the next day; reminder and repeat clear");
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "update"}, {"id", id},
            {"date", "2030-01-15"}, {"allDay", true}, {"days", 0}})));
        Check(state["result"] == "error" && state["error"] == "invalid-time", "zero-day spans are rejected");
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "update"}, {"id", id},
            {"subtasks", Json::array({{{"id", "s1"}, {"title", u8"先做饭"}, {"completed", true}}, {{"id", "s2"}, {"title", u8" 再洗碗 "}, {"completed", false}}})}})));
        task = Find(state, u8"写周报");
        Check(state["result"] == "applied" && (*task)["subtasks"].size() == 2 && (*task)["subtasks"][0]["completed"] == true &&
            (*task)["subtasks"][1]["title"] == u8"再洗碗", "subtasks save, trim and keep their order");
        {
            shell::TodoSession reload((root / "calendar.json").u8string());
            Check(reload.Read().tasks.front().subtasks.size() == 2, "subtasks persist in the shared store");
        }
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "update"}, {"id", id},
            {"subtasks", Json::array({{{"id", "s1"}, {"title", "a"}, {"completed", false}}, {{"id", "s1"}, {"title", "b"}, {"completed", false}}})}})));
        Check(state["result"] == "error", "duplicate subtask ids are rejected");
        board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "open"}, {"id", id}}));
        Check(opened == id, "open hands the task to the native editor");
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "delete"}, {"id", id}})));
        Check(state["result"] == "applied" && state["tasks"].empty(), "delete applies");
        state = Parse(board.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "complete"}, {"id", id}})));
        Check(state["result"] == "error" && state["error"] == "not-found", "deleted tasks report not-found");
        shell::TodoBoard missing(nullptr, {});
        state = Parse(missing.Handle(Command({{"version", 1}, {"scope", "todo-board"}, {"action", "snapshot"}})));
        Check(state["available"] == false && state["writable"] == false, "no store is reported as unavailable");
        std::cout << "TODO_BOARD_PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
