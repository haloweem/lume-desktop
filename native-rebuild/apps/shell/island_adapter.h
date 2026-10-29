#pragma once
#include "apps/shell/todo_session.h"
#include "platform/windows/dynamic_island/island_window.h"
#include "platform/windows/weather/weather_worker.h"
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// What the dynamic island shows from other components: the shared todo store
// and the weather component's latest reading. Callers check for shutdown.
namespace lume::shell {

class IslandTodoAdapter final {
public:
    explicit IslandTodoAdapter(std::shared_ptr<TodoSession> todos) : todos_(std::move(todos)) {}
    std::vector<platform::windows::IslandTodo> ForDay(calendar::Date day) const;
    std::optional<platform::windows::IslandTodo> Find(const std::string& id) const;
    // Days with todos are cached until the store reports a change.
    bool HasTodos(calendar::Date day);
    void Changed() { dirty_ = true; }
    void Toggle(const platform::windows::IslandTodo& row) const;
    void Remove(const platform::windows::IslandTodo& row) const;
private:
    std::shared_ptr<TodoSession> todos_;
    bool dirty_ = true;
    std::set<int> days_;
};

// A bounded projection of the weather component's current reading.
dynamic_island::Weather IslandWeather(const platform::windows::WeatherReading& reading);

}  // namespace lume::shell
