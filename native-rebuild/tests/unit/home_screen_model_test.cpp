#include "core/ui/home_screen_model.h"

#include <cstdlib>
#include <iostream>

namespace {
void Expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void TestNavigationKeepsThePagePathExplicit() {
    lume::ui::HomeScreenModel model;
    Expect(model.Snapshot().page == lume::ui::HomePage::Home, "starts on home page");
    model.Navigate(lume::ui::HomePage::DesktopBeauty);
    Expect(model.Snapshot().page == lume::ui::HomePage::DesktopBeauty, "opens desktop beauty");
    model.Navigate(lume::ui::HomePage::ClockSettings);
    Expect(model.Snapshot().page == lume::ui::HomePage::ClockSettings, "opens clock settings");
    model.Navigate(lume::ui::HomePage::DesktopBeauty);
    Expect(model.Snapshot().page == lume::ui::HomePage::DesktopBeauty, "back returns to widget list");
}

void TestClockSettingsAreUpdatedAndTracked() {
    lume::ui::HomeScreenModel model;
    model.SetClockEnabled(false);
    Expect(!model.Snapshot().clockEnabled, "clock can be disabled");
    Expect(model.Snapshot().dirty, "clock change is marked dirty");
    model.SetClockStyle(lume::ui::ClockStyle::Mond);
    Expect(model.Snapshot().clockStyle == lume::ui::ClockStyle::Mond, "clock style can be changed");
    model.MarkSaved();
    Expect(!model.Snapshot().dirty, "saved clock settings clear dirty state");
    model.LoadClockSettings(true, lume::ui::ClockStyle::InsideDream);
    Expect(model.Snapshot().clockEnabled && model.Snapshot().clockStyle == lume::ui::ClockStyle::InsideDream,
           "persisted settings can be loaded without marking dirty");
}
}

int main() {
    TestNavigationKeepsThePagePathExplicit();
    TestClockSettingsAreUpdatedAndTracked();
    return 0;
}
