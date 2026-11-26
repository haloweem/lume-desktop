#pragma once
#include "core/calendar/calendar_settings.h"
#include "core/todo/todo_types.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace lume::platform::windows {
// The task editor used by the calendar widget, the desktop to-do list and the
// island. Native controls and DirectWrite draw the sheet without starting a
// browser. It shares command validation and persistence with the manager board.
class TodoSheetWindow final {
  public:
    enum class Mode { Task, Countdown };
    struct CountdownAppearance {
        bool singleColor = false;
        std::uint32_t textColor = 0x262626;
        std::uint32_t backgroundColor = 0x2183c0;
        unsigned textAlpha = 255;
        unsigned backgroundAlpha = 255;
        bool countUp = false;
    };
    using Save = std::function<todo::CommandResult(const todo::Command &)>;
    using Closed = std::function<void()>;
    using Now = std::function<todo::Millis()>;
    TodoSheetWindow();
    ~TodoSheetWindow();
    TodoSheetWindow(const TodoSheetWindow &) = delete;
    TodoSheetWindow &operator=(const TodoSheetWindow &) = delete;
    // seed.type Add (with a schedule giving the day) or Update (with existing).
    bool Open(HINSTANCE instance, HWND owner, RECT hostBoundsPhysical, unsigned dpi,
              const calendar::Preferences &, const todo::ILocalTimePort &, todo::Command seed,
              todo::Millis now, std::optional<todo::TaskView> existing, Save, Closed = {}, Now = {}, Mode = Mode::Task,
              std::optional<CountdownAppearance> = {});
    std::optional<CountdownAppearance> CountdownColors() const;
    void Close(bool restoreFocus = true);
    HWND Handle() const noexcept;
    bool IsOpen() const noexcept;
    // Feeds one page message ({version:1,scope:"todo-editor",action,...}) as if
    // the page had posted it; used by tests and keyboard-free automation.
    bool Receive(std::string_view message);
    // The same, for a task editor window found by its handle.
    static bool Receive(HWND window, std::string_view message);
    // Page language for every editor opened afterwards.
    static void SetEnglish(bool english);
  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace lume::platform::windows
