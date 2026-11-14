#include "apps/shell/dynamic_island_session.h"
#include "platform/windows/widget_ui/window_layer_policy.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <gdiplus.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

namespace {
using namespace lume;
using Json = nlohmann::json;
using ui::PageCommand;
namespace layer = platform::windows::window_layer;
constexpr std::uint64_t Minute = 60000;
void Check(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
struct Files final : settings::IFileStore {
    std::map<std::string, std::string> values;
    bool fail = false;
    bool Read(const std::string& path, std::string& out) const override {
        const auto found = values.find(path); if (found == values.end()) return false; out = found->second; return true;
    }
    bool ReadBounded(const std::string& path, std::size_t limit, std::string& out) const override {
        const auto found = values.find(path);
        if (found == values.end() || found->second.size() > limit) return false;
        out = found->second; return true;
    }
    bool Exists(const std::string& path) const override { return values.count(path) != 0; }
    bool Write(const std::string& path, const std::string& value) override {
        if (fail) return false; values[path] = value; return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        if (fail || !Exists(from)) return false; values[to] = values[from]; values.erase(from); return true;
    }
    bool Remove(const std::string& path) override { values.erase(path); return true; }
};
Json Notifications(const shell::DynamicIslandSession& session) { return Json::parse(session.NotificationsState()); }
Json State(const shell::DynamicIslandSession& session) { return Json::parse(session.State()); }
Json Set(shell::DynamicIslandSession& session, const char* key, Json value) {
    const auto encoded = Json{{"version", 1}, {"scope", "island-notifications"}, {"action", "set"},
        {"key", key}, {"value", std::move(value)}}.dump();
    const auto reply = session.HandleNotifications(std::wstring(encoded.begin(), encoded.end()));
    Check(reply.has_value(), "notification setting returns a native acknowledgement");
    return Json::parse(*reply);
}
void Saved(shell::DynamicIslandSession& session, const char* key, Json value) {
    const auto result = Set(session, key, std::move(value));
    Check(result["saved"] == true && result["error"] == "", "notification setting is durably confirmed");
}
bool Overlay(HWND window) {
    return window && (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}
std::vector<HWND> IslandWindows() {
    std::vector<HWND> result;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        wchar_t name[80]{}; GetClassNameW(window, name, 80);
        if (std::wstring(name) == L"LumeWindowsDynamicIsland")
            reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
void Pump(unsigned milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < end);
}
void ClickMiddle(HWND window) {
    RECT area{}; Check(GetClientRect(window, &area) != FALSE, "notification has a client area");
    const auto point = MAKELPARAM((area.right - area.left) / 2, (area.bottom - area.top) / 2);
    SendMessageW(window, WM_LBUTTONDOWN, 0, point); SendMessageW(window, WM_LBUTTONUP, 0, point);
}
void Advance(shell::DynamicIslandSession& session, shell::IslandReminderTick& context, unsigned minutes) {
    for (unsigned i = 0; i < minutes; ++i) {
        context.nowMs += Minute;
        if (++context.minuteOfDay >= 1440) { context.minuteOfDay = 0; ++context.localDay; }
        session.PollNotifications();
    }
}
void NoMessaging(const Json& state) {
    Check(state["receiving"] == false && state["runtime"]["qq"] == "disabled" &&
        state["runtime"]["wechat"] == "disabled", "care and weather do not authorize or start message reception");
}
void NoOtherWidgetWindow() {
    bool found = false;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        wchar_t name[128]{}; GetClassNameW(window, name, 128);
        if (wcsstr(name, L"Weather") != nullptr) *reinterpret_cast<bool*>(data) = true;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    Check(!found, "notification weather consumer does not create a desktop weather widget");
}
std::vector<HWND> NotificationDispatchers() {
    std::vector<HWND> result; HWND previous = nullptr;
    while ((previous = FindWindowExW(HWND_MESSAGE, previous, L"LumeIslandNotificationDispatch", nullptr)) != nullptr) {
        DWORD process = 0;
        const auto thread = GetWindowThreadProcessId(previous, &process);
        if (process == GetCurrentProcessId() && thread == GetCurrentThreadId()) result.push_back(previous);
    }
    return result;
}
HWND OnlyNotificationDispatcher() {
    const auto dispatchers = NotificationDispatchers();
    Check(dispatchers.size() == 1, "an enabled messaging session owns exactly one message-only dispatcher");
    Check(!IsWindowVisible(dispatchers.front()), "the receiver dispatcher never becomes a visible desktop surface");
    return dispatchers.front();
}
void CheckNotificationDispatcher() {
    Check(NotificationDispatchers().empty(), "no earlier notification dispatcher outlives its session");
    Files files; shell::IslandReminderTick context; context.localDay = 20000; context.minuteOfDay = 12 * 60;
    unsigned contextReads = 0;
    shell::DynamicIslandSessionOptions options; options.preferencesPath = "dispatcher-session.json";
    options.notificationContext = [&] { ++contextReads; return context; };
    options.notificationAllowed = [] { return true; };
    options.monotonicMillis = [&] { return context.nowMs; };
    {
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr), options, &files);
        Check(session.Restore() && NotificationDispatchers().empty(), "default opt-out never creates a notification dispatcher");
        Saved(session, "messagesWeChat", true);
        auto state = Notifications(session);
        Check(NotificationDispatchers().empty() && state["runtime"]["islandEnabled"] == false,
            "saved message opt-ins do not create a dispatcher while the island is off");
        Check(state["preferences"]["messagesQQ"] == false && state["preferences"]["messagePreview"] == false &&
            state["runtime"]["qq"] == "disabled", "dispatcher verification uses only WeChat with no QQ listener or message preview");
        session.HandlePage(PageCommand::IslandEnable);
        HWND dispatcher = OnlyNotificationDispatcher();
        const auto readsBeforeWrongCookie = contextReads;
        SendMessageW(dispatcher, WM_APP + 0x362, 0, 0);
        Check(contextReads == readsBeforeWrongCookie, "an invalid dispatcher cookie cannot poll session state or deliver a notice");

        files.fail = true; state = Set(session, "messagesWeChat", false);
        Check(state["saved"] == false && state["preferences"]["messagesWeChat"] == true &&
            OnlyNotificationDispatcher() == dispatcher, "failed opt-out preserves the confirmed dispatcher lifetime");
        files.fail = false; Saved(session, "messagesWeChat", false);
        Check(!IsWindow(dispatcher) && NotificationDispatchers().empty(), "successful message opt-out destroys its dispatcher");
        Saved(session, "messagesWeChat", true);
        const auto restartedDispatcher = OnlyNotificationDispatcher();
        Check(restartedDispatcher != dispatcher, "reenabling messaging creates a new dispatcher window lifetime");
        dispatcher = restartedDispatcher;
        const auto readsBeforeStaleCookie = contextReads;
        SendMessageW(dispatcher, WM_APP + 0x362, 0, 0);
        Check(contextReads == readsBeforeStaleCookie, "a stale invalid wake remains harmless after dispatcher recreation");
        session.HandlePage(PageCommand::IslandDisable);
        Check(!IsWindow(dispatcher) && NotificationDispatchers().empty(), "island disable detaches the receiver and destroys its dispatcher");
        session.HandlePage(PageCommand::IslandEnable); dispatcher = OnlyNotificationDispatcher();
        state = Notifications(session);
        Check(state["preferences"]["messagesQQ"] == false && state["preferences"]["messagePreview"] == false &&
            state["runtime"]["qq"] == "disabled", "dispatcher restart never enables QQ or content preview");
        session.Shutdown();
        Check(!IsWindow(dispatcher) && NotificationDispatchers().empty(), "shutdown destroys the dispatcher after stopping its receiver");
        session.PollNotifications(); Pump(20);
        Check(NotificationDispatchers().empty(), "late queued receiver work cannot recreate a stopped session dispatcher");
    }
    Check(NotificationDispatchers().empty(), "explicit shutdown and destruction leave no message-only dispatcher");
    {
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr), options, &files);
        Check(session.Restore(), "reopen persisted dispatcher preferences");
        OnlyNotificationDispatcher();
    }
    Check(NotificationDispatchers().empty(), "ordinary session destruction also releases its notification dispatcher");
}
void CheckSquidwardPresentation(layer::Context& policy) {
    Check(IslandWindows().empty(), "earlier session leaves no island windows before voice verification");
    auto connected = platform::windows::ConnectedIslandMonitors();
    Check(!connected.empty(), "read one actual monitor for isolated mirror geometry");
    auto first = connected.front(); first.primary = true;
    auto second = first; second.primary = false; second.device = L"verification-voice-screen-2";
    const auto width = first.bounds.right - first.bounds.left;
    OffsetRect(&second.bounds, -width, 0); OffsetRect(&second.work, -width, 0);
    connected = {first, second};
    Files files; shell::IslandReminderTick context;
    context.localDay = 21000; context.minuteOfDay = 17 * 60 + 59; context.secondOfMinute = 54;
    unsigned voiceStarts = 0, voiceStops = 0, levelReads = 0, positionReads = 0;
    std::optional<unsigned> audioPosition = 0;
    bool voicePlaying = false;
    shell::DynamicIslandSessionOptions options; options.preferencesPath = "voice-session.json";
    options.monitors = [&] { return connected; };
    options.notificationContext = [&] { return context; };
    options.monotonicMillis = [&] { return context.nowMs; };
    options.notificationAllowed = [&] { return !context.locked; };
    options.reminderVoice = [&](bool playing) {
        Check(voicePlaying != playing, "shared voice receives balanced start and stop transitions");
        voicePlaying = playing;
        if (playing) ++voiceStarts; else ++voiceStops;
    };
    options.reminderVoiceLevel = [&] { ++levelReads; return voicePlaying ? .75f : 0.f; };
    options.reminderVoicePosition = [&] { ++positionReads; return audioPosition; };
    shell::DynamicIslandSession session(GetModuleHandleW(nullptr), options, &files);
    Check(session.Restore(), "restore isolated voice session without enabling it");
    session.HandlePage(PageCommand::IslandScreenFirst);
    Saved(session, "offWorkStyle", "squidward"); Saved(session, "offWorkEnabled", true);
    Check(!session.Running() && voiceStarts == 0, "saved voice choices alone create no island or audio");
    session.HandlePage(PageCommand::IslandEnable); session.HandlePage(PageCommand::IslandAutoHideOff);
    const auto mirrored = [&](bool overlay, const char* stage) {
        const auto windows = IslandWindows();
        std::cout << "VOICE_STAGE " << stage << " windows=" << windows.size() << " expectedOverlay=" << overlay
            << " starts=" << voiceStarts << " stops=" << voiceStops << " day=" << context.localDay
            << " minute=" << context.minuteOfDay << " second=" << context.secondOfMinute << '\n';
        Check(windows.size() == 2 && State(session)["screen"] == 0, "all-screen policy creates exactly two isolated native islands");
        for (const auto window : windows) {
            std::cout << "VOICE_WINDOW " << stage << " handle=" << window << " overlay=" << Overlay(window)
                << " visible=" << IsWindowVisible(window) << " exStyle=" << GetWindowLongPtrW(window, GWL_EXSTYLE)
                << " suppressed=" << layer::Suppressed(window) << '\n';
            Check(Overlay(window) == overlay, "both native mirrors share the expected notification layer");
        }
    };
    mirrored(false, "before-trigger");
    Check(voiceStarts == 0, "17:59:54 remains silent before an 18:00 squidward reminder");
    const auto foreground = GetForegroundWindow();
    context.nowMs += 1000; context.secondOfMinute = 55; session.PollNotifications();
    mirrored(true, "first-trigger");
    Check(voiceStarts == 1 && voiceStops == 0 && voicePlaying,
        "both actual native mirrors present at 17:59:55 while sharing one voice start");
    session.PollNotifications(); Pump(120);
    Check(voiceStarts == 1 && levelReads > 0 && GetForegroundWindow() == foreground,
        "repeated polls do not restart voice and native rendering reads its PCM level without activation");
    const auto readsBeforeAdvance=positionReads;
    audioPosition=1367;Pump(80);
    Check(positionReads>=readsBeforeAdvance+2 && voiceStarts==1,
        "both native mirrors read the shared recording position without starting another voice");
    audioPosition.reset();Pump(45);
    Check(voiceStarts==1&&voicePlaying,"an unavailable device-position sample does not stop or restart shared voice");
    audioPosition=3720;Pump(45);
    NoMessaging(Notifications(session));
    Saved(session, "offWorkEnabled", false);
    mirrored(false, "reminder-disabled");
    Check(!voicePlaying && voiceStops == 1, "disabling the reminder releases both mirrors and stops voice once");
    Saved(session, "offWorkEnabled", true); session.PollNotifications();
    Check(voiceStarts == 1 && !voicePlaying, "reenabling during the consumed second does not replay voice");
    const auto nextVoice = [&] {
        ++context.localDay; context.minuteOfDay = 17 * 60 + 59; context.secondOfMinute = 54;
        context.nowMs += Minute; context.locked = false; policy.allDisplays = false; layer::Refresh();
        session.PollNotifications();
        const auto starts = voiceStarts;
        context.nowMs += 1000; context.secondOfMinute = 55; session.PollNotifications();
        mirrored(true, "next-day-trigger");
        Check(voicePlaying && voiceStarts == starts + 1 && voiceStarts == voiceStops + 1,
            "a fresh target day starts one shared voice for two native presentations");
    };
    nextVoice();
    session.HandlePage(PageCommand::IslandDisable);
    Check(IslandWindows().empty() && !voicePlaying && voiceStops == 2,
        "disabling the island destroys both mirrors and stops their shared voice once");
    session.HandlePage(PageCommand::IslandEnable); session.PollNotifications();
    mirrored(false, "island-reopened");
    Check(voiceStarts == 2 && !voicePlaying, "reopening the island cannot replay its consumed target day");
    nextVoice();
    context.locked = true; session.PollNotifications();
    mirrored(false, "locked");
    Check(!voicePlaying && voiceStops == 3, "locking clears both live presentations and immediately cancels voice");
    context.locked = false; context.nowMs += 1000; context.secondOfMinute = 56; session.PollNotifications();
    Check(voiceStarts == 3 && !voicePlaying, "unlocking within the early time window does not replay the canceled voice");
    nextVoice();
    policy.allDisplays = true; layer::Refresh();
    // Exercise the same foreground policy check used by the production 1s timer.
    for (const auto window : IslandWindows()) SendMessageW(window, WM_TIMER, 1, 0);
    const auto protectedWindows = IslandWindows();
    Check(protectedWindows.size() == 2, "fullscreen protection preserves both native window lifetimes");
    for (const auto window : protectedWindows)
        Check(!IsWindowVisible(window) && layer::Suppressed(window), "fullscreen protection hides and suppresses both native mirrors");
    Check(!voicePlaying && voiceStops == 4, "fullscreen protection cancels the two live mirrors with one shared voice stop");
    policy.allDisplays = false; layer::Refresh();
    for (const auto window : IslandWindows()) SendMessageW(window, WM_TIMER, 1, 0);
    Pump(40); session.PollNotifications(); mirrored(false, "fullscreen-restored");
    Check(voiceStarts == 4 && !voicePlaying, "leaving fullscreen does not restart a canceled voice");

    ++context.localDay; context.minuteOfDay = 17 * 60 + 59; context.secondOfMinute = 54; context.nowMs += Minute;
    policy.allDisplays = true; layer::Refresh(); session.PollNotifications();
    context.secondOfMinute = 55; context.nowMs += 1000; session.PollNotifications();
    Check(voiceStarts == 4 && !voicePlaying, "a timed notification blocked on both screens never starts voice");
    policy.allDisplays = false; layer::Refresh();
    for (const auto window : IslandWindows()) SendMessageW(window, WM_TIMER, 1, 0);
    session.PollNotifications(); mirrored(false, "fullscreen-trigger-skipped");
    Check(voiceStarts == 4, "a skipped fullscreen countdown is not queued for later playback");

    ++context.localDay; context.minuteOfDay = 17 * 60 + 59; context.secondOfMinute = 54; context.nowMs += Minute;
    context.locked = true; session.PollNotifications();
    context.secondOfMinute = 55; context.nowMs += 1000; session.PollNotifications();
    context.locked = false; context.secondOfMinute = 56; context.nowMs += 1000; session.PollNotifications();
    mirrored(false, "locked-trigger-skipped");
    Check(voiceStarts == 4 && !voicePlaying, "a locked trigger is consumed without audio or an unlock replay");
    Saved(session,"offWorkTime","18:01");
    Check(voiceStarts==4&&!voicePlaying,"saving a different future time rearms without playing immediately");
    files.fail=true;
    const auto rejectedTime=Set(session,"offWorkTime","18:03");
    Check(rejectedTime["saved"]==false&&Notifications(session)["preferences"]["offWorkTime"]=="18:01",
        "a failed time save leaves the confirmed schedule intact");
    files.fail=false;
    context.minuteOfDay=18*60;context.secondOfMinute=54;context.nowMs+=58000;session.PollNotifications();
    Check(voiceStarts==4,"the new schedule remains silent before its own five-second lead");
    context.secondOfMinute=55;context.nowMs+=1000;audioPosition=0;session.PollNotifications();
    mirrored(true,"retimed-same-day");
    Check(voiceStarts==5&&voiceStops==4&&voicePlaying,
        "a saved future time on the same day starts exactly one shared voice on both mirrors");
    Saved(session,"offWorkTime","18:02");
    Check(voiceStarts==5&&voiceStops==5&&!voicePlaying,
        "changing time during playback closes the old capsule and arms the new time");
    context.minuteOfDay=18*60+1;context.secondOfMinute=54;context.nowMs+=59000;session.PollNotifications();
    context.secondOfMinute=55;context.nowMs+=1000;session.PollNotifications();
    mirrored(true,"retimed-again-same-day");
    Check(voiceStarts==6&&voiceStops==5&&voicePlaying,
        "a second distinct time on the same day also starts once without restarting Lume");
    Saved(session,"offWorkTime","18:00");
    Saved(session,"offWorkTime","18:01");
    Saved(session,"offWorkTime","18:02");
    Saved(session,"offWorkEnabled",false);Saved(session,"offWorkEnabled",true);session.PollNotifications();
    mirrored(false,"used-slots-stay-consumed");
    Check(voiceStarts==6&&voiceStops==6&&!voicePlaying,
        "switching back to a consumed slot or toggling it never replays its voice");
    session.Shutdown();
    Check(IslandWindows().empty() && voiceStarts == 6 && voiceStops == 6,
        "shutdown leaves no mirrored windows or outstanding voice leases");
}
void Scenario() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize isolated COM apartment");
    layer::Context policy; policy.foreignForeground = true;
    layer::contextReader = [&] { return policy; }; layer::Start();
    Files files; shell::IslandReminderTick context; context.localDay = 20000; context.minuteOfDay = 12 * 60;
    std::vector<bool> weatherConsumers; unsigned weatherSettings = 0, todoFinds = 0, todoMutations = 0, defaultVoiceCalls = 0;
    shell::DynamicIslandSessionOptions options; options.preferencesPath = "notification-session.json";
    options.monotonicMillis = [&] { return context.nowMs; };
    options.notificationContext = [&] { return context; };
    options.notificationAllowed = [&] { return !context.locked; };
    options.reminderVoice = [&](bool) { ++defaultVoiceCalls; };
    options.reminderVoiceLevel = [] { return 0.f; };
    options.weather = [&] { return context.weather; };
    options.weatherActive = [&](bool enabled) { weatherConsumers.push_back(enabled); };
    options.openWeatherSettings = [&] { ++weatherSettings; };
    options.todos.find = [&](const std::string&) -> std::optional<platform::windows::IslandTodo> { ++todoFinds; return {}; };
    options.todos.toggle = [&](const auto&) { ++todoMutations; };
    options.todos.edit = [&](const auto&, auto) { ++todoMutations; };
    options.todos.remove = [&](const auto&) { ++todoMutations; };
    options.todos.openTask = [&](const auto&) { ++todoMutations; };
    {
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr), options, &files);
        Check(session.Restore(), "restore defaults without starting the island");
        Advance(session, context, 120);
        auto state = Notifications(session);
        Check(!session.Running() && !session.Snapshot().enabled && weatherConsumers.empty() && files.values.empty() &&
            state["runtime"]["islandEnabled"] == false && state["runtime"]["care"] == "off" &&
            state["runtime"]["weather"] == "off", "default notifications have no windows, weather consumer or preference writes");
        NoMessaging(state);
        Check(NotificationDispatchers().empty(), "care and weather defaults do not allocate a message dispatcher");
        Saved(session, "messagesQQ", true);
        const auto selectedSource = session.HandleNotifications(LR"({"version":1,"scope":"island-notifications","action":"snapshot"})");
        Check(selectedSource.has_value(), "selected source has a readable native snapshot");
        state = Json::parse(*selectedSource);
        Check(state["result"] == "snapshot" && state["saved"] == false && state["preferences"]["messagesQQ"] == true &&
            !state["preferences"].contains("messagesEnabled") && state["availability"] == "not-connected" &&
            !session.Running() && weatherConsumers.empty(), "saving a QQ source alone never implies an active receiver or enabled island");
        NoMessaging(state);
        Saved(session, "messagesQQ", false);
        Saved(session, "standEnabled", false); Saved(session, "waterIntervalMinutes", 15);
        Saved(session, "waterGoal", 20); Saved(session, "waterEnabled", true);
        Check(Notifications(session)["runtime"]["care"] == "island-off" && !session.Running() && weatherConsumers.empty(),
            "saving care preferences never implicitly enables the island");
        session.HandlePage(PageCommand::IslandEnable); session.HandlePage(PageCommand::IslandAutoHideOff);
        Check(session.Running() && Notifications(session)["runtime"]["care"] == "active", "explicit island enable starts saved care policy");
        HWND window = session.Handle(); const HWND foreground = GetForegroundWindow();
        Check(!Overlay(window), "idle island keeps its selected desktop layer");
        Advance(session, context, 14); Check(!Overlay(window), "no care reminder before the configured interval");
        const auto savedFile = files.values.at("notification-session-notifications.json");
        files.fail = true; state = Set(session, "waterIntervalMinutes", 240);
        Check(state["saved"] == false && state["error"] == "save" && state["preferences"]["waterIntervalMinutes"] == 15 &&
            files.values.at("notification-session-notifications.json") == savedFile, "failed preference write preserves the persisted interval");
        state = Set(session, "messagesQQ", true);
        Check(state["saved"] == false && state["preferences"]["messagesQQ"] == false,
            "failed source opt-in never configures a QQ receiver");
        const auto afterFailure = session.HandleNotifications(LR"({"version":1,"scope":"island-notifications","action":"snapshot"})");
        Check(afterFailure.has_value(), "failed opt-in remains readable through the snapshot command");
        const auto failureSnapshot = Json::parse(*afterFailure);
        Check(failureSnapshot["result"] == "snapshot" && failureSnapshot["saved"] == false &&
            failureSnapshot["availability"] == "not-connected" && failureSnapshot["preferences"] == state["preferences"],
            "snapshot preserves confirmed preferences and never turns a failed save into a listening claim");
        NoMessaging(failureSnapshot);
        Check(NotificationDispatchers().empty(), "a failed message opt-in cannot create a receiver dispatcher");
        NoMessaging(state); files.fail = false;
        Advance(session, context, 1);
        Check(Overlay(window) && GetForegroundWindow() == foreground, "confirmed care timer reaches a real native transient notice without activation");
        const auto beforeCareRead = files.values;
        Pump(180); ClickMiddle(window);
        Check(Overlay(window) && State(session)["expanded"] == true && GetForegroundWindow() == foreground,
            "care notification click opens its island detail without activating another window");
        Check(todoFinds == 0 && todoMutations == 0 && weatherSettings == 0 && files.values == beforeCareRead,
            "reading care details neither enters todo history nor opens settings or changes preferences");
        session.HandlePage(PageCommand::IslandCollapse);
        Check(!Overlay(window) && State(session)["expanded"] == false,
            "explicit collapse closes care details and restores the configured desktop layer");
        session.HandlePage(PageCommand::IslandExpand);
        SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0); Pump(180);
        Check(todoFinds == 0 && todoMutations == 0, "care notifications never enter todo lookup, history or mutations");
        session.HandlePage(PageCommand::IslandCollapse);
        Advance(session, context, 15); Check(Overlay(window), "next interval can display another transient notice");
        session.HandlePage(PageCommand::IslandDisable);
        state = Notifications(session);
        Check(!IsWindow(window) && !session.Running() && state["runtime"]["islandEnabled"] == false &&
            state["runtime"]["care"] == "island-off", "disabling the island clears the notice and reports island-off");
        Advance(session, context, 120);
        session.HandlePage(PageCommand::IslandEnable); window = session.Handle();
        Check(!Overlay(window), "reenabling never replays the old visible notice or missed intervals");
        Advance(session, context, 14); Check(!Overlay(window), "reenabled care starts a fresh full interval");
        Saved(session, "waterEnabled", false);

        Saved(session, "weatherSevere", false); Saved(session, "weatherTemperatureDrop", false);
        Saved(session, "weatherRain", true);
        state = Notifications(session);
        Check(!weatherConsumers.empty() && weatherConsumers.back() && state["runtime"]["weather"] == "waiting",
            "weather opt-in requests a shared consumer and waits for actual data");
        Check(State(session)["leftWidget"] == 0 && State(session)["rightWidget"] == 0,
            "weather notification opt-in does not select a weather widget");
        NoOtherWidgetWindow(); NoMessaging(state);
        context.weather.enabled = context.weather.available = true; context.weather.city = L"广州";
        context.weather.code = 0; context.weather.temperature = 24; context.weather.hours = 2;
        context.weather.hourly[0] = {L"12", 24, 0, true};
        context.weather.hourly[1] = {L"13", 23, 61, true};
        context.weatherAgeMs = 0; context.weatherRevision = 1;
        session.PollNotifications();
        Check(Notifications(session)["runtime"]["weather"] == "active" && Overlay(window),
            "fresh actual hourly weather reaches the native notification surface");
        const auto beforeWeatherRead = files.values;
        Pump(180); ClickMiddle(window);
        Check(Overlay(window) && State(session)["expanded"] == true && weatherSettings == 0 &&
            todoFinds == 0 && todoMutations == 0 && files.values == beforeWeatherRead,
            "weather notice click reads its island detail without settings, todo access or preference changes");
        session.HandlePage(PageCommand::IslandCollapse);
        Check(!Overlay(window) && State(session)["expanded"] == false && weatherSettings == 0,
            "explicit collapse closes weather details without opening weather settings");
        const auto consumerCalls = weatherConsumers.size();
        files.fail = true; state = Set(session, "weatherRain", false);
        Check(state["saved"] == false && state["preferences"]["weatherRain"] == true &&
            weatherConsumers.size() == consumerCalls && weatherConsumers.back(), "failed weather disable keeps the confirmed shared consumer");
        files.fail = false; Saved(session, "weatherRain", false);
        Check(!weatherConsumers.back() && Notifications(session)["runtime"]["weather"] == "off" && !Overlay(window),
            "successful weather disable releases its consumer and clears pending notices");
        Saved(session, "weatherRain", true);
        Check(weatherConsumers.back(), "weather consumer can be enabled again");
        session.HandlePage(PageCommand::IslandDisable);
        Check(!weatherConsumers.back() && Notifications(session)["runtime"]["weather"] == "island-off",
            "island disable releases weather consumption even with the saved opt-in");
        Saved(session, "weatherRain", false);
        Saved(session, "waterEnabled", false); Saved(session, "standEnabled", false);
        Saved(session, "sleepEnabled", false);
        Saved(session, "offWorkTime", "18:00"); Saved(session, "offWorkEnabled", true);
        context.minuteOfDay = 17 * 60 + 59;
        session.HandlePage(PageCommand::IslandEnable); window = session.Handle();
        Check(!Overlay(window) && Notifications(session)["preferences"]["offWorkEnabled"] == true,
            "confirmed work reminder waits for its configured time on the enabled island");
        Advance(session, context, 1);
        Check(Overlay(window) && defaultVoiceCalls == 0, "configured default end-of-work time reaches the native surface silently");
        const auto beforeWorkRead = files.values;
        Pump(180); ClickMiddle(window);
        Check(Overlay(window) && State(session)["expanded"] == true && files.values == beforeWorkRead &&
            todoFinds == 0 && todoMutations == 0,
            "work reminder opens island details without mutating settings or todos");
        NoMessaging(Notifications(session));
        session.HandlePage(PageCommand::IslandCollapse);
        Advance(session, context, 1);
        Check(!Overlay(window), "work reminder is not replayed on subsequent polls that day");
        session.HandlePage(PageCommand::IslandDisable);
        session.Shutdown(); session.PollNotifications();
        Check(!session.Running() && !session.HandleNotifications(LR"({"version":1,"scope":"island-notifications","action":"snapshot"})"),
            "shutdown cannot restart notification delivery");
    }
    Check(defaultVoiceCalls == 0, "ordinary care, weather and default work reminders never request voice playback");
    CheckSquidwardPresentation(policy);
    CheckNotificationDispatcher();
    layer::Stop(); layer::contextReader = {}; CoUninitialize();
}
} // namespace
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto name = L"LumeIslandNotificationSessionTest-" + std::to_wstring(GetCurrentProcessId());
    const HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    Check(desktop != nullptr, "create private desktop without switching user input");
    std::thread worker([&] {
        Check(SetThreadDesktop(desktop) != FALSE, "bind the test thread to the private desktop");
        Gdiplus::GdiplusStartupInput input; ULONG_PTR token = 0;
        Check(Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok, "initialize isolated drawing");
        Scenario(); Gdiplus::GdiplusShutdown(token);
    });
    worker.join(); Check(CloseDesktop(desktop) != FALSE, "release private desktop");
    std::cout << "ISLAND_NOTIFICATION_SESSION_PASS: persisted policy, native transient delivery, weather consumer, rollback and shutdown\n";
}
