// Manual probe: opens the native control center with the real system backend
// and a Dock with its icon, then saves screenshots. Read-only: no setting changes.
#include "platform/windows/control_center/control_center_window.h"
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/control_center/system_controls.h"
#include "platform/windows/control_center/network_controls.h"
#include <winrt/base.h>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <objbase.h>
using namespace lume;
using namespace lume::platform::windows;
void Pump(int ms){const auto end=GetTickCount64()+ms;MSG m{};while(GetTickCount64()<end){while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}}
void Shot(RECT r,const wchar_t* path){
    if(getenv("LUME_CC_PROBE_NO_SHOTS"))return;
    const int w=r.right-r.left,ht=r.bottom-r.top;
    const auto screen=GetDC(nullptr);const auto dc=CreateCompatibleDC(screen);const auto bmp=CreateCompatibleBitmap(screen,w,ht);SelectObject(dc,bmp);
    BitBlt(dc,0,0,w,ht,screen,r.left,r.top,SRCCOPY|CAPTUREBLT);
    BITMAPINFOHEADER bi{sizeof(bi),w,-ht,1,32,BI_RGB};std::vector<char> px(static_cast<size_t>(w)*ht*4);
    GetDIBits(dc,bmp,0,ht,px.data(),reinterpret_cast<BITMAPINFO*>(&bi),DIB_RGB_COLORS);
    BITMAPFILEHEADER fh{0x4d42,static_cast<DWORD>(sizeof(fh)+sizeof(bi)+px.size()),0,0,sizeof(fh)+sizeof(bi)};
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<char*>(&fh),sizeof fh);f.write(reinterpret_cast<char*>(&bi),sizeof bi);f.write(px.data(),px.size());
    DeleteObject(bmp);DeleteDC(dc);ReleaseDC(nullptr,screen);
}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="net"){winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::cout<<"accent="<<std::hex<<control_center::AccentColor()<<std::dec<<std::endl;control_center::ScanNetworks();Sleep(3500);
        for(const auto& n:control_center::Networks())std::wcout<<L"net signal="<<n.signal<<L" secured="<<n.secured<<L" connected="<<n.connected<<L" known="<<n.known<<std::endl;
        for(const auto& d:control_center::PairedDevices())std::wcout<<L"device connected="<<d.connected<<std::endl;return 0;}
    if(argc>1&&std::string(argv[1])=="tray"){POINT anchor{1400,1200};const bool ok=control_center::OpenTrayOverflow(anchor);Pump(600);
        HWND o=FindWindowW(L"TopLevelWindowForOverflowXamlIsland",nullptr);RECT r{};GetWindowRect(o,&r);
        std::cout<<"tray-open="<<ok<<" visible="<<IsWindowVisible(o)<<" rect="<<r.left<<","<<r.top<<","<<r.right<<","<<r.bottom<<" anchor=1400,1200"<<std::endl;
        Shot({r.left-20,r.top-20,r.right+20,r.bottom+20},L"cc-tray.bmp");keybd_event(VK_ESCAPE,0,0,0);keybd_event(VK_ESCAPE,0,KEYEVENTF_KEYUP,0);return 0;}
    const bool frosted=argc>1&&std::string(argv[1])=="frosted",none=argc>1&&std::string(argv[1])=="none";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ControlCenterWindow::SetEnglish(false);
    std::vector<dock::Item> items{{dock::Kind::Launchpad,{},"启动台",false},{dock::Kind::Lume,{},"Lume",true,0},
        {dock::Kind::Separator,{},{},false},{dock::Kind::SystemTray,{},"控制中心",false}};
    DockWindowPorts ports;DockWindow dock(GetModuleHandleW(nullptr),ports);
    std::cout<<"dock="<<dock.Show(items,frosted)<<std::endl;Pump(800);
    const RECT bar=dock.ScreenBounds();const RECT tile=dock.IconBounds(3);
    Shot({bar.left-20,bar.top-90,bar.right+20,bar.bottom+10},frosted?L"cc-dock-frosted.bmp":L"cc-dock.bmp");
    ControlCenterWindow center;
    const auto look=none?ControlCenterWindow::Material::None:frosted?ControlCenterWindow::Material::Frosted:ControlCenterWindow::Material::Liquid;
    center.Prewarm(GetModuleHandleW(nullptr),dock.Handle(),look);Pump(50);
    const auto opening=GetTickCount64();
    std::cout<<"open="<<center.Toggle(GetModuleHandleW(nullptr),dock.Handle(),tile,look)<<std::endl;
    std::cout<<"toggle-ms="<<GetTickCount64()-opening<<std::endl;Pump(getenv("SHOT_MS")?atoi(getenv("SHOT_MS")):250);
    if(getenv("HIDE_PAGE")){HWND c=FindWindowExW(center.Handle(),nullptr,nullptr,nullptr);ShowWindow(c,SW_HIDE);Pump(800);}
    {HWND c=FindWindowExW(center.Handle(),nullptr,nullptr,nullptr);RECT cr{};GetWindowRect(c,&cr);HWND prev=GetWindow(center.Handle(),GW_HWNDPREV);wchar_t cls[64]{};GetClassNameW(prev,cls,64);std::wcout<<L"child="<<(c!=nullptr)<<L" childvisible="<<IsWindowVisible(c)<<L" above="<<cls<<std::endl;}
    std::cout<<"still-open="<<center.IsOpen()<<std::endl;
    if(center.IsOpen()){RECT r{};GetWindowRect(center.Handle(),&r);std::cout<<"panel "<<r.left<<","<<r.top<<" "<<r.right-r.left<<"x"<<r.bottom-r.top<<" tile-top="<<tile.top<<std::endl;
        Shot({r.left-160,r.top-160,r.right+160,tile.bottom+20},none?L"cc-panel-none.bmp":frosted?L"cc-panel-frosted.bmp":L"cc-panel.bmp");}
    center.Close();Pump(100);std::cout<<"hidden="<<!center.IsOpen()<<" window-kept="<<(center.Handle()!=nullptr)<<std::endl;
    center.Toggle(GetModuleHandleW(nullptr),dock.Handle(),tile,look);Pump(250);std::cout<<"reopened="<<center.IsOpen()<<std::endl;
    if(center.IsOpen()){RECT r{};GetWindowRect(center.Handle(),&r);Shot({r.left-30,r.top-30,r.right+30,tile.bottom+20},L"cc-panel-reopen.bmp");}
    center.Close();dock.Stop();
}
