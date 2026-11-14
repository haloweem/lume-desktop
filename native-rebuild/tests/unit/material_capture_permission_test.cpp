#include "apps/shell/material_capture_permission.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace {
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace lume::shell;
    using C = lume::ui::PageCommand;
    constexpr C glassSelections[] = {
        C::CalendarLiquidGlass, C::CalendarFrostedGlass,
        C::PerformanceLiquidGlass, C::PerformanceFrostedGlass,
        C::AlmanacLiquidGlass, C::AlmanacFrostedGlass,
        C::WeatherLiquidGlass, C::WeatherFrostedGlass,
        C::OrganizerLiquidGlass, C::OrganizerFrostedGlass,
        C::RadialLiquidGlass, C::RadialFrostedGlass,
        C::DockLiquidGlass, C::DockFrostedGlass,
        C::TodayTodoLiquidGlass, C::TodayTodoFrostedGlass,
        C::IslandMaterialLiquid, C::IslandMaterialFrosted
    };
    for (const auto command : glassSelections) {
        Expect(IsGlassMaterialSelection(command), "every explicit glass selection is recognized");
        Expect(ShouldRequestMaterialCaptureAccess(command, false, MaterialCaptureAccessPromptRequired),
               "an explicit glass choice can request consent when the system requires it");
        Expect(!ShouldRequestMaterialCaptureAccess(command, true, MaterialCaptureAccessPromptRequired),
               "verification never requests system consent");
        for (const int access : {-1, 0, 1, 2, 4, 5}) {
            Expect(!ShouldRequestMaterialCaptureAccess(command, false, access),
                   "missing, denied, allowed and unknown statuses do not trigger another prompt");
        }
    }

    for (const auto command : {
        C::Invalid, C::Ready, C::IslandEnable, C::IslandResetAppearance,
        C::DockEnable, C::DockReset, C::CalendarEnable, C::CalendarReset,
        C::IslandMaterialNone, C::DockNoMaterial, C::CalendarSolid, C::TodayTodoSolid
    }) {
        Expect(!ShouldRequestMaterialCaptureAccess(command, false, MaterialCaptureAccessPromptRequired),
               "restore, enable, reset and non-glass commands do not request consent");
    }

    MaterialCapturePermissionChoice choice;
    Expect(!choice.Pending(), "there is no deferred choice at startup");
    Expect(!choice.Replace(C::DockLiquidGlass) && !choice.Finish(),
           "replacing without an active operation does not queue a command");
    choice.Begin(C::IslandMaterialLiquid);
    Expect(choice.Pending(), "the consent operation retains its triggering choice");
    Expect(!choice.Replace(C::Ready), "unrelated page commands do not replace the material choice");
    Expect(choice.Finish() == C::IslandMaterialLiquid,
           "an unrelated command preserves the deferred material");
    Expect(!choice.Finish() && !choice.Pending(), "a completed choice can only be applied once");

    choice.Begin(C::IslandMaterialLiquid);
    Expect(choice.Replace(C::DockFrostedGlass), "a later glass selection replaces the pending command");
    Expect(choice.Finish() == C::DockFrostedGlass, "only the latest glass choice is applied");
    for (const auto command : {C::IslandMaterialNone, C::DockNoMaterial, C::CalendarSolid, C::TodayTodoSolid}) {
        choice.Begin(C::DockFrostedGlass);
        Expect(choice.Replace(command), "a non-glass material selection replaces the pending glass choice");
        Expect(choice.Finish() == command && !choice.Finish(),
               "completion cannot restore a glass choice superseded by a non-glass choice");
    }

    choice.Begin(C::CalendarLiquidGlass);
    choice.Cancel();
    Expect(!choice.Pending() && !choice.Finish(), "closing or shutdown discards the deferred command");
    choice.Begin(C::IslandMaterialFrosted);
    Expect(choice.Finish() == C::IslandMaterialFrosted && !choice.Finish(),
           "a new request after cancellation owns only its own choice");
    std::cout << "PASS material capture consent selection policy and deferred command lifecycle\n";
}
