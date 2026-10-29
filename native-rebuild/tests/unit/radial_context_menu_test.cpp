#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/radial/radial_window.h"
#include "platform/windows/radial/radial_shortcut.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstdlib>
#include <iostream>
namespace {
void Expect(bool value,const char* text){if(!value){std::cerr<<text<<'\n';std::exit(1);}}
VOID CALLBACK QuitTimer(HWND window,UINT,UINT_PTR timer,DWORD){KillTimer(window,timer);PostQuitMessage(23);}
bool released=false,timedOut=false;
VOID CALLBACK ReleaseTimer(HWND window,UINT,UINT_PTR timer,DWORD){
    KillTimer(window,timer);released=true;
    SendMessageW(window,lume::platform::windows::RadialShortcut::ReleaseMessage,0,0);
}
VOID CALLBACK TimeoutTimer(HWND window,UINT,UINT_PTR timer,DWORD){
    KillTimer(window,timer);timedOut=true;PostQuitMessage(24);
}
}
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
    Expect(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"menu drawing initialized");
    const auto owner=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"radial menu test",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Expect(owner!=nullptr,"menu owner created");
    Expect(SetTimer(owner,1,20,QuitTimer)!=0,"quit timer created");
    Expect(LumeWidgetMenu::ShowRadial(owner,{32000,32000},false)==0,"quit dismisses radial context menu");
    MSG message{};Expect(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)&&message.wParam==23,"context menu preserves outer-loop WM_QUIT and code");
    DestroyWindow(owner);
    using namespace lume::platform::windows;
    RadialWindow* active=nullptr;
    RadialWindow radial(GetModuleHandleW(nullptr),[&](UINT command){if(command==RadialWindow::Dismiss&&active)active->Hide();},[]{});
    active=&radial;
    Expect(radial.Show({0,0},false),"isolated radial visible before menu");
    SetTimer(radial.Handle(),11,40,ReleaseTimer);SetTimer(radial.Handle(),12,1000,TimeoutTimer);
    SendMessageW(radial.Handle(),WM_CONTEXTMENU,0,MAKELPARAM(32000,32000));
    KillTimer(radial.Handle(),11);KillTimer(radial.Handle(),12);
    Expect(released&&!radial.Visible(),"release during modal menu hides the radial");
    Expect(!timedOut&&GetCapture()==nullptr,"release closes the owned menu and returns its mouse capture");
    radial.Stop();Gdiplus::GdiplusShutdown(token);CoUninitialize();std::cout<<"PASS radial context-menu shutdown propagation and release dismissal\n";
}
