#pragma once
#include "platform/windows/calendar/native_calendar_window.h"
namespace lume::platform::windows {
class NativeCalendarRuntime final:public calendar::ICalendarRuntime {
public:
    NativeCalendarRuntime(HINSTANCE,std::function<void(calendar::Action)>,std::function<void()> onOpenSettings);
    ~NativeCalendarRuntime() override;
    bool Apply(const calendar::Preferences&) override;
    bool ApplyBounds(const calendar::Preferences&) override;
    bool ApplyTheme(calendar::Preferences&,bool useCurrentPosition) override;
    bool ApplyReset(calendar::Preferences&) override;
    bool ApplyRollback(const calendar::Preferences&) override;
    bool IsRunning() const override;
    void Shutdown() override;
    HWND Handle() const {return window_.Handle();}
    void BindTodos(CalendarTodoPort port){window_.BindTodos(std::move(port));}
    void RefreshTodos(){window_.RefreshTodos();}
    void CancelTodoEditor(){window_.CancelTodoEditor();}
    bool OpenTodo(const std::string& id){return window_.OpenTodoById(id);}
    WidgetMaterialState MaterialState()const{return window_.MaterialState();}
    void SetMaterialChanged(std::function<void()> changed){window_.SetMaterialChanged(std::move(changed));}
    calendar::Profile CurrentBounds() const {return window_.CurrentBounds();}
private:
    bool ApplyCandidate(const calendar::Preferences&,bool explicitBounds);
    NativeCalendarWindow window_;
    std::optional<calendar::Profile> appliedBounds_,rollbackBounds_;
};
}
