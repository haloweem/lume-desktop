#pragma once
#include <windows.h>

namespace lume::platform::windows {
struct ReminderAttention {
    bool notificationsAccepted = false;
    bool foregroundKnown = false;
    bool desktopOrLume = false;
    bool fullscreen = false;
};
constexpr bool ReminderMayPresent(const ReminderAttention& state) {
    return state.notificationsAccepted && state.foregroundKnown && state.desktopOrLume && !state.fullscreen;
}
// Unknown shell/foreground state fails closed. Windowed games and ordinary work
// are covered by allowing only the Explorer desktop or this process in front.
ReminderAttention QueryReminderAttention();
}
