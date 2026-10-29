#pragma once
#include "core/clock/clock_controller.h"
#include "core/ui/page_bridge.h"
#include <optional>
#include <string>
namespace lume::ui {
std::optional<clock::Action> ClockActionForPage(PageCommand command);
std::wstring ClockPageState(const clock::ClockController& controller);
}
