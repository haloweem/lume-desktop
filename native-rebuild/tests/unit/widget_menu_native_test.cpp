#include "platform/windows/widget_ui/LumeWidgetMenu.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
struct Probe
{
	std::atomic<bool> found = false;
	std::atomic<bool> valid = true;
};

BOOL CALLBACK FindMenu(HWND window, LPARAM data)
{
	auto* probe = reinterpret_cast<Probe*>(data);
	wchar_t className[64]{};
	GetClassNameW(window, className, _countof(className));
	if (std::wcscmp(className, L"LumeWidgetMenuWindow") != 0) return TRUE;

	RECT bounds{};
	if (!GetWindowRect(window, &bounds) ||
		bounds.right <= bounds.left || bounds.bottom <= bounds.top ||
		bounds.right - bounds.left < 170 || bounds.bottom - bounds.top < 40)
		probe->valid = false;
	else
		probe->found = true;

	PostMessageW(window, WM_KEYDOWN, VK_ESCAPE, 1);
	return FALSE;
}

void DriveEscape(Probe* probe)
{
	for (int attempt = 0; attempt < 5000 && !probe->found; ++attempt)
	{
		EnumWindows(FindMenu, reinterpret_cast<LPARAM>(probe));
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (!probe->found) probe->valid = false;
}

HWND Owner()
{
	return CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		L"STATIC",
		L"menu-test",
		WS_POPUP,
		0, 0, 1, 1,
		nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}
}

int main()
{
	const HWND owner = Owner();
	if (!owner)
	{
		std::cerr << "FAIL owner creation\n";
		return 1;
	}

	Probe probe;
	std::thread driver(DriveEscape, &probe);
	const UINT result = LumeWidgetMenu::ShowActions(
		owner, POINT{ 32000, 32000 }, true, true, false, false,
		false, true, false, true, false);
	driver.join();

	if (result != 0 || !probe.found || !probe.valid)
	{
		std::cerr << "FAIL custom popup menu smoke\n";
		return 1;
	}
    struct ChildProbe {HWND owner=nullptr,menu=nullptr;};
    const auto findOwned=[](HWND parent){
        ChildProbe menuProbe;menuProbe.owner=parent;
        EnumWindows([](HWND hwnd,LPARAM data)->BOOL{
            auto& current=*reinterpret_cast<ChildProbe*>(data);wchar_t name[64]{};GetClassNameW(hwnd,name,64);
            if(std::wcscmp(name,L"LumeWidgetMenuWindow")==0&&GetWindow(hwnd,GW_OWNER)==current.owner){current.menu=hwnd;return FALSE;}
            return TRUE;
        },reinterpret_cast<LPARAM>(&menuProbe));
        return menuProbe.menu;
    };
    std::atomic<bool> sizeOpened=false;
    std::thread sizeDriver([&]{
        HWND root=nullptr;
        for(int attempt=0;attempt<5000&&!root;++attempt){root=findOwned(owner);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(!root)return;
        PostMessageW(root,WM_KEYDOWN,VK_HOME,1);PostMessageW(root,WM_KEYDOWN,VK_RIGHT,1);
        HWND child=nullptr;
        for(int attempt=0;attempt<5000&&!child;++attempt){child=findOwned(root);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(child){sizeOpened=true;PostMessageW(child,WM_KEYDOWN,VK_HOME,1);PostMessageW(child,WM_KEYDOWN,VK_RETURN,1);}
        else PostMessageW(root,WM_KEYDOWN,VK_ESCAPE,1);
    });
    const UINT sizeResult=LumeWidgetMenu::ShowOrganizer(owner,{200,200},false,true,false,100);
    sizeDriver.join();DestroyWindow(owner);
    if(!sizeOpened||sizeResult!=150){std::cerr<<"FAIL organizer icon size submenu selection\n";return 1;}
	std::cout << "PASS historical custom popup menu opens, fits, and closes with Esc\n";
	return 0;
}
