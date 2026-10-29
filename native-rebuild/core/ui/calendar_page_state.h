#pragma once
#include "core/calendar/calendar_controller.h"
#include "core/calendar/calendar_preferences_store.h"
#include "core/ui/page_bridge.h"
#include <optional>
#include <string>

namespace lume::ui {
enum class CalendarPageResult { Snapshot, Applied, Cancelled };
std::optional<calendar::Action> CalendarActionForPage(PageCommand command);
// Only an Applied response can claim persistence. Cancellation returns the current
// snapshot and controller error without changing either one.
std::wstring CalendarPageState(const calendar::CalendarController& controller,
    calendar::LoadStatus loadStatus = calendar::LoadStatus::Loaded,
    CalendarPageResult result = CalendarPageResult::Snapshot);
}
