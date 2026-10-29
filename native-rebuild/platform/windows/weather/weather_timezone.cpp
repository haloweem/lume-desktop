#include "platform/windows/weather/weather_timezone.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
namespace lume::platform::windows {
namespace {
// Stable ICU C ABI from the Windows SDK's icu.h. Load from System32 at runtime
// so older Windows versions can report unavailable instead of failing to load
// Lume. MinGW does not ship the Microsoft ICU import library/header.
using Calendar = void*;
using ErrorCode = std::int32_t;
using Open = Calendar* (__cdecl*)(const char16_t*, std::int32_t, const char*, std::int32_t, ErrorCode*);
using Close = void (__cdecl*)(Calendar*);
using SetMillis = void (__cdecl*)(Calendar*, double, ErrorCode*);
using Get = std::int32_t (__cdecl*)(const Calendar*, std::int32_t, ErrorCode*);
using Canonical = std::int32_t (__cdecl*)(const char16_t*, std::int32_t, char16_t*, std::int32_t, std::int8_t*, ErrorCode*);
constexpr std::int32_t Gregorian = 1, ZoneOffset = 15, DstOffset = 16;
template<class T> T Load(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address), "ICU function pointer ABI");
    std::memcpy(&result, &address, sizeof(result));
    return result;
}
bool ValidZone(std::string_view zone) {
    return !zone.empty() && zone.size() <= 128 && std::all_of(zone.begin(), zone.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '/' || c == '_' || c == '+' || c == '-';
    });
}
}
struct WeatherTimezone::State {
    HMODULE module = nullptr;
    Open open = nullptr;
    Close close = nullptr;
    SetMillis setMillis = nullptr;
    Get get = nullptr;
    Canonical canonical = nullptr;
    Calendar* calendar = nullptr;
    std::string zone;
    State() {
        module = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return;
        open = Load<Open>(module, "ucal_open");
        close = Load<Close>(module, "ucal_close");
        setMillis = Load<SetMillis>(module, "ucal_setMillis");
        get = Load<Get>(module, "ucal_get");
        canonical = Load<Canonical>(module, "ucal_getCanonicalTimeZoneID");
    }
    ~State() {
        if (calendar && close) close(calendar);
        if (module) FreeLibrary(module);
    }
    bool Select(std::string_view requested) {
        if (!open || !close || !setMillis || !get || !canonical || !ValidZone(requested)) return false;
        if (zone == requested && calendar) return true;
        const std::u16string input(requested.begin(), requested.end());
        std::array<char16_t, 256> output{};
        ErrorCode error = 0;
        std::int8_t systemId = 0;
        canonical(input.data(), static_cast<std::int32_t>(input.size()), output.data(),
            static_cast<std::int32_t>(output.size()), &systemId, &error);
        if (error > 0 || !systemId) return false;
        auto next = open(input.data(), static_cast<std::int32_t>(input.size()), "en_US_POSIX", Gregorian, &error);
        if (error > 0 || !next) {
            if (next) close(next);
            return false;
        }
        if (calendar) close(calendar);
        calendar = next; zone = requested;
        return true;
    }
};
WeatherTimezone::WeatherTimezone() : state_(std::make_unique<State>()) {}
WeatherTimezone::~WeatherTimezone() = default;
std::optional<std::string> WeatherTimezone::Local(weather::Millis utc, std::string_view zone) {
    if (utc < 0 || utc > 253402300799999LL || !state_->Select(zone)) return {};
    ErrorCode error = 0;
    state_->setMillis(state_->calendar, static_cast<double>(utc), &error);
    const auto base = state_->get(state_->calendar, ZoneOffset, &error);
    const auto daylight = state_->get(state_->calendar, DstOffset, &error);
    if (error > 0) return {};
    const auto offset = static_cast<std::int64_t>(base) + daylight;
    if (offset < -24*weather::Hour || offset > 24*weather::Hour || offset % 60000) return {};
    return weather::FormatOffset(utc, static_cast<int>(offset / 60000));
}
}
