#include "StdAfx.h"
#include "platform/windows/lume_window_ids.h"
#include "LumeWidgetMenu.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/widget_ui/icon_art.h"
#include "platform/windows/widget_ui/windows_theme.h"

#include "resource.h"

#include <vector>

namespace
{
constexpr const wchar_t* kWindowClass = lume::window_ids::WidgetMenuClass;
constexpr int kRootWidth = 250;
constexpr int kSubmenuWidth = 170;
constexpr int kRootRowHeight = 42;
constexpr int kSubmenuRowHeight = 32;
constexpr int kPadding = 8;
constexpr UINT kSubmenuCloseTimer = 2;
constexpr UINT kParentHoverTimer = 3;
constexpr UINT kSubmenuCloseDelayMs = 3000;
constexpr int kSubmenuOverlap = 10;
constexpr int kSubmenuKeepAlivePadding = 48;

enum class MenuKind
{
	Root,
	Size,
	Position,
	Material,
	CalendarTheme,
	TimeStyle,
    OrganizerSize,
	SpectrumStyle
};

enum class MenuIcon
{
	None,
	Size,
	Position,
	Color,
	Lock,
	Close,
	Center,
	Refresh,
	Create,
	Material,
	Settings,
	Reset,
	Pin
};

struct MenuItem
{
	std::wstring label;
	UINT command = 0;
	MenuKind submenu = MenuKind::Root;
	MenuIcon icon = MenuIcon::None;
	bool separator = false;
	bool checked = false;
	bool enabled = true;
};

struct MenuState
{
	HWND window = nullptr;
	HWND owner = nullptr;
	MenuKind kind = MenuKind::Root;
	std::vector<MenuItem> items;
	int hover = -1;
	int pressed = -1;
	int scrollOffset = 0;
	int visibleRows = 0;
	int rowHeight = kRootRowHeight;
	int width = kRootWidth;
	int height = 0;
	double scale = 1.0;
	UINT result = 0;
	bool done = false;
	bool keepOwnerFocus = false;
	bool compact = false;
	int x = 0;
	int y = 0;
	int scalePercent = 100;
	bool locked = false;
	bool frosted = false;
	int calendarTheme = 0;
	int timeStyle = 0;
	bool childOpen = false;
	bool parentHoverArmed = false;
};

int Scale(const MenuState* state, int value)
{
	return static_cast<int>(std::lround(value * state->scale));
}

void AddRootItems(MenuState* state)
{
	state->items =
	{
		{ L"\u65f6\u95f4\u6837\u5f0f", 0, MenuKind::TimeStyle, MenuIcon::Color },
		{ L"\u5927\u5c0f", 0, MenuKind::Size, MenuIcon::Size },
		{ L"\u4f4d\u7f6e", 0, MenuKind::Position, MenuIcon::Position },
		{ L"", 0, MenuKind::Root, MenuIcon::None, true },
		{ L"\u7ec4\u4ef6\u989c\u8272", IDM_LUME_WIDGET_COLOR,
			MenuKind::Root, MenuIcon::Color },
		{ L"\u6253\u5f00\u8bbe\u7f6e", 11,
			MenuKind::Root, MenuIcon::Settings },
		{ L"\u9501\u5b9a\u4f4d\u7f6e", IDM_LUME_WIDGET_LOCK,
			MenuKind::Root, MenuIcon::Lock, false, state->locked },
		{ L"", 0, MenuKind::Root, MenuIcon::None, true },
		{ L"\u5173\u95ed\u7ec4\u4ef6", IDM_CLOSESKIN,
			MenuKind::Root, MenuIcon::Close }
	};
}

void AddSizeItems(MenuState* state)
{
	for (int percent = 50; percent <= 300; percent += 10)
	{
		WCHAR label[16];
		_snwprintf_s(label, _TRUNCATE, L"%i%%", percent);
		state->items.push_back(
		{
			label,
			static_cast<UINT>(IDM_LUME_WIDGET_SIZE_FIRST + (percent - 50) / 10),
			MenuKind::Size,
			MenuIcon::None,
			false,
			percent == state->scalePercent
		});
	}
}

void AddPositionItems(MenuState* state)
{
	WCHAR xLabel[32];
	WCHAR yLabel[32];
	_snwprintf_s(xLabel, _TRUNCATE, L"X: %i", state->x);
	_snwprintf_s(yLabel, _TRUNCATE, L"Y: %i", state->y);
	state->items =
	{
		{ xLabel, IDM_LUME_WIDGET_POSITION, MenuKind::Position },
		{ yLabel, IDM_LUME_WIDGET_POSITION, MenuKind::Position },
		{ L"", 0, MenuKind::Position, MenuIcon::None, true },
		{ L"\u5c45\u4e2d", IDM_LUME_WIDGET_CENTER,
			MenuKind::Position, MenuIcon::Center }
	};
}

// The Dock's material submenu also offers "none"; it carries its state in
// timeStyle, which the material submenu does not otherwise use.
constexpr int kDockMaterialGlass = -1, kDockMaterialNone = -2;
constexpr UINT kMaterialNoneCommand = 7;
// The calendar adds an opaque "普通" choice.
constexpr int kCalendarMaterialGlass = -3, kCalendarMaterialSolid = -4;
constexpr UINT kMaterialSolidCommand = 20;

void AddMaterialItems(MenuState* state)
{
	const bool dock = state->timeStyle == kDockMaterialGlass || state->timeStyle == kDockMaterialNone;
	const bool none = state->timeStyle == kDockMaterialNone;
	const bool calendar = state->timeStyle == kCalendarMaterialGlass || state->timeStyle == kCalendarMaterialSolid;
	const bool solid = state->timeStyle == kCalendarMaterialSolid;
	state->items.clear();
	if (calendar) state->items.push_back({ L"\u666e\u901a", kMaterialSolidCommand, MenuKind::Material, MenuIcon::None, false, solid });
	state->items.push_back({ L"\u6db2\u6001\u73bb\u7483", 8, MenuKind::Material, MenuIcon::None, false, !none && !solid && !state->frosted });
	state->items.push_back({ L"\u78e8\u7802\u73bb\u7483", 9, MenuKind::Material, MenuIcon::None, false, !none && !solid && state->frosted });
	if (dock) state->items.push_back({ L"\u65e0", kMaterialNoneCommand, MenuKind::Material, MenuIcon::None, false, none });
}

void AddCalendarThemeItems(MenuState* state)
{
	state->items =
	{
		{ L"\u7ecf\u5178", 13, MenuKind::CalendarTheme, MenuIcon::None, false, state->calendarTheme == 0 },
		{ L"\u8212\u5c55", 14, MenuKind::CalendarTheme, MenuIcon::None, false, state->calendarTheme == 1 },
		{ L"\u5f85\u529e", 15, MenuKind::CalendarTheme, MenuIcon::None, false, state->calendarTheme == 2 }
	};
}

void AddTimeStyleItems(MenuState* state)
{
	state->items =
	{
		{ L"\u6837\u5f0f\u4e00", 15, MenuKind::TimeStyle, MenuIcon::None, false, state->timeStyle == 0 },
		{ L"\u6837\u5f0f\u4e8c", 16, MenuKind::TimeStyle, MenuIcon::None, false, state->timeStyle == 1 },
		{ L"\u6837\u5f0f\u4e09", 17, MenuKind::TimeStyle, MenuIcon::None, false, state->timeStyle == 2 }
	};
}

void AddSpectrumStyleItems(MenuState* state)
{
	state->items =
	{
		{ L"样式一", 30, MenuKind::SpectrumStyle, MenuIcon::None, false, state->timeStyle == 0 },
		{ L"样式二", 31, MenuKind::SpectrumStyle, MenuIcon::None, false, state->timeStyle == 1 },
		{ L"样式三", 32, MenuKind::SpectrumStyle, MenuIcon::None, false, state->timeStyle == 2 }
	};
}

bool IsActionSubmenu(MenuKind kind)
{
	return kind == MenuKind::SpectrumStyle || kind == MenuKind::Material || kind == MenuKind::CalendarTheme || kind == MenuKind::TimeStyle || kind == MenuKind::OrganizerSize;
}

int ItemHeight(const MenuState* state, const MenuItem& item)
{
	return Scale(state, item.separator ? (state->compact ? 7 : 9) : state->rowHeight);
}

int Padding(const MenuState* state) { return state->compact ? 6 : kPadding; }

int ContentHeight(const MenuState* state)
{
	int height = Scale(state, Padding(state) * 2);
	const int end = min(static_cast<int>(state->items.size()),
		state->scrollOffset + state->visibleRows);
	for (int index = state->scrollOffset; index < end; ++index)
		height += ItemHeight(state, state->items[index]);
	return height;
}

RECT ItemRect(const MenuState* state, int wanted)
{
	RECT rect = { 0, 0, 0, 0 };
	int top = Scale(state, Padding(state));
	const int end = min(static_cast<int>(state->items.size()),
		state->scrollOffset + state->visibleRows);
	for (int index = state->scrollOffset; index < end; ++index)
	{
		const int height = ItemHeight(state, state->items[index]);
		if (index == wanted)
		{
			SetRect(&rect, Scale(state, 5), top,
				state->width - Scale(state, 5), top + height);
			return rect;
		}
		top += height;
	}
	return rect;
}

int HitTest(const MenuState* state, POINT point)
{
	const int end = min(static_cast<int>(state->items.size()),
		state->scrollOffset + state->visibleRows);
	for (int index = state->scrollOffset; index < end; ++index)
	{
		const RECT rect = ItemRect(state, index);
		if (PtInRect(&rect, point))
			return state->items[index].separator || !state->items[index].enabled ? -1 : index;
	}
	return -1;
}

bool CursorInside(HWND window)
{
	RECT rect;
	if (!GetWindowRect(window, &rect)) return false;
	POINT cursor;
	if (!GetCursorPos(&cursor)) return false;
	return PtInRect(&rect, cursor) != FALSE;
}

bool CursorNear(HWND window)
{
	RECT rect;
	if (!GetWindowRect(window, &rect)) return false;
	InflateRect(&rect, kSubmenuKeepAlivePadding, kSubmenuKeepAlivePadding);
	POINT cursor;
	if (!GetCursorPos(&cursor)) return false;
	return PtInRect(&rect, cursor) != FALSE;
}

void ArmSubmenuClose(MenuState* state)
{
	if (IsActionSubmenu(state->kind))
		SetTimer(state->window, kSubmenuCloseTimer, kSubmenuCloseDelayMs, nullptr);
}

void AddRoundedRect(Gdiplus::GraphicsPath& path, Gdiplus::RectF rect, float radius)
{
	const float diameter = radius * 2.0f;
	path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
	path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
	path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter,
		diameter, diameter, 0.0f, 90.0f);
	path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
	path.CloseFigure();
}

void DrawIcon(Gdiplus::Graphics& graphics, MenuIcon icon, float x, float y, bool enabled, std::uint32_t rgb)
{
    using lume::platform::windows::icon_art::Icon;
    Icon art;
    switch (icon)
    {
    case MenuIcon::Size: art = Icon::Size; break;
    case MenuIcon::Position: art = Icon::Position; break;
    case MenuIcon::Color: art = Icon::Color; break;
    case MenuIcon::Lock: art = Icon::Lock; break;
    case MenuIcon::Close: art = Icon::Close; break;
    case MenuIcon::Center: art = Icon::Center; break;
    case MenuIcon::Refresh: art = Icon::Refresh; break;
    case MenuIcon::Create: art = Icon::Create; break;
    case MenuIcon::Material: art = Icon::Material; break;
    case MenuIcon::Settings: art = Icon::Settings; break;
    case MenuIcon::Reset: art = Icon::Reset; break;
    case MenuIcon::Pin: art = Icon::Pin; break;
    default: return;
    }
    lume::platform::windows::icon_art::Draw(graphics, art, x, y, 18.0f,
        Gdiplus::Color(enabled ? 255 : 105, static_cast<BYTE>((rgb >> 16) & 255),
            static_cast<BYTE>((rgb >> 8) & 255), static_cast<BYTE>(rgb & 255)));
}
void DrawMenu(MenuState* state, HDC hdc)
{
	RECT client;
	GetClientRect(state->window, &client);
	Gdiplus::Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
	Gdiplus::Graphics graphics(&buffer);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	graphics.ScaleTransform(static_cast<Gdiplus::REAL>(state->scale),
		static_cast<Gdiplus::REAL>(state->scale));

	const auto theme = lume::platform::windows::WindowsTheme();
	const auto color = [](std::uint32_t rgb, BYTE alpha = 255) {
		return Gdiplus::Color(alpha, static_cast<BYTE>((rgb >> 16) & 255),
			static_cast<BYTE>((rgb >> 8) & 255), static_cast<BYTE>(rgb & 255));
	};
	Gdiplus::SolidBrush background(color(theme.panel));
	graphics.FillRectangle(&background, Gdiplus::RectF(0.0f, 0.0f,
		static_cast<Gdiplus::REAL>(client.right / state->scale),
		static_cast<Gdiplus::REAL>(client.bottom / state->scale)));

	lume::platform::windows::direct_write::Style textStyle;
	textStyle.size = state->compact ? 13.0f : 14.0f;
	textStyle.flags |= DT_END_ELLIPSIS;
	Gdiplus::SolidBrush hoverBrush(color(theme.hover, theme.dark ? 28 : 18));
	Gdiplus::Pen divider(color(theme.divider, theme.dark ? 42 : 32), 1.0f);

	const int end = min(static_cast<int>(state->items.size()),
		state->scrollOffset + state->visibleRows);
	for (int index = state->scrollOffset; index < end; ++index)
	{
		const MenuItem& item = state->items[index];
		const RECT physical = ItemRect(state, index);
		Gdiplus::RectF rect(
			static_cast<Gdiplus::REAL>(physical.left / state->scale),
			static_cast<Gdiplus::REAL>(physical.top / state->scale),
			static_cast<Gdiplus::REAL>((physical.right - physical.left) / state->scale),
			static_cast<Gdiplus::REAL>((physical.bottom - physical.top) / state->scale));
		if (item.separator)
		{
			const float lineY = rect.Y + rect.Height / 2.0f;
			graphics.DrawLine(&divider, rect.X + 4, lineY, rect.GetRight() - 4, lineY);
			continue;
		}
		if (index == state->hover)
		{
			Gdiplus::GraphicsPath path;
			AddRoundedRect(path, rect, 5.0f);
			graphics.FillPath(&hoverBrush, &path);
		}

		const float iconX = rect.X + 12.0f;
		const float iconY = rect.Y + (rect.Height - 18.0f) / 2.0f;
		DrawIcon(graphics, item.icon, iconX, iconY, item.enabled, theme.foreground);
		const float textInset = state->compact ? 12.0f : 42.0f;
		Gdiplus::RectF textRect(rect.X + textInset, rect.Y,
			rect.Width - textInset - (state->compact ? 12.0f : 26.0f), rect.Height);
		lume::platform::windows::direct_write::Draw(graphics, item.label, textRect,
			item.enabled ? color(theme.foreground) : color(theme.secondary), textStyle);

		Gdiplus::Pen markPen(color(theme.foreground, theme.dark ? 220 : 180), 1.5f);
		if (item.command == 0 && item.submenu != MenuKind::Root)
		{
			const float arrowX = rect.GetRight() - 17.0f;
			const float arrowY = rect.Y + rect.Height / 2.0f;
			graphics.DrawLine(&markPen, arrowX - 2, arrowY - 4, arrowX + 2, arrowY);
			graphics.DrawLine(&markPen, arrowX + 2, arrowY, arrowX - 2, arrowY + 4);
		}
		if (item.checked)
		{
			const float checkX = rect.GetRight() - 21.0f;
			const float checkY = rect.Y + rect.Height / 2.0f;
			graphics.DrawLine(&markPen, checkX - 4, checkY, checkX - 1, checkY + 3);
			graphics.DrawLine(&markPen, checkX - 1, checkY + 3, checkX + 5, checkY - 4);
		}
	}

	Gdiplus::Graphics target(hdc);
	target.SetPageUnit(Gdiplus::UnitPixel);
	target.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
	target.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
	target.DrawImage(&buffer, Gdiplus::Rect(0, 0, client.right, client.bottom),
		0, 0, client.right, client.bottom, Gdiplus::UnitPixel);
}

UINT GetMenuWindowDpi(HWND window)
{
	return window ? GetDpiForWindow(window) : 0;
}

UINT ShowPopup(HWND owner, POINT position, MenuKind kind,
	int x, int y, int scalePercent, bool locked,
	const std::vector<MenuItem>* rootItems = nullptr, bool frosted = false,
	int calendarTheme = 0, int timeStyle = 0, bool compact = false);

void OpenSubmenu(MenuState* state, int index)
{
	if (state->childOpen || index < 0 || index >= static_cast<int>(state->items.size())) return;
	const auto& item = state->items[index];
	if (item.command || item.submenu == MenuKind::Root) return;
	state->childOpen = true;
	RECT row = ItemRect(state, index);
	POINT child = { state->width - Scale(state, kSubmenuOverlap), row.top };
	ClientToScreen(state->window, &child);
	const UINT command = ShowPopup(state->window, child, item.submenu,
		state->x, state->y, state->scalePercent, state->locked, nullptr,
		state->frosted, state->calendarTheme, state->timeStyle);
	state->childOpen = false;
	if (command)
	{
		state->result = command;
		state->done = true;
	}
	SetCapture(state->window);
	SetForegroundWindow(state->window);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	MenuState* state = reinterpret_cast<MenuState*>(
		GetWindowLongPtr(window, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		const CREATESTRUCT* create = reinterpret_cast<const CREATESTRUCT*>(lParam);
		state = reinterpret_cast<MenuState*>(create->lpCreateParams);
		state->window = window;
		SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
	}
	if (!state) return DefWindowProc(window, message, wParam, lParam);

	switch (message)
	{
	case WM_MOUSEACTIVATE:
		if (state->keepOwnerFocus) return MA_NOACTIVATE;
		break;
	case WM_CAPTURECHANGED:
		if (state->keepOwnerFocus && reinterpret_cast<HWND>(lParam) != window) state->done = true;
		break;
	case WM_ACTIVATEAPP:
		if (!wParam) state->done = true;
		break;
	case WM_CANCELMODE:
	case WM_DESTROY:
		state->done = true;
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	{
		PAINTSTRUCT paint;
		HDC hdc = BeginPaint(window, &paint);
		DrawMenu(state, hdc);
		EndPaint(window, &paint);
		return 0;
	}
	case WM_MOUSEMOVE:
	{
		TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, window, 0 };
		TrackMouseEvent(&track);
		POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		const int hover = HitTest(state, point);
		if (IsActionSubmenu(state->kind))
		{
			KillTimer(window, kSubmenuCloseTimer);
			// The submenu holds capture, so moves over the parent arrive here.
			// Arm the hand-off once: re-arming on every move kept the submenu
			// open for as long as the pointer kept moving.
			bool leaving = false;
			if (hover < 0)
			{
				POINT parentPoint = point;
				MapWindowPoints(window, state->owner, &parentPoint, 1);
				auto* parent = reinterpret_cast<MenuState*>(
					GetWindowLongPtr(state->owner, GWLP_USERDATA));
				RECT rect;
				GetClientRect(state->owner, &rect);
				leaving = parent && PtInRect(&rect, parentPoint) &&
					HitTest(parent, parentPoint) != parent->hover;
			}
			if (!leaving && state->parentHoverArmed)
			{
				KillTimer(window, kParentHoverTimer);
				state->parentHoverArmed = false;
			}
			else if (leaving && !state->parentHoverArmed)
			{
				SetTimer(window, kParentHoverTimer, 90, nullptr);
				state->parentHoverArmed = true;
			}
		}
		if (hover != state->hover)
		{
			state->hover = hover;
			InvalidateRect(window, nullptr, FALSE);
			KillTimer(window, 1);
			if (state->kind == MenuKind::Root && hover >= 0 &&
				IsActionSubmenu(state->items[hover].submenu))
				SetTimer(window, 1, 180, nullptr);
		}
		return 0;
	}
	case WM_MOUSELEAVE:
		if (state->childOpen) return 0;
		if (IsActionSubmenu(state->kind))
		{
			ArmSubmenuClose(state);
			return 0;
		}
		KillTimer(window, 1);
		state->hover = -1;
		InvalidateRect(window, nullptr, FALSE);
		return 0;
	case WM_TIMER:
		if (wParam == kParentHoverTimer)
		{
			KillTimer(window, kParentHoverTimer);
			state->parentHoverArmed = false;
			POINT point;
			GetCursorPos(&point);
			ScreenToClient(state->owner, &point);
			auto* parent = reinterpret_cast<MenuState*>(
				GetWindowLongPtr(state->owner, GWLP_USERDATA));
			RECT rect;
			GetClientRect(state->owner, &rect);
			if (parent && !CursorInside(window) && PtInRect(&rect, point) &&
				HitTest(parent, point) != parent->hover)
			{
				state->done = true;
				PostMessage(state->owner, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
			}
		}
		if (wParam == 1)
		{
			KillTimer(window, 1);
			OpenSubmenu(state, state->hover);
		}
		if (wParam == kSubmenuCloseTimer)
		{
			KillTimer(window, kSubmenuCloseTimer);
			if (!CursorNear(window) && !CursorNear(state->owner)) state->done = true;
		}
		return 0;
	case WM_MOUSEWHEEL:
		if ((state->kind == MenuKind::Size || state->kind == MenuKind::OrganizerSize) &&
			static_cast<int>(state->items.size()) > state->visibleRows)
		{
			const int direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
			state->scrollOffset = min(
				max(0, state->scrollOffset + direction),
				static_cast<int>(state->items.size()) - state->visibleRows);
			state->hover = -1;
			InvalidateRect(window, nullptr, FALSE);
		}
		return 0;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	{
		POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		RECT client;
		GetClientRect(window, &client);
		if (!PtInRect(&client, point))
		{
			state->done = true;
			if (IsActionSubmenu(state->kind))
			{
				POINT screenPoint = point;
				ClientToScreen(window, &screenPoint);
				RECT parentRect;
				GetWindowRect(state->owner, &parentRect);
				if (!PtInRect(&parentRect, screenPoint))
					PostMessage(state->owner, WM_CLOSE, 0, 0);
			}
		}
		else if (message == WM_LBUTTONDOWN)
			state->pressed = HitTest(state, point);
		return 0;
	}
	case WM_LBUTTONUP:
	{
		POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		const int index = HitTest(state, point);
		if (state->pressed != index)
		{
			state->pressed = -1;
			return 0;
		}
		state->pressed = -1;
		if (index < 0) return 0;
		const MenuItem& item = state->items[index];
		if (item.command)
		{
			state->result = item.command;
			state->done = true;
			return 0;
		}
		if (state->kind == MenuKind::Root && item.submenu != MenuKind::Root)
			OpenSubmenu(state, index);
		return 0;
	}
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE)
		{
			state->done = true;
			return 0;
		}
		if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_HOME || wParam == VK_END)
		{
			const int count = static_cast<int>(state->items.size());
			const int direction = wParam == VK_UP || wParam == VK_END ? -1 : 1;
			int next = state->hover;
			if (wParam == VK_HOME || wParam == VK_END || next < 0) next = direction > 0 ? -1 : 0;
			for (int step = 0; step < count; ++step) {
				next = (next + direction + count) % count;
				if (state->items[next].separator || !state->items[next].enabled) continue;
				state->hover = next;
				if (next < state->scrollOffset) state->scrollOffset = next;
				if (next >= state->scrollOffset + state->visibleRows) state->scrollOffset = next - state->visibleRows + 1;
				InvalidateRect(window, nullptr, FALSE);
				break;
			}
			return 0;
		}
		if ((wParam == VK_RETURN || wParam == VK_SPACE || wParam == VK_RIGHT) && state->hover >= 0)
		{
			const auto& item = state->items[state->hover];
			if (item.enabled && !item.separator) {
				if (item.command && wParam != VK_RIGHT) { state->result = item.command; state->done = true; }
				else if (!item.command && item.submenu != MenuKind::Root) OpenSubmenu(state, state->hover);
			}
			return 0;
		}
		break;
	case WM_CLOSE:
		state->done = true;
		return 0;
	}
	return DefWindowProc(window, message, wParam, lParam);
}

bool RegisterWindowClass()
{
	static bool registered = false;
	if (registered) return true;
	WNDCLASSEX windowClass{};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.style = CS_DROPSHADOW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = GetModuleHandle(nullptr);
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	windowClass.lpszClassName = kWindowClass;
	registered = RegisterClassEx(&windowClass) != 0 ||
		GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	return registered;
}

UINT ShowPopup(HWND owner, POINT position, MenuKind kind,
	int x, int y, int scalePercent, bool locked,
	const std::vector<MenuItem>* rootItems, bool frosted, int calendarTheme, int timeStyle, bool compact)
{
	if (!RegisterWindowClass()) return 0;
	// Own the graphics lifetime as well as the modal window. Some callers
	// render their widget with DirectWrite and have no GDI+ session of their own.
	struct GraphicsSession {
		ULONG_PTR token = 0;
		GraphicsSession() {
			Gdiplus::GdiplusStartupInput input;
			if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) token = 0;
		}
		~GraphicsSession() { if (token) Gdiplus::GdiplusShutdown(token); }
	} graphicsSession;
	if (!graphicsSession.token) return 0;
	MenuState state;
	state.owner = owner;
	wchar_t ownerClass[32]{};
	GetClassNameW(owner, ownerClass, 32);
	state.keepOwnerFocus = lstrcmpiW(ownerClass, L"EDIT") == 0;
	state.kind = kind;
	state.x = x;
	state.y = y;
	state.scalePercent = scalePercent;
	state.locked = locked;
	state.frosted = frosted;
	state.calendarTheme = calendarTheme;
	state.timeStyle = timeStyle;
	state.compact = compact && kind == MenuKind::Root;
	state.rowHeight = state.compact ? 30 : kind == MenuKind::Root ? kRootRowHeight : kSubmenuRowHeight;
	state.width = state.compact ? 176 : kind == MenuKind::Root ? kRootWidth :
		IsActionSubmenu(kind) ? 190 : kSubmenuWidth;
	if (kind == MenuKind::Root && rootItems) state.items = *rootItems;
	else if (kind == MenuKind::Root) AddRootItems(&state);
	else if (kind == MenuKind::Size) AddSizeItems(&state);
    else if(kind==MenuKind::OrganizerSize){
        for(int percent=50;percent<=120;percent+=10)
            state.items.push_back({std::to_wstring(percent)+L"%",static_cast<UINT>(100+percent),
                MenuKind::Root,MenuIcon::None,false,percent==scalePercent});
    }
	else if (kind == MenuKind::Position) AddPositionItems(&state);
	else if (kind == MenuKind::CalendarTheme) AddCalendarThemeItems(&state);
	else if (kind == MenuKind::TimeStyle) AddTimeStyleItems(&state);
	else if (kind == MenuKind::SpectrumStyle) AddSpectrumStyleItems(&state);
	else AddMaterialItems(&state);

	HDC dpiContext = GetDC(owner);
	const UINT windowDpi = GetMenuWindowDpi(owner);
	const UINT dpi = windowDpi ? windowDpi :
		(dpiContext ? GetDeviceCaps(dpiContext, LOGPIXELSX) : 96);
	if (dpiContext) ReleaseDC(owner, dpiContext);
	state.scale = dpi ? dpi / 96.0 : 1.0;
	MONITORINFO monitorInfo{};
	monitorInfo.cbSize = sizeof(monitorInfo);
	const HMONITOR monitor = MonitorFromPoint(position, MONITOR_DEFAULTTONEAREST);
	GetMonitorInfo(monitor, &monitorInfo);
	const int maximumRows = max(5,
		(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - Scale(&state, Padding(&state) * 2)) /
		Scale(&state, state.rowHeight));
	state.visibleRows = min(static_cast<int>(state.items.size()), maximumRows);
	if (kind == MenuKind::Size)
	{
		const int selected = min(25, max(0, (scalePercent - 50) / 10));
		state.scrollOffset = min(max(0, selected - state.visibleRows / 2),
			static_cast<int>(state.items.size()) - state.visibleRows);
	}
	state.width = Scale(&state, state.width);
	state.height = ContentHeight(&state);

	if (IsActionSubmenu(kind) && position.x + state.width > monitorInfo.rcWork.right)
	{
		RECT parentRect;
		GetWindowRect(owner, &parentRect);
		position.x = parentRect.left - state.width + Scale(&state, kSubmenuOverlap);
	}
	position.x = min(position.x, monitorInfo.rcWork.right - state.width);
	position.y = min(position.y, monitorInfo.rcWork.bottom - state.height);
	position.x = max(position.x, monitorInfo.rcWork.left);
	position.y = max(position.y, monitorInfo.rcWork.top);

	state.window = CreateWindowEx(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST | (state.keepOwnerFocus ? WS_EX_NOACTIVATE : 0),
		kWindowClass,
		L"",
		WS_POPUP,
		position.x,
		position.y,
		state.width,
		state.height,
		owner,
		nullptr,
		GetModuleHandle(nullptr),
		&state);
	if (!state.window) return 0;

	const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
	DwmSetWindowAttribute(state.window, DWMWA_WINDOW_CORNER_PREFERENCE,
		&corner, sizeof(corner));
	const int backdrop = 3;
	DwmSetWindowAttribute(state.window, static_cast<DWMWINDOWATTRIBUTE>(38),
		&backdrop, sizeof(backdrop));
	SetWindowRgn(state.window, CreateRoundRectRgn(0, 0,
		state.width + 1, state.height + 1, Scale(&state, 10), Scale(&state, 10)), TRUE);
	ShowWindow(state.window, state.keepOwnerFocus ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);
	if (!state.keepOwnerFocus) SetForegroundWindow(state.window);
	SetCapture(state.window);

	MSG message{};
    bool quit=false;
    // A popup created with a child EDIT is owned by its top-level ancestor.
    // Destroying that EDIT therefore does not necessarily destroy the popup.
    // Stop after the dispatched callback if the original owner has gone away.
    while (!state.done && IsWindow(state.owner) && IsWindow(state.window))
	{
        const int got=GetMessage(&message,nullptr,0,0);
        if(got<=0){quit=got==0;break;}
		if (state.keepOwnerFocus && message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST)
			message.hwnd = state.window;
		TranslateMessage(&message);
		DispatchMessage(&message);
	}
	if (GetCapture() == state.window)
		ReleaseCapture();
	DestroyWindow(state.window);
    if(quit)PostQuitMessage(static_cast<int>(message.wParam));
	return state.result;
}
}

UINT LumeWidgetMenu::Show(HWND owner, POINT position, int x, int y,
	int scalePercent, bool locked, int timeStyle)
{
	return ShowPopup(owner, position, MenuKind::Root,
		x, y, scalePercent, locked, nullptr, false, false, timeStyle);
}

UINT LumeWidgetMenu::ShowActions(HWND owner, POINT position, bool canRefresh,
	bool canCreate, bool compact, bool list, bool frosted, bool canMaterial,
	bool locked, bool canCalendarTheme, int calendarTheme, bool solid)
{
	std::vector<MenuItem> items;
	if (canCalendarTheme)
		items.push_back({ L"\u65e5\u5386\u4e3b\u9898", 0, MenuKind::CalendarTheme, MenuIcon::Color });
	if (canMaterial)
		items.push_back({ L"\u6750\u8d28\u8bbe\u7f6e", 0, MenuKind::Material, MenuIcon::Material });
	if (canCalendarTheme)
	{
		// 21 background, 23 text, 22 accent.
		items.push_back({ L"\u7ec4\u4ef6\u989c\u8272", 21, MenuKind::Root, MenuIcon::Color });
		items.push_back({ L"\u6587\u5b57\u989c\u8272", 23, MenuKind::Root, MenuIcon::Color });
		items.push_back({ L"\u5f3a\u8c03\u989c\u8272", 22, MenuKind::Root, MenuIcon::Color });
	}
	if (canRefresh)
		items.push_back({ L"\u5237\u65b0\u5929\u6c14", 2, MenuKind::Root, MenuIcon::Refresh });
	if (canCreate)
	{
		items.push_back({ compact ? L"\u5c55\u5f00\u6a21\u5f0f" : L"\u6536\u7f29\u6a21\u5f0f",
			compact ? 5u : 4u, MenuKind::Root, MenuIcon::Size });
		items.push_back({ list ? L"\u56fe\u6807\u6a21\u5f0f" : L"\u5217\u8868\u6a21\u5f0f",
			list ? 7u : 6u, MenuKind::Root, MenuIcon::Size });
		items.push_back({ L"\u65b0\u5efa\u6536\u7eb3", 3, MenuKind::Root, MenuIcon::Create });
	}
	if (!items.empty())
		items.push_back({ L"", 0, MenuKind::Root, MenuIcon::None, true });
	items.push_back({ L"\u6253\u5f00\u8bbe\u7f6e", 11, MenuKind::Root, MenuIcon::Settings });
	items.push_back({ L"\u9501\u5b9a\u4f4d\u7f6e", 12, MenuKind::Root, MenuIcon::Lock, false, locked });
	items.push_back({ L"\u6062\u590d\u9ed8\u8ba4", 10, MenuKind::Root, MenuIcon::Reset });
	items.push_back({ L"\u5173\u95ed\u7ec4\u4ef6", 1, MenuKind::Root, MenuIcon::Close });
	return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false,
		&items, frosted, calendarTheme,
		canCalendarTheme ? (solid ? kCalendarMaterialSolid : kCalendarMaterialGlass) : 0);
}
UINT LumeWidgetMenu::ShowSpectrum(HWND owner, POINT position, int style, int scalePercent, bool locked)
{
	std::vector<MenuItem> items{
		{ L"音频样式", 0, MenuKind::SpectrumStyle, MenuIcon::Color },
		{ L"组件大小", 0, MenuKind::Size, MenuIcon::Size },
		{ L"", 0, MenuKind::Root, MenuIcon::None, true },
		{ L"打开设置", 11, MenuKind::Root, MenuIcon::Settings },
		{ L"锁定位置", 12, MenuKind::Root, MenuIcon::Lock, false, locked },
		{ L"恢复默认", 10, MenuKind::Root, MenuIcon::Reset },
		{ L"关闭组件", 1, MenuKind::Root, MenuIcon::Close }
	};
	// 样式二 is drawn in one colour, chosen as the clock's 组件颜色 is.
	if (style == 1)
		items.insert(items.begin() + 3, { L"组件颜色", IDM_LUME_WIDGET_COLOR, MenuKind::Root, MenuIcon::Color });
	return ShowPopup(owner, position, MenuKind::Root, 0, 0, scalePercent, locked, &items, false, 0, style);
}
UINT LumeWidgetMenu::ShowRadial(HWND owner, POINT position, bool frosted)
{
    std::vector<MenuItem> items {
        { L"\u6750\u8d28\u8bbe\u7f6e", 0, MenuKind::Material, MenuIcon::Material },
        { L"\u81ea\u5b9a\u4e49\u5feb\u6377\u952e", 13, MenuKind::Root, MenuIcon::Settings },
        { L"", 0, MenuKind::Root, MenuIcon::None, true },
        { L"\u6253\u5f00\u8bbe\u7f6e", 11, MenuKind::Root, MenuIcon::Settings },
        { L"\u6062\u590d\u9ed8\u8ba4", 10, MenuKind::Root, MenuIcon::Reset },
        { L"\u5173\u95ed\u8f6e\u76d8", 1, MenuKind::Root, MenuIcon::Close }
    };
    return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, frosted, false);
}
UINT LumeWidgetMenu::ShowOrganizer(HWND owner,POINT position,bool list,bool frosted,bool locked,int iconScale,bool item)
{
    std::vector<MenuItem> items{
        {L"图标大小",0,MenuKind::OrganizerSize,MenuIcon::Size},
        {list?L"图标模式":L"列表模式",list?7u:6u,MenuKind::Root,MenuIcon::Size},
        {L"新建收纳",3,MenuKind::Root,MenuIcon::Create},
        {L"材质设置",0,MenuKind::Material,MenuIcon::Material},
        {L"",0,MenuKind::Root,MenuIcon::None,true},
        {L"打开设置",11,MenuKind::Root,MenuIcon::Settings},
        {L"锁定位置",12,MenuKind::Root,MenuIcon::Lock,false,locked},
        {L"恢复默认",10,MenuKind::Root,MenuIcon::Reset},
        {L"关闭组件",1,MenuKind::Root,MenuIcon::Close}
    };
    // Removes the reference only; the file itself stays where it is.
    if(item)items.insert(items.begin(),{{L"删除图标",13,MenuKind::Root,MenuIcon::Close},{L"",0,MenuKind::Root,MenuIcon::None,true}});
    return ShowPopup(owner,position,MenuKind::Root,0,0,iconScale,false,&items,frosted,false);
}

UINT LumeWidgetMenu::ShowItems(HWND owner, POINT position, const std::vector<Action>& actions, bool compact)
{
    if (actions.empty()) return 0;
    std::vector<MenuItem> items;
    for (const auto& action : actions)
        items.push_back({action.label, action.command, MenuKind::Root, MenuIcon::None,
            action.label.empty() && !action.command, false, action.enabled});
    return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, false, false, 0, compact);
}

UINT LumeWidgetMenu::ShowNotes(HWND owner, POINT position, bool frosted, bool locked)
{
    std::vector<MenuItem> items {
        { L"新建便签", 3, MenuKind::Root, MenuIcon::Create },
        { L"材质设置", 0, MenuKind::Material, MenuIcon::Material },
        { L"", 0, MenuKind::Root, MenuIcon::None, true },
        { L"打开设置", 11, MenuKind::Root, MenuIcon::Settings },
        { L"锁定位置", 12, MenuKind::Root, MenuIcon::Lock, false, locked },
        { L"恢复默认", 10, MenuKind::Root, MenuIcon::Reset },
        { L"关闭便签", 1, MenuKind::Root, MenuIcon::Close }
    };
    return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, frosted, false);
}

UINT LumeWidgetMenu::ShowDock(HWND owner, POINT position, bool frosted, bool none)
{
    std::vector<MenuItem> items {
        { L"新建收纳", 40, MenuKind::Root, MenuIcon::Create },
        { L"添加系统图标", 46, MenuKind::Root, MenuIcon::Create },
        { L"材质设置", 0, MenuKind::Material, MenuIcon::Material },
        { L"", 0, MenuKind::Root, MenuIcon::None, true },
        { L"打开设置", 11, MenuKind::Root, MenuIcon::Settings },
        { L"恢复默认", 10, MenuKind::Root, MenuIcon::Reset },
        { L"关闭 Dock", 1, MenuKind::Root, MenuIcon::Close }
    };
    return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, frosted, false,
        none ? kDockMaterialNone : kDockMaterialGlass);
}

UINT LumeWidgetMenu::ShowIsland(HWND owner, POINT position, bool frosted, bool none, bool expanded, bool messages)
{
    std::vector<MenuItem> items {
        { expanded ? L"收起" : L"展开", 2, MenuKind::Root, MenuIcon::Center },
        { L"消息提醒", 12, MenuKind::Root, MenuIcon::None, false, messages },
        { L"材质设置", 0, MenuKind::Material, MenuIcon::Material },
        { L"", 0, MenuKind::Root, MenuIcon::None, true },
        { L"打开设置", 11, MenuKind::Root, MenuIcon::Settings },
        { L"恢复默认", 10, MenuKind::Root, MenuIcon::Reset },
        { L"关闭灵动岛", 1, MenuKind::Root, MenuIcon::Close }
    };
    return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, frosted, false,
        none ? kDockMaterialNone : kDockMaterialGlass);
}

UINT LumeWidgetMenu::ShowIslandTodo(HWND owner, POINT position, bool item, bool writable, bool frosted, bool none)
{
    std::vector<MenuItem> items;
    if (item)
    {
        items.push_back({ L"编辑待办", 50, MenuKind::Root, MenuIcon::Settings, false, false, writable });
        items.push_back({ L"删除待办", 51, MenuKind::Root, MenuIcon::Close, false, false, writable });
        items.push_back({ L"", 0, MenuKind::Root, MenuIcon::None, true });
    }
    items.push_back({ L"新建待办", 52, MenuKind::Root, MenuIcon::Create, false, false, writable });
    items.push_back({ L"材质设置", 0, MenuKind::Material, MenuIcon::Material });
    items.push_back({ L"", 0, MenuKind::Root, MenuIcon::None, true });
    items.push_back({ L"收起", 2, MenuKind::Root, MenuIcon::Center });
    items.push_back({ L"打开设置", 11, MenuKind::Root, MenuIcon::Settings });
    items.push_back({ L"关闭灵动岛", 1, MenuKind::Root, MenuIcon::Close });
    return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, frosted, false,
        none ? kDockMaterialNone : kDockMaterialGlass);
}

UINT LumeWidgetMenu::ShowDockItem(HWND owner, POINT position, bool pinned, bool removable, bool canReveal,
                                  bool inCollection, bool dockActions, bool frosted, bool none)
{
    std::vector<MenuItem> items {
        { pinned ? L"从 Dock 删除" : L"固定到 Dock", pinned ? 21u : 20u, MenuKind::Root, pinned ? MenuIcon::Close : MenuIcon::Pin },
    };
    if (canReveal)
        items.push_back({ L"打开所在位置", 22, MenuKind::Root, MenuIcon::Position });
    if (inCollection)
    {
        items.push_back({ L"", 0, MenuKind::Root, MenuIcon::None, true });
        items.push_back({ L"移出收纳", 42, MenuKind::Root, MenuIcon::Close });
    }
    else if (removable)
    {
        items.push_back({ L"", 0, MenuKind::Root, MenuIcon::None, true });
        items.push_back({ L"从启动台移除", 23, MenuKind::Root, MenuIcon::Close });
    }
    if (dockActions)
    {
        // The Dock's own actions follow, so the small bar need not be hit exactly.
        items.push_back({ L"", 0, MenuKind::Root, MenuIcon::None, true });
        items.push_back({ L"新建收纳", 40, MenuKind::Root, MenuIcon::Create });
        items.push_back({ L"添加系统图标", 46, MenuKind::Root, MenuIcon::Create });
        items.push_back({ L"材质设置", 0, MenuKind::Material, MenuIcon::Material });
        items.push_back({ L"", 0, MenuKind::Root, MenuIcon::None, true });
        items.push_back({ L"打开设置", 11, MenuKind::Root, MenuIcon::Settings });
        items.push_back({ L"恢复默认", 10, MenuKind::Root, MenuIcon::Reset });
        items.push_back({ L"关闭 Dock", 1, MenuKind::Root, MenuIcon::Close });
        return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, frosted, false,
            none ? kDockMaterialNone : kDockMaterialGlass);
    }
    return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, false, false);
}

UINT LumeWidgetMenu::ShowLaunchpadAdd(HWND owner, POINT position)
{
    std::vector<MenuItem> items {
        { L"添加文件…", 30, MenuKind::Root, MenuIcon::Create },
        { L"新建收纳", 40, MenuKind::Root, MenuIcon::Create }
    };
    return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, false, false);
}

UINT LumeWidgetMenu::ShowLaunchpadCollection(HWND owner, POINT position, bool dockActions, bool pinned)
{
    std::vector<MenuItem> items {
        { L"重命名", 40, MenuKind::Root, MenuIcon::Create },
        { L"", 0, MenuKind::Root, MenuIcon::None, true },
        { L"删除收纳", 41, MenuKind::Root, MenuIcon::Close }
    };
    if (dockActions) items.insert(items.begin(), {pinned ? L"从 Dock 移除" : L"添加到 Dock", pinned ? 45u : 44u,
        MenuKind::Root, MenuIcon::Pin});
    return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, false, false);
}

UINT LumeWidgetMenu::ShowLaunchpadAddKind(HWND owner, POINT position)
{
    std::vector<MenuItem> items {
        { L"文件…", 30, MenuKind::Root, MenuIcon::Create },
        { L"文件夹…", 31, MenuKind::Root, MenuIcon::Create }
    };
    return ShowPopup(owner, position, MenuKind::Root, 0, 0, 100, false, &items, false, false);
}

UINT LumeWidgetMenu::ShowScreenshotPin(HWND owner, POINT position)
{
    return ShowItems(owner, position, {{L"复制", 1}, {L"保存", 2}, {L"100% 缩放", 3}, {L"关闭", 4}});
}

UINT LumeWidgetMenu::ShowTodayTodo(HWND owner,POINT position,bool item,bool writable,bool,bool frosted,bool locked,bool solid){
    std::vector<MenuItem> items;
    if(item){
        items.push_back({L"编辑待办",50,MenuKind::Root,MenuIcon::Settings,false,false,writable});
        items.push_back({L"删除待办",51,MenuKind::Root,MenuIcon::Close,false,false,writable});
        items.push_back({L"",0,MenuKind::Root,MenuIcon::None,true});
    }
    items.push_back({L"新建待办",52,MenuKind::Root,MenuIcon::Create,false,false,writable});
    items.push_back({L"材质设置",0,MenuKind::Material,MenuIcon::Material});
    items.push_back({L"组件颜色",21,MenuKind::Root,MenuIcon::Color});
    items.push_back({L"",0,MenuKind::Root,MenuIcon::None,true});
    items.push_back({L"打开设置",11,MenuKind::Root,MenuIcon::Settings});
    items.push_back({L"锁定位置",12,MenuKind::Root,MenuIcon::Lock,false,locked});
    items.push_back({L"恢复默认",10,MenuKind::Root,MenuIcon::Reset});
    items.push_back({L"关闭组件",1,MenuKind::Root,MenuIcon::Close});
    return ShowPopup(owner,position,MenuKind::Root,0,0,100,false,&items,frosted,0,
        solid?kCalendarMaterialSolid:kCalendarMaterialGlass);
}
