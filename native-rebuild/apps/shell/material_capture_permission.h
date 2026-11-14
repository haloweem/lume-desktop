#pragma once
#include "core/ui/page_bridge.h"
#include <optional>
#include <utility>

namespace lume::shell {
inline constexpr int MaterialCaptureAccessAllowed = 4;
inline constexpr int MaterialCaptureAccessPromptRequired = 3;

inline bool IsGlassMaterialSelection(ui::PageCommand command) noexcept {
    using C = ui::PageCommand;
    switch (command) {
    case C::CalendarLiquidGlass: case C::CalendarFrostedGlass:
    case C::PerformanceLiquidGlass: case C::PerformanceFrostedGlass:
    case C::AlmanacLiquidGlass: case C::AlmanacFrostedGlass:
    case C::WeatherLiquidGlass: case C::WeatherFrostedGlass:
    case C::OrganizerLiquidGlass: case C::OrganizerFrostedGlass:
    case C::RadialLiquidGlass: case C::RadialFrostedGlass:
    case C::DockLiquidGlass: case C::DockFrostedGlass:
    case C::TodayTodoLiquidGlass: case C::TodayTodoFrostedGlass:
    case C::IslandMaterialLiquid: case C::IslandMaterialFrosted:
        return true;
    default: return false;
    }
}
inline bool IsMaterialSelection(ui::PageCommand command) noexcept {
    using C = ui::PageCommand;
    return IsGlassMaterialSelection(command) || command == C::CalendarSolid ||
        command == C::TodayTodoSolid || command == C::DockNoMaterial || command == C::IslandMaterialNone;
}
inline bool ShouldRequestMaterialCaptureAccess(ui::PageCommand command, bool verification, int access) noexcept {
    return !verification && IsGlassMaterialSelection(command) && access == MaterialCaptureAccessPromptRequired;
}

// UI-thread-only selection ownership. A second choice replaces the deferred
// command without starting another consent operation or applying either twice.
class MaterialCapturePermissionChoice {
public:
    bool Pending() const noexcept { return command_.has_value(); }
    bool Replace(ui::PageCommand command) noexcept {
        if (!Pending() || !IsMaterialSelection(command)) return false;
        command_ = command; return true;
    }
    void Begin(ui::PageCommand command) noexcept { command_ = command; }
    std::optional<ui::PageCommand> Finish() noexcept { return std::exchange(command_, std::nullopt); }
    void Cancel() noexcept { command_.reset(); }
private:
    std::optional<ui::PageCommand> command_;
};
} // namespace lume::shell
