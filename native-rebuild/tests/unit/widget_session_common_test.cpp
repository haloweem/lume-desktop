// The shared widget session helpers must produce exactly what the sessions
// built by hand before: the page compares these strings field by field.
#include "apps/shell/widget_session_common.h"
#include <cstring>
#include <iostream>

using namespace lume;
using platform::windows::WidgetAction;
using platform::windows::WidgetMaterialState;
namespace ws = shell::widget_session;

namespace {
int failures = 0;
void Check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; } }

// Verbatim from performance_session.cpp before the refactor.
std::wstring OldState(const wchar_t* type, bool enabled, bool running, const widget::Preferences& prefs, const wchar_t* materialName,
                      WidgetMaterialState material, widget::LoadState loadState, const std::wstring& error, bool applied, bool saved) {
    const wchar_t* status=material==WidgetMaterialState::Ready?L"native-ready":material==WidgetMaterialState::Starting?L"native-starting":
        material==WidgetMaterialState::Detached?L"inactive":L"transparent-fallback";
    const wchar_t* load=loadState==widget::LoadState::Loaded?L"loaded":loadState==widget::LoadState::Recovered?L"recovered":
        loadState==widget::LoadState::Failed?L"failed":L"fresh";
    return std::wstring(L"{\"version\":1,\"type\":\"")+type+L"-state\",\"enabled\":"+(enabled?L"true":L"false")+
        L",\"running\":"+(running?L"true":L"false")+L",\"locked\":"+(prefs.locked?L"true":L"false")+
        L",\"material\":\""+materialName+L"\",\"materialStatus\":\""+status+
        L"\",\"loadStatus\":\""+load+L"\",\"error\":\""+error+L"\",\"width\":"+std::to_wstring(prefs.width)+
        L",\"height\":"+std::to_wstring(prefs.height)+L",\"result\":\""+(applied?L"applied":L"snapshot")+
        L"\",\"saved\":"+(saved?L"true":L"false")+L"}";
}
// Verbatim from today_todo_session.cpp (hasSolid) and performance/almanac.
std::optional<widget::Preferences> OldAction(WidgetAction action, const widget::Preferences& prefs,
                                             const widget::Preferences& bounds, widget::Limits Limits, bool solid) {
    auto next=prefs;
    switch(action){
        case WidgetAction::Close:next.enabled=false;break;
        case WidgetAction::Lock:next.locked=!next.locked;break;
        case WidgetAction::Liquid:next.frosted=false;if(solid)next.solid=false;break;
        case WidgetAction::Frosted:next.frosted=true;if(solid)next.solid=false;break;
        case WidgetAction::Solid:if(solid)next.solid=true;break;
        case WidgetAction::Reset:next=widget::Defaults(Limits);next.enabled=prefs.enabled;break;
        case WidgetAction::Bounds:next.x=bounds.x;next.y=bounds.y;next.width=bounds.width;next.height=bounds.height;next.placed=true;break;
        case WidgetAction::Settings:return {};
        case WidgetAction::Refresh:return {};
    }
    return next;
}
bool Same(const widget::Preferences& a, const widget::Preferences& b) {
    return a.enabled == b.enabled && a.locked == b.locked && a.frosted == b.frosted && a.placed == b.placed &&
        a.solid == b.solid && a.panelColor == b.panelColor && a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
}

int main() {
    constexpr widget::Limits limits{340, 216, 320, 204, 720, 720};
    const WidgetMaterialState materials[] {WidgetMaterialState::Detached, WidgetMaterialState::Starting,
                                           WidgetMaterialState::Ready, WidgetMaterialState::TransparentFallback};
    const widget::LoadState loads[] {widget::LoadState::Fresh, widget::LoadState::Loaded,
                                     widget::LoadState::Recovered, widget::LoadState::Failed};
    const std::wstring errors[] {L"", L"runtime", L"save", L"rollback"};
    int cases = 0;
    for (const char* type : {"performance", "almanac", "todayTodo"})
    for (const char* material : {"liquid-glass", "frosted-glass", "solid"})
    for (const auto m : materials) for (const auto l : loads) for (const auto& e : errors)
    for (int bits = 0; bits < 32; ++bits) {
        widget::Preferences p; p.locked = bits & 4; p.width = 300 + bits; p.height = 150 + bits * 7;
        const bool en = bits & 1, run = bits & 2, applied = bits & 8, saved = bits & 16;
        const std::wstring wtype(type, type + strlen(type)), wmat(material, material + strlen(material));
        const auto expected = OldState(wtype.c_str(), en, run, p, wmat.c_str(), m, l, e, applied, saved);
        const auto actual = ws::StateJson({type, en, run, p.locked, material, m, l, e, p.width, p.height, applied, saved});
        if (expected != actual) { Check(false, "state json"); std::wcerr << expected << L"\n" << actual << L"\n"; return 1; }
        ++cases;
    }
    const WidgetAction actions[] {WidgetAction::Close, WidgetAction::Settings, WidgetAction::Reset, WidgetAction::Lock,
        WidgetAction::Liquid, WidgetAction::Frosted, WidgetAction::Bounds, WidgetAction::Refresh, WidgetAction::Solid};
    widget::Preferences bounds; bounds.x = -40; bounds.y = 77; bounds.width = 512; bounds.height = 333; bounds.placed = false;
    for (const auto a : actions) for (int bits = 0; bits < 32; ++bits) for (const bool solid : {false, true}) {
        widget::Preferences p; p.enabled = bits & 1; p.locked = bits & 2; p.frosted = bits & 4; p.solid = bits & 8;
        p.placed = bits & 16; p.x = 5; p.y = 6; p.width = 400; p.height = 300; p.panelColor = 0x123456;
        const auto expected = OldAction(a, p, bounds, limits, solid);
        const auto actual = ws::ActionPreferences(a, p, bounds, limits, solid);
        Check(expected.has_value() == actual.has_value() && (!expected || Same(*expected, *actual)), "action preferences");
        ++cases;
    }
    if (!failures) std::cout << "widget session common passed " << cases << " cases\n";
    return failures ? 1 : 0;
}
