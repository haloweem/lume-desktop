#pragma once

#include <optional>
#include <string_view>

namespace lume::calendar {
enum class Theme { Spacious, Split, Todo };
// Views of the todo theme: everything, one day, one week, one month.
enum class BoardView { All, Day, Week, Month };

constexpr std::string_view BoardViewName(BoardView view) noexcept {
    switch (view) {
    case BoardView::All: return "all";
    case BoardView::Day: return "day";
    case BoardView::Week: return "week";
    case BoardView::Month: return "month";
    }
    return {};
}
constexpr std::optional<BoardView> ParseBoardView(std::string_view name) noexcept {
    if (name == "all") return BoardView::All;
    if (name == "day") return BoardView::Day;
    if (name == "week") return BoardView::Week;
    if (name == "month") return BoardView::Month;
    return std::nullopt;
}

constexpr std::string_view ThemeName(Theme theme) noexcept {
    switch (theme) {
    case Theme::Spacious: return "spacious";
    case Theme::Split: return "split";
    case Theme::Todo: return "todo";
    }
    return {};
}

constexpr std::optional<Theme> ParseTheme(std::string_view name) noexcept {
    if (name == "spacious") return Theme::Spacious;
    if (name == "split") return Theme::Split;
    if (name == "todo") return Theme::Todo;
    return std::nullopt;
}

// Original names: spacious is the classic Monday-first theme; split starts Sunday.
constexpr int WeekStartsOn(Theme theme) noexcept {
    switch (theme) {
    case Theme::Spacious: return 1;
    case Theme::Split: case Theme::Todo: return 0;
    }
    return -1;
}
}  // namespace lume::calendar
