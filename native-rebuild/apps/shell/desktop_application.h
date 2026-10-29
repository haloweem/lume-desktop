#pragma once
#include "core/ui/page_bridge.h"
#include "apps/shell/wallpaper_session.h"
#include "apps/shell/application_settings_session.h"
#include "apps/shell/update_notification_session.h"
#include "apps/shell/account_session.h"
#include "apps/shell/music_session.h"
#include "apps/shell/music_background_session.h"
#include "apps/shell/music_provider_account_session.h"
#include "apps/shell/music_online_session.h"
#include "apps/shell/music_library_worker.h"
#include "apps/shell/music_desktop_session.h"
#include "apps/shell/music_font_asset_session.h"
#include "apps/shell/radial_session.h"
#include "apps/shell/dock_session.h"
#include "apps/shell/dynamic_island_session.h"
#include "platform/windows/dynamic_island/system_media_session.h"
#include "platform/windows/application_tray.h"
#include "platform/windows/desktop_icons/desktop_icons_runtime.h"
#include "platform/windows/taskbar/taskbar_runtime.h"
#include "platform/windows/minimize_animation/minimize_animation_runtime.h"
#include <functional>
#include <memory>
#include <string>

namespace lume::shell {
enum class ManagerPage { Home,Clock,Calendar,Performance,Almanac,Weather,Organizer,Icons,Taskbar,MinimizeAnimation,ApplicationSettings,Account,Music,MusicDesktop,Radial,Dock,TodayTodo,DynamicIsland,Spectrum };
struct DesktopApplicationOptions {
    // Explicit paths are required. Production resolves them in RunDesktopApplication;
    // same-process verification supplies paths below its own build-artifacts folder.
    std::string clockPreferencesPath;
    std::string calendarPreferencesPath;
    std::string performancePreferencesPath;
    std::string spectrumPreferencesPath;
    std::string almanacPreferencesPath;
    std::string weatherPreferencesPath;
    std::string todayTodoPreferencesPath;
    std::string dynamicIslandPreferencesPath;
    std::string organizerPreferencesPath;
    RadialSessionOptions radial;
    DockSessionOptions dock;
    std::string desktopIconsPreferencesPath;
    platform::windows::TaskbarPaths taskbarPaths;
    std::string minimizePreferencesPath;
    std::string diagnosticsPath;
    WallpaperSessionOptions wallpaperOptions;
    ApplicationSettingsOptions applicationSettings;
    UpdateNotificationOptions updateNotification;
    AccountSessionOptions accountOptions;
    MusicSessionOptions music;
    std::string musicPreferencesPath;
    MusicBackgroundSessionOptions musicBackground;
    MusicProviderAccountOptions musicAccounts;
    MusicOnlineOptions musicOnline;
    MusicLibraryWorkerOptions musicLibrary;
    MusicDesktopSessionOptions musicDesktop;
    MusicFontAssetSessionOptions musicFonts;
    // The local notes document and the desktop note widget preferences.
    std::string notesPath,notesWidgetPath;
    // Production can temporarily hide these unfinished features without
    // changing their saved preferences or notes document.
    bool radialVisible=true,notesVisible=true,minimizeVisible=true;
    bool startHidden=false;
    std::wstring initialLoginCallback;
    // Used only when verification=true; production always discovers actual Explorer desktops.
    platform::windows::DesktopIconsRuntime::Discover verificationDesktopIcons;
    platform::windows::TaskbarRuntime::PortFactory verificationTaskbar;
    platform::windows::MinimizeAnimationScope verificationMinimizeScope;
    // Test-only; ignored unless verification is enabled.
    std::function<bool()> verificationReminderAllowed;
    bool verification=false;
    std::wstring readyScript;
    std::function<void(const std::wstring&)> onMessage;
};
class DesktopApplication final {
public:
    DesktopApplication(HINSTANCE instance,DesktopApplicationOptions options);
    ~DesktopApplication();
    DesktopApplication(const DesktopApplication&)=delete;
    DesktopApplication& operator=(const DesktopApplication&)=delete;
    int Run();
    void Dispatch(ui::PageCommand command);
    void Open(ManagerPage page=ManagerPage::Home);
    void CloseManager();
    void Exit();
    void OnTray(platform::windows::TrayAction action);
    bool ClockRunning() const;
    bool CalendarRunning() const;
    HWND CalendarHandle() const;
    HWND ManagerHandle() const;
    bool ManagerReady() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
int RunDesktopApplication(HINSTANCE instance);
