#include "core/clock/clock_controller.h"
#include "core/settings/settings_types.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

using namespace lume::clock;
static_assert(std::is_same_v<ClockController::Save,
    std::function<bool(const lume::settings::ClockSettings&)>>,
    "clock persistence accepts only clock settings");
static_assert(std::is_same_v<decltype(std::declval<const ClockController&>().Snapshot()),
    const lume::settings::ClockSettings&>,
    "clock controller exposes only clock settings");
static_assert(!std::is_constructible_v<ClockController, lume::settings::SettingsSnapshot,
    IClockRuntime&, ClockController::Save>,
    "clock controller cannot receive the application settings snapshot");

void Expect(bool value, const char* description) {
    if (!value) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
struct Runtime : IClockRuntime {
    bool running = false, failNext = false;
    int calls = 0, centers = 0;
    Style style = Style::Welfare;
    Appearance appearance;
    std::function<void()> centered;
    bool Apply(bool enabled, Style next, const Appearance& value) override {
        ++calls;
        if (failNext) { failNext = false; return false; }
        running = enabled; style = next; appearance = value; return true;
    }
    bool IsRunning() const override { return running; }
    void Center() override { ++centers; if(centered) centered(); }
    void Shutdown() override { running = false; }
};
int main() {
    Runtime runtime;
    bool failSave = false;
    int saves = 0;
    lume::settings::SettingsSnapshot persisted;
    persisted.widgets.push_back({"weather", 10, 20, 320, 152, true, false});
    ClockController controller(persisted.clock, runtime, [&](const lume::settings::ClockSettings& value) {
        ++saves;
        auto next = persisted;
        next.clock = value;
        if (failSave) return false;
        persisted = std::move(next); return true;
    });
    persisted.widgets.front().x = 42;
    Expect(controller.Restore() && !runtime.running && saves == 0, "restore disabled without disk write");
    const auto initialPosition = controller.Snapshot().Active();
    Expect(controller.Handle({ActionType::SelectStyle,1}) && !runtime.running, "switching disabled style does not enable");
    Expect(runtime.appearance.x == initialPosition.x && runtime.appearance.y == initialPosition.y,
        "disabled style switch keeps the current position");
    Expect(controller.Handle({ActionType::Enable,1}) && runtime.running && runtime.style == Style::Mond, "enable selected real runtime");
    Expect(persisted.schemaVersion == 3 && persisted.widgets.size() == 1 && persisted.widgets.front().x == 42,
        "clock save merges into the latest application settings and preserves other modules");
    Expect(controller.Handle({ActionType::Position,-300,100}), "menu updates coordinates");
    Expect(controller.Handle({ActionType::Scale,170}), "menu updates size");
    Expect(controller.Handle({ActionType::Color,0x123456,128}), "menu updates RGB and alpha");
    Expect(controller.Handle({ActionType::Lock,1}), "menu locks position");
    Expect(controller.Handle({ActionType::SelectStyle,0}), "switch to independent style");
    Expect(runtime.appearance.x == -300 && runtime.appearance.y == 100 && !runtime.appearance.locked,
        "style switch keeps position and restores independent appearance");
    Expect(controller.Handle({ActionType::Position,420,260}), "move the newly selected style");
    Expect(controller.Handle({ActionType::SelectStyle,2}) && runtime.appearance.x == 420 && runtime.appearance.y == 260,
        "third style follows the latest position instead of its saved default");
    Expect(controller.Handle({ActionType::SelectStyle,1}), "return style");
    Expect(runtime.appearance.x == 420 && runtime.appearance.y == 260 && runtime.appearance.scale == 170 && runtime.appearance.alpha == 128 && runtime.appearance.locked, "returning style keeps latest position and restores independent appearance");
    const int priorSaves = saves;
    Expect(!controller.Handle({ActionType::Scale,55}) && !controller.Handle({ActionType::Position,32768,0}) &&
        !controller.Handle({ActionType::Color,-1,20}) && !controller.Handle({ActionType::SelectStyle,3}), "invalid actions rejected");
    Expect(saves == priorSaves, "invalid changes never saved");
    failSave = true;
    Expect(!controller.Handle({ActionType::Enable,0}), "write failure surfaced");
    Expect(persisted.clock.enabled && persisted.clock.style == "mond" && persisted.widgets.front().x == 42,
        "failed save leaves the application settings snapshot unchanged");
    Expect(runtime.running && controller.Snapshot().enabled && std::string(controller.Error()) == "save", "failed save restores runtime and state");
    Expect(!controller.Handle({ActionType::SelectStyle,0}) && runtime.style == Style::Mond &&
        runtime.appearance.x == 420 && runtime.appearance.y == 260 && persisted.clock.profiles[0].x == 420,
        "failed style save rolls back style and position");
    failSave = false;
    runtime.failNext = true;
    Expect(!controller.Handle({ActionType::SelectStyle,2}), "surface failure surfaced");
    Expect(controller.Snapshot().style == "mond" && runtime.style == Style::Mond, "failed surface change restores previous style");
    Expect(controller.Handle({ActionType::Reset}) && runtime.running && controller.Snapshot().style == "welfare", "original reset keeps enabled and selects welfare");
    Expect(runtime.appearance.x == 420 && runtime.appearance.y == 260, "reset style keeps current position");
    Expect(controller.Snapshot().profiles[1].scale == 170, "original reset preserves skin appearance");
    Expect(controller.Handle({ActionType::Center}) && runtime.centers == 1, "center delegated to display-aware runtime");
    runtime.centered = [&] { controller.Handle({ActionType::Position,100,100}); };
    failSave = true;
    Expect(!controller.Handle({ActionType::Center}), "center reports nested position transaction failure");
    failSave = false;
    Expect(controller.Handle({ActionType::Enable,0}) && !runtime.running && !persisted.clock.enabled, "menu close persisted and runtime stopped");
    Expect(controller.Handle({ActionType::Enable,1}), "reopen after close");
    controller.Shutdown();
    Expect(!runtime.running, "shutdown releases runtime");
    std::cout << "Clock transactions, style profiles, failures and shutdown passed\n";
}
