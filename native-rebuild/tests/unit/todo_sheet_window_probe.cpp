// Manual probe: opens the real task sheet popup (WebView2) and saves a
// screenshot of what it renders. Not part of the test suite.
#include "platform/windows/calendar/todo_sheet_window.h"
#include "core/todo/todo_time.h"
#include "platform/windows/todo/windows_local_time.h"
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include <objbase.h>
using namespace lume;
using namespace lume::platform::windows;
void Pump(int ms){const auto end=GetTickCount64()+ms;MSG m{};while(GetTickCount64()<end){while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}}
void Shot(HWND h,const wchar_t* path){
    RECT r{};GetWindowRect(h,&r);const int w=r.right-r.left,ht=r.bottom-r.top;
    const auto screen=GetDC(nullptr);const auto dc=CreateCompatibleDC(screen);const auto bmp=CreateCompatibleBitmap(screen,w,ht);SelectObject(dc,bmp);
    BitBlt(dc,0,0,w,ht,screen,r.left,r.top,SRCCOPY);
    BITMAPINFOHEADER bi{sizeof(bi),w,-ht,1,32,BI_RGB};std::vector<char> px(static_cast<size_t>(w)*ht*4);
    GetDIBits(dc,bmp,0,ht,px.data(),reinterpret_cast<BITMAPINFO*>(&bi),DIB_RGB_COLORS);
    BITMAPFILEHEADER fh{0x4d42,static_cast<DWORD>(sizeof(fh)+sizeof(bi)+px.size()),0,0,sizeof(fh)+sizeof(bi)};
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<char*>(&fh),sizeof fh);f.write(reinterpret_cast<char*>(&bi),sizeof bi);f.write(px.data(),px.size());
    DeleteObject(bmp);DeleteDC(dc);ReleaseDC(nullptr,screen);
}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="foreign"){ // stands in for a WebView2 picker window
        const HWND w=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,L"STATIC",L"foreign",WS_POPUP|WS_VISIBLE,300,300,300,300,nullptr,nullptr,nullptr,nullptr);
        SetWindowPos(w,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);Pump(2500);return 0;}
    argc=argc>1&&std::string(argv[1])=="en"?2:1;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const HWND owner=CreateWindowExW(0,L"STATIC",L"owner",WS_POPUP|WS_VISIBLE,100,100,200,200,nullptr,nullptr,nullptr,nullptr);
    WindowsLocalTime time;const auto now=WindowsLocalTime::NowMillis();
    todo::Command seed;seed.type=todo::CommandType::Add;seed.patch.schedule=todo::DefaultSchedule(time.ToLocal(now)->date,now,time);
    TodoSheetWindow::SetEnglish(argc>1);TodoSheetWindow sheet;int saved=0;
    const bool ok=sheet.Open(GetModuleHandleW(nullptr),owner,RECT{100,100,300,300},GetDpiForWindow(owner),{},time,seed,now,{},
        [&](const todo::Command&){++saved;todo::CommandResult r;r.ok=true;return r;});
    std::cout<<"open="<<ok<<"\n";Pump(5000);
    // A widget / Dock / island raising itself (topmost, like the island) must not cover the sheet.
    const HWND rival=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"rival",WS_POPUP|WS_VISIBLE,200,200,400,400,nullptr,nullptr,nullptr,nullptr);
    SetWindowPos(rival,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);Pump(600);
    bool above=false;for(HWND h=GetWindow(rival,GW_HWNDPREV);h;h=GetWindow(h,GW_HWNDPREV))if(h==sheet.Handle())above=true;
    std::cout<<"sheet-above-rival="<<above<<std::endl;DestroyWindow(rival);
    // A picker from another process (WebView2 select / date lists) must keep its place above the sheet.
    {wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring line=L"\""+std::wstring(exe)+L"\" foreign";
     STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
     if(CreateProcessW(nullptr,line.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)){
        Pump(1200);HWND foreign=nullptr;while((foreign=FindWindowExW(nullptr,foreign,L"STATIC",L"foreign"))){DWORD pid=0;GetWindowThreadProcessId(foreign,&pid);if(pid==pi.dwProcessId)break;}
        bool sheetAbove=false;if(foreign)for(HWND h=GetWindow(foreign,GW_HWNDPREV);h;h=GetWindow(h,GW_HWNDPREV))if(h==sheet.Handle())sheetAbove=true;
        std::cout<<"foreign-found="<<(foreign!=nullptr)<<" foreign-stays-above="<<!sheetAbove<<std::endl;
        WaitForSingleObject(pi.hProcess,5000);CloseHandle(pi.hProcess);CloseHandle(pi.hThread);}}
    std::cout<<"still-open="<<sheet.IsOpen()<<" owner-disabled="<<!IsWindowEnabled(owner)<<"\n";
    if(sheet.IsOpen())Shot(sheet.Handle(),argc>1?L"todo-sheet-en.bmp":L"todo-sheet-zh.bmp");
    sheet.Close(false);std::cout<<"closed owner-enabled="<<(IsWindowEnabled(owner)!=FALSE)<<"\n";
}
