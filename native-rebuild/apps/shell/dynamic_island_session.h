#pragma once

#include "core/dynamic_island/island_model.h"
#include "core/settings/file_store.h"
#include "core/ui/page_bridge.h"
#include "core/widget/widget_preferences.h"
#include "platform/windows/dynamic_island/island_window.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace lume::shell {
struct DynamicIslandSessionOptions {
    std::string preferencesPath;
    // UI-thread ports. The host validates media identity and queues playback work.
    std::function<void(const std::wstring&)> notify;
    std::function<void(dynamic_island::Action, const dynamic_island::Media&)> control;
    std::function<std::optional<dynamic_island::Media>()> externalMedia;
    std::function<void(dynamic_island::Action)> externalControl;
    std::function<void(bool playing)> externalSetPlaying;
    std::function<void()> openSettings;
    std::function<std::uint64_t()> monotonicMillis;
    // The calendar todo store shared with the calendar and today todo widgets.
    platform::windows::IslandTodoSource todos;
    // Current weather, read on demand from the weather component. The island
    // never fetches, locates or stores weather itself.
    std::function<dynamic_island::Weather()> weather;
    std::function<void()> openWeatherSettings;
    std::function<void(bool)> weatherActive;
};

class DynamicIslandSession final {
public:
    DynamicIslandSession(HINSTANCE, DynamicIslandSessionOptions, settings::IFileStore* = nullptr);
    ~DynamicIslandSession();
    bool Restore();
    void Open();
    std::optional<std::wstring> HandlePage(ui::PageCommand);
    std::wstring State() const;
    void Update(dynamic_island::Media);
    void SetEnglish(bool);
    void RefreshTodos();
    // Re-reads the weather component and repaints the weather page.
    void RefreshWeather();
    bool ShowNotice(const std::string& taskId,const std::wstring& title,const std::wstring& time);
    const widget::Preferences& Snapshot() const;
    void Shutdown();
    bool Running() const;
    HWND Handle() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
