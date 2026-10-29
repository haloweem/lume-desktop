#pragma once
#include "core/todo/todo_types.h"
#include "core/calendar/calendar_settings.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace lume::platform::windows {
class CalendarTodoPicker final {
 public:
    using DateChanged=std::function<bool(todo::LocalDateTime)>;
    using ReminderChanged=std::function<void(todo::ReminderDraft)>;
    using CurrentTime=std::function<std::optional<todo::LocalDateTime>()>;
    using ChoiceChanged=std::function<void(int)>;
    CalendarTodoPicker();
    ~CalendarTodoPicker();
    CalendarTodoPicker(const CalendarTodoPicker &)=delete;
    CalendarTodoPicker &operator=(const CalendarTodoPicker &)=delete;
    bool OpenDate(HINSTANCE,HWND owner,HWND trigger,RECT host,unsigned dpi,const calendar::Preferences &,
                  todo::LocalDateTime value,todo::LocalDateTime now,DateChanged,CurrentTime = {});
    bool OpenReminder(HINSTANCE,HWND owner,HWND trigger,RECT host,unsigned dpi,const calendar::Preferences &,
                      todo::ReminderDraft,ReminderChanged);
    // A single-choice list; selecting an option closes the popup. chosen may be -1.
    bool OpenChoice(HINSTANCE,HWND owner,HWND trigger,RECT host,unsigned dpi,const calendar::Preferences &,
                    std::vector<std::wstring> options,int chosen,ChoiceChanged);
    void Close(bool restoreFocus=false);
    HWND Handle() const noexcept;
    bool IsOpen() const noexcept;
 private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
