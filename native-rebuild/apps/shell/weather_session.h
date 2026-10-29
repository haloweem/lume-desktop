#pragma once
#include "core/ui/page_bridge.h"
#include "core/weather/weather_preferences.h"
#include "platform/windows/weather/weather_worker.h"
#include <windows.h>
namespace lume::shell {
class WeatherSession final {
public:
    using Notify=std::function<void(const std::wstring&,bool)>;
    WeatherSession(HINSTANCE,std::string path,Notify,std::function<void()> settings,
        settings::IFileStore* files=nullptr,platform::windows::WeatherBackendFactory={},
        weather::Clock={});
    ~WeatherSession();
    bool Restore();
    // Keep the same weather worker alive for another view, without showing the widget.
    bool SetSharedConsumer(bool);
    std::optional<std::wstring> HandlePage(ui::PageCommand);
    std::wstring State()const;
    void Shutdown();
    bool Running()const;
    HWND Handle()const;
    const weather::Preferences& Snapshot()const;
    platform::windows::WeatherReading Reading()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
