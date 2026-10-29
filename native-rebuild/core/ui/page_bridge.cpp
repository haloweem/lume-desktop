#include "core/ui/page_bridge.h"

namespace lume::ui {
namespace {
bool ValidRadialShortcut(int code) noexcept {
    if (code < 1 || code > 4095) return false;
    const int vk = code % 256, mods = code / 256;
    const bool key = vk == 8 || vk == 9 || vk == 13 || vk == 19 || vk == 20 ||
        vk == 32 || vk == 44 || vk == 45 || vk == 46 || vk == 144 || vk == 145 ||
        (vk >= 33 && vk <= 40) || (vk >= 48 && vk <= 57) || (vk >= 65 && vk <= 90) ||
        (vk >= 96 && vk <= 135) || (vk >= 186 && vk <= 192) ||
        (vk >= 219 && vk <= 222) || vk == 226;
    return key && !((vk == 9 && (mods & 9)) || (vk == 46 && (mods & 3) == 3) ||
        (vk == 76 && (mods & 8)));
}
}

PageCommand ParsePageJson(std::wstring_view json) noexcept {
    // get_WebMessageAsJson returns a JSON value, not an already decoded string.
    // Protocol strings contain no quotes or escapes; the exact allowlist rejects
    // all noncanonical JSON representations, including embedded NUL characters.
    if (json.size() < 2 || json.front() != L'"' || json.back() != L'"') {
        return PageCommand::Invalid;
    }
    return ParsePageCommand(json.substr(1, json.size() - 2));
}

PageCommand ParsePageCommand(std::wstring_view message) noexcept {
    // The longest supported v1 message bounds work before any character access.
    constexpr std::wstring_view longestMessage = L"v1|performance|material|frosted-glass";
    if (message.empty() || message.size() > longestMessage.size()) {
        return PageCommand::Invalid;
    }
    struct NumericCommand { std::wstring_view prefix; int minimum,maximum; PageCommand first; };
    constexpr NumericCommand numericCommands[] {
        {L"v1|minimize|durationMs|",50,2000,PageCommand::MinimizeDurationFirst},
        {L"v1|minimize|unhideDurationMs|",0,5000,PageCommand::MinimizeUnhideDurationFirst},
        {L"v1|radial|shortcut|",1,4095,PageCommand::RadialShortcutFirst},
        {L"v1|dock|iconScale|",50,120,PageCommand::DockIconScaleFirst},
        {L"v1|organizer|iconScale|",50,120,PageCommand::OrganizerIconScaleFirst},
        {L"v1|dock|position|",0,100,PageCommand::DockPositionFirst},
        {L"v1|dock|bottomOffset|",0,160,PageCommand::DockBottomOffsetFirst},
        {L"v1|dock|iconSpacing|",4,24,PageCommand::DockIconSpacingFirst},
        {L"v1|dock|launchpadScale|",60,140,PageCommand::DockLaunchpadScaleFirst},
        {L"v1|dock|launchpadIconScale|",50,120,PageCommand::DockLaunchpadIconScaleFirst},
        {L"v1|spectrum|size|",50,300,PageCommand::SpectrumSizeFirst},
        {L"v1|spectrum|pulse|barWidth|",2,24,PageCommand::SpectrumPulseBarWidthFirst},
        {L"v1|spectrum|pulse|barGap|",0,12,PageCommand::SpectrumPulseBarGapFirst},
        {L"v1|spectrum|blocks|barWidth|",2,24,PageCommand::SpectrumBarWidthFirst},
        {L"v1|spectrum|blocks|barGap|",0,12,PageCommand::SpectrumBarGapFirst},
        {L"v1|spectrum|waves|barWidth|",2,24,PageCommand::SpectrumWavesBarWidthFirst},
        {L"v1|spectrum|waves|barGap|",0,12,PageCommand::SpectrumWavesBarGapFirst},
    };
    for(const auto& command:numericCommands) {
        if(message.substr(0,command.prefix.size())!=command.prefix)continue;
        const auto number=message.substr(command.prefix.size());
        if(number.empty()||number.size()>4||(number.size()>1&&number.front()==L'0'))return PageCommand::Invalid;
        int value=0;
        for(const auto ch:number) {
            if(ch<L'0'||ch>L'9')return PageCommand::Invalid;
            value=value*10+ch-L'0';
        }
        if(value<command.minimum||value>command.maximum)return PageCommand::Invalid;
        if(command.first==PageCommand::RadialShortcutFirst&&!ValidRadialShortcut(value))return PageCommand::Invalid;
        return static_cast<PageCommand>(static_cast<int>(command.first)+value-command.minimum);
    }
    constexpr std::wstring_view delayPrefix = L"v1|icons|delay|";
    if (message.substr(0, delayPrefix.size()) == delayPrefix) {
        const auto number = message.substr(delayPrefix.size());
        if (number.empty() || number.size() > 2 || number.front() == L'0') return PageCommand::Invalid;
        int delay = 0;
        for (const auto ch : number) {
            if (ch < L'0' || ch > L'9') return PageCommand::Invalid;
            delay = delay * 10 + (ch - L'0');
        }
        if (delay < 3 || delay > 60) return PageCommand::Invalid;
        return static_cast<PageCommand>(static_cast<int>(PageCommand::IconsDelayFirst) + delay - 3);
    }

    struct Entry {
        std::wstring_view message;
        PageCommand command;
    };
    static constexpr Entry commands[] = {
        {L"v1|dynamicIsland|enabled|true",PageCommand::IslandEnable},
        {L"v1|dynamicIsland|enabled|false",PageCommand::IslandDisable},
        {L"v1|dynamicIsland|locked|true",PageCommand::IslandLock},
        {L"v1|dynamicIsland|locked|false",PageCommand::IslandUnlock},
        {L"v1|dynamicIsland|reset",PageCommand::IslandReset},
        {L"v1|dynamicIsland|expand",PageCommand::IslandExpand},
        {L"v1|dynamicIsland|collapse",PageCommand::IslandCollapse},
        {L"v1|dynamicIsland|focus",PageCommand::IslandFocus},
        {L"v1|dynamicIsland|stopFocus",PageCommand::IslandStopFocus},
        {L"v1|dynamicIsland|hideFullscreen|true",PageCommand::IslandHideFullscreenOn},
        {L"v1|dynamicIsland|hideFullscreen|false",PageCommand::IslandHideFullscreenOff},
        {L"v1|dynamicIsland|topmost|true",PageCommand::IslandTopmostOn},
        {L"v1|dynamicIsland|topmost|false",PageCommand::IslandTopmostOff},
        {L"v1|dynamicIsland|hover|true",PageCommand::IslandHoverOn},
        {L"v1|dynamicIsland|hover|false",PageCommand::IslandHoverOff},
        {L"v1|dynamicIsland|autoHide|true",PageCommand::IslandAutoHideOn},
        {L"v1|dynamicIsland|autoHide|false",PageCommand::IslandAutoHideOff},
        {L"v1|dynamicIsland|size|small",PageCommand::IslandSizeSmall},
        {L"v1|dynamicIsland|size|standard",PageCommand::IslandSizeStandard},
        {L"v1|dynamicIsland|size|large",PageCommand::IslandSizeLarge},
        {L"v1|dynamicIsland|material|none",PageCommand::IslandMaterialNone},
        {L"v1|dynamicIsland|material|liquid-glass",PageCommand::IslandMaterialLiquid},
        {L"v1|dynamicIsland|material|frosted-glass",PageCommand::IslandMaterialFrosted},
        {L"v1|todayTodo|enabled|true",PageCommand::TodayTodoEnable},
        {L"v1|todayTodo|enabled|false",PageCommand::TodayTodoDisable},
        {L"v1|todayTodo|material|liquid-glass",PageCommand::TodayTodoLiquidGlass},
        {L"v1|todayTodo|material|frosted-glass",PageCommand::TodayTodoFrostedGlass},
        {L"v1|todayTodo|material|solid",PageCommand::TodayTodoSolid},
        {L"v1|todayTodo|reset",PageCommand::TodayTodoReset},
        {L"v1|ready", PageCommand::Ready},
        {L"v1|window|minimize", PageCommand::Minimize},
        {L"v1|window|maximize", PageCommand::ToggleFullscreen},
        {L"v1|window|close", PageCommand::Close},
        {L"v1|window|drag", PageCommand::Drag},
        {L"v1|window|dropzone-local", PageCommand::WallpaperDropZoneLocal},
        {L"v1|window|dropzone-music", PageCommand::MusicDropZone},
        {L"v1|window|dropzone-none", PageCommand::WallpaperDropZoneNone},
        {L"v1|wallpaper|dropFiles", PageCommand::WallpaperDropFiles},
        {L"v1|music|dropFiles", PageCommand::MusicDropFiles},
        {L"v1|clock|enabled|true", PageCommand::ClockEnable},
        {L"v1|clock|enabled|false", PageCommand::ClockDisable},
        {L"v1|clock|style|welfare", PageCommand::ClockWelfare},
        {L"v1|clock|style|mond", PageCommand::ClockMond},
        {L"v1|clock|style|insideDream", PageCommand::ClockInsideDream},
        {L"v1|clock|reset", PageCommand::ClockReset},
        {L"v1|calendar|enabled|true", PageCommand::CalendarEnable},
        {L"v1|calendar|enabled|false", PageCommand::CalendarDisable},
        {L"v1|calendar|theme|spacious", PageCommand::CalendarSpacious},
        {L"v1|calendar|theme|split", PageCommand::CalendarSplit},
        {L"v1|calendar|theme|todo", PageCommand::CalendarTodo},
        {L"v1|calendar|material|liquid-glass", PageCommand::CalendarLiquidGlass},
        {L"v1|calendar|material|frosted-glass", PageCommand::CalendarFrostedGlass},
        {L"v1|calendar|material|solid", PageCommand::CalendarSolid},
        {L"v1|calendar|textColor", PageCommand::CalendarTextColor},
        {L"v1|calendar|highlightColor", PageCommand::CalendarHighlightColor},
        {L"v1|calendar|reset", PageCommand::CalendarReset},
        {L"v1|performance|enabled|true", PageCommand::PerformanceEnable},
        {L"v1|performance|enabled|false", PageCommand::PerformanceDisable},
        {L"v1|performance|material|liquid-glass", PageCommand::PerformanceLiquidGlass},
        {L"v1|performance|material|frosted-glass", PageCommand::PerformanceFrostedGlass},
        {L"v1|performance|reset", PageCommand::PerformanceReset},
        {L"v1|spectrum|enabled|true", PageCommand::SpectrumEnable},
        {L"v1|spectrum|enabled|false", PageCommand::SpectrumDisable},
        {L"v1|spectrum|reset", PageCommand::SpectrumReset},
        {L"v1|spectrum|style|pulse", PageCommand::SpectrumStylePulse},
        {L"v1|spectrum|style|blocks", PageCommand::SpectrumStyleBlocks},
        {L"v1|spectrum|style|waves", PageCommand::SpectrumStyleWaves},
        {L"v1|performance|resource-snapshot", PageCommand::PerformanceResourceSnapshot},
        {L"v1|almanac|enabled|true", PageCommand::AlmanacEnable},
        {L"v1|almanac|enabled|false", PageCommand::AlmanacDisable},
        {L"v1|almanac|material|liquid-glass", PageCommand::AlmanacLiquidGlass},
        {L"v1|almanac|material|frosted-glass", PageCommand::AlmanacFrostedGlass},
        {L"v1|almanac|reset", PageCommand::AlmanacReset},
        {L"v1|weather|enabled|true", PageCommand::WeatherEnable},
        {L"v1|weather|enabled|false", PageCommand::WeatherDisable},
        {L"v1|weather|material|liquid-glass", PageCommand::WeatherLiquidGlass},
        {L"v1|weather|material|frosted-glass", PageCommand::WeatherFrostedGlass},
        {L"v1|weather|reset", PageCommand::WeatherReset},
        {L"v1|weather|refresh", PageCommand::WeatherRefresh},
        {L"v1|weather|location-settings", PageCommand::WeatherLocationSettings},
        {L"v1|weather|city|auto", PageCommand::WeatherCityAuto},
        {L"v1|weather|city|beijing", PageCommand::WeatherCityBeijing},
        {L"v1|weather|city|shanghai", PageCommand::WeatherCityShanghai},
        {L"v1|weather|city|guangzhou", PageCommand::WeatherCityGuangzhou},
        {L"v1|weather|city|shenzhen", PageCommand::WeatherCityShenzhen},
        {L"v1|weather|city|hangzhou", PageCommand::WeatherCityHangzhou},
        {L"v1|weather|city|chengdu", PageCommand::WeatherCityChengdu},
        {L"v1|weather|city|wuhan", PageCommand::WeatherCityWuhan},
        {L"v1|weather|city|nanjing", PageCommand::WeatherCityNanjing},
        {L"v1|organizer|enabled|true", PageCommand::OrganizerEnable},
        {L"v1|organizer|enabled|false", PageCommand::OrganizerDisable},
        {L"v1|organizer|material|liquid-glass", PageCommand::OrganizerLiquidGlass},
        {L"v1|organizer|material|frosted-glass", PageCommand::OrganizerFrostedGlass},
        {L"v1|organizer|reset", PageCommand::OrganizerReset},
        {L"v1|icons|enabled|true", PageCommand::IconsEnable},
        {L"v1|icons|enabled|false", PageCommand::IconsDisable},
        {L"v1|icons|autoHide|true", PageCommand::IconsAutoHideOn},
        {L"v1|icons|autoHide|false", PageCommand::IconsAutoHideOff},
        {L"v1|icons|restoreOnInput|true", PageCommand::IconsRestoreOn},
        {L"v1|icons|restoreOnInput|false", PageCommand::IconsRestoreOff},
        {L"v1|icons|reset", PageCommand::IconsReset},
        {L"v1|taskbar|enabled|true", PageCommand::TaskbarEnable},
        {L"v1|taskbar|enabled|false", PageCommand::TaskbarDisable},
        {L"v1|taskbar|transparent|true", PageCommand::TaskbarTransparentOn},
        {L"v1|taskbar|transparent|false", PageCommand::TaskbarTransparentOff},
        {L"v1|taskbar|autoHide|true", PageCommand::TaskbarAutoHideOn},
        {L"v1|taskbar|autoHide|false", PageCommand::TaskbarAutoHideOff},
        {L"v1|taskbar|hideBehavior|fully-hidden", PageCommand::TaskbarHideFully},
        {L"v1|taskbar|hideBehavior|auto-hide", PageCommand::TaskbarHideAuto},
        {L"v1|taskbar|reset", PageCommand::TaskbarReset},
        {L"v1|minimize|enabled|true", PageCommand::MinimizeEnable},
        {L"v1|minimize|enabled|false", PageCommand::MinimizeDisable},
        {L"v1|minimize|restore|true", PageCommand::MinimizeRestoreOn},
        {L"v1|minimize|restore|false", PageCommand::MinimizeRestoreOff},
        {L"v1|minimize|launch|true", PageCommand::MinimizeLaunchOn},
        {L"v1|minimize|launch|false", PageCommand::MinimizeLaunchOff},
        {L"v1|minimize|multiMonitor|true", PageCommand::MinimizeMultiMonitorOn},
        {L"v1|minimize|multiMonitor|false", PageCommand::MinimizeMultiMonitorOff},
        {L"v1|minimize|unhideTaskbar|true", PageCommand::MinimizeUnhideTaskbarOn},
        {L"v1|minimize|unhideTaskbar|false", PageCommand::MinimizeUnhideTaskbarOff},
        {L"v1|minimize|engine|modern", PageCommand::MinimizeModern},
        {L"v1|minimize|engine|classic", PageCommand::MinimizeClassic},
        {L"v1|minimize|reset", PageCommand::MinimizeReset},
        {L"v1|radial|enabled|true", PageCommand::RadialEnable},
        {L"v1|radial|enabled|false", PageCommand::RadialDisable},
        {L"v1|radial|material|liquid-glass", PageCommand::RadialLiquidGlass},
        {L"v1|radial|material|frosted-glass", PageCommand::RadialFrostedGlass},
        {L"v1|radial|reset", PageCommand::RadialReset},
        {L"v1|radial|capture|true", PageCommand::RadialCaptureBegin},
        {L"v1|radial|capture|false", PageCommand::RadialCaptureEnd},
        {L"v1|dock|enabled|true", PageCommand::DockEnable},
        {L"v1|dock|enabled|false", PageCommand::DockDisable},
        {L"v1|dock|material|liquid-glass", PageCommand::DockLiquidGlass},
        {L"v1|dock|material|frosted-glass", PageCommand::DockFrostedGlass},
        {L"v1|dock|reset", PageCommand::DockReset},
        {L"v1|dock|material|none", PageCommand::DockNoMaterial},
        {L"v1|dock|reflection|true", PageCommand::DockReflectionOn},
        {L"v1|dock|reflection|false", PageCommand::DockReflectionOff},
    };
    // Exact string_view equality also rejects embedded NUL and other controls.
    for (const auto& entry : commands) {
        if (message == entry.message) return entry.command;
    }
    return PageCommand::Invalid;
}

}  // namespace lume::ui
