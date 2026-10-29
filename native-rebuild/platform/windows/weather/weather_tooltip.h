#pragma once
#include "platform/windows/weather/weather_worker.h"
#include <windows.h>
#include <array>
namespace lume::platform::windows {
using WeatherTipText=std::array<std::wstring,9>;
WeatherTipText WeatherTips(const WeatherReading&,const weather::LocalTime&);
// Weather-owned, created with the widget and destroyed before its owner.
// No polling, worker, browser, retained ICU handle or global cache.
class WeatherTooltip final {
public:
    ~WeatherTooltip();
    bool Attach(HWND);
    void Update(const WeatherReading&);
    void Resize(float width,float height);
    void Stop();
private:
    HWND owner_=nullptr,window_=nullptr;
    WeatherTipText text_;
    float width_=0,height_=0;
    unsigned dpi_=0;
};
}
