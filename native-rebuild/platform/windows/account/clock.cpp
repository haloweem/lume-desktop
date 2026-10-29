#include "core/account/backend.h"
#include <windows.h>
#include <cstdio>
namespace lume::account {
namespace {
constexpr std::uint64_t Epoch = 116444736000000000ULL;
std::uint64_t Ticks(FILETIME file) { return (static_cast<std::uint64_t>(file.dwHighDateTime) << 32) | file.dwLowDateTime; }
}
std::int64_t UnixNow() {
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    return static_cast<std::int64_t>((Ticks(time) - Epoch) / 10000000);
}
std::optional<std::int64_t> ParseIsoTime(std::string_view text) {
    if (text.size() < 20 || text.size() > 35 || text[4] != '-' || text[7] != '-' ||
        (text[10] != 'T' && text[10] != 't') || text[13] != ':' || text[16] != ':') return {};
    const auto number = [&](std::size_t offset, std::size_t count) -> int {
        int value = 0;
        if (offset + count > text.size()) return -1;
        for (std::size_t i = offset; i < offset + count; ++i) {
            if (text[i] < '0' || text[i] > '9') return -1;
            value = value * 10 + text[i] - '0';
        }
        return value;
    };
    SYSTEMTIME time{};
    const int year = number(0, 4), month = number(5, 2), day = number(8, 2),
        hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return {};
    time.wYear = static_cast<WORD>(year); time.wMonth = static_cast<WORD>(month); time.wDay = static_cast<WORD>(day);
    time.wHour = static_cast<WORD>(hour); time.wMinute = static_cast<WORD>(minute); time.wSecond = static_cast<WORD>(second);
    std::size_t pos = 19; int milliseconds = 0;
    if (text[pos] == '.') {
        ++pos; const auto start = pos;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            if (pos - start < 3) milliseconds = milliseconds * 10 + text[pos] - '0';
            ++pos;
        }
        if (pos == start || pos - start > 7) return {};
        for (auto n = pos - start; n < 3; ++n) milliseconds *= 10;
    }
    int offset = 0;
    if (pos + 1 == text.size() && (text[pos] == 'Z' || text[pos] == 'z')) {}
    else if (pos + 6 == text.size() && (text[pos] == '+' || text[pos] == '-') && text[pos + 3] == ':') {
        const int h = number(pos + 1, 2), m = number(pos + 4, 2);
        if (h < 0 || h > 23 || m < 0 || m > 59) return {};
        offset = (h * 60 + m) * (text[pos] == '+' ? 1 : -1);
    } else return {};
    FILETIME file{};
    if (!SystemTimeToFileTime(&time, &file) || Ticks(file) < Epoch) return {};
    return static_cast<std::int64_t>((Ticks(file) - Epoch) / 10000) + milliseconds - static_cast<std::int64_t>(offset) * 60000;
}
std::string IsoTime(std::int64_t milliseconds) {
    if (milliseconds < 0 || milliseconds >= 253402300800000LL) return {};
    const auto ticks = Epoch + static_cast<std::uint64_t>(milliseconds) * 10000;
    FILETIME file{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME time{}; if (!FileTimeToSystemTime(&file, &time)) return {};
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return buffer;
}
}
