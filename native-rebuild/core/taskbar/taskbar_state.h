#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lume::taskbar {
enum class HideBehavior { AutoHide, FullyHidden };
struct Preferences {
    bool enabled = false;
    bool transparentTaskbar = true;
    bool autoHideTaskbar = false;
    HideBehavior hideBehavior = HideBehavior::AutoHide;
};
inline bool operator==(const Preferences& a, const Preferences& b) {
    return a.enabled == b.enabled && a.transparentTaskbar == b.transparentTaskbar &&
           a.autoHideTaskbar == b.autoHideTaskbar && a.hideBehavior == b.hideBehavior;
}
enum class Mode { Unknown, Classic, Windows11X64, Windows11Arm64 };
enum class Error {
    None, Load, Save, Busy, Unsupported, MissingPayload, PayloadIntegrity, Conflict,
    Discover, ReadAppearance, ApplyAppearance, ReadAutoHide, ApplyAutoHide,
    CoreStart, CoreExited, CoreStop, Cancelled, Recovery, Watch, SystemEffectsDisabled,
    ReadVisibility, ApplyVisibility
};
const char* ErrorName(Error);
const char* ModeName(Mode);
struct Appearance {
    std::uint64_t window = 0;
    std::uint32_t process = 0;
    std::uint64_t processCreated = 0;
    std::uint32_t accent = 0, flags = 0, color = 0, animation = 0;
};
inline bool SameTarget(const Appearance& a, const Appearance& b) {
    return a.window == b.window && a.process == b.process && a.processCreated == b.processCreated;
}
struct Visibility : Appearance {
    std::uint32_t extendedStyle = 0, colorKey = 0, layeredFlags = 0;
    std::uint8_t alpha = 255;
    bool visible = true;
};
// The recovery journal is committed before every first system mutation. Clearing
// ownership is committed only after restoration succeeds, so replay is idempotent.
struct Record {
    Preferences preferences;
    bool ownsAutoHide = false;
    bool originalAutoHide = false;
    bool ownsCore = false;
    std::vector<Appearance> appearances;
    std::vector<Visibility> hiddenTaskbars;
};
inline bool HasRecovery(const Record& record) {
    return record.ownsAutoHide || record.ownsCore || !record.appearances.empty() || !record.hiddenTaskbars.empty();
}
struct Status {
    Preferences preferences;
    Mode mode = Mode::Unknown;
    Error error = Error::None;
    bool running = false;
    bool pendingRecovery = false;
    std::size_t taskbars = 0;
    // True while user preferences or an auto-hide demand need the worker.
    bool active = false;
};
}  // namespace lume::taskbar
