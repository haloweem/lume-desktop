#include "core/calendar/calendar_model.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace lume::calendar;

namespace {
void Expect(bool value, const char* description) {
    if (!value) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

void TestThemes() {
    Expect(ThemeName(Theme::Spacious) == "spacious" &&
        ThemeName(Theme::Split) == "split" && ThemeName(Theme::Todo) == "todo", "stable theme names");
    Expect(ParseTheme("spacious") == Theme::Spacious &&
        ParseTheme("split") == Theme::Split && ParseTheme("todo") == Theme::Todo, "parse supported themes");
    Expect(!ParseTheme("") && !ParseTheme("Spacious") && !ParseTheme("split ") &&
        !ParseTheme("other"), "reject invalid theme names");
    Expect(WeekStartsOn(Theme::Spacious) == 1 && WeekStartsOn(Theme::Split) == 0 && WeekStartsOn(Theme::Todo) == 0,
        "classic starts Monday and split starts Sunday");
    const auto invalid = static_cast<Theme>(-1);
    Expect(ThemeName(invalid).empty() && WeekStartsOn(invalid) == -1,
        "invalid enum has no name or week start");
}

void TestDateValidationAndArithmetic() {
    static_assert(std::is_trivially_copyable_v<Date>);
    static_assert(std::is_trivially_copyable_v<DayCell>);
    static_assert(std::is_trivially_copyable_v<CalendarModel>);
    constexpr int lowest = std::numeric_limits<int>::min();
    constexpr int highest = std::numeric_limits<int>::max();
    Expect(Date{} == Date{1, 1, 1}, "safe date defaults");
    Expect(Date{2025, 12, 31} < Date{2026, 1, 1} &&
        Date{2026, 8, 31} <= Date{2026, 9, 1} &&
        Date{2026, 9, 2} > Date{2026, 9, 1} &&
        Date{2026, 9, 1} >= Date{2026, 9, 1} &&
        Date{2026, 9, 1} != Date{2026, 9, 2}, "lexicographic date comparison");
    Expect(IsValidDate({1, 1, 1}) && IsValidDate({9999, 12, 31}),
        "supported date endpoints");
    Expect(DaysInMonth(2024, 2) == 29 && DaysInMonth(2000, 2) == 29 &&
        DaysInMonth(1900, 2) == 28 && DaysInMonth(2100, 2) == 28,
        "Gregorian century and four-year leap rules");
    Expect(IsValidDate({2024, 2, 29}) && IsValidDate({2000, 2, 29}) &&
        !IsValidDate({1900, 2, 29}) && !IsValidDate({2100, 2, 29}),
        "leap-day validation");
    Expect(DaysInMonth(2026, 4) == 30 && DaysInMonth(2026, 12) == 31,
        "ordinary month lengths");
    const Date invalidDates[] = {{0, 1, 1}, {10000, 1, 1}, {-1, 1, 1},
        {lowest, 1, 1}, {highest, 1, 1}, {2026, 0, 1}, {2026, 13, 1},
        {2026, lowest, 1}, {2026, highest, 1}, {2026, 1, 0}, {2026, 1, -1},
        {2026, 4, 31}, {2026, 1, lowest}, {2026, 1, highest}};
    for (const auto date : invalidDates) {
        Expect(!IsValidDate(date) && Weekday(date) == -1 && DayOfYear(date) == 0 &&
            !AddDays(date, 0), "invalid dates rejected consistently");
    }
    for (const int year : {lowest, -1, 0, 10000, highest}) {
        Expect(DaysInMonth(year, 1) == 0, "invalid years have no month length");
    }
    for (const int month : {lowest, -1, 0, 13, highest}) {
        Expect(DaysInMonth(2026, month) == 0, "invalid months have no length");
    }
    Expect(Weekday({1, 1, 1}) == 1 && Weekday({2026, 9, 25}) == 5 &&
        Weekday({2000, 1, 1}) == 6 && Weekday({9999, 12, 31}) == 5,
        "weekday uses Sunday zero through Friday maximum endpoint");
    Expect(DayOfYear({2024, 3, 1}) == 61 && DayOfYear({2026, 3, 1}) == 60 &&
        DayOfYear({2024, 12, 31}) == 366 && DayOfYear({2100, 12, 31}) == 365,
        "one-based day of year");
    Expect(AddDays({2024, 2, 28}, 1) == Date{2024, 2, 29} &&
        AddDays({2024, 3, 1}, -1) == Date{2024, 2, 29} &&
        AddDays({1900, 2, 28}, 1) == Date{1900, 3, 1} &&
        AddDays({2026, 12, 31}, 1) == Date{2027, 1, 1} &&
        AddDays({2027, 1, 1}, -1) == Date{2026, 12, 31},
        "day arithmetic crosses leap days and years");
    Expect(AddDays({1, 1, 1}, 3652058) == Date{9999, 12, 31} &&
        AddDays({9999, 12, 31}, -3652058) == Date{1, 1, 1},
        "full supported ordinal range");
    for (const auto date : {Date{1, 1, 1}, Date{2026, 9, 25}, Date{9999, 12, 31}}) {
        Expect(AddDays(date, 0) == date && !AddDays(date, lowest) &&
            !AddDays(date, highest), "extreme day offsets never overflow");
    }
    Expect(!AddDays({1, 1, 1}, -1) && !AddDays({9999, 12, 31}, 1),
        "day arithmetic never escapes supported years");
}

void TestMonthGrids() {
    const Date today{2026, 9, 25};
    const auto monday = MonthGrid(2026, 9, today, 1);
    const auto sunday = MonthGrid(2026, 9, today, 0);
    Expect(monday.size() == 42 && monday.front().date == Date{2026, 8, 31} &&
        monday.back().date == Date{2026, 10, 11}, "September Monday grid endpoints");
    Expect(sunday.front().date == Date{2026, 8, 30} &&
        sunday.back().date == Date{2026, 10, 10}, "September Sunday grid endpoints");
    for (int weekStart = 0; weekStart < 7; ++weekStart) {
        const auto grid = MonthGrid(2026, 9, today, weekStart);
        Expect(Weekday(grid.front().date) == weekStart, "all seven legal week starts");
        int currentCount = 0;
        int todayCount = 0;
        for (std::size_t i = 0; i < grid.size(); ++i) {
            const auto& cell = grid[i];
            Expect(cell.selectable, "ordinary grids contain only selectable dates");
            Expect(cell.currentMonth == (cell.date.month == 9) &&
                cell.today == (cell.date == today), "grid flags match real dates");
            currentCount += cell.currentMonth ? 1 : 0;
            todayCount += cell.today ? 1 : 0;
            if (i != 0) {
                Expect(AddDays(grid[i - 1].date, 1) == cell.date,
                    "ordinary grid is contiguous across all 42 cells");
            }
        }
        Expect(currentCount == 30 && todayCount == 1, "month and today flags counted once");
    }
    const auto lower = MonthGrid(1, 1, {1, 1, 1}, 0);
    Expect(lower.front().date == Date{0, 12, 31} && !lower.front().selectable &&
        !lower.front().currentMonth && !lower.front().today &&
        lower[1].date == Date{1, 1, 1} && lower.back().date == Date{1, 2, 10},
        "year one keeps unselectable year-zero leading cell");
    const auto lowerMonday = MonthGrid(1, 1, {1, 1, 1}, 1);
    Expect(lowerMonday.front().date == Date{1, 1, 1} &&
        lowerMonday.back().date == Date{1, 2, 11},
        "year one Monday endpoints match original JavaScript");
    const auto upper = MonthGrid(9999, 12, {9999, 12, 31}, 1);
    Expect(upper.front().date == Date{9999, 11, 29} &&
        upper[32].date == Date{9999, 12, 31} && upper[33].date == Date{10000, 1, 1} &&
        upper.back().date == Date{10000, 1, 9}, "maximum year keeps trailing filler dates");
    const auto upperSunday = MonthGrid(9999, 12, {9999, 12, 31}, 0);
    Expect(upperSunday.front().date == Date{9999, 11, 28} &&
        upperSunday.back().date == Date{10000, 1, 8},
        "maximum year Sunday endpoints match original JavaScript");
    for (const auto& cell : upper) {
        Expect(cell.selectable == (cell.date.year == 9999),
            "year ten thousand cells are unselectable");
    }
    for (int weekStart = 0; weekStart < 7; ++weekStart) {
        for (const auto month : {Date{1, 1, 1}, Date{9999, 12, 1}}) {
            const auto grid = MonthGrid(month.year, month.month, today, weekStart);
            for (std::size_t i = 1; i < grid.size(); ++i) {
                const auto previous = grid[i - 1].date;
                const auto next = grid[i].date;
                if (previous.year == 0) {
                    Expect(next == (previous.day == 31 ? Date{1, 1, 1} :
                        Date{0, 12, previous.day + 1}), "lower filler dates stay contiguous");
                } else if (previous == Date{9999, 12, 31}) {
                    Expect(next == Date{10000, 1, 1}, "upper filler boundary stays contiguous");
                } else if (previous.year == 10000) {
                    Expect(next == Date{10000, 1, previous.day + 1},
                        "upper filler dates stay contiguous");
                } else {
                    Expect(AddDays(previous, 1) == next, "boundary grid remains contiguous");
                }
                Expect(grid[i].selectable == IsValidDate(next),
                    "boundary selectable flag follows supported date range");
            }
        }
    }
    constexpr int lowest = std::numeric_limits<int>::min();
    constexpr int highest = std::numeric_limits<int>::max();
    const int invalidInputs[][3] = {{0, 1, 0}, {10000, 1, 0}, {lowest, 1, 0},
        {highest, 1, 0}, {2026, -1, 0}, {2026, 0, 0}, {2026, 13, 0},
        {2026, lowest, 0}, {2026, highest, 0}, {2026, 9, -1}, {2026, 9, 7},
        {2026, 9, lowest}, {2026, 9, highest}};
    for (const auto& input : invalidInputs) {
        for (const auto& cell : MonthGrid(input[0], input[1], today, input[2])) {
            Expect(!cell.selectable && !cell.currentMonth && !cell.today,
                "invalid grid inputs produce no active cells");
        }
    }
    for (const auto& cell : MonthGrid(1, 1, {0, 12, 31}, 0)) {
        Expect(!cell.today, "unsupported today cannot mark a filler date");
    }
}

void TestOriginalFourHundredYearGolden() {
    // Independent original calendar.js result; see docs/reference/original-ui/calendar-contract.md.
    // Field order: year, 1-based month, day, currentMonth, today; each little-endian uint32.
    std::uint32_t hash = 2166136261u;
    std::size_t cells = 0;
    for (const int weekStart : {0, 1}) {
        for (int year = 1800; year <= 2199; ++year) {
            for (int month = 1; month <= 12; ++month) {
                for (const auto& cell : MonthGrid(year, month, {2026, 9, 25}, weekStart)) {
                    ++cells;
                    for (const int field : {cell.date.year, cell.date.month, cell.date.day,
                        static_cast<int>(cell.currentMonth), static_cast<int>(cell.today)}) {
                        const auto value = static_cast<std::uint32_t>(field);
                        for (int shift = 0; shift < 32; shift += 8) {
                            hash ^= (value >> shift) & 0xffu;
                            hash *= 16777619u;
                        }
                    }
                }
            }
        }
    }
    Expect(cells == 403200 && hash == 0x8914e816u,
        "400-year month grids match the independently recorded original JavaScript golden");
}

void TestModelBrowsingAndSelection() {
    CalendarModel model({2026, 9, 25});
    Expect(model.Today() == Date{2026, 9, 25} &&
        model.BrowseAnchor() == Date{2026, 9, 25} && model.Selected() == model.Today() &&
        model.CurrentTheme() == Theme::Spacious && model.SummaryDate() == model.Today(),
        "model keeps today, month anchor, and selection as separate states");
    Expect(model.SelectDate({2026, 9, 12}) && model.SetMonth(2026, 9) &&
        model.Selected() == Date{2026, 9, 12}, "refresh preserves a selection inside viewed month");
    Expect(model.SelectDate({2026, 8, 31}) && model.BrowseAnchor() == Date{2026, 9, 1},
        "adjacent month selection does not navigate");
    Expect(!model.SelectDate({2026, 8, 30}) && !model.SelectDate({2026, 10, 12}) &&
        !model.SelectDate({2026, 9, 31}), "selection rejects dates outside grid or calendar");
    Expect(model.SetMonth(2026, 9) && model.Selected() == model.Today(),
        "current-month refresh realigns adjacent selection to today");
    Expect(model.SetMonth(2026, 8) && model.Selected() == Date{2026, 8, 1} &&
        model.SummaryDate() == Date{2026, 8, 1}, "other-month browsing selects and summarizes first day");
    Expect(model.SelectDate({2026, 8, 18}) && model.SetMonth(2026, 8) &&
        model.Selected() == Date{2026, 8, 18}, "other-month refresh keeps in-month selection");
    model.ReturnToToday();
    Expect(model.BrowseAnchor() == Date{2026, 9, 1} && model.Selected() == model.Today(),
        "return to today navigates and realigns selection");
    Expect(model.SelectDate({2026, 9, 12}), "select inside today's month");
    model.ReturnToToday();
    Expect(model.Selected() == Date{2026, 9, 12}, "return to today preserves an in-month selection");
    Expect(model.SelectDate({2026, 10, 1}) && model.SetMonth(2026, 10) &&
        model.Selected() == Date{2026, 10, 1}, "new viewed month keeps already matching selection");
    Expect(model.SetMonth(2026, 12) && model.StepMonth(1) &&
        model.BrowseAnchor() == Date{2027, 1, 1} && model.StepMonth(-13) &&
        model.BrowseAnchor() == Date{2025, 12, 1}, "month steps cross years in both directions");
    constexpr int lowest = std::numeric_limits<int>::min();
    constexpr int highest = std::numeric_limits<int>::max();
    Expect(!model.SetMonth(lowest, 1) && !model.SetMonth(highest, 1) &&
        !model.SetMonth(0, 1) && !model.SetMonth(10000, 1) &&
        !model.SetMonth(2026, lowest) && !model.SetMonth(2026, highest) &&
        !model.StepMonth(lowest) && !model.StepMonth(highest) &&
        model.BrowseAnchor() == Date{2025, 12, 1} && model.Selected() == Date{2025, 12, 1},
        "invalid and extreme month operations leave states unchanged");
    Expect(model.SetMonth(1, 1) && !model.StepMonth(-1) && model.StepMonth(119987) &&
        model.BrowseAnchor() == Date{9999, 12, 1} && !model.StepMonth(1) &&
        model.StepMonth(-119987) && model.BrowseAnchor() == Date{1, 1, 1},
        "month navigation covers full supported range without escaping");
    CalendarModel invalid({0, 2, 99}, static_cast<Theme>(-1));
    Expect(invalid.Today() == Date{1, 1, 1} && invalid.Selected() == Date{1, 1, 1} &&
        invalid.BrowseAnchor() == Date{1, 1, 1} && invalid.CurrentTheme() == Theme::Spacious,
        "invalid initialization falls back safely");
}

void TestThemeRefreshAndTodayUpdates() {
    CalendarModel model({2026, 9, 25});
    Expect(model.SelectDate({2026, 8, 31}) && !model.SetTheme(Theme::Spacious) &&
        model.Selected() == Date{2026, 8, 31}, "same theme does not redraw or refresh selection");
    Expect(!model.SetTheme(static_cast<Theme>(99)) &&
        model.Selected() == Date{2026, 8, 31} && model.CurrentTheme() == Theme::Spacious,
        "invalid theme leaves model unchanged");
    Expect(model.SetTheme(Theme::Split) && model.Selected() == model.Today() &&
        model.Grid().front().date == Date{2026, 8, 30},
        "theme change recalculates grid and realigns selection");
    Expect(model.SelectDate({2026, 9, 10}) && model.SetTheme(Theme::Spacious) &&
        model.Selected() == Date{2026, 9, 10}, "theme change preserves a current-month selection");
    Expect(model.UpdateToday({2026, 9, 26}) && model.Today() == Date{2026, 9, 26} &&
        model.Selected() == Date{2026, 9, 10} && model.BrowseAnchor() == Date{2026, 9, 26} &&
        model.SummaryDate() == Date{2026, 9, 26}, "same-month rollover preserves selected day");
    for (const auto& cell : model.Grid()) {
        Expect(cell.today == (cell.date == Date{2026, 9, 26}),
            "today rollover replaces the previous grid marker");
    }
    Expect(model.SelectDate({2026, 8, 31}) && !model.UpdateToday({2026, 9, 26}) &&
        !model.UpdateToday({2026, 2, 30}) && model.Selected() == Date{2026, 8, 31},
        "unchanged or invalid today does not refresh selection");
    Expect(model.UpdateToday({2026, 10, 1}) && model.BrowseAnchor() == Date{2026, 10, 1} &&
        model.Selected() == model.Today(), "viewing old current month follows a new month");
    Expect(model.SetMonth(2026, 8) && model.SelectDate({2026, 8, 15}) &&
        model.UpdateToday({2027, 1, 1}) && model.BrowseAnchor() == Date{2026, 8, 1} &&
        model.Selected() == Date{2026, 8, 15} && model.SummaryDate() == Date{2026, 8, 1},
        "browsing an older month never follows today rollover");
    Expect(model.SelectDate({2026, 9, 1}) && model.SetTheme(Theme::Split) &&
        model.Selected() == Date{2026, 8, 1}, "theme refresh in old month selects first day");
    Expect(model.SelectDate({2026, 9, 1}) && model.UpdateToday({2027, 1, 2}) &&
        model.Selected() == Date{2026, 8, 1}, "today refresh in old month realigns adjacent selection");
    CalendarModel yearRollover({2026, 12, 31});
    Expect(yearRollover.UpdateToday({2027, 1, 1}) &&
        yearRollover.BrowseAnchor() == Date{2027, 1, 1} &&
        yearRollover.Selected() == Date{2027, 1, 1}, "today rollover follows across year boundary");
    CalendarModel adjacent({2026, 9, 30});
    Expect(adjacent.SelectDate({2026, 10, 2}) && adjacent.UpdateToday({2026, 10, 1}) &&
        adjacent.Selected() == Date{2026, 10, 2}, "rollover retains selection already in new month");
    CalendarModel adjustedToday({2026, 9, 25});
    Expect(adjustedToday.UpdateToday({2026, 9, 26}) &&
        adjustedToday.BrowseAnchor() == Date{2026, 9, 26} &&
        adjustedToday.UpdateToday({2026, 8, 30}) &&
        adjustedToday.BrowseAnchor() == Date{2026, 8, 30} &&
        adjustedToday.SummaryDate() == Date{2026, 8, 30},
        "today adjustment follows the complete date in either direction");
    Expect(adjustedToday.StepMonth(0) &&
        adjustedToday.BrowseAnchor() == Date{2026, 8, 1},
        "explicit month navigation normalizes even a zero step to month start");
}

void TestKeyboardGridClamping() {
    CalendarModel model({2026, 9, 25}, Theme::Split);
    Expect(model.SelectDate({2026, 9, 1}) && model.MoveSelection(-1) &&
        model.Selected() == Date{2026, 8, 31} && model.BrowseAnchor() == Date{2026, 9, 25},
        "split keyboard still reaches hidden adjacent-month dates");
    Expect(model.MoveSelection(-7) && model.Selected() == model.Grid().front().date &&
        !model.MoveSelection(-7) && !model.MoveSelection(-1), "up and left clamp at first cell");
    Expect(model.SelectDate(model.Grid()[40].date) && model.MoveSelection(7) &&
        model.Selected() == model.Grid().back().date && !model.MoveSelection(7) &&
        !model.MoveSelection(1), "down and right clamp at final cell");
    Expect(model.SelectDate(model.Grid()[14].date) && model.MoveSelection(1) &&
        model.Selected() == model.Grid()[15].date && model.MoveSelection(7) &&
        model.Selected() == model.Grid()[22].date && model.MoveSelection(-7) &&
        model.MoveSelection(-1) && model.Selected() == model.Grid()[14].date,
        "all four directions use full-grid indices");
    Expect(!model.MoveSelection(0) && !model.MoveSelection(2) && !model.MoveSelection(-2) &&
        !model.MoveSelection(std::numeric_limits<int>::min()) &&
        !model.MoveSelection(std::numeric_limits<int>::max()) &&
        model.Selected() == model.Grid()[14].date, "keyboard rejects unsupported offsets");
    CalendarModel lower({1, 1, 2}, Theme::Split);
    Expect(lower.MoveSelection(-7) && lower.Selected() == Date{1, 1, 1} &&
        !lower.MoveSelection(-1) && !lower.SelectDate({0, 12, 31}),
        "lower navigation avoids year-zero padding");
    CalendarModel upper({9999, 12, 30});
    Expect(upper.MoveSelection(7) && upper.Selected() == Date{9999, 12, 31} &&
        !upper.MoveSelection(1) && !upper.SelectDate({10000, 1, 1}),
        "upper navigation avoids year-ten-thousand padding");
}
}  // namespace

int main() {
    TestThemes();
    TestDateValidationAndArithmetic();
    TestMonthGrids();
    TestOriginalFourHundredYearGolden();
    TestModelBrowsingAndSelection();
    TestThemeRefreshAndTodayUpdates();
    TestKeyboardGridClamping();
    std::cout << "Calendar dates, years 1/9999 with unselectable 0/10000 padding, "
        "400-year original golden 0x8914e816 (403200 cells), model refresh, "
        "and four-direction clamping passed\n";
}
