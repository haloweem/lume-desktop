#include "core/ui/clock_page_state.h"
namespace lume::ui {
std::optional<clock::Action> ClockActionForPage(PageCommand command) {
    using A = clock::ActionType;
    switch (command) {
    case PageCommand::ClockEnable: return clock::Action{A::Enable,1};
    case PageCommand::ClockDisable: return clock::Action{A::Enable,0};
    case PageCommand::ClockWelfare: return clock::Action{A::SelectStyle,0};
    case PageCommand::ClockMond: return clock::Action{A::SelectStyle,1};
    case PageCommand::ClockInsideDream: return clock::Action{A::SelectStyle,2};
    case PageCommand::ClockReset: return clock::Action{A::Reset};
    default: return std::nullopt;
    }
}
std::wstring ClockPageState(const clock::ClockController& controller) {
    const auto& clock = controller.Snapshot();
    const auto& p = clock.Active();
    const auto boolean = [](bool value) { return value ? L"true" : L"false"; };
    const std::string error(controller.Error());
    return std::wstring(L"{\"version\":1,\"type\":\"settings\",\"clock\":{\"enabled\":") + boolean(clock.enabled) +
        L",\"style\":\"" + std::wstring(clock.style.begin(), clock.style.end()) + L"\",\"running\":" + boolean(controller.Running()) +
        L",\"x\":" + std::to_wstring(p.x) + L",\"y\":" + std::to_wstring(p.y) + L",\"scale\":" + std::to_wstring(p.scale) +
        L",\"color\":" + std::to_wstring(p.color) + L",\"alpha\":" + std::to_wstring(p.alpha) +
        L",\"customColor\":" + boolean(p.customColor) + L",\"locked\":" + boolean(p.locked) + L"},\"saved\":" + boolean(error.empty()) +
        L",\"error\":\"" + std::wstring(error.begin(), error.end()) + L"\"}";
}
}
