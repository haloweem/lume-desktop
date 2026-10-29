#pragma once

#include <array>
#include <optional>

namespace lume::calendar {
struct Date {
    int year = 1;
    int month = 1;
    int day = 1;
};

constexpr bool operator==(Date left, Date right) noexcept {
    return left.year == right.year && left.month == right.month && left.day == right.day;
}
constexpr bool operator!=(Date left, Date right) noexcept { return !(left == right); }
constexpr bool operator<(Date left, Date right) noexcept {
    if (left.year != right.year) return left.year < right.year;
    if (left.month != right.month) return left.month < right.month;
    return left.day < right.day;
}
constexpr bool operator>(Date left, Date right) noexcept { return right < left; }
constexpr bool operator<=(Date left, Date right) noexcept { return !(right < left); }
constexpr bool operator>=(Date left, Date right) noexcept { return !(left < right); }

struct DayCell {
    Date date;
    bool currentMonth = false;
    bool today = false;
    bool selectable = false;
};

// Public date arithmetic accepts only Gregorian dates in years 1 through 9999.
bool IsValidDate(Date date) noexcept;
int DaysInMonth(int year, int month) noexcept;  // Zero for invalid year/month.
int Weekday(Date date) noexcept;              // Sunday = 0; invalid = -1.
int DayOfYear(Date date) noexcept;            // January 1 = 1; invalid = 0.
std::optional<Date> AddDays(Date date, int offset) noexcept;

// Always 42 consecutive cells, with weekStartsOn in [0, 6]. Invalid inputs return
// an inactive grid. Year 0/10000 padding is retained but never selectable.
std::array<DayCell, 42> MonthGrid(int year, int month, Date today, int weekStartsOn) noexcept;
}  // namespace lume::calendar
