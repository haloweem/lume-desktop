#pragma once

#include <cstdint>
#include <optional>

namespace lume::desktop_icons {

// Automatic idle hiding is opt-in; double-click toggling works independently.
struct Preferences {
    bool enabled = false;
    bool enableAutoHide = false;
    int autoHideDelay = 5;
    bool showIconsOnAnyInput = true;
    bool manuallyHidden = false;
};
bool Valid(const Preferences&) noexcept;

struct Observation {
    bool available = true;
    bool visible = true;
    bool inputKnown = true;
    bool fullscreen = false;
    std::uint32_t now = 0;
    std::uint32_t lastInput = 0;
    int mouseX = 0;
    int mouseY = 0;
};

enum class Change { None, HideAutomatically, RestoreAutomatically, HideManually, ShowManually, RestoreOnStop };
bool MakesVisible(Change) noexcept;

// Only confirmed visibility changes advance state. The platform owns discovery,
// timers and input delivery; the session owns saving manual preference changes.
class Policy final {
public:
    explicit Policy(Preferences preferences = {});
    bool Configure(const Preferences&) noexcept;
    const Preferences& Settings() const noexcept { return preferences_; }
    void Detached() noexcept;
    Change Attached(const Observation&) const noexcept;
    Change ManualToggle(const Observation&) const noexcept;
    Change Tick(const Observation&) noexcept;
    Change DesktopInput(const Observation&, bool buttonDown) const noexcept;
    Change Stop(const Observation&) const noexcept;
    void Confirm(Change, const Observation&) noexcept;
    std::optional<std::uint32_t> NextWake(const Observation&) const noexcept;
    bool AutoHidden() const noexcept { return autoHidden_; }

private:
    Preferences preferences_;
    bool autoHidden_ = false;
    std::uint32_t hiddenAt_ = 0;
    std::uint32_t inputAtHide_ = 0;
    int mouseXAtHide_ = 0;
    int mouseYAtHide_ = 0;
};

}  // namespace lume::desktop_icons
