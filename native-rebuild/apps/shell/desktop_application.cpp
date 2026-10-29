#include "apps/shell/desktop_application.h"
#include "platform/windows/text_util.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/ui_stall_trace.h"
#include "apps/shell/calendar_session.h"
#include "platform/windows/music_desktop_controls.h"
#include "platform/windows/calendar/calendar_todo_editor.h"
#include "core/todo/todo_time.h"
#include "apps/shell/performance_session.h"
#include "apps/shell/spectrum_session.h"
#include "apps/shell/resource_snapshot.h"
#include "apps/shell/almanac_session.h"
#include "apps/shell/today_todo_session.h"
#include "apps/shell/weather_session.h"
#include "platform/windows/weather/weather_view.h"
#include "apps/shell/organizer_session.h"
#include "apps/shell/desktop_icons_session.h"
#include "apps/shell/taskbar_session.h"
#include "apps/shell/minimize_animation_session.h"
#include "apps/shell/music_preferences_session.h"
#include "apps/shell/notes_session.h"
#include "core/settings/json_settings_codec.h"
#include "core/settings/settings_store.h"
#include "apps/shell/diagnostic_operations.h"
#include "apps/shell/diagnostic_report.h"
#include "apps/shell/self_check.h"
#include "core/application_settings/application_settings.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "apps/shell/island_adapter.h"
#include "apps/shell/external_lyrics.h"
#include "core/ui/clock_page_state.h"
#include "core/ui/system_feature_page_commands.h"
#include "core/ui/music_library_protocol.h"
#include "platform/windows/clock_runtime.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/music_login_diagnosis.h"
#include "platform/windows/music_score_source.h"
#include "platform/windows/webview_page_window.h"
#include "platform/windows/reminder/local_reminder_runtime.h"
#include "platform/windows/shell_activation.h"
#include "platform/windows/music_system_fonts.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include <objbase.h>
#include <filesystem>
#include <deque>
#include <set>
#include <map>
#include <mutex>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <thread>
#include <shellapi.h>
#include <shlobj.h>
#include <utility>

namespace lume::shell {
using namespace platform::windows;
namespace {
using lume::text::Utf8;
using lume::text::Wide;
TaskbarPaths TaskbarConfiguration(const DesktopApplicationOptions& options) {
    auto paths=options.taskbarPaths;
    if(options.verification)paths.allowSystemEffects=false;
    return paths;
}
MinimizeAnimationScope AnimationScope(const DesktopApplicationOptions& options) {
    if(!options.verification)return {};
    auto scope=options.verificationMinimizeScope;
    // An omitted verification fixture must never install desktop-wide hooks.
    if(!scope.threadId){scope.threadId=GetCurrentThreadId();scope.processId=GetCurrentProcessId();}
    return scope;
}
WallpaperSessionOptions WallpaperOptions(const DesktopApplicationOptions& options,bool balancedMode,
    std::function<void(std::string,std::string)> downloaded,
    std::function<void(std::string,std::string,std::function<void(std::string)>)> authorize) {
    auto wallpaper=options.wallpaperOptions;
    wallpaper.verification=options.verification;
    wallpaper.balancedMode=balancedMode;
    if(!wallpaper.downloadCompleted)wallpaper.downloadCompleted=std::move(downloaded);
    if(!options.verification)wallpaper.authorizeDownload=std::move(authorize);
    return wallpaper;
}
ApplicationSettingsOptions SettingsOptions(const DesktopApplicationOptions& options) {
    auto settings=options.applicationSettings;settings.verification=options.verification;return settings;
}
AccountSessionOptions AccountOptions(const DesktopApplicationOptions& options) {
    auto account=options.accountOptions;account.verification=options.verification;return account;
}
MusicSessionOptions MusicOptions(const DesktopApplicationOptions& options,
                                 std::function<void()> wake) {
    auto music = options.music;
    if (!music.wake) music.wake = std::move(wake);
    return music;
}
MusicProviderAccountOptions MusicAccountOptions(const DesktopApplicationOptions& options,
                                                std::function<void()> wake) {
    auto accounts = options.musicAccounts;
    accounts.verification = options.verification;
    accounts.wake = std::move(wake);
    return accounts;
}
MusicBackgroundSessionOptions MusicBackgroundOptions(const DesktopApplicationOptions& options,
                                                      std::function<void()> wake) {
    auto background = options.musicBackground;
    background.verification = options.verification;
    background.initiallyVisible = false;
    background.wake = std::move(wake);
    if(background.wallpaperLibraryRoot.empty())background.wallpaperLibraryRoot=options.wallpaperOptions.root;
    return background;
}
MusicFontAssetSessionOptions MusicFontOptions(const DesktopApplicationOptions& options,
                                             std::function<std::vector<std::string>()> selected) {
    auto fonts=options.musicFonts;
    fonts.verification=options.verification;
    if(!fonts.selectedFamilies)fonts.selectedFamilies=std::move(selected);
    return fonts;
}
UpdateNotificationOptions UpdateOptions(const DesktopApplicationOptions& options,
                                       UpdateNotificationSession::RequestCheck check,
                                       std::function<bool(const update_notification::Notice&)> show) {
    auto notifications=options.updateNotification;notifications.verification=options.verification;
    // Only an explicitly injected update transport may drive a verification schedule.
    if(options.verification&&options.applicationSettings.testPorts.updates&&!notifications.testPorts.requestCheck)
        notifications.testPorts.requestCheck=std::move(check);
    if(!notifications.showInApp)notifications.showInApp=std::move(show);
    return notifications;
}
}
struct DesktopApplication::State {
    // The calendar's todo form, opened from the island below the capsule. It
    // writes through the shared TodoSession, so every todo view refreshes.
    void OpenIslandTodoEditor(const std::string& id,calendar::Date day){
        if(exiting||!todos)return;
        if(islandEditor.IsOpen()){SetForegroundWindow(islandEditor.Handle());return;}
        const auto snapshot=todos->Read();
        if(!snapshot.writable)return;
        const auto& time=todos->Time();const auto now=WindowsLocalTime::NowMillis();
        todo::Command seed;seed.scope=snapshot.scope;std::optional<todo::TaskView> existing;
        if(!id.empty()){
            const auto found=std::find_if(snapshot.tasks.begin(),snapshot.tasks.end(),[&](const auto& t){return t.id==id&&!t.deletedAt;});
            if(found==snapshot.tasks.end())return;
            existing=*found;seed.id=found->id;seed.type=todo::CommandType::Update;
        } else {
            seed.patch.schedule=todo::DefaultSchedule(day,now,time);
            if(!seed.patch.schedule)return;
        }
        const HWND owner=dynamicIsland.Handle();
        const unsigned dpi=owner?GetDpiForWindow(owner):GetDpiForSystem();
        MONITORINFO monitor{sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor))return;
        const auto work=monitor.rcWork;
        const int width=std::min<int>(work.right-work.left,MulDiv(480,static_cast<int>(dpi),96));
        const int height=std::min<int>(work.bottom-work.top,MulDiv(560,static_cast<int>(dpi),96));
        // Below the island, but never past the bottom of a short work area.
        const int left=work.left+(work.right-work.left-width)/2,
                  top=std::max<int>(work.top,std::min<int>(work.top+MulDiv(64,static_cast<int>(dpi),96),work.bottom-height));
        calendar::Preferences appearance;appearance.material=calendar::Material::FrostedGlass;
        islandEditor.Open(instance,owner,RECT{left,top,left+width,top+height},dpi,appearance,time,seed,now,existing,
            [this](const todo::Command& command){
                todo::CommandResult result;result.error=todo::Error::ReadOnly;
                if(exiting||!todos)return result;
                return todos->Dispatch(command);
            },{},WindowsLocalTime::NowMillis);
    }
    // Music from another player, for its minimized window in the Dock.
    std::optional<NowPlaying> NowPlayingCover() {
        const auto media=systemMedia.Snapshot();
        if(!media||media->app.empty()||media->artwork.empty())return std::nullopt;
        if(media->artwork!=dockCoverKey){dockCoverKey=media->artwork;dockCover=platform::windows::DecodeMediaArtwork(dockCoverKey);}
        if(!dockCover)return std::nullopt;
        return NowPlaying{media->app,dockCover};
    }
    DynamicIslandSessionOptions IslandOptions() {
        DynamicIslandSessionOptions value;
        value.preferencesPath=options.dynamicIslandPreferencesPath;
        value.notify=[this](const std::wstring& message){if(!exiting){Post(message);Wake();}};
        value.control=[this](dynamic_island::Action action,const dynamic_island::Media& media){
            const auto snapshot=music.Snapshot();
            if(exiting||!snapshot.current||snapshot.session!=media.session||snapshot.current->id!=media.id)return;
            ui::MusicPageCommand command;command.session=media.session;command.id=media.id;
            command.action=action==dynamic_island::Action::Previous?ui::MusicAction::Previous:
                action==dynamic_island::Action::Next?ui::MusicAction::Next:ui::MusicAction::Toggle;
            musicCommands.push_back(command);Wake();
        };
        if(!options.verification) {
            value.externalMedia=[this]{
                auto media=systemMedia.Snapshot();
                if(media)externalLyrics.Apply(*media);
                else externalLyrics.Clear();
                return media;
            };
            value.externalControl=[this](dynamic_island::Action action){systemMedia.Control(action);};
            value.externalSetPlaying=[this](bool playing){systemMedia.SetPlaying(playing);};
        }
        value.openSettings=[this]{Open(ManagerPage::DynamicIsland);};
        // Same store as the calendar and today todo widgets: reads go to it on
        // demand and toggles dispatch through it, so every view stays in sync.
        value.todos.forDay=[this](calendar::Date day){return exiting?std::vector<platform::windows::IslandTodo>{}:islandTodos.ForDay(day);};
        value.todos.find=[this](const std::string& id)->std::optional<platform::windows::IslandTodo>{if(exiting)return std::nullopt;return islandTodos.Find(id);};
        value.todos.hasTodos=[this](calendar::Date day){return !exiting&&islandTodos.HasTodos(day);};
        value.todos.toggle=[this](const platform::windows::IslandTodo& row){if(!exiting)islandTodos.Toggle(row);};
        value.todos.openCalendar=[this]{if(!exiting)Open(ManagerPage::Calendar);};
        value.todos.openTask=[this](const std::string& id){if(!exiting&&!calendar.OpenTodo(id))Open(ManagerPage::Calendar);};
        value.todos.writable=[this]{return !exiting&&todos&&todos->Read().writable;};
        value.todos.edit=[this](const std::string& id,calendar::Date day){OpenIslandTodoEditor(id,day);};
        value.todos.remove=[this](const platform::windows::IslandTodo& row){if(!exiting)islandTodos.Remove(row);};
        // Weather page: a bounded projection of the weather component's current
        // reading. Reads are on demand, so the island adds no fetching of its own.
        value.weather=[this]{return exiting?dynamic_island::Weather{}:IslandWeather(weather.Reading());};
        value.openWeatherSettings=[this]{if(!exiting)Open(ManagerPage::Weather);};
        value.weatherActive=[this](bool active){weather.SetSharedConsumer(active);};
        return value;
    }
    State(HINSTANCE instance,DesktopApplicationOptions options):instance(instance),options(std::move(options)),
        diagnostics(diagnostics::FeatureEnabled ? this->options.diagnosticsPath : std::string{}),
        preferences(store.Load(this->options.clockPreferencesPath)),
        clockRuntime(instance,[this](clock::Action action){ClockAction(action);}),
        clock(preferences.clock,clockRuntime,[this](const settings::ClockSettings& value){
            auto next=preferences;next.clock=value;
            if(!store.Save(this->options.clockPreferencesPath,next))return false;
            preferences=std::move(next);return true;
        }),todos(std::make_shared<TodoSession>(this->options.calendarPreferencesPath)),
        calendar(instance,this->options.calendarPreferencesPath,
            [this](const std::wstring& message,bool attention){Post(message);if(remindersStarted)reminders.UpdateAppearance(Appearance());if(attention)Open(ManagerPage::Calendar);Wake();},
            [this]{Open(ManagerPage::Calendar);},todos),
        todayTodo(instance,this->options.todayTodoPreferencesPath,
            Notifier(),
            [this]{Open(ManagerPage::TodayTodo);},todos->Port()),
        performance(instance,this->options.performancePreferencesPath,
            Notifier(ManagerPage::Performance),
            [this]{Open(ManagerPage::Performance);}),
        spectrum(instance,this->options.spectrumPreferencesPath,
            Notifier(ManagerPage::Spectrum),
            [this]{Open(ManagerPage::Spectrum);}),
        almanac(instance,this->options.almanacPreferencesPath,
            Notifier(ManagerPage::Almanac),
            [this]{Open(ManagerPage::Almanac);}),
        weather(instance,this->options.weatherPreferencesPath,
            [this](const std::wstring& message,bool attention){Post(message);if(attention)Open(ManagerPage::Weather);dynamicIsland.RefreshWeather();Wake();},
            [this]{Open(ManagerPage::Weather);}),
        organizer(instance,this->options.organizerPreferencesPath,
            Notifier(ManagerPage::Organizer),
            [this]{Open(ManagerPage::Organizer);},nullptr,{},{},!this->options.verification),
        radial(instance,[this]{auto value=this->options.radial;value.verification=this->options.verification;return value;}(),
            Notifier(ManagerPage::Radial),
            [this](bool shortcut){radialShortcutSettings=shortcut;Open(ManagerPage::Radial);}),
        dock(instance,[this]{auto value=this->options.dock;value.verification=this->options.verification;
                if(!value.nowPlaying)value.nowPlaying=[this]{return NowPlayingCover();};return value;}(),
            Notifier(ManagerPage::Dock),
            [this]{Open(ManagerPage::Home);},[this]{Open(ManagerPage::Dock);},[this]{return visible;}),
        systemMedia(),
        dynamicIsland(instance, IslandOptions()),
        desktopIcons(instance,this->options.desktopIconsPreferencesPath,
            Notifier(ManagerPage::Icons),
            nullptr,this->options.verification&&this->options.verificationDesktopIcons?
                this->options.verificationDesktopIcons:FindDesktopIconsTargets),
        taskbar(instance,TaskbarConfiguration(this->options),
            Notifier(ManagerPage::Taskbar),
            nullptr,this->options.verification?this->options.verificationTaskbar:TaskbarRuntime::PortFactory{}),
        minimize(instance,this->options.minimizePreferencesPath,
            Notifier(ManagerPage::MinimizeAnimation),
            nullptr,AnimationScope(this->options)),
        applicationSettings(instance,SettingsOptions(this->options),
            [this](const std::wstring& message){
                Post(message);
                if(!exiting){dynamicIsland.SetEnglish(applicationSettings.Preferences().language=="en");updateNotifications.SetLanguage(applicationSettings.Preferences().language=="en");updateNotifications.Wake();Wake();}
            },
            [this](bool balanced){return wallpaper.SetBalancedMode(balanced);}),
        wallpaper(instance,WallpaperOptions(this->options,applicationSettings.BalancedMode(),
            [this](std::string id,std::string source){if(!exiting)account.RecordWallpaperDownload(std::move(id),std::move(source));},
            [this](std::string id,std::string source,std::function<void(std::string)> completed){
                if(exiting){completed("WALLPAPER_CANCELLED");return;}
                account.AuthorizeWallpaperDownload(std::move(id),std::move(source),std::move(completed));}),
            Poster()),
        musicBackground(MusicBackgroundOptions(this->options,[this]{Wake();}),files),
        account(instance,AccountOptions(this->options),
            Poster()),
        music(PlaybackOptions()),
        musicAccounts(MusicAccountOptions(this->options,[this]{Wake();})),
        musicOnline(OnlineOptions()),
        musicLibrary(LibraryOptions()),
        musicDesktop(instance,DesktopMusicOptions(),
            [this](const ui::MusicPageCommand& command){
                const auto current=music.Snapshot();
                if(exiting||command.session!=current.session||!current.current||command.id!=current.current->id)return;
                musicCommands.push_back(command);Wake();
            },[this]{Open(ManagerPage::MusicDesktop);}),
        notes(instance,NotesSessionOptions{this->options.notesPath,this->options.notesWidgetPath,this->options.verification,nullptr,
            Poster()}),
        musicPreferences(std::filesystem::u8path(this->options.musicPreferencesPath),files),
        musicFonts(MusicFontOptions(this->options,[this]{return SelectedFontFamilies();})),
        updateNotifications(instance,UpdateOptions(this->options,[this](bool manual){return RequestUpdate(manual);},
            [this](const update_notification::Notice& notice){return PresentUpdate(notice);}),
            [this](bool manual){return RequestUpdate(manual);},[this]{Wake();}),
        reminders(instance,files,todos->BaseDirectory().empty()?std::string{}:todos->BaseDirectory()+"/local/reminder-ledger.json",
            LocalReminderPorts{[this]{return todos->Read();},&todos->Time(),WindowsLocalTime::NowMillis,
                [this]{todos->RefreshTime();},[this](const std::string& id){return !exiting&&calendar.OpenTodo(id);},
                [this]{ReminderStatus();Wake();},
                this->options.verification ? this->options.verificationReminderAllowed : std::function<bool()>{},
                // With the dynamic island on, reminders appear in it instead of the popup.
                [this](const ReminderNotice& notice){return !exiting&&dynamicIsland.ShowNotice(notice.taskId,notice.title,notice.time);}}) {
        applicationSettings.SetUpdateObserver([this](bool manual,const application_settings::ManifestResult& result){
            if(exiting)return;
            updateNotifications.SetLanguage(applicationSettings.Preferences().language=="en");
            updateNotifications.OnCheckCompleted(manual,result);
        });
        todos->SetChanged([this]{
            if(exiting)return;
            islandTodos.Changed();
            calendar.RefreshTodos();todayTodo.Refresh();dynamicIsland.RefreshTodos();
            if(remindersStarted)reminders.Refresh();
        });
        window.ConfigureWallpaper(
            [this](const ui::WallpaperPageCommand& command){
                if(exiting)return;
                if(command.action==ui::WallpaperAction::Stats||command.action==ui::WallpaperAction::Like||
                    command.action==ui::WallpaperAction::EndStats) {
                    if(command.action!=ui::WallpaperAction::EndStats&&!wallpaper.IsOfficialId(command.id)) {
                        account.HandleWallpaper({ui::WallpaperAction::EndStats});return;
                    }
                    account.HandleWallpaper(command);
                }
                else wallpaper.HandlePage(command,window.Handle());
            },
            wallpaper.MediaDirectory(),
            [this](std::vector<std::wstring> paths){wallpaper.HandleDroppedFiles(std::move(paths));});
        window.ConfigureMusicDrop([this](std::vector<std::wstring> paths){
            if(!this->options.musicLibrary.root.empty()) {
                if(!musicLibrary.Import(std::move(paths)))LibraryFailure("busy");
            }
            else if(this->options.verification)music.ImportLocalFiles(std::move(paths));
        });
        window.ConfigureMusicFonts(musicFonts.AssetDirectory().wstring(),
            [this](std::wstring_view filename){return musicFonts.AssetSize(filename);});
        window.ConfigureThreadWake(lume::window_ids::ShellWakeMessage,[this]{DrainMusic();});
        window.ConfigureApplicationCommands([this](std::wstring_view message){
            if(exiting)return;
            if(const auto report=ParseDiagnosticReportCommand(message)) {
                if(report->action=="open-logs")OpenLogFolder();
                else if(report->action=="copy")StartDiagnosticJob(DiagnosticJob::Copy,report->note);
                else if(report->action=="self-check")StartDiagnosticJob(DiagnosticJob::Check,{});
                else StartDiagnosticJob(DiagnosticJob::Report,report->note);
                return;
            }
            if(const auto diagnosticsAction=diagnostics::ParseCommand(message)) {
                if(!diagnostics::FeatureEnabled)return;
                if(*diagnosticsAction==diagnostics::Action::Clear)diagnosticOperations.Clear();
                PostDiagnosticsState();
            } else if(const auto command=ui::ParseApplicationSettingsPageJson(message)) {
                if(command->action==ui::ApplicationSettingsAction::CheckUpdate&&updateNotifications.RequestManual()) {
                    if(!exiting)Post(applicationSettings.State());
                } else if(const auto reply=applicationSettings.HandlePage(*command,window.Handle()))Post(*reply);
            } else if(const auto accountCommand=ui::ParseAccountPageJson(message))account.HandlePage(*accountCommand,window.Handle());
            else if(const auto providerCommand=ParseMusicProviderAccountCommand(message)) {
                Post(musicAccounts.HandlePage(*providerCommand));
                musicOnline.AccountsChanged();
            }
            else if(const auto onlineCommand=ParseMusicOnlineCommand(message)) {
                Post(musicOnline.HandlePage(*onlineCommand));
            }
            else if(const auto libraryCommand=ui::ParseMusicLibraryCommand(message)) {
                LibraryCommand(*libraryCommand);
            }
            else if(const auto replies=NotesReply(message)) {
                for(const auto& reply:*replies)Post(reply);
            }
            else if(const auto reply=musicDesktop.HandleMessage(std::wstring(message))) {
                Post(*reply);
            }
            else if(const auto systemFontsReply=music::fonts::HandleMusicSystemFonts(message)) {
                Post(*systemFontsReply);
            }
            else if(const auto fontReply=musicFonts.HandleMessage(message,window.Handle())) {
                Post(*fontReply);
            }
            else if(const auto backgroundCommand=ui::ParseMusicBackgroundPageJson(message)) {
                Post(musicBackground.HandlePage(*backgroundCommand,window.Handle()));
            }
            else if(const auto preferencesCommand=ui::ParseMusicPreferencesPageJson(message)) {
                Post(musicPreferences.HandlePage(*preferencesCommand));
            } else if(const auto musicCommand=ui::ParseMusicPageJson(message)) {
                if(musicCommand->action==ui::MusicAction::Close ||
                   (musicCommand->action==ui::MusicAction::Visibility&&!musicCommand->flag)) {
                    musicAccounts.CancelActive();musicFonts.Cancel();
                    musicOnline.Cancel();
                    musicLibrary.SetVisible(false);
                    musicBackground.CancelActive();musicBackground.SetVisible(false);
                } else if(musicCommand->action==ui::MusicAction::Open ||
                          (musicCommand->action==ui::MusicAction::Visibility&&musicCommand->flag)) {
                    musicBackground.SetVisible(true);
                    musicLibrary.SetVisible(true);
                    Post(musicBackground.State());
                }
                musicCommands.push_back(*musicCommand);
                Wake();
            }
        });
        window.ConfigureVisibility([this](bool visible){
            if(!visible){radial.CancelCapture();musicAccounts.CancelActive();musicOnline.Cancel();musicLibrary.SetVisible(false);musicFonts.Cancel();musicBackground.CancelActive();musicBackground.SetVisible(false);}
            if(visible && ready && !exiting) {PostCurrentState();updateNotifications.RetryPresentation();}
        });
    }
    MusicSessionOptions PlaybackOptions() {
        auto value=MusicOptions(options,[this]{Wake();});
        value.analysisActive=[this]{ return dynamicIsland.Running(); };
        if(!value.resolveSource && !value.resolveAudioSource)value.resolveAudioSource=[this](const auto& track,auto quality,const auto& cancelled){
            return musicOnline.ResolveAudioSource(track,quality,cancelled);
        };
        if(!value.readOnlineLyrics)value.readOnlineLyrics=[this](const auto& track,const auto& cancelled){
            return musicOnline.LoadLyrics(track,cancelled);
        };
        return value;
    }
    std::optional<std::vector<std::wstring>> NotesReply(std::wstring_view message) {
        if(!options.notesVisible)return {};
        return notes.HandleMessage(std::wstring(message));
    }
    std::optional<std::wstring> RadialReply(ui::PageCommand command) {
        if(!options.radialVisible)return {};
        return radial.HandlePage(command);
    }
    MusicOnlineOptions OnlineOptions() {
        auto value=options.musicOnline;
        value.verification=options.verification;
        if(!value.acquire)value.acquire=[this](auto provider){return musicAccounts.AcquireCredentials(provider);};
        if(!value.current)value.current=[this](const auto& lease){return musicAccounts.IsCurrent(lease);};
        value.wake=[this]{Wake();};
        value.play=[this](std::vector<music::Track> tracks,std::string id,std::string action){
            if(action=="play")music.PlayTracks(std::move(tracks),std::move(id));
            else music.EnqueueTracks(std::move(tracks),action=="insertNext");
            Wake();
        };
        return value;
    }
    MusicLibraryWorkerOptions LibraryOptions() {
        auto value=options.musicLibrary;
        value.wake=[this]{Wake();};
        return value;
    }
    MusicDesktopSessionOptions DesktopMusicOptions() {
        auto value=options.musicDesktop;
        value.verification=options.verification;
        value.notify=[this](const auto& state){if(!exiting)Post(state);};
        value.wake=[this]{Wake();};
        return value;
    }
    void LibraryCommand(const ui::MusicLibraryCommand& command) {
        if(command.action=="visibility"){musicLibrary.SetVisible(command.visible);return;}
        if(options.musicLibrary.root.empty()){LibraryFailure("unavailable");return;}
        bool accepted=false;
        if(command.action=="snapshot")accepted=musicLibrary.Page();
        else if(command.action=="page")accepted=musicLibrary.Page(command.offset,command.limit);
        else if(command.action=="remove")accepted=musicLibrary.Remove(command.id);
        else accepted=musicLibrary.Play(command.id,command.action=="play"?MusicLibraryPlaybackAction::Play:
            command.action=="enqueue"?MusicLibraryPlaybackAction::Enqueue:MusicLibraryPlaybackAction::InsertNext);
        if(!accepted)LibraryFailure("busy");
    }
    void LibraryFailure(const std::string& error) {
        auto json=musicLibraryState;
        json["error"]=error;
        const auto bytes=json.dump(-1,' ',true);
        PublishLibraryState(std::wstring(bytes.begin(),bytes.end()));
    }
    void PublishLibraryState(const std::wstring& state) {
        auto json=nlohmann::json::parse(state);
        json.erase("scope");
        json["type"]="music-library-state";
        json["revision"]=++musicLibraryRevision;
        // Artwork placeholders are native IDs, not URLs. Only explicit media leases leave the shell.
        for(auto& track:json["tracks"]) {
            const auto image=track.value("artwork",std::string{});
            if(image.rfind("http://127.0.0.1:",0)!=0)track["artwork"]="";
        }
        musicLibraryState=json;
        const auto utf8=json.dump(-1,' ',true);
        Post(std::wstring(utf8.begin(),utf8.end()));
    }
    std::vector<std::string> SelectedFontFamilies() {
        std::vector<std::string> result;
        const auto current=nlohmann::json::parse(musicPreferences.State());
        result.push_back(current.at("preferences").at("fontFamily").get<std::string>());
        // Deletion must preserve both durable preference generations.
        for(const auto& suffix:{std::string{},std::string{".bak"}}) {
            const auto path=options.musicPreferencesPath+suffix;
            if(options.musicPreferencesPath.empty())continue;
            std::error_code statusError;
            const auto status=std::filesystem::symlink_status(std::filesystem::u8path(path),statusError);
            if(statusError==std::errc::no_such_file_or_directory)continue;
            if(statusError || status.type()!=std::filesystem::file_type::regular)
                throw std::runtime_error("font references unavailable");
            std::string bytes;
            if(!files.ReadBounded(path,music::kMusicPreferencesMaxBytes,bytes))throw std::runtime_error("font references unavailable");
            const auto record=music::detail::ParseMusicPreferencesJson(bytes);
            music::MusicPreferences value;
            if(!music::DecodeMusicPreferences(record.at("preferences"),value))throw std::runtime_error("font references invalid");
            result.push_back(value.fontFamily);
        }
        return result;
    }
    bool RequestUpdate(bool manual){
        if(exiting||!applicationSettings.RequestUpdateCheck(manual))return false;
        Post(applicationSettings.State());return true;
    }
    bool PresentUpdate(const update_notification::Notice& notice){
        if(exiting)return false;
        // Never open/restore the manager for an asynchronous update result. A
        // user may have switched to a game since requesting a manual check.
        const auto manager=window.Handle();
        if(!notice.manual||!ready||!visible||!manager||IsIconic(manager)||
            GetAncestor(GetForegroundWindow(),GA_ROOT)!=manager)return true;
        using update_notification::NoticeKind;
        const char* kind=notice.kind==NoticeKind::Current?"current":
            notice.kind==NoticeKind::Available?"available":
            notice.kind==NoticeKind::Incompatible?"incompatible":
            notice.kind==NoticeKind::Disabled?"disabled":"error";
        const auto message=nlohmann::json{{"version",1},{"type","app-update-notice"},
            {"kind",kind},{"updateVersion",notice.kind==NoticeKind::Error?"":notice.version},
            {"error",notice.error},{"manual",notice.manual}}.dump(-1,' ',true);
        Post(std::wstring(message.begin(),message.end()));
        return !exiting;
    }
    // Every session callback that only forwards a state message to the page.
    std::function<void(const std::wstring&)> Poster(){return [this](const std::wstring& message){Post(message);Wake();};}
    // The same, and on attention brings the manager up on page.
    std::function<void(const std::wstring&,bool)> Notifier(std::optional<ManagerPage> page={}){
        return [this,page](const std::wstring& message,bool attention){Post(message);if(attention&&page)Open(*page);Wake();};
    }
    // The desktop widgets, last to first: the same order on exit and in the destructor.
    void ShutdownWidgets(){desktopIcons.Shutdown();organizer.Shutdown();weather.Shutdown();todayTodo.Shutdown();almanac.Shutdown();performance.Shutdown();spectrum.Shutdown();calendar.Shutdown();clock.Shutdown();}
    void ShutdownApplicationServices(){
        radial.Shutdown();
        dock.Shutdown();
        dynamicIsland.Shutdown();
        notes.Shutdown();
        musicDesktop.Shutdown();
        musicFonts.Shutdown();
        if(musicLibraryRestored&&!options.musicLibrary.root.empty())musicLibrary.SaveQueue(music.Snapshot());
        music.Shutdown();
        musicLibrary.Shutdown();
        musicOnline.Shutdown();
        musicAccounts.Shutdown();
        updateNotifications.Shutdown();applicationSettings.SetUpdateObserver({});
        applicationSettings.Shutdown();account.Shutdown();
        applicationSettings.FlushPreferences();
        wallpaper.Shutdown();applicationSettings.JoinShutdown();
        musicBackground.Shutdown();
    }
    ~State(){todos->SetChanged({});ShutdownApplicationServices();reminders.Shutdown();minimize.Shutdown();taskbar.Shutdown();ShutdownWidgets();}
    ReminderAppearance Appearance()const{
        const auto& p=calendar.Snapshot();ReminderAppearance out;
        const auto color=[](calendar::Rgba value){return RGB((value.rgb>>16)&255,(value.rgb>>8)&255,value.rgb&255);};
        out.foreground=color(p.textColor);out.accent=color(p.highlightColor);
        if(calendar.Handle())GetWindowRect(calendar.Handle(),&out.anchor);
        return out;
    }
    void ReminderStatus(){
        if(!ready||exiting)return;
        Post(reminders.Error()==LocalReminderError::None
            ?L"{\"version\":1,\"type\":\"reminder-status\",\"available\":true}"
            :L"{\"version\":1,\"type\":\"reminder-status\",\"available\":false}");
    }
    void Wake(){if(thread)PostThreadMessageW(thread,lume::window_ids::ShellWakeMessage,0,0);}
    void RecordDiagnosticMessage(std::string_view bytes) {diagnosticOperations.Record(bytes);}
    void Post(const std::wstring& message){
        TraceState(message);
        bool newEvent=false;
        bool mismatchChanged=false;
        if constexpr(diagnostics::FeatureEnabled) {
          const auto bytes=Utf8(message);
          if(!bytes.empty()) {
            const auto before=diagnostics.Snapshot();
            RecordDiagnosticMessage(bytes);
            const auto after=diagnostics.Snapshot();
            newEvent=LatestEventIdentity(before)!=LatestEventIdentity(after);
            mismatchChanged=before.value("mismatches",nlohmann::json::array())!=
                after.value("mismatches",nlohmann::json::array());
        } }
        window.Post(message);
        const auto observer=options.onMessage;
        if(options.verification&&observer)observer(message);
        if((newEvent||mismatchChanged)&&ready&&visible&&!exiting)PostDiagnosticsState();
        if(newEvent&&!visible&&!exiting)NotifyDiagnosticIssue();
    }
    // Each module state change that touches its health goes to the runtime
    // log, so a report shows when (and in which order) things went wrong.
    void TraceState(const std::wstring& message) {
        if(options.verification||message.size()>262144||message.find(L"-state\"")==std::wstring::npos)return;
        const auto value=nlohmann::json::parse(Utf8(message),nullptr,false);
        if(!value.is_object())return;
        const auto type=value.value("type",std::string{});
        if(type.size()<7||type.compare(type.size()-6,6,"-state")!=0||type.rfind("music",0)==0||type=="resource-snapshot")return;
        std::string signature;
        for(const auto* field:{"error","catalogError","weatherError","materialStatus","loadStatus","status","enabled","running","mode","pendingRecovery"}) {
            const auto item=value.find(field);
            if(item==value.end())continue;
            if(item->is_string())signature+=std::string(" ")+field+"="+item->get<std::string>();
            else if(item->is_boolean())signature+=std::string(" ")+field+"="+(item->get<bool>()?"true":"false");
        }
        auto& last=tracedStates[type];
        if(signature.empty()||last==signature)return;
        last=signature;
        lume::platform::windows::diag::Log("state",type+signature);
    }
    nlohmann::json ModuleStates() {
        auto modules=nlohmann::json::object();
        for(const auto& snapshot:{ui::ClockPageState(clock),calendar.State(),performance.State(),spectrum.State(),
            almanac.State(),todayTodo.State(),weather.State(),organizer.State(),dock.State(),desktopIcons.State(),
            taskbar.State(),minimize.State(),wallpaper.State(),applicationSettings.State()}) {
            const auto value=nlohmann::json::parse(Utf8(snapshot),nullptr,false);
            if(value.is_object())modules[value.value("type",std::string("unknown"))]=value;
        }
        return modules;
    }
    void PostReportState(const nlohmann::json& state) {
        auto message=state;message["version"]=1;message["type"]="diagnostic-report-state";
        const auto wide=Wide(message.dump(-1,' ',false,nlohmann::json::error_handler_t::replace));
        if(!wide.empty())window.Post(wide);
    }
    // Report, copied text and self-check are collected here (module states
    // belong to this thread) and built on one worker at a time.
    enum class DiagnosticJob { Report, Copy, Check, StartupCheck };
    struct DiagnosticJobResult {
        DiagnosticJob kind=DiagnosticJob::Report;
        DiagnosticReportResult report;
        std::string text;
        std::vector<SelfCheck> checks;
    };
    DiagnosticReportInput DiagnosticInput(const std::string& note) {
        DiagnosticReportInput input;
        input.dataDirectory=std::filesystem::u8path(options.applicationSettings.dataDirectory);
        input.logDirectory=lume::platform::windows::diag::Directory();
        wchar_t executable[MAX_PATH]{};
        if(GetModuleFileNameW(nullptr,executable,MAX_PATH))input.installDirectory=std::filesystem::path(executable).parent_path();
        input.version=lume::application_settings::CurrentVersion;
        input.note=note;
        input.modules=ModuleStates();
        return input;
    }
    void CreateReport(const std::string& note){StartDiagnosticJob(DiagnosticJob::Report,note);}
    void StartDiagnosticJob(DiagnosticJob kind,const std::string& note) {
        const bool page=kind!=DiagnosticJob::StartupCheck;
        if(reportRunning){if(page)PostReportState({{"status","working"},{"action",JobAction(kind)}});return;}
        reportRunning=true;
        if(page)PostReportState({{"status","working"},{"action",JobAction(kind)}});
        auto input=DiagnosticInput(note);
        if(reportWorker.thread.joinable())reportWorker.thread.join();
        try {
            reportWorker.thread=std::thread([this,kind,input=std::move(input)]{
                DiagnosticJobResult result;result.kind=kind;
                try {
                    if(kind==DiagnosticJob::Report)result.report=WriteDiagnosticReport(input);
                    else if(kind==DiagnosticJob::Copy)result.text=DiagnosticText(input);
                    else result.checks=RunSelfChecks(input.installDirectory);
                } catch(...) {result.report.error="DIAGNOSTIC_JOB_FAILED";}
                {std::lock_guard<std::mutex> lock(reportMutex);reportResult=std::move(result);}
                Wake();
            });
        } catch(...) {reportRunning=false;if(page)PostReportState({{"status","failed"},{"action",JobAction(kind)},{"error","DIAGNOSTIC_REPORT_THREAD"}});}
    }
    static const char* JobAction(DiagnosticJob kind) {
        return kind==DiagnosticJob::Copy?"copy":kind==DiagnosticJob::Report?"create":"self-check";
    }
    static bool CopyText(const std::string& text) {
        const auto wide=Wide(text);
        if(wide.empty()||!OpenClipboard(nullptr))return false;
        bool copied=false;
        EmptyClipboard();
        if(const auto memory=GlobalAlloc(GMEM_MOVEABLE,(wide.size()+1)*sizeof(wchar_t))) {
            if(auto* target=static_cast<wchar_t*>(GlobalLock(memory))) {
                std::copy(wide.c_str(),wide.c_str()+wide.size()+1,target);GlobalUnlock(memory);
                copied=SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
            }
            if(!copied)GlobalFree(memory);
        }
        CloseClipboard();
        return copied;
    }
    void LogChecks(const std::vector<SelfCheck>& checks) {
        for(const auto& check:checks)
            lume::platform::windows::diag::Log("self-check",std::string(CheckStatusName(check.status))+" "+check.id+": "+check.detail);
    }
    // Startup: tell the user once about each new set of problems, not every boot.
    void NotifyChecks(const std::vector<SelfCheck>& checks) {
        const auto signature=SelfCheckSignature(checks);
        const auto folder=lume::platform::windows::diag::Directory();
        if(folder.empty())return;
        const auto file=folder/L"self-check-notified.txt";
        std::string previous;
        {std::ifstream in(file,std::ios::binary);previous.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());}
        if(signature==previous)return;
        {std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<signature;}
        if(signature.empty()||!diagnosticTray||options.verification||exiting)return;
        std::string titles;
        for(const auto& check:checks)if(check.status==CheckStatus::Problem)titles+=(titles.empty()?"":"、")+check.title;
        diagnosticTray->ShowNotice(L"Lume 自检发现问题",Wide("“"+titles+"”可能无法正常工作。点击查看原因和解决办法。"));
    }
    void DrainReport() {
        std::optional<DiagnosticJobResult> result;
        {std::lock_guard<std::mutex> lock(reportMutex);result.swap(reportResult);}
        if(!result)return;
        reportRunning=false;
        switch(result->kind) {
        case DiagnosticJob::Report:
            if(!result->report.ok){PostReportState({{"status","failed"},{"action","create"},{"error",result->report.error}});return;}
            PostReportState({{"status","done"},{"action","create"},{"file",result->report.path.filename().u8string()}});
            // Show the zip selected in Explorer: the user only has to send it.
            if(PIDLIST_ABSOLUTE item=ILCreateFromPathW(result->report.path.c_str())){SHOpenFolderAndSelectItems(item,0,nullptr,0);ILFree(item);}
            return;
        case DiagnosticJob::Copy:
            if(CopyText(result->text))PostReportState({{"status","done"},{"action","copy"}});
            else PostReportState({{"status","failed"},{"action","copy"},{"error","DIAGNOSTIC_COPY_FAILED"}});
            return;
        case DiagnosticJob::Check:
            LogChecks(result->checks);
            PostReportState({{"status","done"},{"action","self-check"},{"checks",SelfChecksJson(result->checks)}});
            return;
        case DiagnosticJob::StartupCheck:
            LogChecks(result->checks);
            NotifyChecks(result->checks);
            return;
        }
    }
    void OpenLogFolder() {
        const auto folder=lume::platform::windows::diag::Directory();
        if(!folder.empty())ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    }
    void NotifyDiagnosticIssue() {
        if(!diagnostics::FeatureEnabled)return;
        if(options.verification||!diagnosticTray)return;
        const auto now=GetTickCount64();
        if(lastDiagnosticNotice&&now-lastDiagnosticNotice<300000)return;
        if(diagnosticTray->ShowDiagnosticNotice(applicationSettings.Preferences().language=="en"))
            lastDiagnosticNotice=now;
    }
    void PostDiagnosticsState() {
        if(!diagnostics::FeatureEnabled)return;
        if(exiting) return;
        auto state=diagnostics.Snapshot();
        state["version"]=1;
        state["type"]="diagnostics-state";
        state["generatedAt"]=static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        const auto text=state.dump(-1,' ',true,nlohmann::json::error_handler_t::replace);
        const auto wide=Wide(text);
        if(wide.empty())return;
        window.Post(wide);
        const auto observer=options.onMessage;
        if(options.verification&&observer)observer(wide);
    }
    void ObserveNativeEffects() {
        if(!diagnostics::FeatureEnabled)return;
        if(exiting)return;
        const auto before=diagnostics.Snapshot();
        const auto dockMessage=Utf8(dock.State());
        const auto taskbarMessage=Utf8(taskbar.State());
        if(!dockMessage.empty())RecordDiagnosticMessage(dockMessage);
        if(!taskbarMessage.empty())RecordDiagnosticMessage(taskbarMessage);
        if(!dockMessage.empty()) {
            const auto dockState=nlohmann::json::parse(dockMessage,nullptr,false);
            const bool settled=dockState.is_object()&&dockState.value("enabled",false)&&
                dockState.value("loadStatus",std::string{})=="loaded"&&
                dockState.value("error",std::string{}).empty();
            if(settled&&!dockState.value("running",false)) {
                if(!dockNotRunningSince)dockNotRunningSince=GetTickCount64();
                if(GetTickCount64()-dockNotRunningSince>=10000) {
                    diagnostics.RecordMessage(diagnostics::Json{{"version",1},{"type","dock-state"},
                        {"module","dock"},{"action","observe"},{"status","degraded"},
                        {"error","dock-not-running"},{"expected","running"},{"actual","stopped"}});
                }
            } else dockNotRunningSince=0;
        }
        const auto after=diagnostics.Snapshot();
        const bool newEvent=LatestEventIdentity(before)!=LatestEventIdentity(after);
        const bool mismatchChanged=before.value("mismatches",nlohmann::json::array())!=
            after.value("mismatches",nlohmann::json::array());
        if(ready&&visible&&!exiting&&(newEvent||mismatchChanged))PostDiagnosticsState();
        if(newEvent&&!visible&&!exiting)NotifyDiagnosticIssue();
    }
    void Navigate(){
        if(!ready||!navigation)return;
        const auto page=*navigation;navigation.reset();
        if(page==ManagerPage::Home)return;
        if(page==ManagerPage::DynamicIsland){Post(L"{\"version\":1,\"type\":\"navigate\",\"page\":\"dynamicIsland\"}");return;}
        if(page==ManagerPage::Radial){
            Post(radialShortcutSettings?L"{\"version\":1,\"type\":\"navigate\",\"page\":\"radial\",\"section\":\"shortcut\"}":
                L"{\"version\":1,\"type\":\"navigate\",\"page\":\"radial\"}");
            radialShortcutSettings=false;return;
        }
        if(page==ManagerPage::MusicDesktop) {
            Post(L"{\"version\":1,\"type\":\"navigate\",\"page\":\"music\",\"section\":\"desktop\"}");return;
        }
        if(page==ManagerPage::ApplicationSettings||page==ManagerPage::Account) {
            const auto section=page==ManagerPage::Account?L"user":openDiagnosticsOnNext?(diagnostics::FeatureEnabled?L"diagnostics":L"support"):L"general";
            openDiagnosticsOnNext=false;
            Post(std::wstring(L"{\"version\":1,\"type\":\"navigate\",\"page\":\"applicationSettings\",\"section\":\"")+
                section+L"\"}");return;
        }
        const wchar_t* name=page==ManagerPage::TodayTodo?L"todayTodo":page==ManagerPage::Calendar?L"calendar":page==ManagerPage::Performance?L"performance":page==ManagerPage::Spectrum?L"spectrum":page==ManagerPage::Almanac?L"almanac":page==ManagerPage::Weather?L"weather":page==ManagerPage::Organizer?L"organizer":page==ManagerPage::Icons?L"icons":page==ManagerPage::Taskbar?L"taskbar":page==ManagerPage::MinimizeAnimation?L"minimize":page==ManagerPage::Music?L"music":page==ManagerPage::Dock?L"dock":L"clock";
        Post(std::wstring(L"{\"version\":1,\"type\":\"navigate\",\"page\":\"")+name+L"\"}");
    }
    void DrainMusic(){
        DrainReport();
        while(!musicCommands.empty()) {
            const auto command=musicCommands.front();musicCommands.pop_front();
            if(const auto state=music.HandlePage(command))Post(*state);
        }
        if(const auto state=music.DrainNotification())Post(*state);
        if(const auto state=musicAccounts.DrainNotification()){
            Post(*state);musicOnline.AccountsChanged();
        }
        if(const auto state=musicOnline.DrainNotification())Post(*state);
        for(auto& result:musicLibrary.Drain()) {
            if(!result.state.empty())PublishLibraryState(result.state);
            else if(!result.error.empty())LibraryFailure(result.error);
            if(result.restored&&!musicLibraryRestored) {
                musicLibraryRestored=result.error.empty();
                if(result.restoredQueue&&music.Snapshot().session==0)
                    music.RestoreQueue(*result.restoredQueue,true);
            }
            if(!result.error.empty())continue;
            if(result.action==MusicLibraryPlaybackAction::Play)
                music.PlayTracks(std::move(result.playTracks),std::move(result.playId),true);
            else if(result.action==MusicLibraryPlaybackAction::Enqueue||result.action==MusicLibraryPlaybackAction::InsertNext)
                music.EnqueueTracks(std::move(result.playTracks),result.action==MusicLibraryPlaybackAction::InsertNext,true);
        }
        const auto snapshot=music.Snapshot();
        musicDesktop.Update(snapshot);
        dynamic_island::Media islandMedia;
        if(snapshot.current) {
            islandMedia.session=snapshot.session;islandMedia.id=snapshot.current->id;
            islandMedia.artwork=islandCovers.Resolve(*snapshot.current);
            islandMedia.title=Wide(snapshot.current->title);islandMedia.artist=Wide(snapshot.current->artist);
            islandMedia.available=snapshot.status!=music::PlaybackStatus::Error;
            islandMedia.playing=snapshot.playing;islandMedia.position=snapshot.position;
            islandMedia.duration=snapshot.current->duration;
            islandMedia.energy=static_cast<float>(snapshot.analysis.energy);
            if(dynamicIsland.Snapshot().enabled){
                const auto lines=music.CurrentLyrics();
                // Converted once per track and lyrics, not every frame.
                const auto key=snapshot.current->id+"#"+std::to_string(lines.size())+"#"+(lines.empty()?std::string():lines.front().fullText);
                if(key!=islandCuesKey){islandCuesKey=key;islandCues=LyricCuesOf(lines);}
                islandMedia.lyrics=islandCues;
            }
            for(std::size_t i=0;i<islandMedia.spectrum.size();++i) {
                const auto begin=i*music::kMusicAnalysisBins/islandMedia.spectrum.size();
                const auto end=std::max(begin+1,(i+1)*music::kMusicAnalysisBins/islandMedia.spectrum.size());
                float peak=0;for(auto n=begin;n<end;++n)peak=std::max(peak,static_cast<float>(snapshot.analysis.bins[n]));
                islandMedia.spectrum[i]=peak;
            }
        }
        dynamicIsland.Update(std::move(islandMedia));
        if(musicLibraryRestored&&!options.musicLibrary.root.empty())musicLibrary.SaveQueue(snapshot);
        for(const auto& state:musicBackground.Drain())Post(state);
    }
    void PostCurrentState(){
        if(!ready||exiting)return;
        Post(ui::ClockPageState(clock));
        Post(calendar.State());
        Post(performance.State());
        Post(spectrum.State());
        Post(almanac.State());
        Post(todayTodo.State());
        Post(weather.State());
        Post(organizer.State());
        if(options.radialVisible)Post(radial.State());
        Post(dock.State());
        Post(dynamicIsland.State());
        Post(desktopIcons.State());
        Post(taskbar.State());
        if(options.minimizeVisible)Post(minimize.State());
        const auto wallpaperState=wallpaper.State();
        if(!wallpaperState.empty())Post(wallpaperState);
        Post(musicBackground.State());
        Post(applicationSettings.State());
        Post(account.State());
        Post(account.WallpaperState());
        Post(music.State());
        Post(musicAccounts.State());
        Post(musicDesktop.State());
        if(options.notesVisible)Post(notes.State());
        if(!options.musicLibrary.root.empty())musicLibrary.Page();
        if(!options.musicPreferencesPath.empty())Post(musicPreferences.State());
        ReminderStatus();
        PostDiagnosticsState();
    }
    void Open(ManagerPage page){
        if(exiting||(!options.radialVisible&&page==ManagerPage::Radial)||(!options.minimizeVisible&&page==ManagerPage::MinimizeAnimation))return;
        navigation=page;
        if(visible){window.Present();Navigate();}
        else {opening=true;Wake();}
    }
    void CloseManager(){radial.CancelCapture();calendar.CancelEditor();window.RequestClose();}
    void Exit(){
        if(exiting)return;
        exiting=true;updateNotifications.Shutdown();applicationSettings.SetUpdateObserver({});reminders.Shutdown();calendar.CancelEditor();
        // WM_QUIT also unwinds a native editor, which reposts it to the page loop.
        if(window.Handle())window.RequestClose();else {awaitingQuit=true;PostQuitMessage(0);}
        Wake();
    }
    void Tray(TrayAction action){
        switch(action){case TrayAction::Exit:Exit();break;
        // The self-check notice (and the hidden legacy diagnostics notice).
        case TrayAction::OpenDiagnostics:openDiagnosticsOnNext=true;Open(ManagerPage::ApplicationSettings);break;
        case TrayAction::ExportDiagnostics:CreateReport({});break;
        case TrayAction::Open:Open(ManagerPage::Home);break;}
    }
    void ClockAction(clock::Action action){
        if(exiting)return;
        if(action.type==clock::ActionType::OpenSettings){Open(ManagerPage::Clock);return;}
        const bool ok=clock.Handle(action);Post(ui::ClockPageState(clock));
        if(!ok)Open(ManagerPage::Clock);
        Wake();
    }
    void Dispatch(ui::PageCommand command){
        if(exiting)return;
        if(command==ui::PageCommand::Ready){
            ready=true;PostCurrentState();
            if(!exiting)Navigate();
            if(!exiting)updateNotifications.RetryPresentation();
            return;
        }
        if(command==ui::PageCommand::PerformanceResourceSnapshot) {
            Post(resources::EncodeResourceSnapshot(resources::SampleProcessTree(GetCurrentProcessId())));
            return;
        }
        diagnosticOperations.Begin(command,[this]{return nlohmann::json::parse(taskbar.State()).value("revision",0ULL);});
        if(const auto action=ui::ClockActionForPage(command))ClockAction(*action);
        else if(const auto message=calendar.HandlePage(command,window.Handle())){Post(*message);reminders.UpdateAppearance(Appearance());Wake();}
        else if(const auto performanceMessage=performance.HandlePage(command)){Post(*performanceMessage);Wake();}
        else if(const auto spectrumMessage=spectrum.HandlePage(command)){Post(*spectrumMessage);Wake();}
        else if(const auto todayMessage=todayTodo.HandlePage(command)){Post(*todayMessage);Wake();}
        else if(const auto almanacMessage=almanac.HandlePage(command)){Post(*almanacMessage);Wake();}
        else if(const auto weatherMessage=weather.HandlePage(command)){Post(*weatherMessage);dynamicIsland.RefreshWeather();Wake();}
        else if(const auto organizerMessage=organizer.HandlePage(command)){Post(*organizerMessage);Wake();}
        else if(const auto radialMessage=RadialReply(command)){Post(*radialMessage);Wake();}
        else if(const auto dockMessage=dock.HandlePage(command)){Post(*dockMessage);Wake();}
        else if(const auto islandMessage=dynamicIsland.HandlePage(command)){Post(*islandMessage);Wake();}
        else if(const auto iconsMessage=desktopIcons.HandlePage(command)){Post(*iconsMessage);Wake();}
        else if(const auto next=ui::TaskbarPreferencesForPage(command,taskbar.Preferences())) {
            taskbar.Apply(*next);Post(taskbar.State());Wake();
        }
        else if(const auto minimizePreferences=options.minimizeVisible?ui::MinimizePreferencesForPage(command,minimize.Preferences()):std::nullopt) {
            minimize.Apply(*minimizePreferences);Wake();
        }
    }
    int Run(){
        if(ran)return 1;
        ran=true;thread=GetCurrentThreadId();MSG queue{};PeekMessageW(&queue,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
        const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(initialized))return 1;
        // The wallpaper first: its player starts on its own thread and process,
        // so after sign-in it appears while the widgets are still being built.
        wallpaper.Restore();
        clock.Restore();calendar.Restore();performance.Restore();spectrum.Restore();almanac.Restore();todayTodo.Restore();weather.Restore();organizer.Restore();desktopIcons.Restore();taskbar.Restore();
        if(options.minimizeVisible)minimize.Restore();
        if(options.radialVisible)radial.Restore();
        dock.Restore();
        dynamicIsland.SetEnglish(applicationSettings.Preferences().language=="en");dynamicIsland.Restore();
        musicBackground.Restore();
        if(!options.musicLibrary.root.empty())musicLibrary.Restore();
        if(!options.musicDesktop.path.empty())musicDesktop.Restore();
        if(options.notesVisible)notes.Restore();
        applicationSettings.Restore();account.Restore();
        // Capture initial state only when the diagnostics feature is enabled.
        if constexpr(diagnostics::FeatureEnabled)for(const auto& snapshot:{ui::ClockPageState(clock),calendar.State(),performance.State(),spectrum.State(),
            almanac.State(),todayTodo.State(),weather.State(),organizer.State(),dock.State(),desktopIcons.State(),
            taskbar.State(),minimize.State(),wallpaper.State(),applicationSettings.State()})
            RecordDiagnosticMessage(Utf8(snapshot));
        updateNotifications.SetLanguage(applicationSettings.Preferences().language=="en");updateNotifications.Start();
        reminders.UpdateAppearance(Appearance());remindersStarted=true;reminders.Start();
        // Restack desktop widgets when Explorer replaces the desktop host after
        // they were shown (late desktop at boot, wallpaper WorkerW, Explorer restart).
        lume::platform::windows::desktop_level::Watcher desktopLevel;
        if(!options.verification)desktopLevel.Start(instance);
        ApplicationTray tray;
        const bool trayAvailable=tray.Create(instance,[this](TrayAction action){Tray(action);},
            [this](const std::wstring& callback){if(!exiting){account.HandleCallback(callback);Open(ManagerPage::Account);}},
            diagnostics::FeatureEnabled?std::function<void()>([this]{ObserveNativeEffects();}):std::function<void()>{});
        diagnosticTray=trayAvailable?&tray:nullptr;
        // The tray window lives on this UI thread: it answers the hang probe.
        if(trayAvailable&&!options.verification)lume::platform::windows::diag::StartHangWatch(tray.Window());
        if(trayAvailable&&!options.verification)StartDiagnosticJob(DiagnosticJob::StartupCheck,{});
        if(!options.initialLoginCallback.empty()) {
            account.HandleCallback(options.initialLoginCallback);
            options.initialLoginCallback.clear();Open(ManagerPage::Account);
        }
        int result=0;
        bool showManager=!options.startHidden||opening;
        do {
            if(showManager) {
                opening=false;visible=true;ready=false;dock.Refresh();
                result=window.Run(instance,[this](ui::PageCommand command){Dispatch(command);return std::wstring{};},options.verification,options.readyScript);
                ui::MusicPageCommand closeMusic;
                closeMusic.action=ui::MusicAction::Close;
                music.HandlePage(closeMusic);
                musicAccounts.CancelActive();
                musicOnline.Cancel();
                musicLibrary.SetVisible(false);
                musicFonts.Cancel();musicBackground.CancelActive();musicBackground.SetVisible(false);
                visible=false;ready=false;radial.CancelCapture();calendar.CancelEditor();dock.Refresh();
                ui::WallpaperPageCommand releasePreview;releasePreview.action=ui::WallpaperAction::EndPreview;
                wallpaper.HandlePage(releasePreview);
                account.HandleWallpaper({ui::WallpaperAction::EndStats});
            }
            // WebView Run consumes its own WM_QUIT and releases all COM ownership.
            // Retain this same native composition until its final widget closes.
            // Exit must let GetMessage consume the quit, including a native
            // editor's repost. PostQuitMessage is synthesized only after pending
            // messages, so a filtered PeekMessage is not a reliable drain.
            // Dropped animation frames are traced beside diagnostics.json.
            if(const auto slash=options.diagnosticsPath.find_last_of("\\/");slash!=std::string::npos)
                lume::platform::windows::ui_stall::SetLogPath(lume::text::Wide(options.diagnosticsPath.substr(0,slash+1)+"launchpad-frames.log"));
            while((!exiting||awaitingQuit)&&!opening&&(options.startHidden||clock.Running()||calendar.Running()||performance.Handle()||spectrum.Handle()||todayTodo.Handle()||almanac.Handle()||weather.Handle()||organizer.Handle()||radial.Running()||dock.Running()||dock.Snapshot().enabled||dynamicIsland.Running()||desktopIcons.Running()||desktopIcons.HasRecovery()||taskbar.HasWork()||
                (taskbar.Preferences().enabled&&taskbar.Preferences().transparentTaskbar)||minimize.Running()||wallpaper.HasWork()||reminders.HasWork()||applicationSettings.HasWork()||account.HasWork()||music.HasWork()||musicAccounts.HasWork()||musicOnline.HasWork()||musicLibrary.HasWork()||musicDesktop.Running()||notes.Active()||musicBackground.HasWork()||updateNotifications.HasWork())&&trayAvailable){
                MSG message{};const int got=GetMessageW(&message,nullptr,0,0);
                if(got<=0){exiting=true;awaitingQuit=false;break;}
                if(message.message==lume::window_ids::ShellWakeMessage){DrainMusic();continue;}
                TranslateMessage(&message);lume::platform::windows::ui_stall::Run(message);
            }
            showManager=opening;
        }while(!exiting&&opening);
        todos->SetChanged({});ShutdownApplicationServices();reminders.Shutdown();ShutdownWidgets();
        if(!minimize.Shutdown())result=1;
        if(!taskbar.Shutdown())result=1;
        diagnosticTray=nullptr;thread=0;CoUninitialize();return result;
    }
    HINSTANCE instance;
    DesktopApplicationOptions options;
    LocalSettingsFileStore files;
    diagnostics::Collector diagnostics;
    DiagnosticOperations diagnosticOperations{diagnostics};
    settings::JsonSettingsCodec codec;
    settings::SettingsStore store{files,codec};
    settings::SettingsSnapshot preferences;
    NativeClockRuntime clockRuntime;
    clock::ClockController clock;
    std::shared_ptr<TodoSession> todos;
    CalendarSession calendar;
    TodayTodoSession todayTodo;
    PerformanceSession performance;
    SpectrumSession spectrum;
    AlmanacSession almanac;
    WeatherSession weather;
    OrganizerSession organizer;
    RadialSession radial;
    DockSession dock;
    platform::windows::SystemMediaSession systemMedia;
    ExternalLyrics externalLyrics;
    std::string islandCuesKey;
    dynamic_island::LyricCues islandCues;
    std::string dockCoverKey;
    std::shared_ptr<const platform::windows::WidgetBitmap> dockCover;
    DynamicIslandSession dynamicIsland;
    lume::platform::windows::MusicCoverResolver islandCovers{[this]{Wake();}};
    lume::platform::windows::CalendarTodoEditor islandEditor;
    IslandTodoAdapter islandTodos{todos};
    DesktopIconsSession desktopIcons;
    TaskbarSession taskbar;
    MinimizeAnimationSession minimize;
    ApplicationSettingsSession applicationSettings;
    WallpaperSession wallpaper;
    MusicBackgroundSession musicBackground;
    AccountSession account;
    MusicSession music;
    MusicProviderAccountSession musicAccounts;
    MusicOnlineSession musicOnline;
    MusicLibraryWorker musicLibrary;
    MusicDesktopSession musicDesktop;
    NotesSession notes;
    std::uint64_t musicLibraryRevision=0;
    bool musicLibraryRestored=false;
    nlohmann::json musicLibraryState={{"version",1},{"type","music-library-state"},{"loaded",false},
        {"error",""},{"total",0},{"offset",0},{"limit",100},{"tracks",nlohmann::json::array()}};
    MusicPreferencesSession musicPreferences;
    MusicFontAssetSession musicFonts;
    UpdateNotificationSession updateNotifications;
    std::deque<ui::MusicPageCommand> musicCommands;
    LocalReminderRuntime reminders;
    WebViewPageWindow window;
    std::optional<ManagerPage> navigation;
    ApplicationTray* diagnosticTray=nullptr;
    ULONGLONG lastDiagnosticNotice=0;
    bool openDiagnosticsOnNext=false;
    ULONGLONG dockNotRunningSince=0;
    bool radialShortcutSettings=false;
    DWORD thread=0;
    bool visible=false,ready=false,opening=false,exiting=false,ran=false,awaitingQuit=false,remindersStarted=false;
    std::map<std::string,std::string> tracedStates;
    std::mutex reportMutex;
    std::optional<DiagnosticJobResult> reportResult;
    bool reportRunning=false;
    // Last member: joined first on destruction, while everything it uses lives.
    struct Worker {std::thread thread;~Worker(){if(thread.joinable())thread.join();}} reportWorker;
};
DesktopApplication::DesktopApplication(HINSTANCE instance,DesktopApplicationOptions options):state_(std::make_unique<State>(instance,std::move(options))){}
DesktopApplication::~DesktopApplication()=default;
int DesktopApplication::Run(){return state_->Run();}
void DesktopApplication::Dispatch(ui::PageCommand command){state_->Dispatch(command);}
void DesktopApplication::Open(ManagerPage page){state_->Open(page);}
void DesktopApplication::CloseManager(){state_->CloseManager();}
void DesktopApplication::Exit(){state_->Exit();}
void DesktopApplication::OnTray(TrayAction action){state_->Tray(action);}
bool DesktopApplication::ClockRunning()const{return state_->clock.Running();}
bool DesktopApplication::CalendarRunning()const{return state_->calendar.Running();}
HWND DesktopApplication::CalendarHandle()const{return state_->calendar.Handle();}
HWND DesktopApplication::ManagerHandle()const{return state_->window.Handle();}
bool DesktopApplication::ManagerReady()const{return state_->ready&&state_->window.Handle();}
}
int RunDesktopApplication(HINSTANCE instance){
    const auto launch=lume::platform::windows::CurrentShellLaunch();
    if(launch.kind==lume::platform::windows::ShellLaunchKind::Invalid)return 2;
    HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\LumeNativeShell1021");
    if(!singleton)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){
        HWND existing=FindWindowW(lume::window_ids::TrayClass,nullptr);
        if(launch.kind==lume::platform::windows::ShellLaunchKind::LoginCallback) {
            const auto deadline=GetTickCount64()+2000;
            while(!existing&&GetTickCount64()<deadline){Sleep(40);existing=FindWindowW(lume::window_ids::TrayClass,nullptr);}
            const bool forwarded=lume::platform::windows::ForwardLoginActivation(existing,launch.callback);
            CloseHandle(singleton);return forwarded?0:2;
        }
        if(existing&&launch.kind!=lume::platform::windows::ShellLaunchKind::TrayOnly)PostMessageW(existing,lume::window_ids::TrayOpenMessage,0,0);
        CloseHandle(singleton);return 0;
    }
    lume::shell::DesktopApplicationOptions options;
    // Before anything resolves a data path: honour a folder chosen in Settings.
    std::string dataMoveError;
    lume::platform::windows::ApplyPendingDataMove(dataMoveError);
    if(const auto dataRoot=lume::platform::windows::GetUserDataRootPath();!dataRoot.empty())
        lume::platform::windows::diag::Initialize(std::filesystem::u8path(dataRoot)/L"Diagnostics");
    lume::platform::windows::diag::InstallCrashHandler();
    lume::platform::windows::diag::Log("app",std::string("Lume ")+lume::application_settings::CurrentVersion+" started"+
        (launch.kind==lume::platform::windows::ShellLaunchKind::TrayOnly?" at sign-in (tray only)":"")+", pid "+std::to_string(GetCurrentProcessId()));
    // The environment once per run, off the startup path.
    std::thread([]{
        try {
            const auto system=lume::shell::DescribeSystem();
            lume::platform::windows::diag::Log("app","environment "+nlohmann::json{{"windows",system["windows"]},{"gpus",system["hardware"]["gpus"]},
                {"displays",system["displays"]},{"appearance",system["appearance"]},{"shell",system["shell"]}}.dump(-1,' ',false,nlohmann::json::error_handler_t::replace));
        } catch(...) {}
    }).detach();
    options.startHidden=launch.kind==lume::platform::windows::ShellLaunchKind::TrayOnly;
    // Temporary product visibility only; saved radial and notes files remain
    // untouched so both features can be restored for the next iteration.
    options.radialVisible=false;
    options.notesVisible=false;
    // The macOS minimize animation is likewise set aside for now: no entry, not
    // started, its preferences kept.
    options.minimizeVisible=false;
    options.initialLoginCallback=launch.callback;
    options.applicationSettings.preferencesPath=lume::platform::windows::GetApplicationSettingsPath();
    // The settings page only displays these; resolved here so the session stays
    // free of platform dependencies.
    options.applicationSettings.dataDirectory=lume::platform::windows::GetUserDataRootPath();
    options.applicationSettings.setDataDirectory=[](const std::string& chosen){
        return lume::platform::windows::SetDataDirectoryChoice(chosen);
    };
    options.applicationSettings.wallpaperDirectory=[](const std::string& preferred){
        return lume::platform::windows::GetWallpaperRootPath(preferred);
    };
    options.diagnosticsPath=lume::platform::windows::GetDiagnosticsPath();
    if(!options.applicationSettings.preferencesPath.empty())
        options.updateNotification.statePath=(std::filesystem::u8path(options.applicationSettings.preferencesPath).parent_path()/L"update-notification.json").u8string();
    options.accountOptions.storageDirectory=lume::platform::windows::GetAccountRootPath();
    options.clockPreferencesPath=lume::platform::windows::GetUiPreferencesPath();
    options.calendarPreferencesPath=lume::platform::windows::GetCalendarPreferencesPath();
    options.performancePreferencesPath=lume::platform::windows::GetPerformancePreferencesPath();
    options.spectrumPreferencesPath=lume::platform::windows::GetSpectrumPreferencesPath();
    options.almanacPreferencesPath=lume::platform::windows::GetAlmanacPreferencesPath();
    options.todayTodoPreferencesPath=lume::platform::windows::GetTodayTodoPreferencesPath();
    options.weatherPreferencesPath=lume::platform::windows::GetWeatherPreferencesPath();
    options.organizerPreferencesPath=lume::platform::windows::GetOrganizerPreferencesPath();
    options.radial.preferencesPath=lume::platform::windows::GetRadialPreferencesPath();
    options.dock.preferencesPath=lume::platform::windows::GetDockPreferencesPath();
    options.dynamicIslandPreferencesPath=lume::platform::windows::GetDynamicIslandPreferencesPath();
    options.desktopIconsPreferencesPath=lume::platform::windows::GetDesktopIconsPreferencesPath();
    options.taskbarPaths.stateFile=lume::platform::windows::GetTaskbarPreferencesPath();
    if(!options.taskbarPaths.stateFile.empty())
        options.taskbarPaths.cacheRoot=std::filesystem::u8path(options.taskbarPaths.stateFile).parent_path()/L"native-taskbar-runtime";
    std::wstring executable(32768,L'\0');
    const auto executableLength=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(executableLength&&executableLength<executable.size()) {
        executable.resize(executableLength);
        options.taskbarPaths.payloadRoot=std::filesystem::path(executable).parent_path()/L"taskbar-runtime";
        options.wallpaperOptions.executable=(std::filesystem::path(executable).parent_path()/L"wallpaper-runtime"/L"mpv.exe").wstring();
        options.music.executable=(std::filesystem::path(executable).parent_path()/L"LumeMusicHost.exe").wstring();
    }
    // 音符律动 melody analysis: whole-song decode and analysis on the session worker.
    options.music.analyzeScore=[](const std::string& source,const std::function<bool()>& cancelled){
        const auto audio=lume::music::DecodeScoreAudio(source,cancelled);
        return audio.mono.empty()?lume::music::MelodyAnalysis{}:lume::music::AnalyzeMelody(audio.mono,audio.rate,cancelled);
    };
    options.taskbarPaths.allowSystemEffects=true;
    options.minimizePreferencesPath=lume::platform::windows::GetMinimizePreferencesPath();
    options.musicPreferencesPath=lume::platform::windows::GetMusicPreferencesPath();
    if(!options.musicPreferencesPath.empty()) {
        const auto directory=std::filesystem::u8path(options.musicPreferencesPath).parent_path();
        options.musicLibrary.root=directory/L"music-library";
        options.musicDesktop.path=(directory/L"music-widget.json").u8string();
        options.notesPath=(directory/L"notes.json").u8string();
        options.notesWidgetPath=(directory/L"notes-widget.json").u8string();
    }
    if(!options.musicPreferencesPath.empty())
        options.musicAccounts.root=std::filesystem::u8path(options.musicPreferencesPath).parent_path()/L"music-accounts";
    // Folia-style login self-check: name the broken link (DNS/IPv6/TLS/timeout/proxy).
    options.musicAccounts.diagnoseHost=[](const std::string& host){
        return lume::platform::windows::LoginVerdictToken(lume::platform::windows::DiagnoseLoginHost(host));
    };
    if(!options.musicPreferencesPath.empty())
        options.musicFonts.root=std::filesystem::u8path(options.musicPreferencesPath).parent_path()/L"music-fonts";
    options.musicBackground.root=lume::platform::windows::GetMusicBackgroundRootPath();
    options.musicBackground.statePath=lume::platform::windows::GetMusicBackgroundPreferencesPath();
    // The wallpaper folder is resolved once, before any window exists: an
    // explicit choice from Settings wins, otherwise it follows the program.
    std::string wallpaperDirectory;
    if(!options.applicationSettings.preferencesPath.empty()){
        lume::platform::windows::LocalSettingsFileStore settingsFile;
        std::string bytes;
        if(settingsFile.Read(options.applicationSettings.preferencesPath,bytes)){
            const auto decoded=lume::application_settings::DecodePreferences(bytes);
            if(decoded.error.empty())wallpaperDirectory=decoded.preferences.wallpaperDirectory;
        }
    }
    options.wallpaperOptions.root=lume::platform::windows::GetWallpaperRootPath(wallpaperDirectory);
    int result=1;
    {lume::shell::DesktopApplication app(instance,std::move(options));result=app.Run();}
    lume::platform::windows::diag::Log("app","exited, code "+std::to_string(result));
    ReleaseMutex(singleton);CloseHandle(singleton);return result;
}
