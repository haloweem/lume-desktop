#pragma once
#include <windows.h>
#include <cwchar>

// Names Lume's windows use to recognise each other: window properties and
// class names that more than one file (or process: the material DLL, the
// minimize hook, the installer's shutdown helper) depends on. Changing a
// value here changes it for every user at once; the values are a contract
// with already-running older processes and tests, so do not rename them.
// Classes used only inside one file stay private to that file.
namespace lume::window_ids {

// Window properties.
// A desktop widget: takes part in desktop-level stacking and snapping.
inline constexpr wchar_t DesktopWidgetProp[] = L"LumeDesktopWidget";
// A desktop widget that always stays at the bottom of that layer, under the
// Dock and every other widget, and is never raised by a click.
inline constexpr wchar_t DesktopBottomProp[] = L"LumeDesktopBottom";
// A desktop widget that holds desktop icons (the icon organizer): it stays
// above every ordinary widget and below the Dock, whatever was clicked last.
inline constexpr wchar_t DesktopShelfProp[] = L"LumeDesktopShelf";
// On a click-through display window: the desktop widget it draws for. It
// shares that widget's place in the desktop layer, directly below it.
inline constexpr wchar_t DesktopCompanionOwnerProp[] = L"LumeDesktopCompanionOwner";
// A window that owns a glass material instance.
inline constexpr wchar_t GlassOwnerProp[] = L"LumeGlassOwner";
// On a glass surface window: the HWND it renders glass for.
inline constexpr wchar_t GlassSurfaceOwnerProp[] = L"LumeGlassSurfaceOwner";

// Class names.
// Every native desktop surface (Dock, clock, calendar, widgets) starts with this.
inline constexpr wchar_t DesktopClassPrefix[] = L"LumeNativeDesktop";
inline constexpr wchar_t DockIconsClass[] = L"LumeNativeDesktopDock";
inline constexpr wchar_t DockBarClass[] = L"LumeNativeDesktopDockBar";
inline constexpr wchar_t LaunchpadClass[] = L"LumeDockLaunchpad";
inline constexpr wchar_t CollectionPopupClass[] = L"LumeDockCollectionPopup";
inline constexpr wchar_t WidgetMenuClass[] = L"LumeWidgetMenuWindow";
inline constexpr wchar_t ManagerClass[] = L"LumeOriginalPageWindow";
inline constexpr wchar_t TrayClass[] = L"LumeNativeApplicationTray";
inline constexpr wchar_t GlassSurfaceClass[] = L"LumeSharedMaterialSurfaceV1";

// Private messages that cross a window or process boundary. A receiver must
// never reuse one of these numbers for its own private message. Values are
// unchanged from before they were collected here.
// Posted by a second LumeShell to the running tray window: open the manager.
inline constexpr UINT TrayOpenMessage = WM_APP + 42;
// Posted to the shell's UI thread (no window): drain queued music work.
inline constexpr UINT ShellWakeMessage = WM_APP + 43;
// Also cross-window, defined beside their protocols and checked below:
// widget_snap::Message/CancelMessage (WM_APP+0x6a4/0x6a5, sent between
// desktop widgets) and LumeMaterialStatusChangedMessage (WM_APP+0x5a0, posted
// by LumeGlass.dll to the window owning the glass).

inline bool HasDesktopClassPrefix(const wchar_t* name) {
    return name && wcsncmp(name, DesktopClassPrefix, sizeof(DesktopClassPrefix) / sizeof(wchar_t) - 1) == 0;
}
inline bool IsClass(HWND window, const wchar_t* name) {
    wchar_t actual[64]{};
    return GetClassNameW(window, actual, 64) && wcscmp(actual, name) == 0;
}

}  // namespace lume::window_ids
