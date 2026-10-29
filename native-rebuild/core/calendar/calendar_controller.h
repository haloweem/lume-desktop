#pragma once
#include "core/calendar/calendar_settings.h"
#include <functional>

namespace lume::calendar {
class ICalendarRuntime {
public:
    virtual ~ICalendarRuntime() = default;
    virtual bool Apply(const Preferences& value) = 0;
    virtual bool ApplyBounds(const Preferences& value) { return Apply(value); }
    virtual bool ApplyTheme(Preferences& value, bool useCurrentPosition) { (void)useCurrentPosition; return Apply(value); }
    // A reset can preserve live geometry in its candidate before the single save.
    virtual bool ApplyReset(Preferences& value) { return Apply(value); }
    virtual bool ApplyRollback(const Preferences& value) { return Apply(value); }
    virtual bool IsRunning() const = 0;
    virtual void Shutdown() = 0;
};
enum class ActionType { Enable, SelectTheme, Material, TextColor, HighlightColor, Lock, Bounds, Reset, BoardView, PanelColor };
struct Action {
    ActionType type;
    int value = 0;
    int secondary = 0;
    int width = 0;
    int height = 0;
    // Automatic placement reports must not silently repair a failed/recovered load.
    bool automatic = false;
};
enum class ControllerError { None, Invalid, Runtime, Save, Rollback };
class CalendarController final {
public:
    using Save = std::function<bool(const Preferences&)>;
    CalendarController(Preferences initial, ICalendarRuntime& runtime, Save save);
    bool Restore();
    bool Handle(Action action);
    void Shutdown();
    const Preferences& Snapshot() const { return state_; }
    bool Running() const { return runtime_.IsRunning(); }
    ControllerError Error() const { return error_; }
private:
    Preferences state_;
    ICalendarRuntime& runtime_;
    Save save_;
    ControllerError error_ = ControllerError::None;
};
}  // namespace lume::calendar
