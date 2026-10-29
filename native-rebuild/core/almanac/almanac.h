#pragma once
#include "core/calendar/calendar_date.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace lume::almanac {
struct Day {
    std::string_view lunarDate,year;
    std::string cycle;
    std::vector<std::string_view> yi,ji;
};
// Exact original lunar-javascript 1.7.7 results, 1900-01-01..2100-12-31.
// Views refer to immutable compiled storage. Unsupported/invalid dates are absent.
std::optional<Day> Lookup(calendar::Date);
}
