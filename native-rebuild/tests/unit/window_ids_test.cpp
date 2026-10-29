// The names and message numbers Lume's windows and processes share must keep
// their values (older running processes and the installer rely on them) and
// the cross-window messages must not collide.
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/material/material_api.h"
#include "platform/windows/widget_ui/widget_snap_protocol.h"
#include <cstdlib>
#include <cwchar>
#include <iostream>

namespace ids = lume::window_ids;
namespace snap = lume::platform::windows::widget_snap;

constexpr UINT CrossWindow[] = {ids::TrayOpenMessage, ids::ShellWakeMessage, snap::Message, snap::CancelMessage,
                                LumeMaterialStatusChangedMessage};
constexpr bool Distinct() {
    for (std::size_t i = 0; i < std::size(CrossWindow); ++i)
        for (std::size_t j = i + 1; j < std::size(CrossWindow); ++j)
            if (CrossWindow[i] == CrossWindow[j]) return false;
    return true;
}
static_assert(Distinct(), "cross-window messages must be distinct");
static_assert(ids::TrayOpenMessage == WM_APP + 42 && ids::ShellWakeMessage == WM_APP + 43, "values are a contract");

namespace {
int failures = 0;
void Check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; } }
}

int main() {
    Check(!wcscmp(ids::DesktopWidgetProp, L"LumeDesktopWidget"), "desktop widget property");
    Check(!wcscmp(ids::GlassOwnerProp, L"LumeGlassOwner"), "glass owner property");
    Check(!wcscmp(ids::GlassSurfaceOwnerProp, L"LumeGlassSurfaceOwner"), "glass surface owner property");
    Check(!wcscmp(ids::DockBarClass, L"LumeNativeDesktopDockBar"), "Dock bar class (minimize hook finds it)");
    Check(!wcscmp(ids::TrayClass, L"LumeNativeApplicationTray"), "tray class (installer finds it)");
    Check(!wcscmp(ids::ManagerClass, L"LumeOriginalPageWindow"), "manager class (installer finds it)");
    Check(!wcscmp(ids::GlassSurfaceClass, L"LumeSharedMaterialSurfaceV1"), "glass surface class");
    Check(ids::HasDesktopClassPrefix(ids::DockIconsClass) && ids::HasDesktopClassPrefix(ids::DockBarClass) &&
          ids::HasDesktopClassPrefix(L"LumeNativeDesktopClock") && ids::HasDesktopClassPrefix(L"LumeNativeDesktopWidget"),
          "desktop surfaces carry the prefix");
    Check(!ids::HasDesktopClassPrefix(ids::LaunchpadClass) && !ids::HasDesktopClassPrefix(L"LumeNativeDesk") &&
          !ids::HasDesktopClassPrefix(nullptr), "other classes do not");
    if (!failures) std::cout << "window ids passed\n";
    return failures ? 1 : 0;
}
