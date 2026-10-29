#include "core/ui/home_screen_model.h"

namespace lume::ui {

HomeScreenModel::HomeScreenModel(bool clockEnabled, ClockStyle clockStyle) {
    state_.clockEnabled = clockEnabled;
    state_.clockStyle = clockStyle;
}

void HomeScreenModel::Navigate(HomePage page) noexcept {
    state_.page = page;
}

void HomeScreenModel::SetClockEnabled(bool enabled) noexcept {
    if (state_.clockEnabled == enabled) return;
    state_.clockEnabled = enabled;
    state_.dirty = true;
}

void HomeScreenModel::SetClockStyle(ClockStyle style) noexcept {
    if (state_.clockStyle == style) return;
    state_.clockStyle = style;
    state_.dirty = true;
}

void HomeScreenModel::LoadClockSettings(bool enabled, ClockStyle style) noexcept {
    state_.clockEnabled = enabled;
    state_.clockStyle = style;
    state_.dirty = false;
}

}  // namespace lume::ui
