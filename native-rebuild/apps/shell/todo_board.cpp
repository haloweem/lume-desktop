#include "apps/shell/todo_board.h"
#include "core/todo/todo_sheet.h"
#include "platform/windows/text_util.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>

namespace lume::shell {
namespace {
using Json = nlohmann::json;
using platform::windows::WindowsLocalTime;
constexpr std::size_t MaxTasks = 1000;
}  // namespace
std::wstring TodoBoard::State(std::string_view result, std::string_view error) const {
    Json message{{"version", 1}, {"type", "todo-board-state"}, {"result", std::string(result)}, {"error", std::string(error)}};
    const auto now = WindowsLocalTime::NowMillis();
    if (!todos_) {
        message["writable"] = false;message["available"] = false;message["today"] = "";message["tasks"] = Json::array();
    } else {
        const auto snapshot = todos_->Read();
        const auto& time = todos_->Time();
        const auto today = time.ToLocal(now);
        message["available"] = true;
        message["writable"] = snapshot.writable;
        message["today"] = today ? todo::SheetDate(today->date) : "";
        auto tasks = Json::array();
        std::vector<const todo::TaskView*> rows;
        for (const auto& task : snapshot.tasks)
            if (!task.deletedAt && !task.projected) rows.push_back(&task);
        // Open tasks first, then the most recently changed completed ones.
        std::stable_sort(rows.begin(), rows.end(), [](const auto* a, const auto* b) {
            if (a->completed != b->completed) return !a->completed;
            return a->completed && a->updatedAt > b->updatedAt;
        });
        if (rows.size() > MaxTasks) rows.resize(MaxTasks);
        for (const auto* task : rows) {
            tasks.push_back(todo::SheetRow(*task, time));
        }
        message["tasks"] = std::move(tasks);
    }
    const auto text = message.dump(-1, ' ', false, Json::error_handler_t::replace);
    return text::Wide(text);
}

std::optional<std::wstring> TodoBoard::Handle(std::wstring_view raw) {
    if (raw.find(L"todo-board") == std::wstring_view::npos) return std::nullopt;
    const auto message = Json::parse(text::Utf8(raw), nullptr, false);
    if (!message.is_object() || message.value("version", 0) != 1 || message.value("scope", std::string{}) != "todo-board")
        return std::nullopt;
    const auto action = message.value("action", std::string{});
    if (action == "snapshot") return State();
    if (!todos_) return State("error", "read-only");
    const auto snapshot = todos_->Read();
    if (!snapshot.writable) return State("error", "read-only");
    const auto& time = todos_->Time();
    const auto now = WindowsLocalTime::NowMillis();
    const auto invalid = [&] { return State("error", "invalid"); };
    todo::Command command;command.scope = snapshot.scope;
    const todo::TaskView* existing = nullptr;
    if (action != "add") {
        const auto id = message.find("id");
        if (id == message.end() || !id->is_string()) return invalid();
        const auto found = std::find_if(snapshot.tasks.begin(), snapshot.tasks.end(),
            [&](const auto& task) { return task.id == id->get<std::string>() && !task.deletedAt; });
        if (found == snapshot.tasks.end()) return State("error", "not-found");
        existing = &*found;command.id = found->id;
    }
    if (action == "open") {
        if (!open_) return invalid();
        const auto start = existing->schedule ? time.ToLocal(existing->schedule->startAt) : std::nullopt;
        open_(existing->id, start ? start->date : existing->day.value_or(calendar::Date{}));
        return State();
    }
    if (action == "complete" || action == "restore" || action == "delete") {
        command.type = action == "complete" ? todo::CommandType::Complete :
            action == "restore" ? todo::CommandType::Restore : todo::CommandType::Delete;
    } else if (action == "add" || action == "update") {
        command.type = action == "add" ? todo::CommandType::Add : todo::CommandType::Update;
        if (const auto error = todo::ReadSheetPatch(message, existing, action == "add", time, now, command.patch); !error.empty())
            return State("error", error);
    } else return invalid();
    const auto result = todos_->Dispatch(command);
    if (!result.ok) return State("error", todo::SheetErrorName(result.error));
    return State("applied");
}
}  // namespace lume::shell
