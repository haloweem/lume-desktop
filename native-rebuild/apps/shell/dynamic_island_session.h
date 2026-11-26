#pragma once
#include "apps/shell/material_selection_request.h"

#include "core/dynamic_island/island_model.h"
#include "core/settings/file_store.h"
#include "core/ui/page_bridge.h"
#include "core/widget/widget_preferences.h"
#include "apps/shell/island_reminder_scheduler.h"
#include "platform/windows/dynamic_island/island_window.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lume::shell {
struct IslandCountdownSettings {
    std::string title;
    int date = 0;
    bool reminder = false;
    platform::windows::IslandCountdownStyle style = platform::windows::IslandCountdownStyle::Standard;
    std::uint32_t textColor = 0x262626;
    std::uint32_t backgroundColor = 0x2183c0;
    std::uint32_t solidColor = 0x8264a5;
    unsigned textAlpha = 255;
    unsigned backgroundAlpha = 255;
    unsigned solidAlpha = 255;
    bool countUp = false;
};
struct DynamicIslandSessionOptions {
    std::string preferencesPath;
    // UI-thread ports. The host validates media identity and queues playback work.
    std::function<void(const std::wstring&)> notify;
    std::function<void(dynamic_island::Action, const dynamic_island::Media&)> control;
    std::function<std::optional<dynamic_island::Media>()> externalMedia;
    std::function<void(dynamic_island::Action)> externalControl;
    std::function<void(bool playing)> externalSetPlaying;
    std::function<void()> openSettings;
    platform::windows::IslandWindow::EditWidget editWidget;
    std::function<std::uint64_t()> monotonicMillis;
    // The calendar todo store shared with the calendar and today todo widgets.
    platform::windows::IslandTodoSource todos;
    // Current weather, read on demand from the weather component. The island
    // never fetches, locates or stores weather itself.
    std::function<dynamic_island::Weather()> weather;
    // UTC milliseconds of the provider snapshot, so stale cached readings do
    // not generate weather alerts. Zero means that freshness is unknown.
    std::function<std::uint64_t()> weatherUpdatedAt;
    // Optional deterministic context for isolated reminder verification.
    std::function<IslandReminderTick()> notificationContext;
    std::function<bool()> notificationAllowed;
    std::function<void()> openWeatherSettings;
    std::function<void()> openPerformanceSettings;
    std::function<void(bool)> weatherActive;
    // Bundled Forest sample background; independent of the user's desktop.
    std::function<std::wstring()> previewWallpaper;
    // Optional display inventory port; production reads the connected Windows monitors.
    std::function<std::vector<platform::windows::IslandMonitor>()> monitors;
    // Optional isolated voice ports. Production uses a dedicated WAV device;
    // verification can observe synchronized presentation without audible output.
    std::function<void(bool)> reminderVoice;
    std::function<float()> reminderVoiceLevel;
    std::function<std::optional<unsigned>()> reminderVoicePosition;
};

class DynamicIslandSession final {
public:
    DynamicIslandSession(HINSTANCE, DynamicIslandSessionOptions, settings::IFileStore* = nullptr);
    ~DynamicIslandSession();
    bool Restore();
    void SetMaterialSelectionHandler(MaterialSelectionHandler);
    void Open();
    std::optional<std::wstring> HandlePage(ui::PageCommand);
    std::optional<std::wstring> HandleNotifications(std::wstring_view);
    std::wstring NotificationsState() const;
    void PollNotifications();
    std::wstring State() const;
    void Update(dynamic_island::Media);
    void SetEnglish(bool);
    void RefreshTodos();
    // Re-reads the weather component for compact widgets.
    void RefreshWeather();
    IslandCountdownSettings CountdownSettings() const;
    bool SaveCountdownSettings(IslandCountdownSettings);
    void ReleasePreview();
    bool ShowNotice(const std::string& taskId,const std::wstring& title,const std::wstring& time);
    const widget::Preferences& Snapshot() const;
    void Shutdown();
    bool Running() const;
    // Atomic consumer state, safe for the music worker to query.
    bool AnalysisActive() const;
    HWND Handle() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
