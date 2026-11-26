#include "apps/shell/dynamic_island_session.h"
#include "platform/windows/dynamic_island/off_work_voice_captions.h"
#include "apps/shell/island_notification_preferences.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/text_util.h"
#include "platform/windows/dynamic_island/island_window.h"
#include "platform/windows/dynamic_island/island_preview.h"
#include "platform/windows/dynamic_island/notification_receiver.h"
#include "platform/windows/dynamic_island/notification_audio.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "platform/windows/widget_ui/window_layer_policy.h"
#include "vendor/nlohmann/json.hpp"
#include <utility>
#include <algorithm>
#include <chrono>
#include <deque>
#include <atomic>
#include <shellapi.h>

namespace lume::shell {
using namespace platform::windows;
namespace {
struct ReminderVoice {
    NotificationAudio audio;
    std::function<void(bool)> playback;
    std::function<float()> level;
    std::function<std::optional<unsigned>()> position;
    mutable unsigned lastPositionMs = 0;
    unsigned viewers = 0;
    bool played = false, playing = false;
    void Present(bool visible) {
        if (visible) {
            if (viewers++ == 0 && !played) {
                played = true;
                if (playback) { playback(true); playing = true; }
                else playing = audio.PlayOffWork();
                diag::Log("island-notifications", playing ? "off-work-voice=start" : "off-work-voice=unavailable");
            }
        } else if (viewers && --viewers == 0) Stop();
    }
    void Stop() {
        if (!playing) return;
        playing = false;
        if (playback) playback(false);
        else audio.Stop();
    }
    float Level() const { return playing && viewers ? (level ? level() : audio.Level()) : 0.f; }
    bool Playing() const { return playing && viewers && (playback || audio.Playing()); }
    std::wstring Caption(bool english) const {
        if (!played) return {}; // Folding can draw once before its playback lease starts.
        if (played && (!playing || !viewers || (!playback && !audio.Playing())))
            lastPositionMs = NotificationAudio::DurationMilliseconds;
        else if (playing && viewers) {
            const auto actual = position ? position() : (playback ? std::nullopt : audio.PositionMilliseconds());
            if (actual) lastPositionMs = std::max(lastPositionMs, std::min(*actual, NotificationAudio::DurationMilliseconds));
        }
        return std::wstring(OffWorkVoiceCaption(lastPositionMs, english));
    }
    ~ReminderVoice() { Stop(); }
};
}
struct DynamicIslandSession::Impl {
    DynamicIslandSessionOptions options;
    MaterialSelectionHandler materialSelection;
    HINSTANCE instance;
    LocalSettingsFileStore local;
    settings::IFileStore& files;
    IslandNotificationPreferences notificationPreferences;
    IslandReminderScheduler reminderScheduler;
    IslandNotificationReceiver notificationReceiver;
    struct PendingNotification { IslandNotification notice; std::uint64_t expires=0; bool deferredLogged=false; };
    std::deque<PendingNotification> pendingNotifications;
    std::string lastNotificationRuntime;
    NotificationReceiverState lastReceiverState;
    bool notificationsEnabled=false;
    static constexpr UINT NotificationWakeMessage=WM_APP+0x362;
    static constexpr UINT_PTR OffWorkTimer=1;
    HWND notificationWakeWindow=nullptr;
    WPARAM notificationWakeCookie=0;
    std::uint64_t offWorkTimerDue=0;
    widget::Limits limits{260,48,220,44,560,220};
    widget::PreferencesStore store;
    widget::Preferences prefs;
    widget::LoadState load = widget::LoadState::Fresh;
    dynamic_island::Model model;
    IslandWindow window;
    std::vector<std::unique_ptr<IslandWindow>> mirrors;
    std::vector<IslandMonitor> screens;
    ULONGLONG screensChecked=0;
    std::string error;
    IslandDisplay display;
    int lastCountdownNoticeDay=0;
    std::unique_ptr<IslandPreviewRenderer> previewRenderer;
    bool stopped = false, english = false;
    bool weatherRequested = false, previewActive = false;
    // True while the island shows another player's Windows media session.
    bool external = false;
    std::atomic<bool> analysisVisible{false};
    void RefreshAnalysisVisibility(){
        bool visible=false;
        EachWindow([&](auto& target){visible=visible||window_layer::ContentVisible(target.Handle());});
        analysisVisible.store(visible,std::memory_order_relaxed);
    }
    // Play/pause shows at once; players confirm a few hundred ms later. Until
    // a snapshot agrees (or 2.5 s pass) older snapshots do not flip it back.
    std::optional<bool> expectedPlaying;
    ULONGLONG expectedUntil = 0;
    void Expect(dynamic_island::Media& media) {
        if (!expectedPlaying) return;
        if (!media.available || media.playing == *expectedPlaying || GetTickCount64() >= expectedUntil) { expectedPlaying.reset(); return; }
        media.playing = *expectedPlaying;
    }
    Impl(HINSTANCE module, DynamicIslandSessionOptions o, settings::IFileStore* f)
        : options(std::move(o)), instance(module), files(f?*f:local), notificationPreferences(files,options.preferencesPath),
          store(files,limits), prefs(widget::Defaults(limits)),
          window(module,[this](dynamic_island::Action action){Action(action);},options.notificationAllowed,
              options.editWidget) {
        window.SetTodoSource(options.todos);
        const auto loaded=store.Load(options.preferencesPath);prefs=loaded.value;load=loaded.state;
        prefs.locked = true;
        screens=ReadScreens();
        LoadDisplay();
    }
    std::vector<IslandMonitor> ReadScreens() const {
        return options.monitors?options.monitors():ConnectedIslandMonitors();
    }
    int PrimaryScreen() const {
        const auto found=std::find_if(screens.begin(),screens.end(),[](const auto& screen){return screen.primary;});
        return found==screens.end()?1:static_cast<int>(found-screens.begin())+1;
    }
    template<class F> void EachWindow(F action) {
        action(window);for(const auto& mirror:mirrors)action(*mirror);
    }
    bool Expanded() const {
        return window.Expanded()||std::any_of(mirrors.begin(),mirrors.end(),[](const auto& mirror){return mirror->Expanded();});
    }
    static IslandDisplay ReleasedDisplay(IslandDisplay value) {
        // Retired compact selections may remain in existing preferences, but
        // must not render, widen, receive clicks or request telemetry at runtime.
        value.leftWidget=dynamic_island::CompactWidget::None;
        value.rightWidget=dynamic_island::CompactWidget::None;
        // The focus widget is being redesigned. Keep the saved layout untouched
        // while the island and settings expose music, calendar, performance,
        // to-dos and clock; a hidden-only layout falls back to music.
        if(value.expandedWidgets.empty())return value;
        std::vector<dynamic_island::ExpandedWidget> shown;
        for(const auto item:value.expandedWidgets)if(Released(item))shown.push_back(item);
        value.expandedWidgets=shown.empty()?std::vector{dynamic_island::ExpandedWidget::Music}:std::move(shown);
        return value;
    }
    static bool Released(dynamic_island::ExpandedWidget value) {
        return value==dynamic_island::ExpandedWidget::Music||value==dynamic_island::ExpandedWidget::CalendarWeather||
            value==dynamic_island::ExpandedWidget::Todos||value==dynamic_island::ExpandedWidget::Clock||
            value==dynamic_island::ExpandedWidget::System||value==dynamic_island::ExpandedWidget::Countdown;
    }
    void StopWindows() {analysisVisible=false;mirrors.clear();window.Stop();}
    bool ProjectDisplay(const widget::Preferences& next,const IslandDisplay& value) {
        if(!next.enabled){StopWindows();return true;}
        if(screens.empty())return false;
        auto targets=screens;
        if(value.screen!=0){
            const int selected=value.screen>0&&value.screen<=static_cast<int>(screens.size())?value.screen:PrimaryScreen();
            targets={screens[static_cast<std::size_t>(selected-1)]};
        }
        while(mirrors.size()+1<targets.size()){
            const auto mirrorIndex=mirrors.size();
            auto mirror=std::make_unique<IslandWindow>(instance,[this,mirrorIndex](dynamic_island::Action action){
                // Only the first window polls media and the display inventory.
                if(action!=dynamic_island::Action::Tick)Action(action,mirrorIndex<mirrors.size()?mirrors[mirrorIndex]->Handle():nullptr);
            },options.notificationAllowed,options.editWidget);
            mirror->SetTodoSource(options.todos);mirrors.push_back(std::move(mirror));
        }
        const auto released=ReleasedDisplay(value);
        for(std::size_t index=0;index<targets.size();++index){
            auto& target=index==0?window:*mirrors[index-1];
            target.SetMonitor(targets[index]);
            if(!target.SetDisplay(released)||!target.Start(next,released))return false;
        }
        mirrors.resize(targets.size()-1);
        RefreshAnalysisVisibility();
        return true;
    }
    void RefreshScreens() {
        screensChecked=GetTickCount64();
        auto current=ReadScreens();
        if(current.empty()||current==screens)return;
        const auto previous=screens;screens=std::move(current);
        if(prefs.enabled&&!ProjectDisplay(prefs,display)){
            screens=previous;error=ProjectDisplay(prefs,display)?"runtime":"rollback";
        }
        RefreshWeather();Paint();Notify();
    }
    // Display options live beside the shared widget preferences, whose format
    // other widgets use; missing or unreadable files keep the defaults.
    std::string DisplayPath()const {
        auto path=options.preferencesPath;
        if(path.empty())return {};
        const auto dot=path.rfind(".json");
        return dot!=std::string::npos&&dot+5==path.size()?path.substr(0,dot)+"-display.json":path+".display";
    }
    static const char* MaterialName(IslandMaterial value){return value==IslandMaterial::Liquid?"liquid-glass":value==IslandMaterial::Frosted?"frosted-glass":"none";}
    static const char* SizeName(IslandSize size){return size==IslandSize::Small?"small":size==IslandSize::Large?"large":"standard";}
    static const char* ShapeName(IslandShape shape){return shape==IslandShape::Standard?"standard":"capsule";}
    static const char* CalendarStyleName(IslandCalendarStyle style){return style==IslandCalendarStyle::ChineseAlmanac?"chinese-almanac":"default";}
    static const char* CountdownStyleName(IslandCountdownStyle style){
        switch(style){
        case IslandCountdownStyle::Violet:return "violet";
        default:return "standard";
        }
    }
    static const char* ClockStyleName(IslandClockStyle style){
        switch(style){
        case IslandClockStyle::Progress:return "progress";
        case IslandClockStyle::Digital:return "digital";
        case IslandClockStyle::Date:return "date";
        case IslandClockStyle::Flip:return "flip";
        case IslandClockStyle::Pixel:return "pixel";
        default:return "simple";
        }
    }
    static const char* SpeedName(IslandAnimationSpeed speed){return speed==IslandAnimationSpeed::Slow?"slow":speed==IslandAnimationSpeed::Fast?"fast":"normal";}
    static const char* BounceName(IslandBounce bounce){return bounce==IslandBounce::Subtle?"subtle":bounce==IslandBounce::Bouncy?"bouncy":"normal";}
    void LoadDisplay() {
        std::string text;const auto path=DisplayPath();
        // Earlier builds included custom side/workspace data. Ignore those
        // fields while retaining the ordinary display options in the same file.
        if(path.empty()||!files.Exists(path)||!files.ReadBounded(path,8192,text))return;
        const auto json=nlohmann::json::parse(text,nullptr,false);
        if(!json.is_object()||json.value("version",0)!=1)return;
        const auto screen=json.find("screen");
        if(screen!=json.end()&&screen->is_number_integer()&&*screen>=-1&&*screen<=64)display.screen=screen->get<int>();
        const auto flag=[&](const char* key,bool& out){const auto it=json.find(key);if(it!=json.end()&&it->is_boolean())out=it->get<bool>();};
        flag("hideFullscreen",display.hideFullscreen);
        flag("topmost",display.topmost);flag("hoverExpand",display.hoverExpand);flag("autoHide",display.autoHide);
        // Legacy chat-message preferences are intentionally no longer consumed.
        const auto layout=json.find("expandedLayout");
        if(layout!=json.end()&&layout->is_number_integer()&&*layout>=0&&*layout<=dynamic_island::ExpandedLayoutMaximum)
            dynamic_island::DecodeExpandedLayout(layout->get<unsigned>(),display.expandedWidgets);
        const auto delay=json.find("hoverDelayMs");
        if(delay!=json.end()&&delay->is_number_integer()&&*delay>=100&&*delay<=1000)display.hoverDelayMs=SnapIslandHoverDelay(delay->get<unsigned>());
        const auto speed=json.find("animationSpeed");
        if(speed!=json.end()&&speed->is_string()){
            if(*speed=="slow")display.animationSpeed=IslandAnimationSpeed::Slow;
            else if(*speed=="fast")display.animationSpeed=IslandAnimationSpeed::Fast;
        }
        const auto bounce=json.find("bounce");
        if(bounce!=json.end()&&bounce->is_string()){
            if(*bounce=="subtle")display.bounce=IslandBounce::Subtle;
            else if(*bounce=="bouncy")display.bounce=IslandBounce::Bouncy;
        }
        for(const auto* key:{"leftWidget","rightWidget"}){
            const auto field=json.find(key);if(field==json.end()||!field->is_string())continue;
            auto value=dynamic_island::ParseCompactWidget(field->get<std::string>());
            if(value==dynamic_island::CompactWidget::Count)continue;
            if(value==dynamic_island::CompactWidget::Files)value=dynamic_island::CompactWidget::None;
            (std::string_view(key)=="leftWidget"?display.leftWidget:display.rightWidget)=value;
        }
        const auto size=json.value("size",std::string{});
        if(size=="small")display.size=IslandSize::Small;else if(size=="large")display.size=IslandSize::Large;
        else if(size=="standard")display.size=IslandSize::Standard;
        const auto material=json.value("material",std::string{});
        if(material=="liquid-glass")display.material=IslandMaterial::Liquid;
        else if(material=="frosted-glass")display.material=IslandMaterial::Frosted;
        else if(material=="none")display.material=IslandMaterial::None;
        const auto shape=json.find("shape");
        if(shape!=json.end()&&shape->is_string()) {
            if(*shape=="standard")display.shape=IslandShape::Standard;
            else if(*shape=="capsule")display.shape=IslandShape::Capsule;
        }
        const auto clockStyle=json.find("clockStyle");
        if(clockStyle!=json.end()&&clockStyle->is_string()){
            if(*clockStyle=="progress")display.clockStyle=IslandClockStyle::Progress;
            else if(*clockStyle=="digital")display.clockStyle=IslandClockStyle::Digital;
            else if(*clockStyle=="date")display.clockStyle=IslandClockStyle::Date;
            else if(*clockStyle=="flip")display.clockStyle=IslandClockStyle::Flip;
            else if(*clockStyle=="pixel")display.clockStyle=IslandClockStyle::Pixel;
        }
        const auto calendarStyle=json.find("calendarStyle");
        if(calendarStyle!=json.end()&&calendarStyle->is_string()&&*calendarStyle=="chinese-almanac")
            display.calendarStyle=IslandCalendarStyle::ChineseAlmanac;
        const auto countdownStyle=json.find("countdownStyle");
        if(countdownStyle!=json.end()&&countdownStyle->is_string()){
            // Removed layouts from older releases fall back to the standard card.
            if(*countdownStyle=="violet")display.countdownStyle=IslandCountdownStyle::Violet;
        }
        if(const auto day=json.find("lastCountdownNoticeDay");day!=json.end()&&day->is_number_integer()&&*day>0&&*day<=2932896)
            lastCountdownNoticeDay=day->get<int>();
        const auto countdownDate=json.find("countdownDate");
        if(countdownDate!=json.end()&&countdownDate->is_number_integer()&&*countdownDate>=0&&*countdownDate<=2932896)
            display.countdownDate=countdownDate->get<int>();
        const auto countdownCountUp=json.find("countdownCountUp");
        if(countdownCountUp!=json.end()&&countdownCountUp->is_boolean())
            display.countdownCountUp=countdownCountUp->get<bool>();
        const auto countdownTitle=json.find("countdownTitle");
        if(countdownTitle!=json.end()&&countdownTitle->is_string()&&countdownTitle->get<std::string>().size()<=2000)
            display.countdownTitle=countdownTitle->get<std::string>();
        const auto countdownReminder=json.find("countdownReminder");
        if(countdownReminder!=json.end()&&countdownReminder->is_boolean())
            display.countdownReminder=countdownReminder->get<bool>();
        const auto color=[&](const char* key,std::uint32_t& value,unsigned maximum=0xffffff){
            const auto field=json.find(key);
            if(field!=json.end()&&field->is_number_integer()&&*field>=0&&*field<=maximum)
                value=field->get<std::uint32_t>();
        };
        color("countdownTextColor",display.countdownTextColor);
        color("countdownBackgroundColor",display.countdownBackgroundColor);
        color("countdownSolidColor",display.countdownSolidColor);
        color("countdownTextAlpha",display.countdownTextAlpha,255);
        color("countdownBackgroundAlpha",display.countdownBackgroundAlpha,255);
        color("countdownSolidAlpha",display.countdownSolidAlpha,255);
    }
    bool SaveDisplay(const IslandDisplay& next) {
        const auto path=DisplayPath();if(path.empty())return true;
        const auto text=nlohmann::json{{"version",1},{"screen",next.screen},{"hideFullscreen",next.hideFullscreen},
            {"topmost",next.topmost},{"hoverExpand",next.hoverExpand},{"autoHide",next.autoHide},{"size",SizeName(next.size)},
            {"expandedLayout",dynamic_island::EncodeExpandedLayout(next.expandedWidgets)},
            {"material",MaterialName(next.material)},{"shape",ShapeName(next.shape)},{"clockStyle",ClockStyleName(next.clockStyle)},
            {"calendarStyle",CalendarStyleName(next.calendarStyle)},
            {"countdownStyle",CountdownStyleName(next.countdownStyle)},
            {"countdownDate",next.countdownDate},{"lastCountdownNoticeDay",lastCountdownNoticeDay},
            {"countdownCountUp",next.countdownCountUp},
            {"countdownTitle",next.countdownTitle},{"countdownReminder",next.countdownReminder},
            {"countdownTextColor",next.countdownTextColor},{"countdownBackgroundColor",next.countdownBackgroundColor},
            {"countdownSolidColor",next.countdownSolidColor},
            {"countdownTextAlpha",next.countdownTextAlpha},{"countdownBackgroundAlpha",next.countdownBackgroundAlpha},
            {"countdownSolidAlpha",next.countdownSolidAlpha},
            {"hoverDelayMs",next.hoverDelayMs},{"animationSpeed",SpeedName(next.animationSpeed)},{"bounce",BounceName(next.bounce)},
            {"leftWidget",dynamic_island::CompactWidgetId(next.leftWidget)},
            {"rightWidget",dynamic_island::CompactWidgetId(next.rightWidget)}}.dump();
        const auto temporary=path+".tmp";
        if(files.Write(temporary,text)&&files.Rename(temporary,path))return true;
        files.Remove(temporary);return false;
    }
    bool CommitDisplay(const IslandDisplay& next) {
        if(stopped)return false;
        // Apply first: a failed window rebuild must leave the saved file untouched.
        const auto previous=display;
        if(prefs.enabled&&!ProjectDisplay(prefs,next)){error=ProjectDisplay(prefs,previous)?"runtime":"rollback";Paint();return false;}
        if(!SaveDisplay(next)){error=!prefs.enabled||ProjectDisplay(prefs,previous)?"save":"rollback";Paint();return false;}
        display=next;SyncWeatherConsumer(prefs.enabled);RefreshWeather();error.clear();return true;
    }
    void SyncWeatherConsumer(bool enabled) {
        const auto& notifications=notificationPreferences.Snapshot();
        const bool wanted=enabled&&(
            dynamic_island::HasExpandedWidget(ReleasedDisplay(display).expandedWidgets,dynamic_island::ExpandedWidget::CalendarWeather)||
            notifications.weatherSevere||notifications.weatherRain||notifications.weatherTemperatureDrop);
        if(wanted==weatherRequested)return;
        weatherRequested=wanted;
        if(options.weatherActive)options.weatherActive(wanted);
    }
    std::uint64_t Now()const {return options.monotonicMillis?options.monotonicMillis():GetTickCount64();}
    IslandReminderTick NotificationContext() const {
        if(options.notificationContext)return options.notificationContext();
        IslandReminderTick tick;tick.nowMs=Now();tick.english=english;
        SYSTEMTIME localTime{};GetLocalTime(&localTime);
        tick.minuteOfDay=localTime.wHour*60+localTime.wMinute;
        tick.secondOfMinute=localTime.wSecond;
        tick.localDay=calendar::DaysSinceUnixEpoch({localTime.wYear,localTime.wMonth,localTime.wDay});
        LASTINPUTINFO input{sizeof(input)};
        if(GetLastInputInfo(&input))tick.idleMs=static_cast<DWORD>(GetTickCount()-input.dwTime);
        else tick.locked=true;
        const auto desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
        wchar_t name[80]{};DWORD bytes=0;
        if(!desktop)tick.locked=true;
        else {tick.locked=tick.locked||!GetUserObjectInformationW(desktop,UOI_NAME,name,sizeof(name),&bytes)||_wcsicmp(name,L"Default")!=0;CloseDesktop(desktop);}
        tick.weather=options.weather?options.weather():dynamic_island::Weather{};
        tick.weatherRevision=options.weatherUpdatedAt?options.weatherUpdatedAt():0;
        const auto utc=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(tick.weatherRevision&&utc>=0&&tick.weatherRevision<=static_cast<std::uint64_t>(utc))tick.weatherAgeMs=static_cast<std::uint64_t>(utc)-tick.weatherRevision;
        return tick;
    }
    nlohmann::json NotificationRuntime() const {
        const auto& p=notificationPreferences.Snapshot();const auto context=NotificationContext();const auto receiver=notificationReceiver.Snapshot();
        const bool care=p.waterEnabled||p.standEnabled||p.sleepEnabled||p.offWorkEnabled;
        const bool weather=p.weatherSevere||p.weatherRain||p.weatherTemperatureDrop;
        return {{"islandEnabled",notificationsEnabled},{"care",!care?"off":notificationsEnabled?"active":"island-off"},
            {"weather",!weather?"off":!notificationsEnabled?"island-off":context.weather.available&&!context.weather.failed&&context.weatherAgeMs<=IslandReminderScheduler::MaxWeatherAgeMillis?"active":"waiting"},
            {"qq",receiver.qq},{"wechat",receiver.wechat},{"qqRealtime",notificationsEnabled&&receiver.qqRealtime}};
    }
    std::wstring NotificationsState(std::wstring_view base={}) const {
        const auto text=base.empty()?notificationPreferences.State():std::wstring(base);
        auto state=nlohmann::json::parse(text);const auto runtime=NotificationRuntime();
        const auto& p=notificationPreferences.Snapshot();
        const bool receiving=notificationsEnabled&&((p.messagesQQ&&(runtime["qq"]=="connected"||runtime["qq"]=="attention"))||(p.messagesWeChat&&runtime["wechat"]=="listening"));
        state["runtime"]=runtime;state["receiving"]=receiving;state["availability"]=receiving?"ready":"not-connected";
        const auto encoded=state.dump(-1,' ',true);return {encoded.begin(),encoded.end()};
    }
    void NotifyNotificationRuntime(){
        const auto runtime=NotificationRuntime();const auto encoded=runtime.dump();
        if(encoded==lastNotificationRuntime)return;
        lastNotificationRuntime=encoded;
        diag::Log("island-notifications","runtime enabled="+std::string(notificationsEnabled?"true":"false")+
            " qq="+runtime["qq"].get<std::string>()+" wechat="+runtime["wechat"].get<std::string>()+
            " qqMode="+(runtime["qqRealtime"].get<bool>()?std::string("events"):std::string("polling")));
        if(options.notify)options.notify(NotificationsState());
    }
    void ClearNotifications(){pendingNotifications.clear();EachWindow([](auto& target){target.ClearNotifications();});}
    static LRESULT CALLBACK NotificationWakeProcedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
        if(message==WM_NCCREATE){
            const auto create=reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        const auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==NotificationWakeMessage){
            if(self&&wparam==self->notificationWakeCookie)self->PollNotifications();
            return 0;
        }
        if(message==WM_TIMER&&wparam==OffWorkTimer){
            if(self){KillTimer(hwnd,OffWorkTimer);self->offWorkTimerDue=0;self->PollNotifications();}
            return 0;
        }
        if(message==WM_NCDESTROY)SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
        return DefWindowProcW(hwnd,message,wparam,lparam);
    }
    void SyncNotificationWake(bool enabled){
        if(!enabled){
            notificationReceiver.SetWakeTarget(nullptr,0,0);
            if(notificationWakeWindow)DestroyWindow(notificationWakeWindow);
            notificationWakeWindow=nullptr;notificationWakeCookie=0;
            offWorkTimerDue=0;
            return;
        }
        if(notificationWakeWindow)return;
        WNDCLASSW type{};type.hInstance=instance;type.lpfnWndProc=NotificationWakeProcedure;
        type.lpszClassName=L"LumeIslandNotificationDispatch";
        if(!RegisterClassW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
        static std::atomic<WPARAM> nextCookie{1};
        notificationWakeCookie=nextCookie.fetch_add(1,std::memory_order_relaxed);
        notificationWakeWindow=CreateWindowExW(0,type.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,this);
        if(notificationWakeWindow)notificationReceiver.SetWakeTarget(notificationWakeWindow,NotificationWakeMessage,notificationWakeCookie);
        // The regular island tick remains a fallback if the dispatcher cannot
        // be created. The receiver never invokes session code on its worker.
    }
    void ScheduleOffWorkTimer(){
        if(!notificationWakeWindow)return;
        const auto& p=notificationPreferences.Snapshot();
        if(!notificationsEnabled||!p.offWorkEnabled||p.offWorkStyle!="squidward"||options.notificationContext){
            KillTimer(notificationWakeWindow,OffWorkTimer);offWorkTimerDue=0;return;
        }
        SYSTEMTIME clockTime{};GetLocalTime(&clockTime);
        constexpr unsigned dayMs=86400000;
        const unsigned target=((static_cast<unsigned>(p.offWorkTime[0]-'0')*10+static_cast<unsigned>(p.offWorkTime[1]-'0'))*3600+
            (static_cast<unsigned>(p.offWorkTime[3]-'0')*10+static_cast<unsigned>(p.offWorkTime[4]-'0'))*60+86400-5)%86400*1000;
        const unsigned current=((clockTime.wHour*60u+clockTime.wMinute)*60u+clockTime.wSecond)*1000u+clockTime.wMilliseconds;
        unsigned delay=(target+dayMs-current)%dayMs;if(!delay)delay=dayMs;
        const auto due=Now()+delay;
        if(offWorkTimerDue&&(due>offWorkTimerDue?due-offWorkTimerDue:offWorkTimerDue-due)<=25)return;
        if(SetTimer(notificationWakeWindow,OffWorkTimer,delay,nullptr))offWorkTimerDue=due;
        else offWorkTimerDue=0;
    }
    void SyncNotifications(bool enabled,bool clear=false){
        notificationsEnabled=enabled;
        const auto& p=notificationPreferences.Snapshot();
        SyncNotificationWake(enabled&&(p.messagesQQ||p.messagesWeChat||(p.offWorkEnabled&&p.offWorkStyle=="squidward")));
        notificationReceiver.Configure({enabled,p.messagesQQ,p.messagesWeChat,p.messagePreview});
        if(clear||!enabled)ClearNotifications();
        if(!enabled){auto configured=p;configured.waterEnabled=configured.standEnabled=configured.sleepEnabled=configured.offWorkEnabled=false;
            configured.weatherSevere=configured.weatherRain=configured.weatherTemperatureDrop=false;reminderScheduler.Tick(configured,NotificationContext());}
        else PollNotifications();
        NotifyNotificationRuntime();
    }
    void QueueNotification(IslandNotification notice){
        pendingNotifications.erase(std::remove_if(pendingNotifications.begin(),pendingNotifications.end(),[&](const auto& entry){
            return entry.notice.id==notice.id||(!notice.coalesceKey.empty()&&entry.notice.coalesceKey==notice.coalesceKey);
        }),pendingNotifications.end());
        if(pendingNotifications.size()>=10){
            const auto oldest=std::find_if(pendingNotifications.begin(),pendingNotifications.end(),[](const auto& entry){return !entry.notice.immediate;});
            if(oldest!=pendingNotifications.end())pendingNotifications.erase(oldest);
            else if(!notice.immediate)return;
            else pendingNotifications.pop_back();
        }
        if(notice.immediate)pendingNotifications.push_front({std::move(notice),Now()+1000});
        else pendingNotifications.push_back({std::move(notice),Now()+120000});
    }
    void PollNotifications(){
        if(stopped)return;
        const auto& p=notificationPreferences.Snapshot();
        if(!notificationsEnabled)return;
        ScheduleOffWorkTimer();
        auto tick=NotificationContext();
        const auto receiverState=notificationReceiver.Snapshot();
        if(((lastReceiverState.qq=="connected"||lastReceiverState.qq=="attention")&&receiverState.qq!="connected"&&receiverState.qq!="attention")||
            (lastReceiverState.wechat=="listening"&&receiverState.wechat!="listening"))ClearNotifications();
        lastReceiverState=receiverState;
        for(auto& reminder:reminderScheduler.Tick(p,tick)){
            constexpr const char* reminderIcons[]={"water","stand","sleep","severe","rain","temperature","off-work"};
            IslandNotification notice{std::move(reminder.id),std::move(reminder.title),std::move(reminder.body),{},reminderIcons[static_cast<std::size_t>(reminder.kind)]};
            if(reminder.kind==IslandReminderKind::OffWork&&reminder.style=="squidward"){
                auto voice=std::make_shared<ReminderVoice>();voice->playback=options.reminderVoice;voice->level=options.reminderVoiceLevel;
                voice->position=options.reminderVoicePosition;
                notice.icon="off-work-squidward";notice.immediate=true;notice.durationMs=18000;
                notice.presented=[voice](bool visible){voice->Present(visible);};
                notice.audioLevel=[voice]{return voice->Level();};
                notice.audioPlaying=[voice]{return voice->Playing();};
                notice.voiceCaption=[voice,language=english]{return voice->Caption(language);};
            }
            QueueNotification(std::move(notice));
        }
        for(auto& message:notificationReceiver.Drain()){
            if(message.source=="qq"?!p.messagesQQ:!p.messagesWeChat)continue;
            const auto source=message.source;
            diag::Log("island-notifications","received source="+source);
            if(message.title.empty())message.title=source=="qq"?(english?L"QQ notification":L"QQ 通知"):(english?L"WeChat alert":L"微信提醒");
            if(message.body.empty())message.body=source=="qq"?(receiverState.qq=="attention"?(english?L"QQ needs your attention. Click to open.":L"QQ 请求你的注意，轻点打开应用"):(english?L"New notification. Click to open QQ.":L"有新通知，轻点打开 QQ")):(english?L"WeChat needs your attention. Click to open.":L"微信请求你的注意，轻点打开应用");
            QueueNotification({std::move(message.id),std::move(message.title),std::move(message.body),[this,source]{notificationReceiver.OpenApp(source);},source,source});
        }
        const auto now=Now();
        while(!pendingNotifications.empty()&&pendingNotifications.front().expires<=now){
            diag::Log("island-notifications","expired type="+pendingNotifications.front().notice.icon);
            pendingNotifications.pop_front();
        }
        if(tick.locked)ClearNotifications();
        else if(!pendingNotifications.empty()){
            bool shown=false;EachWindow([&](auto& target){shown=target.ShowNotification(pendingNotifications.front().notice)||shown;});
            if(shown){diag::Log("island-notifications","accepted type="+pendingNotifications.front().notice.icon);pendingNotifications.pop_front();}
            else if(pendingNotifications.front().notice.immediate){
                diag::Log("island-notifications","timed-voice-skipped type="+pendingNotifications.front().notice.icon);
                pendingNotifications.pop_front();
            }
            else if(!pendingNotifications.front().deferredLogged){
                pendingNotifications.front().deferredLogged=true;
                diag::Log("island-notifications","deferred type="+pendingNotifications.front().notice.icon);
            }
        }
        else if(display.countdownReminder&&display.countdownDate>0&&tick.localDay==display.countdownDate&&
                lastCountdownNoticeDay!=display.countdownDate&&dynamic_island::HasExpandedWidget(display.expandedWidgets,dynamic_island::ExpandedWidget::Countdown)){
            IslandNotification notice{"countdown-"+std::to_string(display.countdownDate),
                display.countdownTitle.empty()?(english?L"Countdown":L"倒数日"):lume::text::Wide(display.countdownTitle),
                english?L"Today is the target date.":L"今天是目标日。"};
            bool shown=false;
            EachWindow([&](auto& target){shown=target.ShowNotification(notice)||shown;});
            if(shown){lastCountdownNoticeDay=display.countdownDate;SaveDisplay(display);}
        }
        NotifyNotificationRuntime();
    }
    std::optional<std::wstring> HandleNotifications(std::wstring_view raw){
        if(stopped||raw.size()>1024||raw.find(L"island-notifications")==std::wstring_view::npos)return {};
        const auto command=nlohmann::json::parse(raw,nullptr,false);
        if(command.is_object()&&command.size()==3&&command.value("version",nlohmann::json{})==1&&
            command.value("scope",nlohmann::json{})=="island-notifications"&&command.contains("action")&&command["action"].is_string()){
            const auto action=command["action"].get<std::string>();
            if(action=="authorize"||action=="open-system-settings"){
                // Older pages may still send these commands. Acknowledge them
                // without opening consent or Settings UI.
                auto reply=nlohmann::json::parse(NotificationsState());reply["result"]="applied";reply["saved"]=false;
                const auto encoded=reply.dump(-1,' ',true);return std::wstring(encoded.begin(),encoded.end());
            }
        }
        auto reply=notificationPreferences.Handle(raw);if(!reply)return {};
        const auto result=nlohmann::json::parse(*reply);
        if(result.value("saved",false)){SyncNotifications(prefs.enabled,true);SyncWeatherConsumer(prefs.enabled);}
        return NotificationsState(*reply);
    }
    const char* LoadStatus()const {
        switch(load) {
        case widget::LoadState::Loaded:return "loaded";
        case widget::LoadState::Recovered:return "recovered";
        case widget::LoadState::Failed:return "failed";
        default:return "fresh";
        }
    }
    std::wstring State(bool applied=false,bool saved=false)const {
        auto ids=nlohmann::json::array();
        for(std::size_t index=0;index<screens.size();++index)ids.push_back(index+1);
        if(ids.empty())ids.push_back(1);
        const auto json=nlohmann::json{{"version",1},{"type","dynamicIsland-state"},
            {"enabled",prefs.enabled},{"running",window.Handle()!=nullptr},{"locked",prefs.locked},
            {"expanded",Expanded()},{"focusing",model.Focusing()},{"finished",model.Finished()},
            {"screen",display.screen<0?PrimaryScreen():display.screen},{"screens",std::move(ids)},
            {"remaining",model.RemainingSeconds()},{"hideFullscreen",display.hideFullscreen},
            {"topmost",display.topmost},{"hover",display.hoverExpand},{"autoHide",display.autoHide},{"size",SizeName(display.size)},
            {"material",MaterialName(display.material)},{"shape",ShapeName(display.shape)},{"clockStyle",ClockStyleName(display.clockStyle)},
            {"calendarStyle",CalendarStyleName(display.calendarStyle)},
            {"countdownStyle",CountdownStyleName(display.countdownStyle)},
            {"countdownDate",display.countdownDate},
            {"countdownTitle",display.countdownTitle},{"countdownReminder",display.countdownReminder},
            {"hoverDelayMs",display.hoverDelayMs},{"animationSpeed",SpeedName(display.animationSpeed)},{"bounce",BounceName(display.bounce)},
            {"leftWidget",static_cast<int>(display.leftWidget)},{"rightWidget",static_cast<int>(display.rightWidget)},
            {"expandedLayout",dynamic_island::EncodeExpandedLayout(ReleasedDisplay(display).expandedWidgets)},
            {"loadStatus",LoadStatus()},{"error",error},
            {"result",applied?"applied":"snapshot"},{"saved",saved}}.dump();
        return lume::text::Wide(json);
    }
    void Notify() {if(!stopped&&options.notify)options.notify(State());}
    void Paint() {
        SYSTEMTIME time{};GetLocalTime(&time);
        wchar_t clock[16]{},date[96]{};
        swprintf_s(clock,L"%02u:%02u",time.wHour,time.wMinute);
        GetDateFormatEx(english?L"en-US":L"zh-CN",DATE_LONGDATE,&time,nullptr,date,96,nullptr);
        const IslandPresentation view{model.Playback(),clock,date,model.RemainingSeconds(),model.Focusing(),model.Finished(),english};
        EachWindow([&](auto& target){target.Update(view);});
    }
    bool Runtime(const widget::Preferences& next) {
        if(!ProjectDisplay(next,display))return false;
        SyncWeatherConsumer(next.enabled);
        RefreshWeather();Paint();return true;
    }
    void RefreshWeather() {
        if(stopped)return;
        const auto weather=options.weather?options.weather():dynamic_island::Weather{};
        EachWindow([&](auto& target){target.SetWeather(weather);});
    }
    bool Commit(widget::Preferences next) {
        if(stopped)return false;
        next.locked = true;
        next.placed = false;
        const auto previous=prefs;
        if(!Runtime(next)) {Runtime(previous);error="runtime";return false;}
        if(!store.Save(options.preferencesPath,next)) {
            error=Runtime(previous)?"save":"rollback";return false;
        }
        prefs=next;load=widget::LoadState::Loaded;error.clear();
        SyncNotifications(prefs.enabled,true);
        if(!prefs.enabled)model.CancelFocus();
        return true;
    }
    bool Restore() {const bool restored=!stopped&&Runtime(prefs);if(restored)SyncNotifications(prefs.enabled);return restored;}
    void Action(dynamic_island::Action action,HWND source=nullptr) {
        if(stopped)return;
        using dynamic_island::Action;
        switch(action) {
        case Action::ExpansionChanged:RefreshWeather();Notify();return;
        case Action::Tick:{
            RefreshAnalysisVisibility();
            PollNotifications();
            const bool focusChanged=model.Tick(Now());
            if (window_layer::balancedIdle) return;
            RefreshScreens();
            if(focusChanged)Paint();
            if(!analysisVisible.load(std::memory_order_relaxed))return;
            // Lume Music has priority; otherwise follow the current Windows media
            // session every second so position, track and state stay current.
            if((model.Playback().available&&!external)||!options.externalMedia)return;
            if(auto media=options.externalMedia()) { external=true;Expect(*media);model.Update(std::move(*media)); Paint(); }
            else if(external) { external=false;model.Update({});Paint(); }
            return;
        }
        case Action::Settings:if(options.openSettings)options.openSettings();return;
        case Action::WeatherSettings:if(options.openWeatherSettings)options.openWeatherSettings();return;
        case Action::PerformanceSettings:if(options.openPerformanceSettings)options.openPerformanceSettings();return;
        case Action::Close:{auto next=prefs;next.enabled=false;Commit(next);Notify();return;}
        case Action::MaterialNone:case Action::MaterialLiquid:case Action::MaterialFrosted:{
            const auto request=materialSelection;
            if(request){request(action==Action::MaterialLiquid?ui::PageCommand::IslandMaterialLiquid:
                action==Action::MaterialFrosted?ui::PageCommand::IslandMaterialFrosted:ui::PageCommand::IslandMaterialNone,source?source:window.Handle());return;}
            auto value=display;
            value.material=action==Action::MaterialLiquid?IslandMaterial::Liquid:
                action==Action::MaterialFrosted?IslandMaterial::Frosted:IslandMaterial::None;
            if(CommitDisplay(value))Paint();
            Notify();return;
        }
        case Action::Reset:{if(const auto reply=HandlePage(ui::PageCommand::IslandReset);reply&&options.notify)options.notify(*reply);return;}
        case Action::Focus:case Action::CancelFocus:
            if(!prefs.enabled||!dynamic_island::HasExpandedWidget(ReleasedDisplay(display).expandedWidgets,dynamic_island::ExpandedWidget::Focus))return;
            if(action==Action::Focus)model.StartFocus(Now());else model.CancelFocus();Paint();Notify();return;
        default:{
            if(!prefs.enabled)return;
            const bool toggle=action==Action::Toggle&&model.Playback().available;
            const bool play=toggle&&!model.Playback().playing;
            if(model.Playback().available&&!external&&options.control)options.control(action,model.Playback());
            else if(toggle&&options.externalSetPlaying)options.externalSetPlaying(play);
            else if(options.externalControl)options.externalControl(action);
            if(toggle){
                expectedPlaying=play;expectedUntil=GetTickCount64()+2500;
                auto media=model.Playback();media.playing=play;model.Update(std::move(media));Paint();
            }
            return;
        }
        }
    }
    std::optional<std::wstring> HandlePage(ui::PageCommand command) {
        if(stopped)return {};
        using ui::PageCommand;
        auto next=prefs;
        const int number=static_cast<int>(command);
        if(number>=static_cast<int>(PageCommand::IslandExpandedLayoutFirst)&&number<=static_cast<int>(PageCommand::IslandExpandedLayoutLast)){
            auto value=display;
            if(!dynamic_island::DecodeExpandedLayout(static_cast<unsigned>(number-static_cast<int>(PageCommand::IslandExpandedLayoutFirst)),value.expandedWidgets))return {};
            if(!std::all_of(value.expandedWidgets.begin(),value.expandedWidgets.end(),Released))return State(true,false);
            const bool saved=CommitDisplay(value);
            if(saved){load=widget::LoadState::Loaded;
                if(!dynamic_island::HasExpandedWidget(display.expandedWidgets,dynamic_island::ExpandedWidget::Focus))model.CancelFocus();
                Paint();}
            return State(true,saved);
        }
        if(number>=static_cast<int>(PageCommand::IslandCountdownDateFirst)&&number<=static_cast<int>(PageCommand::IslandCountdownDateLast)){
            auto value=display;value.countdownDate=number-static_cast<int>(PageCommand::IslandCountdownDateFirst);
            const bool saved=CommitDisplay(value);if(saved){load=widget::LoadState::Loaded;Paint();}
            return State(true,saved);
        }
        if(number>=static_cast<int>(PageCommand::IslandHoverDelayFirst)&&number<=static_cast<int>(PageCommand::IslandHoverDelayLast)){
            auto value=display;value.hoverDelayMs=SnapIslandHoverDelay(100+number-static_cast<int>(PageCommand::IslandHoverDelayFirst));
            const bool saved=CommitDisplay(value);if(saved){load=widget::LoadState::Loaded;Paint();}
            return State(true,saved);
        }
        if(number>=static_cast<int>(PageCommand::IslandScreenFirst)&&number<=static_cast<int>(PageCommand::IslandScreenLast)){
            RefreshScreens();
            const int screen=number-static_cast<int>(PageCommand::IslandScreenFirst);
            if(screen>static_cast<int>(screens.size())){error="screen";return State(true,false);}
            auto value=display;value.screen=screen;
            const bool saved=CommitDisplay(value);if(saved){load=widget::LoadState::Loaded;Paint();}
            return State(true,saved);
        }
        const bool left=number>=static_cast<int>(PageCommand::IslandLeftWidgetFirst)&&number<=static_cast<int>(PageCommand::IslandLeftWidgetLast);
        const bool right=number>=static_cast<int>(PageCommand::IslandRightWidgetFirst)&&number<=static_cast<int>(PageCommand::IslandRightWidgetLast);
        if(left||right){
            auto value=display;
            const int first=static_cast<int>(left?PageCommand::IslandLeftWidgetFirst:PageCommand::IslandRightWidgetFirst);
            auto widget=static_cast<dynamic_island::CompactWidget>(number-first);
            if(widget==dynamic_island::CompactWidget::Files)widget=dynamic_island::CompactWidget::None;
            if(left)value.leftWidget=widget;else value.rightWidget=widget;
            const bool saved=CommitDisplay(value);
            if(saved){load=widget::LoadState::Loaded;Paint();}
            return State(true,saved);
        }
        switch(command) {
        case PageCommand::Ready:RefreshScreens();return State();
        case PageCommand::IslandPreviewStop:ReleasePreview();return std::wstring{};
        case PageCommand::IslandPreviewCompact:case PageCommand::IslandPreviewExpanded:{
            if(GetTickCount64()-screensChecked>=1000)RefreshScreens();
            const bool expanded=command==PageCommand::IslandPreviewExpanded;
            previewActive=true;SyncWeatherConsumer(prefs.enabled);
            RefreshWeather();
            if(!previewRenderer)previewRenderer=std::make_unique<IslandPreviewRenderer>();
            const auto foreground=window.Preview(ReleasedDisplay(display),expanded,1.f/30);
            const auto frame=previewRenderer->Render(foreground,ReleasedDisplay(display),options.previewWallpaper?options.previewWallpaper():std::wstring{});
            const auto image=IslandPreviewRenderer::JpegDataUrl(frame);
            const auto readings=nlohmann::json::parse(window.CompactReadings());
            const auto value=nlohmann::json{{"version",1},{"type","dynamicIsland-preview"},{"mode",expanded?"expanded":"compact"},
                {"shape",ShapeName(display.shape)},{"material",MaterialName(display.material)},{"size",SizeName(display.size)},
                {"clockStyle",ClockStyleName(display.clockStyle)},
                {"calendarStyle",CalendarStyleName(display.calendarStyle)},
                {"animationSpeed",SpeedName(display.animationSpeed)},{"bounce",BounceName(display.bounce)},
                {"width",image.empty()?0u:frame.width},{"height",image.empty()?0u:frame.height},{"image",image},
                {"animating",foreground.animating&&!image.empty()},
                {"leftWidget",static_cast<int>(display.leftWidget)},{"rightWidget",static_cast<int>(display.rightWidget)},
                {"expandedLayout",dynamic_island::EncodeExpandedLayout(ReleasedDisplay(display).expandedWidgets)},
                {"readings",readings},{"error",image.empty()?"render":""}}.dump(-1,' ',true);
            return std::wstring(value.begin(),value.end());
        }
        case PageCommand::IslandEnable:next.enabled=true;break;
        case PageCommand::IslandDisable:next.enabled=false;break;
        case PageCommand::IslandLock:case PageCommand::IslandUnlock:next.locked=true;break;
        case PageCommand::IslandReset:{
            next=widget::Defaults(limits);next.enabled=prefs.enabled;
            const bool shown=CommitDisplay(IslandDisplay{});const bool saved=Commit(next)&&shown;
            if(saved){model.CancelFocus();Paint();}return State(true,saved);
        }
        case PageCommand::IslandHideFullscreenOn:case PageCommand::IslandHideFullscreenOff:
        case PageCommand::IslandTopmostOn:case PageCommand::IslandTopmostOff:
        case PageCommand::IslandHoverOn:case PageCommand::IslandHoverOff:
        case PageCommand::IslandAutoHideOn:case PageCommand::IslandAutoHideOff:
        case PageCommand::IslandAnimationSlow:case PageCommand::IslandAnimationNormal:case PageCommand::IslandAnimationFast:
        case PageCommand::IslandBounceSubtle:case PageCommand::IslandBounceNormal:case PageCommand::IslandBounceBouncy:
        case PageCommand::IslandSizeSmall:case PageCommand::IslandSizeStandard:case PageCommand::IslandSizeLarge:
        case PageCommand::IslandMaterialNone:case PageCommand::IslandMaterialLiquid:case PageCommand::IslandMaterialFrosted:
        case PageCommand::IslandClockSimple:case PageCommand::IslandClockProgress:
        case PageCommand::IslandClockDigital:case PageCommand::IslandClockDate:
        case PageCommand::IslandClockFlip:case PageCommand::IslandClockPixel:
        case PageCommand::IslandCalendarDefault:case PageCommand::IslandCalendarChineseAlmanac:
        case PageCommand::IslandCountdownStandard:case PageCommand::IslandCountdownViolet:
        case PageCommand::IslandShapeStandard:case PageCommand::IslandShapeCapsule:case PageCommand::IslandResetAppearance:{
            auto value=display;
            switch(command) {
            case PageCommand::IslandHideFullscreenOn:value.hideFullscreen=true;break;
            case PageCommand::IslandHideFullscreenOff:value.hideFullscreen=false;break;
            case PageCommand::IslandTopmostOn:value.topmost=true;break;
            case PageCommand::IslandTopmostOff:value.topmost=false;break;
            case PageCommand::IslandHoverOn:value.hoverExpand=true;break;
            case PageCommand::IslandHoverOff:value.hoverExpand=false;break;
            case PageCommand::IslandAnimationSlow:value.animationSpeed=IslandAnimationSpeed::Slow;break;
            case PageCommand::IslandAnimationNormal:value.animationSpeed=IslandAnimationSpeed::Normal;break;
            case PageCommand::IslandAnimationFast:value.animationSpeed=IslandAnimationSpeed::Fast;break;
            case PageCommand::IslandBounceSubtle:value.bounce=IslandBounce::Subtle;break;
            case PageCommand::IslandBounceNormal:value.bounce=IslandBounce::Normal;break;
            case PageCommand::IslandBounceBouncy:value.bounce=IslandBounce::Bouncy;break;
            case PageCommand::IslandAutoHideOn:value.autoHide=true;break;
            case PageCommand::IslandAutoHideOff:value.autoHide=false;break;
            case PageCommand::IslandSizeSmall:value.size=IslandSize::Small;break;
            case PageCommand::IslandSizeLarge:value.size=IslandSize::Large;break;
            case PageCommand::IslandSizeStandard:value.size=IslandSize::Standard;break;
            case PageCommand::IslandMaterialNone:value.material=IslandMaterial::None;break;
            case PageCommand::IslandMaterialLiquid:value.material=IslandMaterial::Liquid;break;
            case PageCommand::IslandMaterialFrosted:value.material=IslandMaterial::Frosted;break;
            case PageCommand::IslandShapeStandard:value.shape=IslandShape::Standard;break;
            case PageCommand::IslandShapeCapsule:value.shape=IslandShape::Capsule;break;
            case PageCommand::IslandClockSimple:value.clockStyle=IslandClockStyle::Simple;break;
            case PageCommand::IslandClockProgress:value.clockStyle=IslandClockStyle::Progress;break;
            case PageCommand::IslandClockDigital:value.clockStyle=IslandClockStyle::Digital;break;
            case PageCommand::IslandClockDate:value.clockStyle=IslandClockStyle::Date;break;
            case PageCommand::IslandClockFlip:value.clockStyle=IslandClockStyle::Flip;break;
            case PageCommand::IslandClockPixel:value.clockStyle=IslandClockStyle::Pixel;break;
            case PageCommand::IslandCalendarDefault:value.calendarStyle=IslandCalendarStyle::Default;break;
            case PageCommand::IslandCalendarChineseAlmanac:value.calendarStyle=IslandCalendarStyle::ChineseAlmanac;break;
            case PageCommand::IslandCountdownStandard:value.countdownStyle=IslandCountdownStyle::Standard;break;
            case PageCommand::IslandCountdownViolet:value.countdownStyle=IslandCountdownStyle::Violet;break;
            case PageCommand::IslandResetAppearance:value.shape=IslandShape::Capsule;value.size=IslandSize::Standard;value.material=IslandMaterial::None;break;
            default:break;
            }
            const bool saved=CommitDisplay(value);
            if(saved){load=widget::LoadState::Loaded;Paint();}
            return State(true,saved);
        }
        case PageCommand::IslandExpand:case PageCommand::IslandCollapse:
            EachWindow([&](auto& target){target.Expand(command==PageCommand::IslandExpand);});return State(true);
        case PageCommand::IslandFocus:case PageCommand::IslandStopFocus:
            if(!prefs.enabled||!dynamic_island::HasExpandedWidget(ReleasedDisplay(display).expandedWidgets,dynamic_island::ExpandedWidget::Focus))return {};
            Action(command==PageCommand::IslandFocus?dynamic_island::Action::Focus:dynamic_island::Action::CancelFocus);return State(true);
        default:return {};
        }
        const bool saved=Commit(next);return State(true,saved);
    }
    void Open() {if(stopped)return;auto next=prefs;next.enabled=true;if(Commit(next))EachWindow([](auto& target){target.Expand(true);});Notify();}
    void ReleasePreview(){previewRenderer.reset();window.ReleasePreview();previewActive=false;SyncWeatherConsumer(prefs.enabled);}
    void Shutdown() {if(stopped)return;ClearNotifications();notificationReceiver.SetWakeTarget(nullptr,0,0);notificationReceiver.Shutdown();SyncNotificationWake(false);notificationsEnabled=false;ReleasePreview();EachWindow([](auto& target){target.SetTodoSource({});});StopWindows();SyncWeatherConsumer(false);stopped=true;model.CancelFocus();options.notify={};options.control={};options.openSettings={};options.editWidget={};options.weatherActive={};}
    ~Impl(){Shutdown();}
};
DynamicIslandSession::DynamicIslandSession(HINSTANCE instance,DynamicIslandSessionOptions options,settings::IFileStore* files)
    : impl_(std::make_unique<Impl>(instance,std::move(options),files)) {}
DynamicIslandSession::~DynamicIslandSession()=default;
bool DynamicIslandSession::Restore(){const bool ok=impl_->Restore();if(!ok&&!impl_->stopped)impl_->error="runtime";return ok;}
void DynamicIslandSession::SetMaterialSelectionHandler(MaterialSelectionHandler handler){impl_->materialSelection=std::move(handler);}
void DynamicIslandSession::Open(){impl_->Open();}
std::optional<std::wstring> DynamicIslandSession::HandlePage(ui::PageCommand command){return impl_->HandlePage(command);}
std::optional<std::wstring> DynamicIslandSession::HandleNotifications(std::wstring_view message){
    return impl_->HandleNotifications(message);
}
std::wstring DynamicIslandSession::NotificationsState()const{return impl_->NotificationsState();}
void DynamicIslandSession::PollNotifications(){impl_->PollNotifications();}
std::wstring DynamicIslandSession::State()const{return impl_->State();}
void DynamicIslandSession::Update(dynamic_island::Media media){
    if(impl_->stopped)return;
    // Keep a Windows media session on screen while Lume has nothing to play.
    if(!media.available&&impl_->external)return;
    if(media.available)impl_->external=false;
    impl_->Expect(media);
    const auto old=impl_->model.Playback();impl_->model.Update(std::move(media));
    const auto& next=impl_->model.Playback();
    if(old.session!=next.session||old.id!=next.id||old.artwork!=next.artwork||old.title!=next.title||old.artist!=next.artist||
       old.playing!=next.playing||old.available!=next.available||old.duration!=next.duration||old.lyrics!=next.lyrics||
       static_cast<std::uint64_t>(old.position)!=static_cast<std::uint64_t>(next.position)||
       old.energy!=next.energy||old.spectrum!=next.spectrum)impl_->Paint();
}
void DynamicIslandSession::SetEnglish(bool english){impl_->english=english;if(!impl_->stopped)impl_->Paint();}
void DynamicIslandSession::Shutdown(){impl_->Shutdown();}
bool DynamicIslandSession::ShowNotice(const std::string& id,const std::wstring& title,const std::wstring& time){
    if(impl_->stopped||!impl_->prefs.enabled)return false;
    bool shown=false;impl_->EachWindow([&](auto& target){shown=target.ShowNotice(id,title,time)||shown;});return shown;
}
void DynamicIslandSession::RefreshTodos(){if(!impl_->stopped)impl_->EachWindow([](auto& target){target.RefreshTodos();});}
void DynamicIslandSession::RefreshWeather(){impl_->RefreshWeather();}
IslandCountdownSettings DynamicIslandSession::CountdownSettings()const{
    const auto& display=impl_->display;
    return {display.countdownTitle,display.countdownDate,display.countdownReminder,display.countdownStyle,
        display.countdownTextColor,display.countdownBackgroundColor,display.countdownSolidColor,
        display.countdownTextAlpha,display.countdownBackgroundAlpha,display.countdownSolidAlpha,display.countdownCountUp};
}
bool DynamicIslandSession::SaveCountdownSettings(IslandCountdownSettings settings){
    if(impl_->stopped||settings.date<0||settings.date>2932896||settings.title.size()>2000||
        settings.textColor>0xffffff||settings.backgroundColor>0xffffff||settings.solidColor>0xffffff||
        settings.textAlpha>255||settings.backgroundAlpha>255||settings.solidAlpha>255)return false;
    auto next=impl_->display;next.countdownTitle=std::move(settings.title);next.countdownDate=settings.date;
    next.countdownCountUp=settings.countUp;
    next.countdownReminder=settings.reminder;
    next.countdownTextColor=settings.textColor;next.countdownBackgroundColor=settings.backgroundColor;
    next.countdownSolidColor=settings.solidColor;
    next.countdownTextAlpha=settings.textAlpha;next.countdownBackgroundAlpha=settings.backgroundAlpha;
    next.countdownSolidAlpha=settings.solidAlpha;
    if(!impl_->CommitDisplay(next))return false;
    impl_->load=widget::LoadState::Loaded;impl_->Paint();impl_->Notify();return true;
}
void DynamicIslandSession::ReleasePreview(){impl_->ReleasePreview();}
bool DynamicIslandSession::Running()const{return impl_->window.Handle()!=nullptr;}
bool DynamicIslandSession::AnalysisActive()const{return impl_->analysisVisible.load(std::memory_order_relaxed);}
HWND DynamicIslandSession::Handle()const{return impl_->window.Handle();}
const widget::Preferences& DynamicIslandSession::Snapshot()const{return impl_->prefs;}
}
