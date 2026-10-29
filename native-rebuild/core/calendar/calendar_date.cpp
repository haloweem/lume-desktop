#include "core/calendar/calendar_date.h"

#include <cstdint>

namespace lume::calendar {
namespace {
constexpr int kMonthLengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
constexpr int kMonthOffsets[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

constexpr bool IsLeapYear(int year) noexcept {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

// Internal precondition: year is in [1, 10000]. All intermediates fit in int.
constexpr int DaysBeforeYear(int year) noexcept {
    const int previous = year - 1;
    return 365 * previous + previous / 4 - previous / 100 + previous / 400;
}

constexpr int kLastOrdinal = DaysBeforeYear(10000) - 1;

// Internal precondition: date is valid. January 1 of year 1 has ordinal zero.
int Ordinal(Date date) noexcept {
    return DaysBeforeYear(date.year) + kMonthOffsets[date.month - 1] + date.day - 1 +
        (date.month > 2 && IsLeapYear(date.year) ? 1 : 0);
}

// The only callers supply a supported date or at most 41 days of grid padding.
// Padding is restricted to December of year 0 and January of year 10000.
Date FromOrdinal(int ordinal) noexcept {
    if (ordinal < 0) return {0, 12, 32 + ordinal};
    if (ordinal > kLastOrdinal) return {10000, 1, ordinal - kLastOrdinal};
    int year = 1;
    int end = 10000;
    while (year + 1 < end) {
        const int middle = year + (end - year) / 2;
        if (DaysBeforeYear(middle) <= ordinal) year = middle;
        else end = middle;
    }
    int remaining = ordinal - DaysBeforeYear(year);
    int month = 1;
    while (remaining >= DaysInMonth(year, month)) {
        remaining -= DaysInMonth(year, month);
        ++month;
    }
    return {year, month, remaining + 1};
}
}  // namespace

int DaysInMonth(int year, int month) noexcept {
    if (year < 1 || year > 9999 || month < 1 || month > 12) return 0;
    return kMonthLengths[month - 1] + (month == 2 && IsLeapYear(year) ? 1 : 0);
}

bool IsValidDate(Date date) noexcept {
    const int days = DaysInMonth(date.year, date.month);
    return date.day >= 1 && date.day <= days;
}

int Weekday(Date date) noexcept {
    return IsValidDate(date) ? (Ordinal(date) + 1) % 7 : -1;
}

int DayOfYear(Date date) noexcept {
    return IsValidDate(date) ? Ordinal(date) - DaysBeforeYear(date.year) + 1 : 0;
}

std::optional<Date> AddDays(Date date, int offset) noexcept {
    if (!IsValidDate(date)) return std::nullopt;
    const auto ordinal = static_cast<std::int64_t>(Ordinal(date)) + offset;
    if (ordinal < 0 || ordinal > kLastOrdinal) return std::nullopt;
    return FromOrdinal(static_cast<int>(ordinal));
}

std::array<DayCell, 42> MonthGrid(int year, int month, Date today, int weekStartsOn) noexcept {
    std::array<DayCell, 42> cells{};
    const Date first{year, month, 1};
    if (!IsValidDate(first) || weekStartsOn < 0 || weekStartsOn > 6) return cells;
    const int offset = (Weekday(first) - weekStartsOn + 7) % 7;
    const int start = Ordinal(first) - offset;
    const bool validToday = IsValidDate(today);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const Date date = FromOrdinal(start + static_cast<int>(i));
        cells[i] = {date, date.year == year && date.month == month,
            validToday && date == today, IsValidDate(date)};
    }
    return cells;
}
}  // namespace lume::calendar
