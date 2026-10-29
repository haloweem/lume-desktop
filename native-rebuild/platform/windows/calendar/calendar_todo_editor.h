#pragma once
#include "core/calendar/calendar_settings.h"
#include "core/todo/todo_types.h"
#include <windows.h>
#include <functional>
#include <memory>

namespace lume::platform::windows {
// Modeless native editor. Host bounds are physical screen pixels; all form geometry is DIP.
// The time port must outlive the editor. No repository or persistence dependency enters this UI.
class CalendarTodoEditor final {
  public:
    using Save = std::function<todo::CommandResult(const todo::Command &)>;
    using Closed = std::function<void()>;
    using Now = std::function<todo::Millis()>;
    CalendarTodoEditor();
    ~CalendarTodoEditor();
    CalendarTodoEditor(const CalendarTodoEditor &) = delete;
    CalendarTodoEditor &operator=(const CalendarTodoEditor &) = delete;
    bool Open(HINSTANCE instance, HWND owner, RECT hostBoundsPhysical, unsigned dpi,
              const calendar::Preferences &, const todo::ILocalTimePort &, todo::Command seed,
              todo::Millis now, std::optional<todo::TaskView> existing, Save, Closed = {}, Now = {});
    void Close(bool restoreFocus = true);
    HWND Handle() const noexcept;
    bool IsOpen() const noexcept;
  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace lume::platform::windows
