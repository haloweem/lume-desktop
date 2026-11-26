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
    const auto waitForMenu=[&](HWND parent){
        const auto deadline=GetTickCount64()+3000;
        while(GetTickCount64()<deadline){
            if(HWND window=findOwned(parent))return window;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return static_cast<HWND>(nullptr);
    };
    const auto focusRow=[](HWND window,int row){
        PostMessageW(window,WM_KEYDOWN,VK_HOME,1);
        for(int i=0;i<row;++i)PostMessageW(window,WM_KEYDOWN,VK_DOWN,1);
    };
    const auto selectOrganizer=[&](bool item,bool canDelete,int rootRow,int childRow,UINT expected){
        std::thread menuDriver([&]{
            HWND root=waitForMenu(owner);
            if(!root)return;
            focusRow(root,rootRow);
            HWND target=root;
            if(childRow>=0){
                PostMessageW(root,WM_KEYDOWN,VK_RIGHT,1);
                target=waitForMenu(root);
                if(!target){PostMessageW(root,WM_KEYDOWN,VK_ESCAPE,1);return;}
                focusRow(target,childRow);
            }
            PostMessageW(target,WM_KEYDOWN,VK_RETURN,1);
            // If a direct row unexpectedly opens a submenu, end the failed case.
            const auto deadline=GetTickCount64()+2000;
            while(IsWindow(root)&&GetTickCount64()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if(HWND child=findOwned(root))PostMessageW(child,WM_KEYDOWN,VK_ESCAPE,1);
            if(IsWindow(root))PostMessageW(root,WM_KEYDOWN,VK_ESCAPE,1);
        });
        const auto selected=LumeWidgetMenu::ShowOrganizer(owner,{200,200},childRow%2==0,childRow>=2,true,false,item,canDelete);
        menuDriver.join();
        if(selected==expected)return true;
        std::cerr<<"FAIL organizer menu row "<<rootRow<<", child "<<childRow<<": expected "<<expected<<", got "<<selected<<'\n';
        return false;
    };
    bool organizerMenus=true;
    for(int option=0;option<4;++option)
        organizerMenus=selectOrganizer(false,false,0,option,static_cast<UINT>(4+option))&&organizerMenus;
    organizerMenus=selectOrganizer(true,true,2,0,8)&&organizerMenus; // Material immediately precedes deletion.
    organizerMenus=selectOrganizer(true,true,3,0,LumeWidgetMenu::OrganizerRemoveIcon)&&organizerMenus;
    organizerMenus=selectOrganizer(true,true,3,1,LumeWidgetMenu::OrganizerDeleteIcon)&&organizerMenus;
    organizerMenus=selectOrganizer(true,false,3,1,LumeWidgetMenu::OrganizerRemoveIcon)&&organizerMenus; // Disabled delete is skipped.
    organizerMenus=selectOrganizer(false,false,3,-1,11)&&organizerMenus; // Background has no item actions.
    organizerMenus=selectOrganizer(true,true,6,-1,1)&&organizerMenus; // Close follows lock directly.
    DestroyWindow(owner);
    if(!organizerMenus)return 1;
	std::cout << "PASS organizer display choices, deletion submenu, disabled delete and root menu order\n";
	return 0;
}
