#pragma once
#include "core/widget/widget_preferences.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/material/widget_material_runtime.h"
#include "platform/windows/widget_ui/native_widget_window.h"
#include <optional>
#include <string>

// Pieces the single-window desktop widget sessions (performance, almanac,
// today's todo) share word for word. Each session still owns its runtime,
// rollback and error codes, which differ on purpose.
namespace lume::shell::widget_session {

inline const char* MaterialStatusName(platform::windows::WidgetMaterialState state) {
    using platform::windows::WidgetMaterialState;
    return state == WidgetMaterialState::Ready ? "native-ready" : state == WidgetMaterialState::Starting ? "native-starting" :
        state == WidgetMaterialState::Detached ? "inactive" : "transparent-fallback";
}
inline const char* LoadStatusName(widget::LoadState state) {
    return state == widget::LoadState::Loaded ? "loaded" : state == widget::LoadState::Recovered ? "recovered" :
        state == widget::LoadState::Failed ? "failed" : "fresh";
}

// The page's "<type>-state" message; every value is ASCII except error, which
// the sessions only ever set to fixed ASCII codes.
struct StateFields {
    const char* type;
    bool enabled, running, locked;
    const char* material;
    platform::windows::WidgetMaterialState materialState;
    widget::LoadState loadState;
    const std::wstring& error;
    int width, height;
    bool applied, saved;
};
inline std::wstring StateJson(const StateFields& f) {
    const auto wide = [](const char* text) { return std::wstring(text, text + std::char_traits<char>::length(text)); };
    return L"{\"version\":1,\"type\":\"" + wide(f.type) + L"-state\",\"enabled\":" + (f.enabled ? L"true" : L"false") +
        L",\"running\":" + (f.running ? L"true" : L"false") + L",\"locked\":" + (f.locked ? L"true" : L"false") +
        L",\"material\":\"" + wide(f.material) + L"\",\"materialStatus\":\"" + wide(MaterialStatusName(f.materialState)) +
        L"\",\"loadStatus\":\"" + wide(LoadStatusName(f.loadState)) + L"\",\"error\":\"" + f.error + L"\",\"width\":" +
        std::to_wstring(f.width) + L",\"height\":" + std::to_wstring(f.height) + L",\"result\":\"" +
        (f.applied ? L"applied" : L"snapshot") + L"\",\"saved\":" + (f.saved ? L"true" : L"false") + L"}";
}

// The preferences a menu action asks for. Settings and Refresh change nothing
// (nullopt). Widgets without an opaque panel ignore Solid and leave solid
// untouched for Liquid/Frosted, as they always have.
inline std::optional<widget::Preferences> ActionPreferences(platform::windows::WidgetAction action,
    const widget::Preferences& current, const widget::Preferences& bounds, widget::Limits limits, bool hasSolid) {
    using platform::windows::WidgetAction;
    auto next = current;
    switch (action) {
        case WidgetAction::Close: next.enabled = false; break;
        case WidgetAction::Lock: next.locked = !next.locked; break;
        case WidgetAction::Liquid: next.frosted = false; if (hasSolid) next.solid = false; break;
        case WidgetAction::Frosted: next.frosted = true; if (hasSolid) next.solid = false; break;
        case WidgetAction::Solid: if (hasSolid) next.solid = true; break;
        case WidgetAction::Reset: next = widget::Defaults(limits); next.enabled = current.enabled; break;
        case WidgetAction::Bounds:
            next.x = bounds.x; next.y = bounds.y; next.width = bounds.width; next.height = bounds.height; next.placed = true; break;
        case WidgetAction::Settings: return {};
        case WidgetAction::Refresh: return {};
    }
    return next;
}

}  // namespace lume::shell::widget_session
