#include "apps/shell/island_reminder_scheduler.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lume::shell;
constexpr std::uint64_t Minute = 60000, Hour = 60 * Minute;
void Check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
bool Has(const std::vector<IslandReminderNotice>& notices, IslandReminderKind kind) {
    return std::any_of(notices.begin(), notices.end(), [kind](const auto& item) { return item.kind == kind; });
}
struct Fixture {
    IslandReminderScheduler scheduler;
    IslandNotificationSettings p;
    IslandReminderTick tick;
    Fixture() { tick.localDay = 20000; tick.minuteOfDay = 12 * 60; }
    std::vector<IslandReminderNotice> Poll() { return scheduler.Tick(p, tick); }
    std::vector<IslandReminderNotice> Advance(unsigned minutes) {
        std::vector<IslandReminderNotice> result;
        for (unsigned i = 0; i < minutes; ++i) {
            tick.nowMs += Minute;
            if (++tick.minuteOfDay == 1440) { tick.minuteOfDay = 0; ++tick.localDay; }
            auto notices = Poll(); result.insert(result.end(), notices.begin(), notices.end());
        }
        return result;
    }
    std::vector<IslandReminderNotice> Seconds(unsigned seconds) {
        tick.nowMs += static_cast<std::uint64_t>(seconds) * 1000;
        const auto total = static_cast<unsigned>(tick.minuteOfDay * 60 + tick.secondOfMinute) + seconds;
        tick.localDay += total / 86400;
        tick.minuteOfDay = static_cast<int>((total % 86400) / 60);
        tick.secondOfMinute = static_cast<int>(total % 60);
        return Poll();
    }
    void Weather() {
        p.weatherSevere = p.weatherRain = p.weatherTemperatureDrop = true;
        tick.weather.enabled = tick.weather.available = true;
        tick.weather.city = L"广州";
        tick.weather.code = 0; tick.weather.temperature = 25;
        tick.weatherAgeMs = 0; tick.weatherRevision = 1;
        tick.weather.hours = 6;
        for (std::size_t i = 0; i < 6; ++i)
            tick.weather.hourly[i] = {std::to_wstring(12 + i), 25, 0, true};
    }
};
void Care() {
    Fixture f;
    Check(f.Poll().empty() && f.Advance(240).empty(), "defaults never emit reminders");
    f.p.waterEnabled = f.p.standEnabled = true; f.p.waterIntervalMinutes = f.p.standIntervalMinutes = 15;
    Check(f.Poll().empty() && f.Advance(14).empty(), "manual opt-in starts a full interval");
    auto notices = f.Advance(1);
    Check(notices.size() == 2 && Has(notices, IslandReminderKind::Water) && Has(notices, IslandReminderKind::Stand),
        "active water and stand intervals issue one notice each");
    Check(f.Poll().empty(), "repeated ticks never replay due reminders");
    f.p.waterEnabled = false; f.p.standEnabled = false;
    Check(f.Poll().empty() && f.Advance(30).empty(), "child switches disable their own delivery");
    f.p.waterEnabled = true;
    Check(f.Poll().empty() && f.Advance(14).empty(), "reenabling starts from zero instead of old elapsed time");
    Check(Has(f.Advance(1), IslandReminderKind::Water), "water resumes after its newly armed interval");
    f.p.waterIntervalMinutes = 30;
    Check(f.Poll().empty() && f.Advance(29).empty(), "changing the interval starts a full new interval");
    Check(Has(f.Advance(1), IslandReminderKind::Water), "the new interval is honored");

    Fixture goal;
    goal.p.waterEnabled = true; goal.p.standEnabled = false; goal.p.waterGoal = 1; goal.p.waterIntervalMinutes = 15;
    goal.Poll(); Check(goal.Advance(60).size() == 1, "daily water goal limits reminders, not claimed consumption");
    ++goal.tick.localDay;
    Check(goal.Advance(15).size() == 1, "water reminder budget resets on the next local day");

    Fixture absent;
    absent.p.waterEnabled = absent.p.standEnabled = true; absent.p.waterIntervalMinutes = absent.p.standIntervalMinutes = 15;
    absent.Poll(); absent.Advance(14); absent.tick.idleMs = IslandReminderScheduler::IdleBreakMillis;
    Check(absent.Advance(20).empty(), "idle time never accumulates a sitting interval or emits a backlog");
    absent.tick.idleMs = 0;
    Check(absent.Advance(14).empty() && absent.Advance(1).size() == 2, "returning from idle requires a full active interval");
    absent.tick.locked = true;
    Check(absent.Advance(30).empty(), "lock screen suppresses care reminders");
    absent.tick.locked = false;
    Check(absent.Advance(14).empty(), "unlock does not replay the time spent locked");
    absent.tick.nowMs += 8 * Hour;
    Check(absent.Poll().empty() && absent.Advance(14).empty(), "sleep/resume gaps discard elapsed time instead of bursting");
    Check(absent.Advance(1).size() == 2, "normal active timing resumes after wake");
}
void Sleep() {
    Fixture f;
    f.p.sleepEnabled = true;
    f.p.sleepTime = "00:10"; f.p.sleepLeadMinutes = 30;
    f.tick.minuteOfDay = 23 * 60 + 39;
    Check(f.Poll().empty() && Has(f.Advance(1), IslandReminderKind::Sleep), "bedtime lead may trigger on the preceding day");
    Check(f.Poll().empty(), "the same trigger minute does not duplicate bedtime notices");
    --f.tick.minuteOfDay; f.Poll();
    Check(f.Advance(1).empty(), "clock rollback cannot repeat a bedtime day's reminder");
    Check(Has(f.Advance(1440), IslandReminderKind::Sleep), "the next bedtime day is independently eligible");

    Fixture midnight;
    midnight.p.sleepEnabled = true;
    midnight.p.sleepTime = "00:30"; midnight.p.sleepLeadMinutes = 30;
    midnight.tick.minuteOfDay = 1439; midnight.Poll();
    Check(Has(midnight.Advance(1), IslandReminderKind::Sleep), "a trigger at midnight crosses the civil day correctly");

    Fixture wake;
    wake.p.sleepEnabled = true;
    wake.tick.minuteOfDay = 22 * 60 + 59; wake.Poll();
    wake.tick.nowMs += Hour; wake.tick.minuteOfDay = 23 * 60 + 59;
    Check(wake.Poll().empty(), "resume after bedtime does not issue missed bedtime reminders");
    Fixture locked;
    locked.p = wake.p; locked.tick.minuteOfDay = 22 * 60 + 59; locked.Poll(); locked.tick.locked = true;
    Check(locked.Advance(1).empty(), "locked bedtime trigger is suppressed");
    locked.tick.locked = false;
    Check(locked.Poll().empty(), "unlocking in the same minute does not replay the locked trigger");
}
void OffWork() {
    static_assert(static_cast<int>(IslandReminderKind::TemperatureDrop) == 5 &&
        static_cast<int>(IslandReminderKind::OffWork) == 6, "new reminder kinds preserve existing values");
    const auto enable = [](Fixture& f) { f.p.offWorkEnabled = true; };
    Fixture defaults;
    Check(!defaults.p.offWorkEnabled && defaults.p.offWorkTime == "18:00" && defaults.p.offWorkStyle == "default" &&
        defaults.tick.secondOfMinute == 0, "off-work is independently opt-in and old ticks default to zero seconds");
    defaults.tick.minuteOfDay = 18 * 60;
    Check(defaults.Poll().empty(), "default settings do not emit at the off-work time");
    defaults.p.offWorkEnabled = true;
    Check(Has(defaults.Poll(), IslandReminderKind::OffWork), "the child switch alone enables a due work reminder");

    Fixture f; enable(f); f.tick.minuteOfDay = 17 * 60 + 59;
    Check(f.Poll().empty(), "off-work waits until its configured local time");
    const auto notices = f.Advance(1);
    Check(notices.size() == 1 && notices.front().kind == IslandReminderKind::OffWork &&
        notices.front().title == L"该下班啦" && !notices.front().body.empty() && notices.front().style == "default" &&
        notices.front().id.find("off-work-") == 0, "18:00 emits one unchanged localized reminder with its frozen style");
    Check(f.Poll().empty(), "repeated ticks in the trigger minute do not replay off-work");
    f.p.offWorkEnabled = false; Check(f.Poll().empty(), "off-work can be disabled independently");
    f.p.offWorkEnabled = true; Check(f.Poll().empty(), "reenabling cannot replay today's delivered reminder");
    f.p.offWorkTime = "18:01";
    Check(f.Poll().empty() && Has(f.Advance(1), IslandReminderKind::OffWork),
        "changing to a new future time delivers another off-work reminder that day");
    const auto deliveredDay = f.tick.localDay;
    f.tick.minuteOfDay = 17 * 60 + 59; f.Poll();
    Check(f.Advance(2).empty(), "a same-day clock rollback cannot repeat off-work");
    f.tick.localDay = deliveredDay - 1; f.tick.minuteOfDay = 18 * 60; f.Poll();
    Check(f.Advance(1).empty(), "rolling the date backward cannot replay an earlier day");
    f.tick.localDay = deliveredDay + 1; f.tick.minuteOfDay = 18 * 60; f.Poll();
    Check(Has(f.Advance(1), IslandReminderKind::OffWork), "the next local day has its own off-work reminder");

    Fixture exactMinute; enable(exactMinute); exactMinute.tick.minuteOfDay = 18 * 60; exactMinute.tick.secondOfMinute = 45;
    Check(Has(exactMinute.Poll(), IslandReminderKind::OffWork), "default retains first opt-in anywhere within its target minute");
    Fixture changed; enable(changed); changed.tick.minuteOfDay = 17 * 60 + 59; changed.Poll();
    changed.p.offWorkTime = "18:10";
    Check(changed.Poll().empty() && changed.Advance(10).empty(), "a changed time replaces the former trigger");
    changed.tick.english = true;
    const auto english = changed.Advance(1);
    Check(english.size() == 1 && english.front().kind == IslandReminderKind::OffWork &&
        english.front().title == L"Time to finish work" && !english.front().body.empty(),
        "the changed time emits an English reminder when due");

    Fixture past; enable(past); past.tick.minuteOfDay = 18 * 60 + 5;
    Check(past.Poll().empty(), "starting after the configured time never catches up an old reminder");
    past.p.offWorkTime = "18:04";
    Check(past.Poll().empty() && past.Advance(2).empty(), "changing to an elapsed time never issues a backlog");
    past.p.offWorkEnabled = false; past.Poll();
    past.tick.minuteOfDay = 18 * 60 + 6; past.p.offWorkEnabled = true;
    Check(past.Poll().empty(), "reenabling after the configured time does not replay it");

    Fixture crossed; enable(crossed); crossed.tick.minuteOfDay = 17 * 60 + 59; crossed.Poll();
    crossed.tick.nowMs += 2 * Minute; crossed.tick.minuteOfDay = 18 * 60 + 1;
    Check(Has(crossed.Poll(), IslandReminderKind::OffWork), "a normal two-minute tick crossing can deliver off-work");
    Fixture seconds; enable(seconds); seconds.tick.minuteOfDay = 17 * 60 + 59; seconds.tick.secondOfMinute = 59;
    seconds.Poll(); Check(Has(seconds.Seconds(2), IslandReminderKind::OffWork), "second-level crossing can pass the exact default trigger");
    Fixture staleClock; enable(staleClock); staleClock.tick.minuteOfDay = 17 * 60 + 59; staleClock.Poll();
    staleClock.tick.nowMs += Minute; staleClock.tick.minuteOfDay = 18 * 60 + 2;
    Check(staleClock.Poll().empty(), "a wall-clock jump past the two-minute window never catches up");
    staleClock.tick.minuteOfDay = 17 * 60 + 59; staleClock.Poll();
    Check(staleClock.Advance(1).empty(), "clock rollback cannot replay a trigger already consumed by a wall-clock jump");
    staleClock.p.offWorkTime = "18:01"; staleClock.Poll();
    Check(Has(staleClock.Advance(1), IslandReminderKind::OffWork),
        "missing one default trigger does not block a newly configured future time");

    Fixture midnight; enable(midnight); midnight.p.offWorkTime = "00:00";
    midnight.tick.minuteOfDay = 1439; midnight.Poll();
    Check(Has(midnight.Advance(1), IslandReminderKind::OffWork), "off-work at midnight crosses the civil day correctly");
    Check(midnight.Poll().empty(), "midnight's repeated tick remains deduplicated");
    Check(Has(midnight.Advance(1440), IslandReminderKind::OffWork), "midnight reminders remain eligible the following day");
    Fixture lateDay; enable(lateDay); lateDay.p.offWorkTime = "23:59";
    lateDay.tick.minuteOfDay = 23 * 60 + 58; lateDay.tick.secondOfMinute = 59; lateDay.Poll();
    Check(Has(lateDay.Seconds(61), IslandReminderKind::OffWork), "a normal crossing after midnight belongs to the preceding target day");

    for (const auto* time : {"", "18:0", "6:00", "24:00", "18:60", "aa:bb", "18:00 "}) {
        Fixture invalid; enable(invalid); invalid.p.offWorkTime = time; invalid.tick.minuteOfDay = 18 * 60;
        Check(invalid.Poll().empty() && invalid.Advance(2).empty(), "malformed off-work times never emit");
    }
    for (int invalid = 0; invalid < 6; ++invalid) {
        Fixture bad; enable(bad); bad.tick.minuteOfDay = 18 * 60;
        if (invalid == 0) bad.tick.minuteOfDay = -1;
        if (invalid == 1) bad.tick.minuteOfDay = 1440;
        if (invalid == 2) bad.tick.localDay = -4000001;
        if (invalid == 3) bad.tick.localDay = 4000001;
        if (invalid == 4) bad.tick.secondOfMinute = -1;
        if (invalid == 5) bad.tick.secondOfMinute = 60;
        Check(bad.Poll().empty(), "invalid local calendar or seconds input cannot produce off-work reminders");
    }
    Fixture invalidStyle; enable(invalidStyle); invalidStyle.p.offWorkStyle = "other"; invalidStyle.tick.minuteOfDay = 18 * 60;
    Check(invalidStyle.Poll().empty(), "unrecognized internal styles fail closed");

    for (bool squidward : {false, true}) for (int protection = 0; protection < 4; ++protection) {
        Fixture protectedTime; enable(protectedTime);
        protectedTime.p.offWorkStyle = squidward ? "squidward" : "default";
        protectedTime.tick.minuteOfDay = 17 * 60 + 59;
        protectedTime.tick.secondOfMinute = squidward ? 54 : 59;
        if (protection == 3) protectedTime.tick.nowMs = Hour;
        protectedTime.Poll();
        if (protection == 0) protectedTime.tick.locked = true;
        if (protection == 1) protectedTime.tick.idleMs = IslandReminderScheduler::IdleBreakMillis;
        if (protection == 2) protectedTime.tick.nowMs += IslandReminderScheduler::MaxTickGapMillis;
        if (protection == 3) protectedTime.tick.nowMs = 0;
        Check(protectedTime.Seconds(1).empty(), "lock, idle, long heartbeat gaps and monotonic rollback suppress both styles");
        protectedTime.tick.locked = false; protectedTime.tick.idleMs = 0;
        Check(protectedTime.Poll().empty(), "leaving protection never replays a suppressed trigger");
        protectedTime.tick.minuteOfDay = 17 * 60 + 59;
        protectedTime.tick.secondOfMinute = squidward ? 54 : 59;
        protectedTime.Poll();
        Check(protectedTime.Seconds(1).empty(), "a rollback across a protected crossing cannot replay it");
        protectedTime.p.offWorkTime = "18:01";
        protectedTime.Poll();
        const auto rescheduled = protectedTime.Seconds(60);
        Check(rescheduled.size() == 1 && rescheduled.front().kind == IslandReminderKind::OffWork &&
            rescheduled.front().style == (squidward ? "squidward" : "default"),
            "a protected crossing consumes only its original time; a new future time remains eligible in both styles");
        protectedTime.p.offWorkTime = "18:00";
        for (bool retrySquidward : {false, true}) {
            protectedTime.p.offWorkStyle = retrySquidward ? "squidward" : "default";
            protectedTime.tick.minuteOfDay = 17 * 60 + 59;
            protectedTime.tick.secondOfMinute = retrySquidward ? 54 : 59;
            Check(protectedTime.Poll().empty() && protectedTime.Seconds(1).empty(),
                "returning to a protected time remains blocked even after another time delivers or the style changes");
        }
    }
    for (bool squidward : {false, true}) for (bool idle : {false, true}) {
        Fixture returning; enable(returning); returning.p.offWorkStyle = squidward ? "squidward" : "default";
        returning.tick.minuteOfDay = 17 * 60 + 59; returning.tick.secondOfMinute = squidward ? 54 : 59;
        returning.tick.locked = !idle; returning.tick.idleMs = idle ? IslandReminderScheduler::IdleBreakMillis : 0;
        returning.Poll(); returning.tick.locked = false; returning.tick.idleMs = 0;
        Check(returning.Seconds(2).empty(), "a crossing that begins during absence is consumed even if the current tick is present");
        returning.p.offWorkTime = "18:01"; returning.Poll();
        const auto rescheduled = returning.Seconds(59);
        Check(rescheduled.size() == 1 && rescheduled.front().kind == IslandReminderKind::OffWork,
            "after an absence consumes one time, another future time can remind normally");
    }

    Fixture all; enable(all);
    all.p.waterEnabled = all.p.standEnabled = all.p.sleepEnabled = true;
    all.p.waterIntervalMinutes = all.p.standIntervalMinutes = 15;
    all.p.sleepTime = "18:00"; all.p.sleepLeadMinutes = 0;
    all.tick.minuteOfDay = 17 * 60 + 45; all.Poll();
    Check(all.Advance(14).empty(), "simultaneous reminder fixture has no early notices");
    all.Weather(); all.tick.weather.code = 95; all.tick.weather.hourly[1] = {L"18", 10, 95, true};
    const auto simultaneous = all.Advance(1);
    Check(simultaneous.size() == IslandReminderScheduler::MaxNoticesPerTick && simultaneous.size() == 7 &&
        Has(simultaneous, IslandReminderKind::Water) && Has(simultaneous, IslandReminderKind::Stand) &&
        Has(simultaneous, IslandReminderKind::Sleep) && Has(simultaneous, IslandReminderKind::OffWork) &&
        Has(simultaneous, IslandReminderKind::SevereWeather) && Has(simultaneous, IslandReminderKind::Rain) &&
        Has(simultaneous, IslandReminderKind::TemperatureDrop), "all seven due kinds fit without dropping an existing reminder");
}
void Squidward() {
    const auto enable = [](Fixture& f) { f.p.offWorkEnabled = true; f.p.offWorkStyle = "squidward"; };
    Fixture f; enable(f); f.tick.minuteOfDay = 17 * 60 + 59; f.tick.secondOfMinute = 54;
    Check(f.Poll().empty(), "squidward waits until five seconds before the configured time");
    const auto notice = f.Seconds(1);
    Check(notice.size() == 1 && notice.front().kind == IslandReminderKind::OffWork && notice.front().style == "squidward",
        "squidward emits once at 17:59:55 for an 18:00 target");
    Check(notice.front().title == L"章鱼哥" && notice.front().body == L"我下班了，蟹老板",
        "Squidward uses the voice title and dialogue instead of generic wrap-up copy");
    f.p.offWorkStyle = "default";
    Check(notice.front().style == "squidward" && f.Seconds(5).empty(), "queued style is immutable and the target time does not deliver again");
    f.p.offWorkTime = "18:01"; f.Poll();
    Check(Has(f.Advance(1), IslandReminderKind::OffWork),
        "after the voiced time was delivered, a different future time can use the default style on the same day");

    Fixture crossing; enable(crossing); crossing.tick.minuteOfDay = 17 * 60 + 59; crossing.tick.secondOfMinute = 53;
    crossing.Poll(); Check(Has(crossing.Seconds(3), IslandReminderKind::OffWork), "a second-level crossing need not land on the exact trigger second");
    Fixture stalled; enable(stalled); stalled.tick.minuteOfDay = 17 * 60 + 59; stalled.tick.secondOfMinute = 53;
    stalled.Poll(); Check(stalled.Seconds(5).empty(), "a countdown three seconds late is consumed without late playback");
    stalled.tick.secondOfMinute = 54; stalled.Poll();
    Check(stalled.Seconds(1).empty(), "rolling back after a late countdown cannot replay the consumed target day");
    stalled.p.offWorkTime = "18:01"; stalled.Poll();
    Check(Has(stalled.Seconds(60), IslandReminderKind::OffWork),
        "a missed voiced countdown does not block a newly configured future time");
    Fixture lateStart; enable(lateStart); lateStart.tick.minuteOfDay = 17 * 60 + 59; lateStart.tick.secondOfMinute = 56;
    Check(lateStart.Poll().empty() && lateStart.Seconds(4).empty(), "starting after the early second never plays a late countdown");
    Fixture switchBefore; enable(switchBefore); switchBefore.tick.minuteOfDay = 17 * 60 + 59; switchBefore.tick.secondOfMinute = 54;
    switchBefore.Poll(); switchBefore.p.offWorkStyle = "default";
    Check(switchBefore.Seconds(1).empty() && Has(switchBefore.Seconds(5), IslandReminderKind::OffWork),
        "changing style before its trigger replaces the early reminder with the normal time");

    Fixture midnight; enable(midnight); midnight.p.offWorkTime = "00:00";
    midnight.tick.minuteOfDay = 1439; midnight.tick.secondOfMinute = 54; midnight.Poll();
    const auto targetDay = midnight.tick.localDay + 1;
    Check(Has(midnight.Seconds(1), IslandReminderKind::OffWork), "00:00 squidward triggers on the preceding day at 23:59:55");
    Check(midnight.Seconds(5).empty() && midnight.tick.localDay == targetDay, "crossing midnight does not reset the target day's deduplication");
    midnight.p.offWorkStyle = "default"; midnight.Poll();
    midnight.p.offWorkTime = "23:59"; midnight.tick.minuteOfDay = 1438; midnight.tick.secondOfMinute = 59; midnight.Poll();
    Check(Has(midnight.Seconds(1), IslandReminderKind::OffWork),
        "a different time on the target day remains eligible after the previous-day midnight countdown");
    ++midnight.tick.localDay; midnight.tick.minuteOfDay = 1438; midnight.tick.secondOfMinute = 59; midnight.Poll();
    Check(Has(midnight.Seconds(1), IslandReminderKind::OffWork), "the following target day remains independently eligible");

    Fixture midnightCross; enable(midnightCross); midnightCross.p.offWorkTime = "00:00";
    midnightCross.tick.minuteOfDay = 1439; midnightCross.tick.secondOfMinute = 54; midnightCross.Poll();
    Check(midnightCross.Seconds(6).empty(), "a midnight heartbeat five seconds after the early point consumes the correct target day without late playback");
    midnightCross.p.offWorkStyle = "default";
    Check(midnightCross.Poll().empty(), "the missed early crossing is not replayed at the target day's midnight");
    Fixture midnightReturn; enable(midnightReturn); midnightReturn.p.offWorkTime = "00:00";
    midnightReturn.tick.minuteOfDay = 1439; midnightReturn.tick.secondOfMinute = 54;
    midnightReturn.tick.locked = true; midnightReturn.Poll(); midnightReturn.tick.locked = false;
    Check(midnightReturn.Seconds(2).empty(), "returning one second late cannot replay a countdown whose prior tick was locked");
    for (int protection = 0; protection < 4; ++protection) {
        Fixture hidden; enable(hidden); hidden.p.offWorkTime = "00:00";
        hidden.tick.minuteOfDay = 1439; hidden.tick.secondOfMinute = 54;
        if (protection == 3) hidden.tick.nowMs = Hour;
        hidden.Poll();
        if (protection == 0) hidden.tick.locked = true;
        if (protection == 1) hidden.tick.idleMs = IslandReminderScheduler::IdleBreakMillis;
        if (protection == 2) hidden.tick.nowMs += IslandReminderScheduler::MaxTickGapMillis;
        if (protection == 3) hidden.tick.nowMs = 0;
        Check(hidden.Seconds(1).empty(), "midnight early crossings are consumed under every protection");
        hidden.tick.locked = false; hidden.tick.idleMs = 0;
        Check(hidden.Seconds(5).empty(), "returning after a protected countdown does not play it at midnight");
        hidden.p.offWorkStyle = "default";
        Check(hidden.Poll().empty(), "returning at the target day's midnight cannot replay its protected countdown");
        hidden.p.offWorkTime = "00:01"; hidden.Poll();
        Check(Has(hidden.Advance(1), IslandReminderKind::OffWork),
            "a suppressed midnight time does not block a different future time on that target day");
    }
}
void OffWorkRescheduling() {
    for (bool squidward : {false, true}) {
        const auto style = squidward ? "squidward" : "default";
        Fixture changed;
        changed.p.offWorkEnabled = true; changed.p.offWorkStyle = style;
        changed.tick.minuteOfDay = 17 * 60 + 59; changed.tick.secondOfMinute = squidward ? 54 : 59;
        Check(changed.Poll().empty(), "a newly configured off-work time waits for its trigger");
        auto notices = changed.Seconds(1);
        Check(notices.size() == 1 && notices.front().kind == IslandReminderKind::OffWork,
            "the original time delivers before rescheduling regression checks");
        const auto deliveredDay = changed.tick.localDay;
        for (const auto* time : {"18:01", "18:02", "18:03"}) {
            changed.p.offWorkTime = time;
            Check(changed.Poll().empty() && changed.Seconds(59).empty(),
                "successive future times each wait for their own default or five-second-early trigger");
            notices = changed.Seconds(1);
            Check(notices.size() == 1 && notices.front().kind == IslandReminderKind::OffWork && notices.front().style == style,
                "each newly selected future time can deliver once on the same local day");
            Check(changed.Poll().empty(), "repeated polls cannot duplicate the newly delivered time");
        }
        for (int slot = 0; slot < 4; ++slot) {
            changed.p.offWorkTime = std::string("18:0") + std::to_string(slot);
            for (bool retrySquidward : {false, true}) {
                changed.p.offWorkStyle = retrySquidward ? "squidward" : "default";
                changed.tick.minuteOfDay = 18 * 60 + slot - 1;
                changed.tick.secondOfMinute = retrySquidward ? 54 : 59;
                Check(changed.Poll().empty() && changed.Seconds(1).empty(),
                    "switching back to any consumed time cannot replay it through a style change or wall-clock rollback");
                changed.p.offWorkEnabled = false; Check(changed.Poll().empty(), "disabling a consumed time stays silent");
                changed.p.offWorkEnabled = true; Check(changed.Poll().empty(),
                    "reenabling a consumed time cannot bypass that time's delivery history");
            }
        }
        changed.p.offWorkStyle = style; changed.p.offWorkTime = "18:00";
        changed.tick.localDay = deliveredDay + 1;
        changed.tick.minuteOfDay = 17 * 60 + 59; changed.tick.secondOfMinute = squidward ? 54 : 59;
        Check(changed.Poll().empty() && Has(changed.Seconds(1), IslandReminderKind::OffWork),
            "the original consumed time can be reused on the next local day");
        changed.p.offWorkTime = "18:01"; changed.Poll();
        Check(Has(changed.Seconds(60), IslandReminderKind::OffWork),
            "each rescheduled time also gets its own allowance on the next day");

        Fixture unconsumed;
        unconsumed.p.offWorkEnabled = true; unconsumed.p.offWorkStyle = style;
        unconsumed.tick.minuteOfDay = 17 * 60 + 59; unconsumed.tick.secondOfMinute = squidward ? 54 : 59;
        unconsumed.Poll(); unconsumed.p.offWorkTime = "18:01";
        Check(unconsumed.Poll().empty() && unconsumed.Seconds(1).empty(),
            "replacing an armed time before it fires cancels that old trigger");
        unconsumed.p.offWorkTime = "18:02"; unconsumed.Poll();
        Check(unconsumed.Seconds(59).empty(), "an abandoned future time is not emitted while another time is selected");
        unconsumed.p.offWorkTime = "18:01";
        Check(unconsumed.Poll().empty() && Has(unconsumed.Seconds(1), IslandReminderKind::OffWork),
            "merely selecting then replacing a future time does not consume it before its eventual trigger");

        Fixture midnight;
        midnight.p.offWorkEnabled = true; midnight.p.offWorkStyle = style; midnight.p.offWorkTime = "00:00";
        midnight.tick.minuteOfDay = 1439; midnight.tick.secondOfMinute = squidward ? 54 : 59;
        const auto targetDay = midnight.tick.localDay + 1;
        Check(midnight.Poll().empty() && Has(midnight.Seconds(1), IslandReminderKind::OffWork),
            "midnight time delivers for the following civil target day in both styles");
        midnight.p.offWorkTime = "00:01"; midnight.Poll();
        Check(midnight.Seconds(59).empty() && Has(midnight.Seconds(1), IslandReminderKind::OffWork),
            "00:01 remains independently eligible after 00:00 even when the first voice began before midnight");
        Check(midnight.tick.localDay == targetDay, "midnight rescheduling remains on the intended target day");
        midnight.p.offWorkTime = "00:00";
        for (bool retrySquidward : {false, true}) {
            midnight.p.offWorkStyle = retrySquidward ? "squidward" : "default";
            midnight.tick.localDay = targetDay - 1; midnight.tick.minuteOfDay = 1439;
            midnight.tick.secondOfMinute = retrySquidward ? 54 : 59;
            Check(midnight.Poll().empty() && midnight.Seconds(1).empty(),
                "returning across midnight or changing style cannot replay a consumed 00:00 target day");
        }
        midnight.p.offWorkStyle = style;
        midnight.tick.localDay = targetDay; midnight.tick.minuteOfDay = 1439;
        midnight.tick.secondOfMinute = squidward ? 54 : 59;
        Check(midnight.Poll().empty() && Has(midnight.Seconds(1), IslandReminderKind::OffWork),
            "the same midnight time becomes eligible again for the next civil target day");
    }
}
void Weather() {
    Fixture f; f.Weather(); f.tick.weather.hourly[1].code = 95;
    auto notices = f.Poll();
    Check(Has(notices, IslandReminderKind::Rain) && Has(notices, IslandReminderKind::SevereWeather),
        "fresh hourly WMO thunderstorm predicts rain and strong weather");
    for (const auto& notice : notices)
        Check(notice.title.find(L"预警") == std::wstring::npos && notice.body.find(L"官方") == std::wstring::npos,
            "forecast conditions never pretend to be an official weather warning");
    Check(f.Poll().empty(), "same provider data does not repeat a weather alert");
    ++f.tick.weatherRevision; f.tick.nowMs += Minute;
    Check(f.Poll().empty(), "fetching an unchanged forecast does not repeat an alert");
    f.tick.weather.hourly[1].label = L"14"; ++f.tick.weatherRevision;
    Check(f.Poll().empty(), "changed forecasts respect the weather cooldown");
    f.tick.nowMs += 3 * Hour; ++f.tick.weatherRevision;
    Check(!f.Poll().empty(), "a changed risk can remind again after cooldown");

    for (int invalid = 0; invalid < 7; ++invalid) {
        Fixture bad; bad.Weather(); bad.tick.weather.code = 99; bad.tick.weather.hourly[1].code = 61;
        if (invalid == 0) bad.p.weatherRain = bad.p.weatherSevere = bad.p.weatherTemperatureDrop = false;
        if (invalid == 1) bad.tick.weather.enabled = false;
        if (invalid == 2) bad.tick.weather.available = false;
        if (invalid == 3) bad.tick.weather.failed = true;
        if (invalid == 4) bad.tick.weather.city = L" ";
        if (invalid == 5) bad.tick.weatherAgeMs = IslandReminderScheduler::MaxWeatherAgeMillis + 1;
        if (invalid == 6) bad.tick.weatherRevision = 0;
        Check(bad.Poll().empty(), "disabled, unavailable, stale or unidentifiable weather never emits");
    }
    Fixture currentRain; currentRain.Weather(); currentRain.tick.weather.code = 61;
    Check(currentRain.Poll().empty(), "current rain alone is not invented into an upcoming rain forecast");
    Fixture children; children.Weather(); children.p.weatherRain = children.p.weatherSevere = children.p.weatherTemperatureDrop = false;
    children.tick.weather.code = 99; children.tick.weather.hourly[1] = {L"13", 10, 95, true};
    Check(children.Poll().empty(), "weather child opt-ins gate all types independently");

    Fixture cooling; cooling.Weather(); cooling.tick.english = true; cooling.Poll();
    cooling.Advance(30); cooling.tick.weather.temperature = 19; ++cooling.tick.weatherRevision;
    notices = cooling.Poll();
    Check(Has(notices, IslandReminderKind::TemperatureDrop) && notices.front().title == L"The temperature is dropping",
        "adjacent fresh readings detect a real configured drop and localize the notice");
    Fixture forecast; forecast.Weather(); forecast.tick.weather.hourly[2].temperature = 18;
    Check(Has(forecast.Poll(), IslandReminderKind::TemperatureDrop), "nearby hourly temperatures can predict cooling");
    Fixture staleBaseline; staleBaseline.Weather(); staleBaseline.Poll();
    staleBaseline.tick.nowMs += 2 * Hour; ++staleBaseline.tick.weatherRevision; staleBaseline.tick.weather.temperature = 10;
    Check(staleBaseline.Poll().empty(), "widely separated readings are not treated as a sudden temperature drop");
    Fixture otherCity; otherCity.Weather(); otherCity.Poll();
    otherCity.tick.weather.city = L"哈尔滨"; otherCity.tick.weather.temperature = -10;
    for (auto& hour : otherCity.tick.weather.hourly) hour.temperature = -10;
    ++otherCity.tick.weatherRevision;
    Check(otherCity.Poll().empty(), "switching cities never compares unrelated temperatures");
    Fixture malformed; malformed.Weather(); malformed.tick.weather.hours = 999;
    for (auto& hour : malformed.tick.weather.hourly) hour = {};
    Check(malformed.Poll().empty(), "bounded hour traversal rejects missing forecast labels instead of inventing zero-degree cooling");
}
} // namespace
int main() {
    try {
        Care(); Sleep(); OffWork(); Squidward(); OffWorkRescheduling(); Weather();
        std::cout << "ISLAND_REMINDER_SCHEDULER_PASS: opt-ins, intervals, absence, bedtime, off-work, fresh weather, deduplication\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
