#include "core/clock/clock_controller.h"
#include <utility>
namespace lume::clock {
ClockController::ClockController(settings::ClockSettings initial, IClockRuntime& runtime, Save save)
    : state_(std::move(initial)), runtime_(runtime), save_(std::move(save)) {}
bool ClockController::Restore() {
    const bool ok = runtime_.Apply(state_.enabled, ParseStyle(state_.style), state_.Active());
    error_ = ok ? "" : "runtime";
    return ok;
}
bool ClockController::Handle(Action action) {
    if (action.type == ActionType::Center) {
        if (!Running()) return false;
        error_ = "";
        runtime_.Center();
        return *error_ == '\0';
    }
    auto next = state_;
    auto& p = next.Active();
    switch (action.type) {
    case ActionType::Enable:
        if (action.value != 0 && action.value != 1) return false;
        next.enabled = action.value != 0; break;
    case ActionType::SelectStyle:
        if (action.value < 0 || action.value > 2) return false;
        next.style = StyleName(static_cast<Style>(action.value));
        next.Active().x = state_.Active().x;
        next.Active().y = state_.Active().y;
        break;
    case ActionType::Scale: p.scale = action.value; break;
    case ActionType::Position: p.x = action.value; p.y = action.secondary; break;
    case ActionType::Color: p.color = action.value; p.alpha = action.secondary; p.customColor = true; break;
    case ActionType::Lock:
        if (action.value != 0 && action.value != 1) return false;
        p.locked = action.value != 0; break;
    case ActionType::Reset:
        next.style = "welfare";
        next.Active().x = state_.Active().x;
        next.Active().y = state_.Active().y;
        break;
    default: return false;
    }
    if (!ValidAppearance(next.Active())) return false;
    if (!runtime_.Apply(next.enabled, ParseStyle(next.style), next.Active())) {
        error_ = runtime_.Apply(state_.enabled, ParseStyle(state_.style), state_.Active()) ? "runtime" : "rollback";
        return false;
    }
    if (!save_(next)) {
        error_ = runtime_.Apply(state_.enabled, ParseStyle(state_.style), state_.Active()) ? "save" : "rollback";
        return false;
    }
    state_ = std::move(next);
    error_ = "";
    return true;
}
void ClockController::Shutdown() { runtime_.Shutdown(); }
}
