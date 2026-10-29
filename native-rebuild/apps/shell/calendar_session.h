#pragma once
#include "core/calendar/calendar_preferences_store.h"
#include "core/ui/calendar_page_state.h"
#include "platform/windows/calendar_runtime.h"
#include "platform/windows/local_settings_file_store.h"
#include "apps/shell/todo_session.h"
#include <functional>
#include <optional>

namespace lume::shell {
// One calendar owns its persistence, controller, native surface and modal editor.
// Notifications only carry a page state and whether settings needs attention.
class CalendarSession final {
public:
    using Notify = std::function<void(const std::wstring&,bool)>;
    CalendarSession(HINSTANCE instance,std::string path,Notify notify,std::function<void()> openSettings,
                    std::shared_ptr<TodoSession> todos={});
    ~CalendarSession();
    CalendarSession(const CalendarSession&) = delete;
    CalendarSession& operator=(const CalendarSession&) = delete;
    bool Restore();
    std::optional<std::wstring> HandlePage(ui::PageCommand command,HWND owner);
    void HandleNative(calendar::Action action);
    std::wstring State(ui::CalendarPageResult result=ui::CalendarPageResult::Snapshot) const;
    void RefreshTodos(){if(runtime_.IsRunning())runtime_.RefreshTodos();}
    void CancelEditor();
    void Shutdown();
    bool OpenTodo(const std::string& taskId);
    bool Running() const { return controller_.Running(); }
    HWND Handle() const { return runtime_.Handle(); }
    const calendar::Preferences& Snapshot() const { return controller_.Snapshot(); }
    calendar::LoadStatus LoadStatus() const { return loaded_.status; }
private:
    bool Apply(calendar::Action action);
    std::wstring EditColor(ui::PageCommand command,HWND owner);
    std::string path_;
    Notify notify_;
    std::function<void()> openSettings_;
    platform::windows::LocalSettingsFileStore files_;
    calendar::CalendarPreferencesStore store_{files_};
    calendar::PreferencesLoadResult loaded_;
    std::shared_ptr<TodoSession> todos_;
    platform::windows::NativeCalendarRuntime runtime_;
    calendar::CalendarController controller_;
    bool stopped_=false,editing_=false;
    unsigned long long editorEpoch_=0;
};
}
