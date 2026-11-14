#pragma once
#include "apps/shell/todo_session.h"
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace lume::shell {
// Page protocol for the shared to-do board shown in the island and to-do list
// settings pages. Every page reads and writes the same TodoSession, so the
// calendar, the desktop list, the island and these pages always agree.
//
// Page -> host: {"version":1,"scope":"todo-board","action":...}
//   snapshot | add{title,date,time?,color?} | update{id,title?,date?,time?,color?,notes?}
//   complete{id} | restore{id} | delete{id} | open{id}
// Host -> page: {"version":1,"type":"todo-board-state",...}
class TodoBoard final {
public:
    using Open = std::function<void(const std::string& id, calendar::Date day)>;
    TodoBoard(TodoSession* todos, Open open) : todos_(todos), open_(std::move(open)) {}
    std::wstring State(std::string_view result = "snapshot", std::string_view error = {}) const;
    // Empty when the message is not a to-do board command.
    std::optional<std::wstring> Handle(std::wstring_view message);
private:
    TodoSession* todos_;
    Open open_;
};
}  // namespace lume::shell
