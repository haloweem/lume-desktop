#pragma once
#include "core/calendar/calendar_controller.h"
#include "platform/windows/calendar/calendar_month_picker.h"
#include "platform/windows/calendar/calendar_snap.h"
#include "platform/windows/calendar/calendar_todo_editor.h"
#include "platform/windows/calendar/calendar_day_tooltip.h"
#include "platform/windows/material/widget_material_runtime.h"
#include <windows.h>
#include <functional>
#include <memory>
namespace lume::platform::windows {
class NativeCalendarWindow final {
public:
    NativeCalendarWindow(HINSTANCE,std::function<void(calendar::Action)>,std::function<void()> onOpenSettings,std::function<calendar::Date()> todayProvider={});
    ~NativeCalendarWindow();
    NativeCalendarWindow(const NativeCalendarWindow&)=delete;
    NativeCalendarWindow& operator=(const NativeCalendarWindow&)=delete;
    bool Configure(const calendar::Preferences&,bool preservePosition=false,bool explicitBounds=false);
    bool Start(bool preservePosition=false); void Stop();
    HWND Handle() const { return window_; }
    bool IsRunning() const { return window_!=nullptr; }
    WidgetMaterialState MaterialState() const { return material_.State(); }
    LumeMaterialStatus MaterialStatus() const { return material_.Status(); }
    void SetMaterialChanged(std::function<void()> callback) { onMaterialChanged_=std::move(callback); }
    const calendar::CalendarModel& Model() const { return model_; }
    const CalendarLayout& Layout() const { return layout_; }
    CalendarMonthPicker& Picker() { return picker_; }
    void BindTodos(CalendarTodoPort);
    void CancelTodoEditor();
    void RefreshTodos();
    bool OpenTodoById(const std::string& id);
    bool AgendaOpen() const {return view_.agendaOpen;}
    const CalendarAgendaLayout& AgendaLayout() const {return agenda_;}
    CalendarTodoEditor& TodoEditor() {return todoEditor_;}
    calendar::Profile CurrentBounds() const;
    std::optional<calendar::Profile> ConfirmedBounds() const { return interactionBounds_?interactionBounds_:confirmedBounds_; }
private:
    static LRESULT CALLBACK WindowProc(HWND,UINT,WPARAM,LPARAM);
    bool Paint(); void ArmMidnightTimer(); void RefreshToday();
    void UpdateMaterial();
    bool ApplyBounds(bool clamp,const calendar::Profile* bounds=nullptr); void CommitBounds(bool automatic=false);
    void Dispatch(calendar::Action); void ShowMenu(POINT); void TogglePicker();
    void Key(UINT); void Activate(CalendarHit);
    void NotifySnap(const CalendarSnapState&,const std::vector<HWND>&);
    void ClearSnap(bool linger=false);
    void ToggleAgenda();void OpenTodo(std::optional<std::size_t> task={});
    void OpenRequestedTodo();
    static constexpr UINT kOpenTodo=WM_APP+0x583;
    std::string pendingTodoId_;
    todo::CommandResult SubmitTodo(const todo::Command&);
    bool AgendaClick(float,float);bool AgendaKey(UINT);void AgendaScroll(float);
    void AgendaAction(int);void ArmAgendaTimer();
    void RevealAgendaFocus();
    bool RefreshTodoClock();
    HINSTANCE instance_; HWND window_=nullptr;
    std::function<void(calendar::Action)> handler_;std::function<void()> onOpenSettings_;
    std::function<calendar::Date()> todayProvider_;
    calendar::Preferences preferences_;calendar::CalendarModel model_;
    std::optional<calendar::Profile> confirmedBounds_;
    std::optional<calendar::Profile> interactionBounds_;
    std::unique_ptr<CalendarRenderer> renderer_; CalendarBitmap bitmap_;CalendarLayout layout_;
    WidgetMaterialRuntime material_;
    std::function<void()> onMaterialChanged_;
    CalendarMonthPicker picker_; CalendarRenderState view_;CalendarSnapState snap_;
    CalendarDayTooltip dayTooltip_;
    CalendarTodoPort todoPort_;CalendarTodoView todos_;CalendarAgendaLayout agenda_;
    CalendarTodoEditor todoEditor_;bool todoBusy_=false;
    unsigned dpi_=96;bool dragging_=false,sizing_=false,menuOpen_=false,applying_=false,stopping_=false;
    POINT dragCursor_{};RECT dragOrigin_{};
    RECT dpiSuggested_{};bool dpiPending_=false;
    std::vector<std::pair<HWND,unsigned>> snapTargets_;
    unsigned remoteEdges_=0;HWND remoteSource_=nullptr;
    bool suppressPickerClick_=false;
    ULONGLONG closedPickerClickUntil_=0;
    std::array<bool,2> initialSubmitted_{};
    // Only the classic and spacious themes have the toggled agenda pane.
    bool AgendaPane() const {return preferences_.theme!=calendar::Theme::Todo;}
    // The todo theme always shows its schedules; other themes only with the agenda open.
    bool SchedulesShown() const {return view_.agendaOpen||preferences_.theme==calendar::Theme::Todo;}
    // The todo theme's 日/周 hour grid.
    bool TimeGridShown() const {return preferences_.theme==calendar::Theme::Todo&&
        (preferences_.boardView==calendar::BoardView::Day||preferences_.boardView==calendar::BoardView::Week);}
    bool initialPending_=false;
    std::shared_ptr<bool> alive_=std::make_shared<bool>(true);
};
}
