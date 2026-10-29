#pragma once

#include "core/calendar/calendar_date.h"
#include "core/calendar/calendar_types.h"

namespace lume::calendar {
class CalendarModel final {
public:
    explicit CalendarModel(Date today, Theme theme = Theme::Spacious) noexcept;

    Date Today() const noexcept { return today_; }
    Date Selected() const noexcept { return selected_; }
    // Initial/followed anchors retain today's day; explicit month navigation uses day 1.
    Date BrowseAnchor() const noexcept { return browseAnchor_; }
    Theme CurrentTheme() const noexcept { return theme_; }
    const std::array<DayCell, 42>& Grid() const noexcept { return grid_; }
    Date SummaryDate() const noexcept;

    // Accepted month/selection operations return true, including existing values.
    bool SetMonth(int year, int month) noexcept;
    bool StepMonth(int offset) noexcept;
    bool SelectDate(Date date) noexcept;
    // Only +/-1 and +/-7 are supported; returns true only when selection moves.
    bool MoveSelection(int offset) noexcept;
    // Return true only on a valid change. Unchanged input does not refresh.
    bool SetTheme(Theme theme) noexcept;
    bool UpdateToday(Date today) noexcept;
    void ReturnToToday() noexcept;

private:
    void Refresh() noexcept;

    Date today_;
    Date browseAnchor_;
    Date selected_;
    Theme theme_;
    std::array<DayCell, 42> grid_{};
};
}  // namespace lume::calendar
