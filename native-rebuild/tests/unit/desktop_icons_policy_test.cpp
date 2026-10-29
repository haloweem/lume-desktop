#include "core/desktop_icons/desktop_icons_policy.h"
#include "core/desktop_icons/desktop_icons_input.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace lume::desktop_icons;
namespace {
void Check(bool passed, const char* reason) {
    if (!passed) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
Preferences Enabled() { Preferences p; p.enabled = true; p.enableAutoHide = true; return p; }
void ConfirmHide(Policy& policy, Observation& o) {
    const auto change = policy.Tick(o);
    Check(change == Change::HideAutomatically, "idle threshold requests hide");
    Check(!policy.AutoHidden(), "request alone does not record successful hide");
    o.visible = false;
    policy.Confirm(change, o);
    Check(policy.AutoHidden(), "confirmed hide owns restore");
}
}
int main() {
    const Preferences defaults;
    Check(!defaults.enabled && !defaults.enableAutoHide && defaults.autoHideDelay == 5 &&
          defaults.showIconsOnAnyInput && !defaults.manuallyHidden, "automatic hiding defaults to off");
    auto invalid = defaults;
    invalid.autoHideDelay = 2;
    Check(!Valid(invalid), "reject below source range");
    invalid.autoHideDelay = 61;
    Check(!Valid(invalid), "reject above source range");
    invalid.autoHideDelay = 60;
    Check(Valid(invalid), "accept source upper bound");

    Observation o;
    o.now = 10000; o.lastInput = 0;
    Policy disabled;
    Check(disabled.Tick(o) == Change::None && !disabled.NextWake(o) &&
          disabled.ManualToggle(o) == Change::None, "disabled has no work");
    auto manualOnly = defaults; manualOnly.enabled = true;
    Policy defaultEnabled(manualOnly);
    Check(defaultEnabled.Tick(o) == Change::None && !defaultEnabled.NextWake(o),
          "enabling with defaults does not schedule idle hiding");
    Check(defaultEnabled.ManualToggle(o) == Change::HideManually,
          "double-click hiding remains available without automatic hiding");
    o.visible = false; defaultEnabled.Confirm(Change::HideManually, o);
    Check(defaultEnabled.ManualToggle(o) == Change::ShowManually,
          "double-click can restore manually hidden icons");
    o.visible = true;

    Policy policy(Enabled());
    o.now = 4000;
    Check(policy.Tick(o) == Change::None && policy.NextWake(o) == 1000u,
          "schedule actual remaining idle time");
    o.now = 5000; o.fullscreen = true;
    Check(policy.Tick(o) == Change::None && policy.NextWake(o) == 1000u,
          "fullscreen guards hide and defers recheck");
    o.fullscreen = false; o.inputKnown = false;
    Check(policy.Tick(o) == Change::None && policy.NextWake(o) == 1000u,
          "missing last-input data never hides");
    o.inputKnown = true;
    ConfirmHide(policy, o);
    Check(policy.NextWake(o) == 500u && policy.Tick(o) == Change::None,
          "auto-hidden state polls only for restore");
    o.lastInput = 5001; o.now = 5002;
    Check(policy.Tick(o) == Change::RestoreAutomatically, "new keyboard input restores");
    Check(policy.AutoHidden(), "failed restore retains ownership");
    o.visible = true;
    policy.Confirm(Change::RestoreAutomatically, o);
    Check(!policy.AutoHidden(), "confirmed restore clears ownership");

    Check(policy.ManualToggle(o) == Change::HideManually, "blank double-click hides visible icons");
    o.visible = false; policy.Confirm(Change::HideManually, o);
    o.now = 5003; o.lastInput = 5003; o.mouseX = -1200;
    Check(policy.Settings().manuallyHidden && !policy.NextWake(o) &&
          policy.Tick(o) == Change::None && policy.DesktopInput(o, true) == Change::None,
          "manually hidden icons ignore all input and need no timer");
    Check(policy.ManualToggle(o) == Change::ShowManually, "hidden desktop can be toggled visible");
    o.visible = true; policy.Confirm(Change::ShowManually, o);
    Check(!policy.Settings().manuallyHidden, "manual show clears saved hide preference");

    o.now = 11000; ConfirmHide(policy, o);
    Check(policy.DesktopInput(o, false) == Change::None, "synthetic stationary mouse move ignored");
    --o.mouseX;
    Check(policy.DesktopInput(o, false) == Change::RestoreAutomatically,
          "physical cursor change restores without waiting for polling");
    Check(policy.DesktopInput(o, true) == Change::RestoreAutomatically, "desktop button restores");
    Check(policy.ManualToggle(o) == Change::ShowManually, "manual command supersedes auto-hidden state");
    o.visible = true; policy.Confirm(Change::ShowManually, o);
    Check(!policy.AutoHidden(), "manual command clears automatic marker");

    o.now = 20000; o.lastInput = 15000; ConfirmHide(policy, o);
    auto noRestore = policy.Settings(); noRestore.showIconsOnAnyInput = false;
    Check(policy.Configure(noRestore), "configure restore setting");
    o.lastInput = 20001; o.now = 20002;
    Check(!policy.NextWake(o) && policy.Tick(o) == Change::None &&
          policy.DesktopInput(o, true) == Change::None, "disabled restore has no polling");
    Check(policy.Stop(o) == Change::RestoreOnStop, "unload restores hidden icons");

    Policy external(Enabled());
    Check(external.Tick(o) == Change::None && !external.NextWake(o),
          "external hidden state is not claimed as auto-hidden");
    o.available = false;
    Check(external.Tick(o) == Change::None && external.ManualToggle(o) == Change::None &&
          external.Stop(o) == Change::None && !external.NextWake(o), "missing desktop has no visibility work");

    auto manual = Enabled(); manual.manuallyHidden = true;
    Policy resumed(manual); o = {}; o.visible = true;
    Check(resumed.Attached(o) == Change::HideManually, "recreated Explorer receives saved manual state");
    o.visible = false; resumed.Confirm(Change::HideManually, o);
    Check(resumed.Stop(o) == Change::RestoreOnStop, "stop shows even manually hidden icons");
    o.visible = true; resumed.Confirm(Change::RestoreOnStop, o);
    Check(resumed.Settings().manuallyHidden, "unload preserves manual preference for next enable");

    Policy rollover(Enabled());
    o = {}; o.now = 100; o.lastInput = std::numeric_limits<std::uint32_t>::max() - 4899;
    ConfirmHide(rollover, o);
    o.now = 101; o.lastInput = 101;
    Check(rollover.Tick(o) == Change::RestoreAutomatically, "32-bit tick rollover preserves new input");
    rollover.Detached();
    Check(!rollover.AutoHidden(), "Explorer replacement clears stale automatic ownership");

    Policy zero(Enabled());
    o = {}; o.now = 0; o.lastInput = std::numeric_limits<std::uint32_t>::max() - 4999;
    ConfirmHide(zero, o);
    Check(zero.AutoHidden(), "zero tick is valid rather than an unset marker");
    o.lastInput = 1; o.now = 1;
    Check(zero.Tick(o) == Change::RestoreAutomatically, "zero-tick hide can restore");

    auto unchanged = policy.Settings(); invalid = unchanged; invalid.autoHideDelay = 0;
    Check(!policy.Configure(invalid) && policy.Settings().autoHideDelay == unchanged.autoHideDelay,
          "invalid update does not mutate settings");
    Check(MakesVisible(Change::RestoreAutomatically) && MakesVisible(Change::ShowManually) &&
          MakesVisible(Change::RestoreOnStop) && !MakesVisible(Change::HideManually) &&
          !MakesVisible(Change::HideAutomatically), "visibility actions are explicit");
    DoubleClick clicks;
    Check(!clicks.Press(1, true, -100, 0, 0, 500, 2, 2) &&
          clicks.Press(1, true, -98, 2, 500, 500, 2, 2), "double-click includes rectangle/time boundary");
    Check(!clicks.Press(1, true, -98, 2, 501, 500, 2, 2), "third click begins a new pair");
    Check(!clicks.Press(1, false, -98, 2, 502, 500, 2, 2) &&
          !clicks.Press(1, true, -98, 2, 503, 500, 2, 2), "icon click breaks blank click sequence");
    Check(!clicks.Press(2, true, -98, 2, 504, 500, 2, 2), "different desktop never shares a click pair");
    Check(!clicks.Press(2, true, -95, 2, 505, 500, 2, 2), "outside half double-click rectangle restarts pair");
    clicks.Reset();
    Check(!clicks.Press(2, true, 0, 0, 0xfffffff0U, 500, 2, 2) &&
          clicks.Press(2, true, 0, 0, 4, 500, 2, 2), "double-click survives DWORD rollover");
    std::cout << "desktop icon policy source contract passed\n";
}
