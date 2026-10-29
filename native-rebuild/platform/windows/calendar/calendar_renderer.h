#pragma once
#include "platform/windows/calendar/calendar_layout.h"
#include "core/calendar/calendar_model.h"
#include "core/calendar/calendar_settings.h"
#include "platform/windows/calendar/calendar_todo_view.h"
#include <cstdint>
#include <memory>
#include <vector>
namespace lume::platform::windows {
struct CalendarBitmap { unsigned width=0,height=0; std::vector<std::uint32_t> pixels; };
struct CalendarRenderState {
    int focus=-1; bool keyboardFocus=false,pickerVisible=false; unsigned alignedEdges=0; int hover=-1;
    bool agendaOpen=false,todoAvailable=false,materialReady=false;
    int pressedNavigation=-1;
    float agendaScroll=0;
    // Day/week views of the todo theme: scrolled DIPs, or negative to start near the first event.
    float boardScroll=-1;
    // 全 view: the day's schedule and the upcoming list scroll on their own.
    float boardDayScroll=0,boardUpcomingScroll=0;
    // 日/周: Ctrl+wheel zoom of the hour height.
    float boardZoom=1;
    // 月: each day cell scrolls its own items; reset when the month changes.
    std::array<float,42> cellScroll{};
    calendar::Date cellScrollMonth{};
    int agendaFocus=-1;
    const CalendarTodoView* todos=nullptr;
    const todo::ILocalTimePort* time=nullptr;
    todo::Millis now=0;
    std::wstring todoStatus;
};
struct CalendarPickerRenderState { int year=2026,month=1,focus=3; bool editing=false; std::wstring draft; calendar::Date selected,today; std::size_t caret=0; bool selectAll=false,materialReady=false; };
// Shared Segoe UI / YaHei UI foreground. WidgetMaterialRuntime composes native
// glass underneath; this bitmap also remains usable when capture is unavailable.
// Top-down premultiplied BGRA is suitable for UpdateLayeredWindow.
class CalendarRenderer final {
public:
    CalendarRenderer();
    ~CalendarRenderer();
    bool Initialize();
    bool Render(const calendar::Preferences&,const calendar::CalendarModel&,unsigned dpi,CalendarBitmap&,const CalendarRenderState& state={},CalendarLayout* layout=nullptr,CalendarAgendaLayout* agenda=nullptr);
    bool RenderPicker(const calendar::Preferences&,const CalendarPickerRenderState&,float width,float height,unsigned dpi,CalendarBitmap&);
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace lume::platform::windows
