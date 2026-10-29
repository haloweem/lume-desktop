#pragma once
#include "core/settings/clock_settings.h"
#include <functional>

namespace lume::clock {
class IClockRuntime {
public:
    virtual ~IClockRuntime() = default;
    virtual bool Apply(bool enabled, Style style, const Appearance& appearance) = 0;
    virtual bool IsRunning() const = 0;
    virtual void Center() = 0;
    virtual void Shutdown() = 0;
};
class ClockController final {
public:
    using Save = std::function<bool(const settings::ClockSettings&)>;
    ClockController(settings::ClockSettings initial, IClockRuntime& runtime, Save save);
    bool Restore();
    bool Handle(Action action);
    void Shutdown();
    const settings::ClockSettings& Snapshot() const { return state_; }
    bool Running() const { return runtime_.IsRunning(); }
    const char* Error() const { return error_; }
private:
    settings::ClockSettings state_;
    IClockRuntime& runtime_;
    Save save_;
    const char* error_ = "";
};
}
