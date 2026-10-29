#include "core/calendar/calendar_controller.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace lume::calendar;
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
class Runtime final : public ICalendarRuntime {
public:
    bool Apply(const Preferences& value) override {
        applied.push_back(value);
        events += 'a';
        running = value.enabled;
        if (!results.empty()) { const bool ok = results.front(); results.erase(results.begin()); return ok; }
        return true;
    }
    bool IsRunning() const override { return running; }
    bool ApplyBounds(const Preferences& value) override {++boundsApplications;return Apply(value);}
    bool ApplyReset(Preferences& value) override {
        ++resets;
        if(normalizeReset){value.Active()->x=44;value.Active()->y=55;value.Active()->hasPosition=true;}
        return Apply(value);
    }
    void Shutdown() override { ++shutdowns; running = false; }
    bool running = false;
    int shutdowns = 0;
    int resets=0;bool normalizeReset=false;
    int boundsApplications=0;
    std::vector<bool> results;
    std::vector<Preferences> applied;
    std::string events;
};
void RestoreAndTransactions() {
    Runtime runtime;
    int saves = 0;
    bool saveOk = true;
    CalendarController controller({},runtime,[&](const Preferences& p) {
        ++saves; runtime.events += 's';
        Expect(runtime.applied.back().enabled == p.enabled, "runtime receives candidate before save");
        return saveOk;
    });
    Expect(controller.Restore() && saves == 0 && runtime.applied.size() == 1, "restore applies without saving");
    runtime.events.clear();
    Expect(controller.Handle({ActionType::Enable,1}) && controller.Snapshot().enabled && controller.Running(), "enable succeeds");
    Expect(runtime.events == "as" && saves == 1 && controller.Error() == ControllerError::None, "apply then save then commit");
    runtime.results = {false,true}; runtime.events.clear();
    Expect(!controller.Handle({ActionType::Enable,0}) && saves == 1, "runtime failure skips save");
    Expect(controller.Error() == ControllerError::Runtime && controller.Snapshot().enabled &&
        runtime.applied.back().enabled && runtime.events == "aa", "runtime failure replays original preferences");
    saveOk = false; runtime.events.clear();
    Expect(!controller.Handle({ActionType::Enable,0}) && controller.Error() == ControllerError::Save, "save failure reported");
    Expect(controller.Snapshot().enabled && runtime.applied.back().enabled && runtime.events == "asa", "save failure rolls back without state commit");
    runtime.results = {true,false};
    Expect(!controller.Handle({ActionType::Enable,0}) && controller.Error() == ControllerError::Rollback, "save rollback failure visible");
    runtime.results = {false,false};
    Expect(!controller.Handle({ActionType::Enable,0}) && controller.Error() == ControllerError::Rollback, "runtime rollback failure visible");
    runtime.results.clear(); saveOk = true;
    Expect(controller.Handle({ActionType::Enable,0}) && controller.Error() == ControllerError::None, "success clears prior failure");
    controller.Shutdown();
    Expect(runtime.shutdowns == 1 && !controller.Running(), "shutdown delegates to runtime");
    runtime.results = {false};
    Expect(!controller.Restore() && controller.Error() == ControllerError::Runtime, "restore failure reported");
    runtime.results.clear();saveOk=true;runtime.normalizeReset=true;
    Expect(controller.Handle({ActionType::Bounds,-100,-100,820,500})&&runtime.resets==0&&runtime.boundsApplications==1,"Bounds uses explicit geometry application only");
    Expect(controller.Handle({ActionType::Reset})&&runtime.resets==1&&controller.Snapshot().Active()->x==44,"Reset saves normalized live position");
    saveOk=false;runtime.events.clear();
    Expect(!controller.Handle({ActionType::Reset})&&runtime.resets==2&&runtime.events=="asa"&&controller.Error()==ControllerError::Save,"Reset save failure rolls back through ordinary Apply");
}
void InvalidHasNoSideEffects() {
    Runtime runtime; int saves = 0;
    CalendarController controller({},runtime,[&](const Preferences&) { ++saves; return true; });
    const std::vector<Action> invalid = {
        {static_cast<ActionType>(999)}, {ActionType::Enable,2}, {ActionType::Lock,-1},
        {ActionType::SelectTheme,-1}, {ActionType::SelectTheme,3}, {ActionType::Material,-1}, {ActionType::Material,3}, {ActionType::BoardView,4},
        {ActionType::TextColor,-1,255}, {ActionType::TextColor,0,256},
        {ActionType::HighlightColor,0x1000000,255}, {ActionType::HighlightColor,0,-1},
        {ActionType::Bounds,32768,0,820,500}, {ActionType::Bounds,0,-32769,820,500},
        {ActionType::Bounds,0,0,319,500}, {ActionType::Bounds,0,0,820,721}
    };
    for (const auto action : invalid)
        Expect(!controller.Handle(action) && controller.Error() == ControllerError::Invalid &&
            runtime.applied.empty() && saves == 0, "invalid action never reaches runtime or save");
    Preferences bad; bad.theme = static_cast<Theme>(7);
    CalendarController broken(bad,runtime,[&](const Preferences&) { ++saves; return true; });
    Expect(!broken.Restore() && broken.Error() == ControllerError::Invalid && !broken.Snapshot().Active(), "invalid initial state preserved for diagnostics");
    Expect(!broken.Handle({ActionType::Reset}) && runtime.applied.empty() && saves == 0, "invalid initial state cannot reach runtime");
}
void ThemesColorsAndLockedBounds() {
    Runtime runtime;
    Preferences initial; initial.enabled = true; initial.profiles[0] = {50,70,900,600,true};
    initial.profiles[1].width = 1100; initial.profiles[1].height = 650;
    CalendarController controller(initial,runtime,[](const Preferences&) { return true; });
    Expect(controller.Restore() && controller.Handle({ActionType::SelectTheme,1}), "switch running theme");
    Expect(controller.Snapshot().Active()->x == 50 && controller.Snapshot().Active()->y == 70 &&
        controller.Snapshot().Active()->hasPosition && controller.Snapshot().Active()->width == 1100 &&
        controller.Snapshot().Active()->height == 650, "first target position inherits live profile without changing target size");
    Expect(controller.Handle({ActionType::Lock,1}) && controller.Handle({ActionType::Bounds,-20,80,320,720}), "lock permits platform-reported resizing");
    Expect(controller.Snapshot().locked && controller.Snapshot().Active()->hasPosition && controller.Snapshot().Active()->width == 320, "bounds commit while locked");
    Expect(controller.Handle({ActionType::SelectTheme,0}) && controller.Snapshot().Active()->x == -20 &&
        controller.Snapshot().Active()->y == 80 && controller.Snapshot().Active()->width == 900,
        "switching back keeps the calendar where it is, with that theme's size");
    Expect(controller.Handle({ActionType::SelectTheme,1}) && controller.Snapshot().Active()->x == -20 &&
        controller.Snapshot().Active()->width == 320, "a previously used theme also takes the current position");
    Expect(controller.Handle({ActionType::Material,1}) && controller.Handle({ActionType::TextColor,0x123456,7}) &&
        controller.Handle({ActionType::HighlightColor,0xabcdef,0}), "appearance actions accepted");
    Expect(controller.Snapshot().material == Material::FrostedGlass && controller.Snapshot().textColor.alpha == 7 &&
        controller.Snapshot().highlightColor.rgb == 0xabcdef && controller.Snapshot().highlightColor.alpha == 0, "appearance values retained");
    Preferences disabled; disabled.profiles[0] = {30,40,820,500,true};
    CalendarController offline(disabled,runtime,[](const Preferences&) { return true; });
    Expect(offline.Handle({ActionType::SelectTheme,1}) && !offline.Snapshot().Active()->hasPosition, "disabled switching does not inherit position even with stale running runtime");
    Preferences noPosition; noPosition.enabled = true;
    CalendarController fresh(noPosition,runtime,[](const Preferences&) { return true; });
    Expect(fresh.Restore() && fresh.Handle({ActionType::SelectTheme,1}) && !fresh.Snapshot().Active()->hasPosition, "unpositioned source cannot supply fallback");
}
void ResetRules() {
    for (const bool enabled : {false,true}) {
        Runtime runtime;
        Preferences p; p.enabled = enabled; p.theme = Theme::Split; p.locked = true;
        p.material = Material::LiquidGlass; p.textColor = {0x12,5}; p.highlightColor = {0x34,6};
        p.profiles[0] = {10,20,350,340,true}; p.profiles[1] = {300,400,1400,700,true};
        CalendarController controller(p,runtime,[](const Preferences&) { return true; });
        Expect(controller.Restore() && controller.Handle({ActionType::Reset}), "reset valid preferences");
        const auto& reset = controller.Snapshot();
        Expect(reset.enabled == enabled && reset.theme == Theme::Spacious && reset.material == Material::Solid &&
            !reset.locked && reset.textColor.rgb == 0xffffff && reset.textColor.alpha == 255 &&
            reset.highlightColor.rgb == 0x0a84ff && reset.highlightColor.alpha == 255, "reset keeps enabled and resets appearance");
        Expect(reset.profiles[0].width == 820 && reset.profiles[0].height == 500 &&
            reset.profiles[1].width == 820 && reset.profiles[1].height == 500, "reset restores both sizes");
        Expect(reset.profiles[0].x == (enabled ? 300 : 10) && reset.profiles[0].y == (enabled ? 400 : 20) &&
            reset.profiles[0].hasPosition && reset.profiles[1].x == 300 && reset.profiles[1].hasPosition,
            "reset only transfers active position when running");
    }
    Runtime runtime; Preferences p; p.enabled = true; p.theme = Theme::Split;
    p.profiles[0] = {10,20,350,340,true};
    CalendarController controller(p,runtime,[](const Preferences&) { return true; });
    Expect(controller.Restore() && controller.Handle({ActionType::Reset}) && controller.Snapshot().Active()->x == 10,
        "running reset without active position keeps spacious position");
}
void DisabledResetPreservesPositionsWithStaleRuntime() {
    Runtime runtime;
    runtime.running = true;
    Preferences initial;
    initial.theme = Theme::Split;
    initial.profiles[0] = {10,20,350,340,true};
    initial.profiles[1] = {300,400,1400,700,true};
    Preferences saved;
    CalendarController controller(initial,runtime,[&](const Preferences& value) { saved = value; return true; });
    Expect(controller.Handle({ActionType::Reset}), "disabled reset accepts stale running runtime");
    const auto& reset = controller.Snapshot();
    Expect(!reset.enabled && reset.theme == Theme::Spacious && reset.profiles[0].x == 10 &&
        reset.profiles[0].y == 20 && reset.profiles[0].hasPosition && reset.profiles[1].x == 300 &&
        reset.profiles[1].y == 400 && reset.profiles[1].hasPosition,
        "disabled reset preserves each theme position despite stale running runtime");
    Expect(!saved.enabled && saved.profiles[0].x == 10 && saved.profiles[1].x == 300 &&
        runtime.applied.size() == 1 && runtime.applied[0].profiles[0].x == 10,
        "runtime and persistence receive preserved disabled positions");
}
}
int main() { RestoreAndTransactions(); InvalidHasNoSideEffects(); ThemesColorsAndLockedBounds(); ResetRules(); DisabledResetPreservesPositionsWithStaleRuntime(); }
