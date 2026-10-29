#pragma once
#include "platform/windows/calendar/calendar_layout.h"
namespace lume::platform::windows {
struct CalendarTodoFormLayout {
    CalendarRect panel, heading, viewport, footer, feedback;
    // Coordinates below are relative to the unscrolled form content.
    CalendarRect title, start, end, reminder, repeat, colors, notes;
    float contentHeight = 372;
    float MaximumScroll() const noexcept;
    float ScrollToReveal(float current, CalendarRect control) const noexcept;
};
CalendarTodoFormLayout CalculateTodoFormLayout(float width, float height) noexcept;
CalendarRect PlaceTodoPicker(CalendarRect host, CalendarRect anchor, float width, float height) noexcept;
int TodoPickerWeekCount(int year, int month) noexcept;
float TodoDatePickerHeight(int year, int month, bool monthView = false) noexcept;
} // namespace lume::platform::windows
