#include "core/desktop_icons/desktop_icons_policy.h"
#include <stdexcept>

namespace lume::desktop_icons {
bool Valid(const Preferences& preferences) noexcept {
    return preferences.autoHideDelay >= 3 && preferences.autoHideDelay <= 60;
}
bool MakesVisible(Change change) noexcept {
    return change == Change::ShowManually || change == Change::RestoreAutomatically ||
           change == Change::RestoreOnStop;
}
Policy::Policy(Preferences preferences) : preferences_(preferences) {
    if (!Valid(preferences)) throw std::invalid_argument("desktop icon delay");
}
bool Policy::Configure(const Preferences& preferences) noexcept {
    if (!Valid(preferences)) return false;
    preferences_ = preferences;
    if (!preferences.enabled) Detached();
    return true;
}
void Policy::Detached() noexcept { autoHidden_ = false; }
Change Policy::Attached(const Observation& o) const noexcept {
    return preferences_.enabled && preferences_.manuallyHidden && o.available && o.visible
        ? Change::HideManually : Change::None;
}
Change Policy::ManualToggle(const Observation& o) const noexcept {
    if (!preferences_.enabled || !o.available) return Change::None;
    return o.visible ? Change::HideManually : Change::ShowManually;
}
Change Policy::Tick(const Observation& o) noexcept {
    if (!preferences_.enabled || !o.available) return Change::None;
    if (o.visible) autoHidden_ = false; // An external show cancels our old ownership.
    if (!preferences_.enableAutoHide || !o.inputKnown) return Change::None;
    if (o.visible) {
        const auto idle = std::uint32_t(o.now - o.lastInput);
        return !o.fullscreen && idle >= static_cast<std::uint32_t>(preferences_.autoHideDelay) * 1000U
            ? Change::HideAutomatically : Change::None;
    }
    // Compare ages rather than ordering DWORD ticks: GetTickCount wraps at 49.7 days.
    return autoHidden_ && preferences_.showIconsOnAnyInput && o.lastInput != inputAtHide_ &&
           std::uint32_t(o.now - o.lastInput) <= std::uint32_t(o.now - hiddenAt_)
        ? Change::RestoreAutomatically : Change::None;
}
Change Policy::DesktopInput(const Observation& o, bool buttonDown) const noexcept {
    return preferences_.enabled && o.available && !o.visible && autoHidden_ &&
           preferences_.showIconsOnAnyInput &&
           (buttonDown || o.mouseX != mouseXAtHide_ || o.mouseY != mouseYAtHide_)
        ? Change::RestoreAutomatically : Change::None;
}
Change Policy::Stop(const Observation& o) const noexcept {
    return o.available && !o.visible ? Change::RestoreOnStop : Change::None;
}
void Policy::Confirm(Change change, const Observation& o) noexcept {
    if (change == Change::None || !o.available || o.visible != MakesVisible(change)) return;
    autoHidden_ = change == Change::HideAutomatically;
    if (autoHidden_) {
        hiddenAt_ = o.now;
        inputAtHide_ = o.lastInput;
        mouseXAtHide_ = o.mouseX;
        mouseYAtHide_ = o.mouseY;
    }
    if (change == Change::HideManually) preferences_.manuallyHidden = true;
    else if (change == Change::ShowManually) preferences_.manuallyHidden = false;
}
std::optional<std::uint32_t> Policy::NextWake(const Observation& o) const noexcept {
    if (!preferences_.enabled || !preferences_.enableAutoHide || !o.available) return {};
    if (!o.visible) {
        if (autoHidden_ && preferences_.showIconsOnAnyInput) return 500;
        return {};
    }
    if (!o.inputKnown || o.fullscreen) return 1000;
    const auto threshold = static_cast<std::uint32_t>(preferences_.autoHideDelay) * 1000U;
    const auto idle = std::uint32_t(o.now - o.lastInput);
    return idle >= threshold ? 1U : threshold - idle;
}
}  // namespace lume::desktop_icons
