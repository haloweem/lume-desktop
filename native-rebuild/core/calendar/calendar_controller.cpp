#include "core/calendar/calendar_controller.h"
#include <utility>
namespace lume::calendar {
CalendarController::CalendarController(Preferences initial, ICalendarRuntime& runtime, Save save)
    : state_(std::move(initial)), runtime_(runtime), save_(std::move(save)) {}
bool CalendarController::Restore() {
    if (!ValidPreferences(state_)) { error_ = ControllerError::Invalid; return false; }
    const bool ok = runtime_.Apply(state_);
    error_ = ok ? ControllerError::None : ControllerError::Runtime;
    return ok;
}
bool CalendarController::Handle(Action action) {
    error_ = ControllerError::Invalid;
    if (!ValidPreferences(state_)) return false;
    auto next = state_;
    bool useCurrentPosition = false;
    switch (action.type) {
    case ActionType::Enable:
        if (action.value != 0 && action.value != 1) return false;
        next.enabled = action.value != 0;
        break;
    case ActionType::SelectTheme: {
        next.theme = static_cast<Theme>(action.value);
        auto* target = next.Active();
        if (!target) return false;
        // A visible calendar changes look in place: the new theme takes the
        // current position instead of jumping to where it was last used.
        useCurrentPosition = state_.enabled && runtime_.IsRunning();
        const auto* current = state_.Active();
        if (useCurrentPosition && current->hasPosition) {
            target->x = current->x;
            target->y = current->y;
            target->hasPosition = true;
        }
        break;
    }
    case ActionType::Material: next.material = static_cast<Material>(action.value); break;
    case ActionType::TextColor: next.textColor = {action.value,action.secondary}; break;
    case ActionType::HighlightColor: next.highlightColor = {action.value,action.secondary}; break;
    case ActionType::PanelColor:
        next.panelColor = {action.value,action.secondary};
        next.material = Material::Solid;
        break;
    case ActionType::BoardView:
        if (action.value < 0 || action.value > 3) return false;
        next.boardView = static_cast<BoardView>(action.value);
        break;
    case ActionType::Lock:
        if (action.value != 0 && action.value != 1) return false;
        next.locked = action.value != 0;
        break;
    case ActionType::Bounds:
        *next.Active() = {action.value,action.secondary,action.width,action.height,true};
        break;
    case ActionType::Reset: {
        next = Preferences{};
        next.enabled = state_.enabled;
        for (std::size_t i = 0; i < next.profiles.size(); ++i) {
            next.profiles[i].x = state_.profiles[i].x;
            next.profiles[i].y = state_.profiles[i].y;
            next.profiles[i].hasPosition = state_.profiles[i].hasPosition;
        }
        if (state_.enabled && runtime_.IsRunning() && state_.Active()->hasPosition) {
            next.Active()->x = state_.Active()->x;
            next.Active()->y = state_.Active()->y;
            next.Active()->hasPosition = true;
        }
        break;
    }
    default: return false;
    }
    if (!ValidPreferences(next)) return false;
    const bool applied = action.type == ActionType::Reset ? runtime_.ApplyReset(next) :
        action.type == ActionType::SelectTheme ? runtime_.ApplyTheme(next,useCurrentPosition) :
        action.type == ActionType::Bounds ? runtime_.ApplyBounds(next) : runtime_.Apply(next);
    if (!applied) {
        error_ = runtime_.ApplyRollback(state_) ? ControllerError::Runtime : ControllerError::Rollback;
        return false;
    }
    if (!save_ || !save_(next)) {
        error_ = runtime_.ApplyRollback(state_) ? ControllerError::Save : ControllerError::Rollback;
        return false;
    }
    state_ = std::move(next);
    error_ = ControllerError::None;
    return true;
}
void CalendarController::Shutdown() { runtime_.Shutdown(); }
}
