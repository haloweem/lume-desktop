#include "core/ui/calendar_page_state.h"
namespace lume::ui {
std::optional<calendar::Action> CalendarActionForPage(PageCommand command) {
    using A = calendar::ActionType;
    switch (command) {
    case PageCommand::CalendarEnable: return calendar::Action{A::Enable,1};
    case PageCommand::CalendarDisable: return calendar::Action{A::Enable,0};
    case PageCommand::CalendarSpacious: return calendar::Action{A::SelectTheme,static_cast<int>(calendar::Theme::Spacious)};
    case PageCommand::CalendarSplit: return calendar::Action{A::SelectTheme,static_cast<int>(calendar::Theme::Split)};
    case PageCommand::CalendarTodo: return calendar::Action{A::SelectTheme,static_cast<int>(calendar::Theme::Todo)};
    case PageCommand::CalendarLiquidGlass: return calendar::Action{A::Material,static_cast<int>(calendar::Material::LiquidGlass)};
    case PageCommand::CalendarFrostedGlass: return calendar::Action{A::Material,static_cast<int>(calendar::Material::FrostedGlass)};
    case PageCommand::CalendarSolid: return calendar::Action{A::Material,static_cast<int>(calendar::Material::Solid)};
    case PageCommand::CalendarReset: return calendar::Action{A::Reset};
    default: return std::nullopt;
    }
}
std::wstring CalendarPageState(const calendar::CalendarController& controller,
    calendar::LoadStatus loadStatus,CalendarPageResult result) {
    const auto& p = controller.Snapshot();
    const auto boolean = [](bool value) { return value ? L"true" : L"false"; };
    const auto rgba = [](calendar::Rgba value,calendar::Rgba fallback) {
        const auto color=calendar::FormatRgba(calendar::ValidRgba(value)?value:fallback);
        return std::wstring(color.begin(),color.end());
    };
    const wchar_t* error=L"";
    switch (controller.Error()) {
    case calendar::ControllerError::None: break;
    case calendar::ControllerError::Invalid: error=L"invalid";break;
    case calendar::ControllerError::Runtime: error=L"runtime";break;
    case calendar::ControllerError::Save: error=L"save";break;
    case calendar::ControllerError::Rollback: error=L"rollback";break;
    default: error=L"invalid";break;
    }
    const wchar_t* load=L"failed";
    switch (loadStatus) {
    case calendar::LoadStatus::Loaded: load=L"loaded";break;
    case calendar::LoadStatus::Fresh: load=L"fresh";break;
    case calendar::LoadStatus::RecoveredBackup: load=L"recovered-backup";break;
    case calendar::LoadStatus::Failed: break;
    }
    if (load==std::wstring_view(L"failed") && !*error) error=L"load";
    if (!calendar::ValidPreferences(p)) error=L"invalid";
    const wchar_t* outcome=L"snapshot";
    switch (result) {
    case CalendarPageResult::Snapshot: break;
    case CalendarPageResult::Applied: outcome=L"applied";break;
    case CalendarPageResult::Cancelled: outcome=L"cancelled";break;
    }
    const bool saved=result==CalendarPageResult::Applied && !*error;
    // Every string comes from a fixed enum or validated hexadecimal formatter.
    return std::wstring(L"{\"version\":1,\"type\":\"calendar-settings\",\"calendar\":{\"enabled\":") + boolean(p.enabled) +
        L",\"theme\":\"" + (p.theme==calendar::Theme::Split?L"split":p.theme==calendar::Theme::Todo?L"todo":L"spacious") +
        L"\",\"material\":\"" + (p.material==calendar::Material::FrostedGlass?L"frosted-glass":p.material==calendar::Material::Solid?L"solid":L"liquid-glass") +
        L"\",\"textColor\":\"" + rgba(p.textColor,{0xffffff,255}) + L"\",\"highlightColor\":\"" + rgba(p.highlightColor,{0x0a84ff,255}) +
        L"\",\"locked\":" + boolean(p.locked) + L",\"running\":" + boolean(controller.Running()) +
        L"},\"saved\":" + boolean(saved) + L",\"error\":\"" + error +
        L"\",\"materialStatus\":\"transparent-fallback\",\"loadStatus\":\"" + load + L"\",\"result\":\"" + outcome + L"\"}";
}
}
