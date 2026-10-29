#include "StdAfx.h"
#include "platform/windows/widget_ui/ui_font.h"
#include "platform/windows/widget_ui/direct_write_edit.h"
#include "LumeWidgetPositionEditor.h"

#include <commctrl.h>

namespace
{
constexpr WCHAR kWindowClass[] = L"LumeWidgetPositionEditorWindow";
constexpr int kEditX = 5201;
constexpr int kEditY = 5202;

struct EditorState
{
	HWND window = nullptr;
	HWND owner = nullptr;
	HWND editX = nullptr;
	HWND editY = nullptr;
	HFONT font = nullptr;
	HFONT numericFont = nullptr;
	int x = 0;
	int y = 0;
	bool accepted = false;
	bool done = false;
	double scale = 1.0;
};

void SetInteger(HWND edit, int value)
{
	WCHAR text[24];
	std::swprintf(text, _countof(text), L"%i", value);
	SetWindowText(edit, text);
}

bool ReadInteger(HWND edit, int& value)
{
	WCHAR text[32] = {};
	GetWindowText(edit, text, _countof(text));
	WCHAR* end = nullptr;
	errno = 0;
	const long parsed = wcstol(text, &end, 10);
	while (end && iswspace(*end)) ++end;
	if (errno == ERANGE || end == text || (end && *end) ||
		parsed < -32768 || parsed > 32767)
	{
		return false;
	}
	value = (int)parsed;
	return true;
}

void Apply(EditorState* state)
{
	int x = 0;
	int y = 0;
	if (!ReadInteger(state->editX, x))
	{
		MessageBeep(MB_ICONWARNING);
		SetFocus(state->editX);
		SendMessage(state->editX, EM_SETSEL, 0, -1);
		return;
	}
	if (!ReadInteger(state->editY, y))
	{
		MessageBeep(MB_ICONWARNING);
		SetFocus(state->editY);
		SendMessage(state->editY, EM_SETSEL, 0, -1);
		return;
	}
	state->x = x;
	state->y = y;
	state->accepted = true;
	state->done = true;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	EditorState* state = reinterpret_cast<EditorState*>(
		GetWindowLongPtr(window, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		const CREATESTRUCT* create = reinterpret_cast<CREATESTRUCT*>(lParam);
		state = reinterpret_cast<EditorState*>(create->lpCreateParams);
		state->window = window;
		SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)state);
	}
	if (!state) return DefWindowProc(window, message, wParam, lParam);

	switch (message)
	{
	case WM_CREATE:
	{
		const auto dip = [state](int value) { return static_cast<int>(std::lround(value * state->scale)); };
		state->font = CreateFont(-dip(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY, DEFAULT_PITCH, lume::platform::windows::ui_font::Family(L"位置"));
		state->numericFont = CreateFont(-dip(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY, DEFAULT_PITCH, lume::platform::windows::ui_font::Family());
		CreateWindow(L"STATIC", L"X", WS_CHILD | WS_VISIBLE,
			dip(24), dip(48), dip(24), dip(24), window, nullptr, nullptr, nullptr);
		CreateWindow(L"STATIC", L"Y", WS_CHILD | WS_VISIBLE,
			dip(24), dip(88), dip(24), dip(24), window, nullptr, nullptr, nullptr);
		state->editX = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			dip(55), dip(44), dip(225), dip(30), window, (HMENU)(INT_PTR)kEditX, nullptr, nullptr);
		state->editY = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			dip(55), dip(84), dip(225), dip(30), window, (HMENU)(INT_PTR)kEditY, nullptr, nullptr);
		CreateWindow(L"BUTTON", L"\u53d6\u6d88",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			dip(132), dip(136), dip(72), dip(32), window, (HMENU)IDCANCEL, nullptr, nullptr);
		CreateWindow(L"BUTTON", L"\u786e\u5b9a",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			dip(212), dip(136), dip(72), dip(32), window, (HMENU)IDOK, nullptr, nullptr);
		for (HWND child = GetWindow(window, GW_CHILD); child;
			child = GetWindow(child, GW_HWNDNEXT))
		{
			const int id = GetDlgCtrlID(child);
			const auto font = id == IDCANCEL || id == IDOK || !state->numericFont ? state->font : state->numericFont;
			SendMessage(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
			SetWindowTheme(child, L"Explorer", nullptr);
			if (!lume::platform::windows::direct_write_edit::Attach(child)) return -1;
		}
		SetInteger(state->editX, state->x);
		SetInteger(state->editY, state->y);
		SetFocus(state->editX);
		SendMessage(state->editX, EM_SETSEL, 0, -1);
		return 0;
	}
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
		{
			Apply(state);
			return 0;
		}
		if (LOWORD(wParam) == IDCANCEL)
		{
			state->done = true;
			return 0;
		}
		break;
	case WM_CLOSE:
		state->done = true;
		return 0;
	case WM_DESTROY:
		state->done = true;
		if (state->font)
		{
			DeleteObject(state->font);
			state->font = nullptr;
		}
		if (state->numericFont) { DeleteObject(state->numericFont); state->numericFont = nullptr; }
		return 0;
	}
	return DefWindowProc(window, message, wParam, lParam);
}

bool RegisterWindowClass()
{
	static bool registered = false;
	if (registered) return true;
	WNDCLASSEX windowClass{}; windowClass.cbSize = sizeof(windowClass);
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = GetModuleHandle(nullptr);
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.hbrBackground = CreateSolidBrush(RGB(246, 246, 246));
	windowClass.lpszClassName = kWindowClass;
	registered = RegisterClassEx(&windowClass) != 0 ||
		GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	return registered;
}
}

bool LumeWidgetPositionEditor::Show(HWND owner, int& x, int& y)
{
	if (!RegisterWindowClass()) return false;
	EditorState state;
	state.owner = owner;
	state.x = x;
	state.y = y;

	state.scale = GetDpiForWindow(owner) / 96.0;
	if (state.scale <= 0.0) state.scale = 1.0;
	const int width = static_cast<int>(std::lround(320 * state.scale));
	const int height = static_cast<int>(std::lround(210 * state.scale));
	RECT ownerRect;
	GetWindowRect(owner, &ownerRect);
	int left = ownerRect.left + max(0, (ownerRect.right - ownerRect.left - width) / 2);
	int top = ownerRect.top + max(0, (ownerRect.bottom - ownerRect.top - height) / 2);
	MONITORINFO monitor{}; monitor.cbSize=sizeof(monitor);
	if (GetMonitorInfo(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor)) {
		left=max(monitor.rcWork.left,min(left,monitor.rcWork.right-width));
		top=max(monitor.rcWork.top,min(top,monitor.rcWork.bottom-height));
	}
	state.window = CreateWindowEx(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		kWindowClass,
		L"\u4f4d\u7f6e",
		WS_POPUP | WS_CAPTION | WS_SYSMENU,
		left, top, width, height,
		owner, nullptr, GetModuleHandle(nullptr), &state);
	if (!state.window) return false;

	const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
	DwmSetWindowAttribute(state.window, DWMWA_WINDOW_CORNER_PREFERENCE,
		&corner, sizeof(corner));
	EnableWindow(owner, FALSE);
	ShowWindow(state.window, SW_SHOWNORMAL);
	SetForegroundWindow(state.window);

	MSG message{};
	while (!state.done)
	{
		const BOOL result = GetMessage(&message, nullptr, 0, 0);
		if (result <= 0) {
			if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
			break;
		}
		if (!IsDialogMessage(state.window, &message))
		{
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
	}
	DestroyWindow(state.window);
	if (IsWindow(owner)) EnableWindow(owner, TRUE);
	if (!state.accepted) return false;
	x = state.x;
	y = state.y;
	return true;
}
