#pragma once
#include "core/ui/page_bridge.h"
#include "core/taskbar/taskbar_state.h"
#include "core/minimize_animation/minimize_animation.h"
#include <optional>

namespace lume::ui {
inline std::optional<taskbar::Preferences> TaskbarPreferencesForPage(
    PageCommand command,taskbar::Preferences next) {
    switch(command) {
        case PageCommand::TaskbarEnable:next.enabled=true;break;
        case PageCommand::TaskbarDisable:next.enabled=false;break;
        case PageCommand::TaskbarTransparentOn:next.transparentTaskbar=true;break;
        case PageCommand::TaskbarTransparentOff:next.transparentTaskbar=false;break;
        case PageCommand::TaskbarAutoHideOn:next.autoHideTaskbar=true;break;
        case PageCommand::TaskbarAutoHideOff:next.autoHideTaskbar=false;break;
        case PageCommand::TaskbarHideFully:next.hideBehavior=taskbar::HideBehavior::FullyHidden;break;
        case PageCommand::TaskbarHideAuto:next.hideBehavior=taskbar::HideBehavior::AutoHide;break;
        case PageCommand::TaskbarReset:{
            const bool enabled=next.enabled;next={};next.enabled=enabled;break;
        }
        default:return {};
    }
    return next;
}
inline std::optional<minimize_animation::Preferences> MinimizePreferencesForPage(
    PageCommand command,minimize_animation::Preferences next) {
    if(command>=PageCommand::MinimizeDurationFirst&&command<=PageCommand::MinimizeDurationLast) {
        next.durationMs=static_cast<int>(command)-static_cast<int>(PageCommand::MinimizeDurationFirst)+50;
        return next;
    }
    if(command>=PageCommand::MinimizeUnhideDurationFirst&&command<=PageCommand::MinimizeUnhideDurationLast) {
        next.unhideDurationMs=static_cast<int>(command)-static_cast<int>(PageCommand::MinimizeUnhideDurationFirst);
        return next;
    }
    switch(command) {
        case PageCommand::MinimizeEnable:next.enabled=true;break;
        case PageCommand::MinimizeDisable:next.enabled=false;break;
        case PageCommand::MinimizeRestoreOn:next.restore=true;break;
        case PageCommand::MinimizeRestoreOff:next.restore=false;break;
        case PageCommand::MinimizeLaunchOn:next.launch=true;break;
        case PageCommand::MinimizeLaunchOff:next.launch=false;break;
        case PageCommand::MinimizeMultiMonitorOn:next.multiMonitor=true;break;
        case PageCommand::MinimizeMultiMonitorOff:next.multiMonitor=false;break;
        case PageCommand::MinimizeUnhideTaskbarOn:next.unhideTaskbar=true;break;
        case PageCommand::MinimizeUnhideTaskbarOff:next.unhideTaskbar=false;break;
        case PageCommand::MinimizeModern:next.engine=minimize_animation::Engine::Modern;break;
        case PageCommand::MinimizeClassic:next.engine=minimize_animation::Engine::Classic;break;
        case PageCommand::MinimizeReset:{
            const bool enabled=next.enabled;next={};next.enabled=enabled;break;
        }
        default:return {};
    }
    return next;
}
}
