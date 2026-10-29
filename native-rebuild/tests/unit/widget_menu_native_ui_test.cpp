#include "platform/windows/widget_ui/LumeWidgetMenu.h"

#include <windows.h>

#include <iostream>

namespace
{
VOID CALLBACK CloseMenuTimer(HWND, UINT, UINT_PTR, DWORD)
{
	EnumWindows([](HWND window, LPARAM) -> BOOL
	{
		wchar_t className[64]{};
		GetClassNameW(window, className, _countof(className));
		if (lstrcmpW(className, L"LumeWidgetMenuWindow") == 0)
		{
			PostMessageW(window, WM_KEYDOWN, VK_ESCAPE, 1);
			return FALSE;
		}
		return TRUE;
	}, 0);
}

bool CloseWithEscape(HWND owner)
{
	const UINT_PTR timer = SetTimer(owner, 1, 1, CloseMenuTimer);
	const UINT result = LumeWidgetMenu::Show(
		owner, POINT{ 32000, 32000 }, -32768, 32767, 100, false, 0);
	KillTimer(owner, timer);
	return result == 0;
}
}

int main()
{
	const HWND owner = CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		L"STATIC",
		L"menu-ui-test",
		WS_POPUP,
		0, 0, 1, 1,
		nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!owner)
	{
		std::cerr << "FAIL owner creation\n";
		return 1;
	}

	bool passed = true;
	for (int iteration = 0; iteration < 5; ++iteration)
		passed = CloseWithEscape(owner) && passed;
	DestroyWindow(owner);

	if (!passed)
	{
		std::cerr << "FAIL repeated custom popup open/close\n";
		return 1;
	}
	std::cout << "PASS 5 historical custom popup open/close cycles\n";
	return 0;
}
