#pragma once
#include "core/todo/todo_repository.h"
#include "platform/windows/todo/windows_local_time.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/calendar/calendar_todo_view.h"
namespace lume::shell {
// UI-thread local session; all disk locations derive from the caller's calendar path.
class TodoSession final {
public:
    explicit TodoSession(const std::string& calendarPath,platform::windows::WindowsLocalTime::ZoneReader reader={});
    platform::windows::CalendarTodoPort Port();
    todo::RepositorySnapshot Read()const;
    todo::CommandResult Dispatch(const todo::Command&);
    const std::string& BaseDirectory()const{return base_;}
    const todo::ILocalTimePort& Time()const{return time_;}
    void RefreshTime();
    void SetChanged(std::function<void()> changed){changed_=std::move(changed);}
private:
    std::function<void()> changed_;
    std::string base_;
    platform::windows::WindowsLocalTime time_;
    platform::windows::LocalSettingsFileStore files_;
    std::unique_ptr<todo::TodoRepository> repository_;
};
}
