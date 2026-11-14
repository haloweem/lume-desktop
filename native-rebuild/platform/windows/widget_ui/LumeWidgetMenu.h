#pragma once
#include <windows.h>
#include <string>
#include <vector>

class LumeWidgetMenu
{
public:
	// 共用 Windows 原生菜单交互；返回命令由调用方处理，0 为取消。
	// ShowActions: 10 恢复默认，11 打开设置，其余命令保持调用方约定。
	static UINT ShowActions(HWND owner, POINT position, bool canRefresh, bool canCreate, bool compact = false, bool list = false, bool frosted = false, bool canMaterial = false, bool locked = false, bool canCalendarTheme = false, int calendarTheme = 0, bool solid = false);
	static UINT Show(HWND owner, POINT position, int x, int y,
		int scalePercent, bool locked, int timeStyle = 0);
    // Radial popup has no draggable position; command 13 opens shortcut settings.
	static UINT ShowRadial(HWND owner, POINT position, bool frosted);
    // Organizer: checked collapse toggle (4 on, 5 off), list/icons 6/7, colors 230..235.
    // item: opened on an icon, so the menu starts with 删除图标 (13).
    static UINT ShowOrganizer(HWND, POINT, bool collapsed, bool list, bool frosted, bool locked, bool item = false, int accent = 0);
    // 50 edit, 51 delete, 52 new task, 53 new checklist; other widget commands unchanged.
    static UINT ShowTodayTodo(HWND, POINT, bool item, bool writable, bool canCreate, bool frosted, bool locked, bool solid = false);
    static UINT ShowNotes(HWND owner, POINT position, bool frosted, bool locked);
    // Dock background: 40 new collection, 46 add a system icon, 47 system tray, 8/9 material, 11 settings, 10 reset, 1 close.
    // Material submenu adds "无" (command 7) for a Dock without glass.
    static UINT ShowDock(HWND owner, POINT position, bool frosted, bool none = false, bool systemTray = false);
    // Dynamic island: 8/9 material, 7 none, 2 expand/collapse, 11 settings, 10 reset, 1 close.
    static UINT ShowIsland(HWND owner, POINT position, bool frosted, bool none, bool expanded,
                           bool canExpand = true);
    // Dock or launchpad item: 20 pin, 21 unpin, 22 open location, 23 remove from launchpad.
    // `canReveal` is false for virtual entries such as the Lume manager item.
    static UINT ShowDockItem(HWND owner, POINT position, bool pinned, bool removable, bool canReveal = true,
                             bool inCollection = false, bool dockActions = false, bool frosted = false, bool none = false,
                             bool runningItem = false, bool systemTray = false);
    // Launchpad add button: a single "add files" entry plus "new collection",
    // followed by a small file/folder choice for the add entry.
    static UINT ShowLaunchpadAdd(HWND owner, POINT position);
    static UINT ShowLaunchpadAddKind(HWND owner, POINT position);
    // Launchpad collection tile: 40 rename, 41 delete (contents return to the grid).
    static UINT ShowLaunchpadCollection(HWND owner, POINT position, bool dockActions = false, bool pinned = false);
	struct Action { std::wstring label; UINT command=0; bool enabled=true; };
	// Application menus are explicitly invoked tools (for example the tray),
	// independent of desktop-widget visibility and fullscreen suppression.
	enum class Scope { Desktop, Application };
	// Compact text-only layout for short menus such as the application tray.
	static UINT ShowItems(HWND owner, POINT position, const std::vector<Action>& items, bool compact = false,
		Scope scope = Scope::Desktop);
	static UINT ShowScreenshotPin(HWND owner, POINT position);
	// Audio spectrum: 30 + style index (样式一, 样式二), IDM_LUME_WIDGET_SIZE_FIRST.. for 50..300%,
	// then 11 settings, 12 lock, 10 reset, 1 close.
	static UINT ShowSpectrum(HWND owner, POINT position, int style, int scalePercent, bool locked);
};
