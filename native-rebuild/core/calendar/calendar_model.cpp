#include "core/calendar/calendar_model.h"

#include <algorithm>
#include <cstdint>

namespace lume::calendar {
namespace {
bool SameMonth(Date left, Date right) noexcept {
    return left.year == right.year && left.month == right.month;
}
}  // namespace

CalendarModel::CalendarModel(Date today, Theme theme) noexcept
    : today_(IsValidDate(today) ? today : Date{}),
      browseAnchor_(today_), selected_(today_),
      theme_(WeekStartsOn(theme) >= 0 ? theme : Theme::Spacious) {
    Refresh();
}

Date CalendarModel::SummaryDate() const noexcept {
    return SameMonth(browseAnchor_, today_) ? today_ : browseAnchor_;
}

void CalendarModel::Refresh() noexcept {
    grid_ = MonthGrid(browseAnchor_.year, browseAnchor_.month, today_, WeekStartsOn(theme_));
    if (!SameMonth(selected_, browseAnchor_)) {
        selected_ = SameMonth(browseAnchor_, today_) ? today_ :
            Date{browseAnchor_.year, browseAnchor_.month, 1};
    }
}

bool CalendarModel::SetMonth(int year, int month) noexcept {
    if (!IsValidDate({year, month, 1})) return false;
    browseAnchor_ = {year, month, 1};
    Refresh();
    return true;
}

bool CalendarModel::StepMonth(int offset) noexcept {
    const auto month = static_cast<std::int64_t>(browseAnchor_.year - 1) * 12 +
        browseAnchor_.month - 1 + offset;
    if (month < 0 || month >= 9999 * 12) return false;
    return SetMonth(static_cast<int>(month / 12) + 1, static_cast<int>(month % 12) + 1);
}

bool CalendarModel::SelectDate(Date date) noexcept {
    for (const auto& cell : grid_) {
        if (cell.selectable && cell.date == date) {
            selected_ = date;
            return true;
        }
    }
    return false;
}

bool CalendarModel::MoveSelection(int offset) noexcept {
    if (offset != -1 && offset != 1 && offset != -7 && offset != 7) return false;
    int first = -1;
    int last = -1;
    int selected = -1;
    for (std::size_t i = 0; i < grid_.size(); ++i) {
        if (!grid_[i].selectable) continue;
        const int index = static_cast<int>(i);
        if (first == -1) first = index;
        last = index;
        if (grid_[i].date == selected_) selected = index;
    }
    if (selected == -1) return false;
    // Selectable dates are contiguous; clamping also skips endpoint year padding.
    const int next = std::clamp(selected + offset, first, last);
    if (next == selected) return false;
    selected_ = grid_[static_cast<std::size_t>(next)].date;
    return true;
}

bool CalendarModel::SetTheme(Theme theme) noexcept {
    if (WeekStartsOn(theme) < 0 || theme == theme_) return false;
    theme_ = theme;
    Refresh();
    return true;
}

bool CalendarModel::UpdateToday(Date today) noexcept {
    if (!IsValidDate(today) || today == today_) return false;
    if (SameMonth(browseAnchor_, today_)) browseAnchor_ = today;
    today_ = today;
    Refresh();
    return true;
}

void CalendarModel::ReturnToToday() noexcept {
    SetMonth(today_.year, today_.month);
}
}  // namespace lume::calendar
