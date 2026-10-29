#include "core/calendar/lunar_calendar.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

using namespace lume::calendar;

namespace {
int failures = 0;

void Expect(bool value, const char* description) {
    if (!value) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void ExpectEmpty(Date date) {
    const auto result = LookupLunar(date);
    Expect(result.label.empty() && result.lunarDate.empty() &&
        result.festivals.empty() && result.solarTerm.empty() && !result.holiday,
        "invalid or unsupported date returns every field empty");
}

void TestRangeAndInvalidDates() {
    const auto first = LookupLunar({1900, 1, 1});
    Expect(first.label == u8"\u5143\u65e6\u8282" &&
        first.lunarDate == u8"\u814a\u6708\u521d\u4e00" &&
        first.festivals == std::vector<std::string_view>{u8"\u5143\u65e6\u8282"} &&
        first.solarTerm.empty() && !first.holiday, "lower endpoint matches original");
    const auto last = LookupLunar({2100, 12, 31});
    Expect(last.label == u8"\u814a\u6708" && last.lunarDate == u8"\u814a\u6708\u521d\u4e00" &&
        last.festivals.empty() && last.solarTerm.empty() && !last.holiday,
        "upper endpoint matches original");
    for (const Date date : {Date{1, 1, 1}, {1899, 12, 31}, {2101, 1, 1}, {9999, 12, 31},
        {0, 1, 1}, {2026, 0, 1}, {2026, 13, 1}, {2026, 1, 0}, {2026, 4, 31},
        {2026, 2, 29}, {1900, 2, 29}, {2100, 2, 29},
        {std::numeric_limits<int>::min(), 1, 1}, {std::numeric_limits<int>::max(), 1, 1},
        {2026, std::numeric_limits<int>::max(), 1}, {2026, 1, std::numeric_limits<int>::min()}}) {
        ExpectEmpty(date);
    }
    Expect(LookupLunar({2024, 2, 29}).lunarDate == u8"\u6b63\u6708\u4e8c\u5341" &&
        !LookupLunar({2000, 2, 29}).lunarDate.empty(), "valid Gregorian leap days supported");
}

void TestFestivalsAndSolarTerms() {
    const auto spring = LookupLunar({2026, 2, 17});
    Expect(spring.label == u8"\u6625\u8282" &&
        spring.lunarDate == u8"\u6b63\u6708\u521d\u4e00" &&
        spring.festivals == std::vector<std::string_view>{u8"\u6625\u8282"} &&
        spring.solarTerm.empty(), "Spring Festival overrides first-day month label");
    const auto qingming = LookupLunar({2026, 4, 5});
    Expect(qingming.label == u8"\u6e05\u660e" && qingming.solarTerm == qingming.label &&
        qingming.festivals.empty() && qingming.lunarDate == u8"\u4e8c\u6708\u5341\u516b",
        "solar term overrides lunar day label");
    const auto overlap = LookupLunar({2026, 3, 20});
    Expect(overlap.label == u8"\u9f99\u5934\u8282" &&
        overlap.solarTerm == u8"\u6625\u5206" &&
        overlap.festivals == std::vector<std::string_view>{u8"\u9f99\u5934\u8282"},
        "festival takes priority while retaining simultaneous solar term");
    const auto multiple = LookupLunar({2020, 10, 1});
    Expect(multiple.label == u8"\u4e2d\u79cb\u8282" && multiple.festivals ==
        std::vector<std::string_view>{u8"\u4e2d\u79cb\u8282", u8"\u56fd\u5e86\u8282"},
        "multiple festivals retain lunar-first order");
}

void TestLunarMonthLabelsAndHolidaySchedules() {
    const auto first = LookupLunar({2026, 3, 19});
    Expect(first.label == u8"\u4e8c\u6708" && first.lunarDate == u8"\u4e8c\u6708\u521d\u4e00",
        "first lunar day displays its month");
    const auto leapMonth = LookupLunar({2023, 3, 22});
    Expect(leapMonth.label == u8"\u95f0\u4e8c\u6708" &&
        leapMonth.lunarDate == u8"\u95f0\u4e8c\u6708\u521d\u4e00", "leap lunar month retained");
    const auto rest = LookupLunar({2026, 2, 15});
    Expect(rest.label == u8"\u5eff\u516b" && rest.holiday &&
        rest.holiday->name == u8"\u6625\u8282" && !rest.holiday->work,
        "published holiday has explicit non-working flag");
    const auto work = LookupLunar({2026, 2, 14});
    Expect(work.holiday && work.holiday->name == u8"\u6625\u8282" && work.holiday->work,
        "published makeup working day retained independently from festival label");
    const auto unknown = LookupLunar({2099, 1, 1});
    Expect(!unknown.lunarDate.empty() && !unknown.holiday,
        "unsupported holiday schedule is absent rather than predicted");
    Expect(!first.holiday && !LookupLunar({1900, 1, 31}).holiday,
        "ordinary day and historical festival do not imply statutory holidays");
}

void WriteString(std::ostream& stream, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    stream << '"';
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') stream << '\\' << static_cast<char>(ch);
        else if (ch < 0x20) stream << "\\u00" << hex[ch >> 4] << hex[ch & 0xf];
        else stream << static_cast<char>(ch);
    }
    stream << '"';
}

// Explicit development-only mode; not run by CTest or linked into the product.
bool ExportEveryDate(const char* file) {
    std::ofstream stream(std::filesystem::u8path(file), std::ios::binary);
    if (!stream) return false;
    std::size_t count = 0;
    for (Date date{1900, 1, 1};; date = *AddDays(date, 1)) {
        const auto details = LookupLunar(date);
        stream << "{\"date\":\"" << date.year << '-' << std::setw(2) << std::setfill('0')
            << date.month << '-' << std::setw(2) << date.day << "\",\"label\":";
        WriteString(stream, details.label);
        stream << ",\"lunarDate\":";
        WriteString(stream, details.lunarDate);
        stream << ",\"festivals\":[";
        for (std::size_t i = 0; i < details.festivals.size(); ++i) {
            if (i != 0) stream << ',';
            WriteString(stream, details.festivals[i]);
        }
        stream << "],\"solarTerm\":";
        WriteString(stream, details.solarTerm);
        stream << ",\"holiday\":";
        if (details.holiday) {
            stream << "{\"name\":";
            WriteString(stream, details.holiday->name);
            stream << ",\"work\":" << (details.holiday->work ? "true" : "false") << '}';
        } else stream << "null";
        stream << "}\n";
        ++count;
        if (date == Date{2100, 12, 31}) break;
    }
    stream.close();
    if (!stream) return false;
    std::cout << "LUNAR_EXPORT_PASS: " << count << " dates\n";
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--export-json") {
        return ExportEveryDate(argv[2]) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1) {
        std::cerr << "Usage: lume-lunar-calendar-test [--export-json <file>]\n";
        return EXIT_FAILURE;
    }
    TestRangeAndInvalidDates();
    TestFestivalsAndSolarTerms();
    TestLunarMonthLabelsAndHolidaySchedules();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "LUNAR_CALENDAR_TEST_PASS\n";
    return EXIT_SUCCESS;
}
