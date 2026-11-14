#include "apps/shell/island_reminder_scheduler.h"
#include <algorithm>
#include <utility>

namespace lume::shell {
namespace {
constexpr std::uint64_t Minute = 60000, Hour = 60 * Minute;
int TimeOfDay(const std::string& text) {
    if (text.size() != 5 || text[2] != ':') return -1;
    for (const auto i : {0, 1, 3, 4}) if (text[i] < '0' || text[i] > '9') return -1;
    const int hour = (text[0] - '0') * 10 + text[1] - '0';
    const int minute = (text[3] - '0') * 10 + text[4] - '0';
    return hour < 24 && minute < 60 ? hour * 60 + minute : -1;
}
bool Rain(int code) {
    switch (code) {
    case 51: case 53: case 55: case 56: case 57: case 61: case 63: case 65:
    case 66: case 67: case 80: case 81: case 82: case 95: case 96: case 99: return true;
    default: return false;
    }
}
bool Severe(int code) {
    switch (code) {
    case 65: case 67: case 75: case 82: case 86: case 95: case 96: case 99: return true;
    default: return false;
    }
}
bool Temperature(int value) { return value >= -100 && value <= 70; }
bool HourLabel(const std::wstring& text) {
    if (text.empty() || text.size() > 5) return false;
    const auto colon = text.find(L':');
    const auto hours = colon == std::wstring::npos ? text.size() : colon;
    if (hours < 1 || hours > 2 || (colon != std::wstring::npos && text.size() != hours + 3)) return false;
    int hour = 0;
    for (std::size_t i = 0; i < hours; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return false;
        hour = hour * 10 + text[i] - L'0';
    }
    return hour < 24 && (colon == std::wstring::npos ||
        (text[colon + 1] >= L'0' && text[colon + 1] <= L'5' && text[colon + 2] >= L'0' && text[colon + 2] <= L'9'));
}
std::wstring Fingerprint(const IslandReminderTick& tick, const IslandNotificationSettings& p) {
    const auto& weather = tick.weather;
    auto result = std::to_wstring(tick.weatherRevision) + L":" + std::to_wstring(weather.code) +
        L":" + std::to_wstring(weather.temperature) + L":" + std::to_wstring(p.weatherSevere) +
        std::to_wstring(p.weatherRain) + std::to_wstring(p.weatherTemperatureDrop) + L":" +
        std::to_wstring(p.temperatureDropDegrees);
    for (std::size_t i = 0; i < std::min(weather.hours, weather.hourly.size()); ++i) {
        const auto& hour = weather.hourly[i];
        result += L"|" + hour.label + L":" + std::to_wstring(hour.code) + L":" + std::to_wstring(hour.temperature);
    }
    return result;
}
} // namespace

std::vector<IslandReminderNotice> IslandReminderScheduler::Tick(
    const IslandNotificationSettings& p, const IslandReminderTick& tick) {
    std::vector<IslandReminderNotice> notices;
    notices.reserve(MaxNoticesPerTick);
    const bool gap = initialized_ && (tick.nowMs < lastTick_ || tick.nowMs - lastTick_ > MaxTickGapMillis);
    const auto elapsed = initialized_ && !gap ? tick.nowMs - lastTick_ : 0;
    initialized_ = true; lastTick_ = tick.nowMs;
    const bool present = !tick.locked && tick.idleMs < IdleBreakMillis;
    const auto emit = [&](IslandReminderKind kind, const char* name, std::wstring title, std::wstring body,
                          std::string style = "default") {
        if (notices.size() < MaxNoticesPerTick)
            notices.push_back({std::string(name) + "-" + std::to_string(tick.nowMs), std::move(title), std::move(body), kind, std::move(style)});
    };
    if (!waterDay_ || *waterDay_ != tick.localDay) { waterDay_ = tick.localDay; waterCount_ = 0; }
    const auto intervalDue = [&](Interval& interval, bool enabled, int minutes) {
        minutes = std::clamp(minutes, 15, 240);
        const bool restarted = !interval.active || interval.minutes != minutes;
        interval.active = enabled; interval.minutes = minutes;
        if (!enabled || !present || gap || restarted) { interval.elapsed = 0; return false; }
        interval.elapsed += elapsed;
        if (interval.elapsed < static_cast<std::uint64_t>(minutes) * Minute) return false;
        interval.elapsed = 0; // one reminder, never replay missed intervals
        return true;
    };
    if (intervalDue(water_, p.waterEnabled, p.waterIntervalMinutes) &&
        waterCount_ < static_cast<unsigned>(std::clamp(p.waterGoal, 1, 20))) {
        ++waterCount_;
        emit(IslandReminderKind::Water, "water", tick.english ? L"Time for some water" : L"该喝点水了",
            tick.english ? L"Take a moment to stay hydrated." : L"忙碌时，也记得补充水分。");
    }
    if (intervalDue(stand_, p.standEnabled, p.standIntervalMinutes))
        emit(IslandReminderKind::Stand, "stand", tick.english ? L"Time to take a break" : L"起来活动一下吧",
            tick.english ? L"Stand up and stretch for a moment." : L"起身走走，舒展一下。");

    const int bedtime = TimeOfDay(p.sleepTime);
    const auto sleepKey = p.sleepTime + "/" + std::to_string(p.sleepLeadMinutes);
    const bool validTime = tick.minuteOfDay >= 0 && tick.minuteOfDay < 1440 &&
        tick.secondOfMinute >= 0 && tick.secondOfMinute < 60 &&
        tick.localDay >= -4000000 && tick.localDay <= 4000000;
    if (!p.sleepEnabled || bedtime < 0 || p.sleepLeadMinutes < 0 ||
        p.sleepLeadMinutes > 120 || !validTime) {
        sleepMinute_.reset(); sleepSettings_.clear();
    } else {
        const auto minute = tick.localDay * 1440 + tick.minuteOfDay;
        const int before = bedtime - p.sleepLeadMinutes;
        const int trigger = (before + 1440) % 1440;
        const auto triggerAt = tick.localDay * 1440 + trigger;
        const auto bedDay = tick.localDay + (before < 0 ? 1 : 0);
        const bool armed = sleepMinute_.has_value() && sleepSettings_ == sleepKey;
        const bool crossed = armed ? *sleepMinute_ < triggerAt && minute >= triggerAt &&
            minute - *sleepMinute_ <= 2 : minute == triggerAt;
        if (present && !gap && crossed && (!sleepBedDay_ || *sleepBedDay_ != bedDay)) {
            sleepBedDay_ = bedDay;
            emit(IslandReminderKind::Sleep, "sleep", tick.english ? L"Time to wind down" : L"准备休息吧",
                tick.english ? L"Your bedtime is " + std::wstring(p.sleepTime.begin(), p.sleepTime.end()) + L". Give yourself time to relax."
                    : L"设定的就寝时间是 " + std::wstring(p.sleepTime.begin(), p.sleepTime.end()) + L"，慢慢结束今天吧。");
        }
        sleepMinute_ = minute; sleepSettings_ = sleepKey;
    }

    const int offWork = TimeOfDay(p.offWorkTime);
    const bool squidward = p.offWorkStyle == "squidward";
    if (!p.offWorkEnabled || offWork < 0 || !validTime || (!squidward && p.offWorkStyle != "default")) {
        offWorkSecond_.reset(); offWorkSettings_.clear(); offWorkPresent_ = false;
    } else {
        constexpr std::int64_t DaySeconds = 86400;
        const auto second = tick.localDay * DaySeconds + tick.minuteOfDay * 60 + tick.secondOfMinute;
        const auto key = p.offWorkTime + "/" + p.offWorkStyle;
        const bool armed = offWorkSecond_.has_value() && offWorkSettings_ == key;
        // Adjacent target days handle both a 00:00 reminder at 23:59:55 and
        // a normal heartbeat that crosses that trigger into the next day.
        for (const auto targetDay : {tick.localDay - 1, tick.localDay, tick.localDay + 1}) {
            const auto triggerAt = targetDay * DaySeconds + offWork * 60 - (squidward ? 5 : 0);
            const bool crossed = armed && *offWorkSecond_ < triggerAt && second >= triggerAt;
            const bool initial = !armed && second >= triggerAt &&
                second - triggerAt < (squidward ? 1 : 60);
            const auto consumed = offWorkDays_.find(offWork);
            if ((!crossed && !initial) || (consumed != offWorkDays_.end() && targetDay <= consumed->second)) continue;
            // Consume this time slot even when protected. A different future
            // time can still be scheduled today, while switches, styles and
            // clock rollback cannot replay a consumed slot.
            offWorkDays_[offWork] = targetDay;
            const bool recent = !armed || second - *offWorkSecond_ <= static_cast<std::int64_t>(MaxTickGapMillis / 1000);
            const bool onTime = !squidward || second - triggerAt <= 1;
            if (present && (!armed || offWorkPresent_) && !gap && recent && onTime)
                emit(IslandReminderKind::OffWork, "off-work",
                    squidward ? (tick.english ? L"Squidward" : L"章鱼哥") :
                        (tick.english ? L"Time to finish work" : L"该下班啦"),
                    squidward ? (tick.english ? L"I'm off work, Mr. Krabs." : L"我下班了，蟹老板") :
                        (tick.english ? L"Wrap up for today and enjoy your evening." : L"收尾一下今天的工作，准备下班吧。"),
                    p.offWorkStyle);
        }
        offWorkSecond_ = second; offWorkSettings_ = key; offWorkPresent_ = present;
    }

    const auto& weather = tick.weather;
    const bool fresh = (p.weatherSevere || p.weatherRain || p.weatherTemperatureDrop) &&
        present && weather.enabled && weather.available && !weather.failed &&
        !weather.city.empty() && weather.city.size() <= 240 &&
        weather.city.find_first_not_of(L" \t\r\n") != std::wstring::npos && tick.weatherRevision != 0 &&
        tick.weatherAgeMs <= MaxWeatherAgeMillis && Temperature(weather.temperature);
    if (!fresh) {
        previousTemperature_.reset(); weatherFingerprint_.clear();
        return notices;
    }
    if (weatherCity_ != weather.city) {
        weatherCity_ = weather.city; weatherFingerprint_.clear(); previousTemperature_.reset();
        weatherAlerts_ = {};
    }
    const auto fingerprint = Fingerprint(tick, p);
    if (fingerprint == weatherFingerprint_) return notices;
    weatherFingerprint_ = fingerprint;
    const auto alert = [&](std::size_t index, bool enabled, std::wstring key, IslandReminderKind kind,
                           const char* name, std::wstring title, std::wstring body) {
        auto& state = weatherAlerts_[index];
        if (!enabled || key.empty()) { state.key.clear(); return; }
        if (state.key == key || (state.lastAt && (tick.nowMs < *state.lastAt || tick.nowMs - *state.lastAt < 3 * Hour))) return;
        state.key = std::move(key); state.lastAt = tick.nowMs;
        emit(kind, name, std::move(title), std::move(body));
    };
    // Projection hours are already ordered in the selected city's timezone.
    // Skip its current/past hour using snapshot age, without comparing city
    // forecast labels to the computer's potentially different local timezone.
    const auto firstHour = static_cast<std::size_t>(tick.weatherAgeMs / Hour) + 1;
    const auto endHour = std::min({weather.hours, weather.hourly.size(), firstHour + 3});
    int severeCode = Severe(weather.code) ? weather.code : 0;
    std::wstring severeHour;
    std::wstring rainKey;
    std::optional<int> futureLow;
    for (std::size_t i = firstHour; i < endHour; ++i) {
        const auto& hour = weather.hourly[i];
        if (!HourLabel(hour.label)) continue;
        if (!severeCode && Severe(hour.code)) { severeCode = hour.code; severeHour = hour.label; }
        if (rainKey.empty() && Rain(hour.code)) rainKey = hour.label + L":" + std::to_wstring(hour.code);
        if (Temperature(hour.temperature) && (!futureLow || hour.temperature < *futureLow)) futureLow = hour.temperature;
    }
    const auto dayKey = std::to_wstring(tick.localDay) + L":";
    alert(0, p.weatherSevere, severeCode ? dayKey + std::to_wstring(severeCode) + L":" + severeHour : L"",
        IslandReminderKind::SevereWeather, "severe-weather", tick.english ? L"Strong weather conditions" : L"强天气提醒",
        weather.city + (tick.english ? L": heavy rain, snow or thunderstorms are in the weather data. Check conditions before going out."
            : L"近期天气数据中有较强降水、降雪或雷雨，出门前请留意天气。"));
    alert(1, p.weatherRain, rainKey.empty() ? L"" : dayKey + rainKey, IslandReminderKind::Rain, "rain",
        tick.english ? L"Rain may be on the way" : L"出门记得带伞",
        weather.city + (tick.english ? L": the hourly forecast shows rain in the next few hours." : L"未来几小时的逐时预报中有降雨，出门记得带伞。"));
    const int threshold = std::clamp(p.temperatureDropDegrees, 1, 15);
    const bool adjacent = previousTemperature_ && !gap && tick.nowMs >= previousWeatherAt_ &&
        tick.nowMs - previousWeatherAt_ <= MaxWeatherAgeMillis;
    const bool observedDrop = adjacent && *previousTemperature_ - weather.temperature >= threshold;
    const bool forecastDrop = futureLow && weather.temperature - *futureLow >= threshold;
    const auto dropKey = observedDrop || forecastDrop ? dayKey + std::to_wstring(threshold) + L":" +
        std::to_wstring(forecastDrop ? *futureLow : weather.temperature) : L"";
    alert(2, p.weatherTemperatureDrop, dropKey, IslandReminderKind::TemperatureDrop, "temperature-drop",
        tick.english ? L"The temperature is dropping" : L"降温了，记得添衣",
        weather.city + (forecastDrop ? (tick.english ? L": the next few hours may be at least " : L"未来几小时可能降温至少 ")
            : (tick.english ? L": the latest reading is at least " : L"最新气温较上次下降至少 ")) +
            std::to_wstring(threshold) + (tick.english ? L"°C cooler. Keep an extra layer handy." : L"°C，记得适当添衣。"));
    previousTemperature_ = weather.temperature; previousWeatherAt_ = tick.nowMs;
    return notices;
}
} // namespace lume::shell
