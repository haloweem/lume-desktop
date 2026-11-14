#pragma once
#include "apps/shell/island_notification_preferences.h"
#include "core/dynamic_island/island_model.h"
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lume::shell {
enum class IslandReminderKind { Water, Stand, Sleep, SevereWeather, Rain, TemperatureDrop, OffWork };
struct IslandReminderNotice {
    std::string id;
    std::wstring title, body;
    IslandReminderKind kind = IslandReminderKind::Water;
    std::string style = "default";
};
struct IslandReminderTick {
    std::uint64_t nowMs = 0;       // monotonic, e.g. GetTickCount64()
    std::int64_t localDay = 0;     // consecutive civil days, never YYYYMMDD
    int minuteOfDay = 0;
    std::uint64_t idleMs = 0;
    bool locked = false, english = false;
    dynamic_island::Weather weather;
    // Age of the actual provider snapshot, not time since this view last saw it.
    std::uint64_t weatherAgeMs = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t weatherRevision = 0; // stable updatedAt/sourceUpdatedAt of that snapshot
    int secondOfMinute = 0;
};

// Pure policy: no timers, I/O, permissions, weather requests or window ownership.
// Call on the UI timer and immediately after confirmed preference changes.
class IslandReminderScheduler final {
public:
    static constexpr std::uint64_t IdleBreakMillis = 5 * 60 * 1000;
    static constexpr std::uint64_t MaxTickGapMillis = 2 * 60 * 1000;
    static constexpr std::uint64_t MaxWeatherAgeMillis = 90 * 60 * 1000;
    static constexpr std::size_t MaxNoticesPerTick = 7;
    std::vector<IslandReminderNotice> Tick(const IslandNotificationSettings&, const IslandReminderTick&);
private:
    struct Interval { bool active = false; int minutes = 0; std::uint64_t elapsed = 0; };
    struct WeatherAlert { std::wstring key; std::optional<std::uint64_t> lastAt; };
    bool initialized_ = false;
    std::uint64_t lastTick_ = 0;
    std::optional<std::int64_t> waterDay_, sleepMinute_, sleepBedDay_;
    std::optional<std::int64_t> offWorkSecond_;
    // Each configured minute has its own target-day watermark. Retiming to a
    // different slot rearms that day; toggling a consumed slot cannot replay it.
    // The validated HH:MM key bounds this map to 1440 entries per process.
    std::map<int, std::int64_t> offWorkDays_;
    bool offWorkPresent_ = false;
    unsigned waterCount_ = 0;
    Interval water_, stand_;
    std::string sleepSettings_;
    std::string offWorkSettings_;
    std::wstring weatherCity_, weatherFingerprint_;
    std::optional<int> previousTemperature_;
    std::uint64_t previousWeatherAt_ = 0;
    std::array<WeatherAlert, 3> weatherAlerts_{};
};
} // namespace lume::shell
