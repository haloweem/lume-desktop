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
    const auto selectOrganizer=[&](bool collapsed,bool list,bool item,unsigned down,UINT expected){
        std::atomic<bool> opened=false,childOpened=false;
        std::thread organizerDriver([&]{
            HWND root=nullptr;
            for(int attempt=0;attempt<5000&&!root;++attempt){root=findOwned(owner);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            if(!root)return;
            opened=true;
            PostMessageW(root,WM_KEYDOWN,VK_HOME,1);
            for(unsigned index=0;index<down;++index)PostMessageW(root,WM_KEYDOWN,VK_DOWN,1);
            // The former first-row size submenu must now be a direct action.
            PostMessageW(root,WM_KEYDOWN,VK_RIGHT,1);
            PostMessageW(root,WM_KEYDOWN,VK_RETURN,1);
            const auto deadline=GetTickCount64()+2000;
            while(IsWindow(root)&&GetTickCount64()<deadline){
                if(const HWND child=findOwned(root)){
                    childOpened=true;PostMessageW(child,WM_KEYDOWN,VK_ESCAPE,1);PostMessageW(root,WM_KEYDOWN,VK_ESCAPE,1);break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(IsWindow(root))PostMessageW(root,WM_KEYDOWN,VK_ESCAPE,1);
        });
        const UINT selected=LumeWidgetMenu::ShowOrganizer(owner,{200,200},collapsed,list,true,false,item);
        organizerDriver.join();
        if(!opened||childOpened||selected!=expected){
            std::cerr<<"FAIL organizer direct menu action: expected "<<expected<<", got "<<selected<<'\n';return false;
        }
        return true;
    };
    bool organizerMenus=true;
    for(const bool collapsed:{false,true}){
        organizerMenus=selectOrganizer(collapsed,false,false,0,collapsed?5u:4u)&&organizerMenus;
        organizerMenus=selectOrganizer(collapsed,false,true,0,13)&&organizerMenus;
        organizerMenus=selectOrganizer(collapsed,false,true,1,collapsed?5u:4u)&&organizerMenus;
    }
    organizerMenus=selectOrganizer(false,false,false,1,6)&&organizerMenus;
    organizerMenus=selectOrganizer(true,true,false,1,7)&&organizerMenus;
    for(int accent=0;accent<6;++accent){
        std::atomic<bool> opened=false;
        std::thread colorDriver([&]{
            HWND root=nullptr,child=nullptr;
            for(int attempt=0;attempt<5000&&!root;++attempt){root=findOwned(owner);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            if(!root)return;
            PostMessageW(root,WM_KEYDOWN,VK_HOME,1);
            for(int row=0;row<4;++row)PostMessageW(root,WM_KEYDOWN,VK_DOWN,1);
            PostMessageW(root,WM_KEYDOWN,VK_RIGHT,1);
            for(int attempt=0;attempt<5000&&!child;++attempt){child=findOwned(root);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            if(!child){PostMessageW(root,WM_KEYDOWN,VK_ESCAPE,1);return;}
            opened=true;
            PostMessageW(child,WM_KEYDOWN,VK_HOME,1);
            for(int row=0;row<accent;++row)PostMessageW(child,WM_KEYDOWN,VK_DOWN,1);
            PostMessageW(child,WM_KEYDOWN,VK_RETURN,1);
        });
        const auto selected=LumeWidgetMenu::ShowOrganizer(owner,{32000,32000},false,false,true,false,false,accent);
        colorDriver.join();
        if(!opened||selected!=static_cast<UINT>(230+accent)){
            std::cerr<<"FAIL organizer color submenu: expected "<<230+accent<<", got "<<selected<<'\n';organizerMenus=false;
        }
    }
    DestroyWindow(owner);
    if(!organizerMenus)return 1;
	std::cout << "PASS popup opens and closes; organizer direct actions and all six color submenu actions\n";
	return 0;
}
