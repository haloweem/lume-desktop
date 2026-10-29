#pragma once
#include "core/settings/file_store.h"
#include "core/ui/page_bridge.h"
#include "core/widget/widget_preferences.h"
#include "platform/windows/calendar/calendar_todo_view.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
namespace lume::shell {
class TodayTodoSession final {
public:
    using Notify=std::function<void(const std::wstring&,bool)>;

    TodayTodoSession(HINSTANCE,std::string path,Notify,std::function<void()> settings,
                   platform::windows::CalendarTodoPort todos,
                   settings::IFileStore* files=nullptr);
    ~TodayTodoSession();
    bool Restore();
    std::optional<std::wstring> HandlePage(ui::PageCommand);
    std::wstring State()const;
    void Shutdown();
    bool Running()const;
    HWND Handle()const;
    const widget::Preferences& Snapshot()const;
    calendar::Date DisplayedDate()const;
    void Refresh();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
