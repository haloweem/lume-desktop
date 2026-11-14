#include "platform/windows/calendar/todo_sheet_window.h"
#include "todo_test_support.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <tlhelp32.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <iostream>
using namespace lume;
using namespace lume::platform::windows;
using namespace todo_test;
namespace {
struct Apartment {HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Apartment(){Expect(SUCCEEDED(result),"match the production shell's UI COM apartment");}void Close(){if(SUCCEEDED(result))CoUninitialize();result=E_FAIL;}~Apartment(){Close();}};
void Pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
HWND Field(HWND window,int id){struct Find{int id;HWND result=nullptr;}find{id};EnumChildWindows(window,[](HWND child,LPARAM value)->BOOL{
    auto& state=*reinterpret_cast<Find*>(value);if(GetDlgCtrlID(child)==state.id){state.result=child;return FALSE;}return TRUE;
},reinterpret_cast<LPARAM>(&find));return find.result;}
void Click(HWND window,int id){const auto field=Field(window,id);Expect(field!=nullptr,"native control exists");SendMessageW(field,BM_CLICK,0,0);Pump();}
void Text(HWND window,int id,const wchar_t* value){const auto field=Field(window,id);Expect(field!=nullptr,"native text field exists");SetWindowTextW(field,value);Pump();}
void Selection(HWND window,int id,int index){Expect(Field(window,id)!=nullptr,"native choice exists");SendMessageW(Field(window,id),CB_SETCURSEL,index,0);}
HWND Picker(HWND owner){HWND result=nullptr;struct Search{HWND owner;HWND* result;}search{owner,&result};EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL{auto& value=*reinterpret_cast<Search*>(data);wchar_t name[64]{};GetClassNameW(window,name,64);if(GetWindow(window,GW_OWNER)==value.owner&&std::wstring(name)==L"LumeCalendarTodoPicker"){*value.result=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return result;}
void Date(HWND window,int id,int year,int month,int day,int hour=0,int minute=0){SYSTEMTIME value{};value.wYear=static_cast<WORD>(year);value.wMonth=static_cast<WORD>(month);value.wDay=static_cast<WORD>(day);value.wHour=static_cast<WORD>(hour);value.wMinute=static_cast<WORD>(minute);SendMessageW(Field(window,id),DTM_SETSYSTEMTIME,GDT_VALID,reinterpret_cast<LPARAM>(&value));}
unsigned Children(){unsigned count=0;const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);Expect(snapshot!=INVALID_HANDLE_VALUE,"process snapshot");PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);if(Process32FirstW(snapshot,&entry))do{if(entry.th32ParentProcessID==GetCurrentProcessId())++count;}while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return count;}
void Shot(HWND window,const std::filesystem::path& path){RECT rect{};GetClientRect(window,&rect);const int width=rect.right,height=rect.bottom;
    const auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);Expect(bitmap&&pixels,"native image buffer");const auto old=SelectObject(dc,bitmap);
    Expect(PrintWindow(window,dc,0)!=FALSE,"capture native form including controls");GdiFlush();const auto bytes=static_cast<DWORD>(width*height*4);BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+bytes;
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char*>(&header),sizeof(header));file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));file.write(static_cast<char*>(pixels),bytes);file.close();SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
void ComposedShot(HWND window,const std::filesystem::path& path){
    RECT rect{};GetWindowRect(window,&rect);const int width=rect.right-rect.left,height=rect.bottom-rect.top;
    const auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);const auto old=SelectObject(dc,bitmap);BitBlt(dc,0,0,width,height,screen,rect.left,rect.top,SRCCOPY|CAPTUREBLT);GdiFlush();
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+static_cast<DWORD>(width*height*4);
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char*>(&header),sizeof(header));file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));file.write(static_cast<char*>(pixels),width*height*4);file.close();SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
}
void Backdrop(HWND window,COLORREF color){const auto old=reinterpret_cast<HBRUSH>(SetClassLongPtrW(window,GCLP_HBRBACKGROUND,reinterpret_cast<LONG_PTR>(CreateSolidBrush(color))));if(old)DeleteObject(old);RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);DwmFlush();Sleep(60);DwmFlush();Pump();}
void CheckComposited(HWND window,const std::filesystem::path& diagnostics){
    RECT rect{};GetWindowRect(window,&rect);const auto dpi=GetDpiForWindow(window);const auto screen=GetDC(nullptr);
    for(const auto point:{POINT{8,160},POINT{280,700},POINT{552,160}}){
        const POINT position{rect.left+MulDiv(point.x,dpi,96),rect.top+MulDiv(point.y,dpi,96)};
        const auto covering=WindowFromPoint(position);const bool unobstructed=GetAncestor(covering,GA_ROOT)==window;
        const auto pixel=GetPixel(screen,position.x,position.y);
        if(!unobstructed||pixel!=RGB(28,28,32)){
            wchar_t kind[128]{};GetClassNameW(covering,kind,128);DWORD process=0;GetWindowThreadProcessId(covering,&process);
            std::wcerr<<L"composited pixel x="<<point.x<<L" y="<<point.y<<L" rgb="<<std::hex<<pixel<<std::dec<<L" coveringClass="<<kind<<L" coveringProcess="<<process<<L" testProcess="<<GetCurrentProcessId()<<L'\n';
            if(!diagnostics.empty()){std::filesystem::create_directories(diagnostics);ComposedShot(window,diagnostics/"native-todo-composited-failure.bmp");}
            Expect(unobstructed,"compositor sample is not obscured by another application");
            Expect(false,"actual compositor retains the original opaque task-sheet background");
        }
    }
    ReleaseDC(nullptr,screen);
}
}
int main(int argc,char** argv){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const bool composited=argc>1&&std::string(argv[1])=="--composited";
    const auto previousDesktop=GetThreadDesktop(GetCurrentThreadId());
    const auto desktop=composited?nullptr:CreateDesktopW((L"LumeTodoNative-"+std::to_wstring(GetCurrentProcessId())).c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    Expect(composited||(desktop&&SetThreadDesktop(desktop)),"use a private desktop without changing the user's screen");
    Apartment apartment;
    RECT host{0,0,600,900};unsigned editorDpi=96;
    const wchar_t* ownerClass=L"STATIC";
    if(composited){MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);editorDpi=GetDpiForSystem();const auto work=monitor.rcWork;
        const int width=std::min(static_cast<int>(work.right-work.left),MulDiv(608,editorDpi,96)),height=std::min(static_cast<int>(work.bottom-work.top),MulDiv(868,editorDpi,96));host={work.left+std::min(MulDiv(24,editorDpi,96),static_cast<int>(work.right-work.left)-width),work.top+(work.bottom-work.top-height)/2,0,0};host.right=host.left+width;host.bottom=host.top+height;
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"LumeTodoCompositedBackdrop";wc.hbrBackground=CreateSolidBrush(RGB(214,30,160));RegisterClassW(&wc);ownerClass=wc.lpszClassName;}
    const auto owner=CreateWindowExW(composited?WS_EX_TOPMOST|WS_EX_TOOLWINDOW:0,ownerClass,L"Native todo test owner",WS_POPUP|WS_VISIBLE,host.left,host.top,host.right-host.left,host.bottom-host.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);Expect(owner!=nullptr,"owner window");
    Utc time;Files files;int ids=0;todo::TodoRepository repository(files,time,"native-todo",[&]{return std::to_string(++ids);});repository.SwitchAccount("local");
    const auto now=At(2026,11,14,10);TodoSheetWindow sheet;unsigned saved=0;std::vector<ULONGLONG> elapsed;
    const auto children=Children();
    const auto open=[&](std::optional<todo::TaskView> existing=std::nullopt){todo::Command seed;seed.scope=repository.Read().scope;seed.patch.schedule=todo::DefaultSchedule({2026,11,14},now,time);
        if(existing){seed.type=todo::CommandType::Update;seed.id=existing->id;seed.patch.schedule.reset();}
        const auto started=GetTickCount64();Expect(sheet.Open(GetModuleHandleW(nullptr),owner,host,editorDpi,{},time,seed,now,existing,[&](const todo::Command& command){++saved;return repository.Dispatch(command,now);}),"native sheet opens");elapsed.push_back(GetTickCount64()-started);
        Expect(sheet.IsOpen()&&IsWindowVisible(sheet.Handle()),"form is complete and visible before Open returns");Expect(!IsWindowEnabled(owner),"modal owner disabled");Expect(GetModuleHandleW(L"WebView2Loader.dll")==nullptr,"native form does not load a browser loader");};
    open();const auto first=sheet.Handle();Expect(Field(first,101)&&Field(first,114),"native edit and date controls created");
    if(composited){
        const auto diagnostics=argc>2?std::filesystem::path(argv[2]):std::filesystem::path{};
        Backdrop(owner,RGB(214,30,160));CheckComposited(first,diagnostics);Backdrop(owner,RGB(15,211,172));CheckComposited(first,diagnostics);Backdrop(owner,RGB(17,17,20));
        if(argc>2){std::filesystem::create_directories(argv[2]);ComposedShot(first,std::filesystem::path(argv[2])/"native-todo-composited-new.bmp");}
        Click(first,120);Pump();DwmFlush();if(argc>2)ComposedShot(first,std::filesystem::path(argv[2])/"native-todo-composited-more.bmp");
        sheet.Close();const auto brush=reinterpret_cast<HBRUSH>(GetClassLongPtrW(owner,GCLP_HBRBACKGROUND));DestroyWindow(owner);UnregisterClassW(ownerClass,GetModuleHandleW(nullptr));if(brush)DeleteObject(brush);
        std::cout<<"NATIVE_TODO_COMPOSITED_PASS backgrounds=2 opaqueRgb=1c1c20 firstMs="<<elapsed.front()<<" dpi="<<editorDpi<<'\n';return 0;
    }
    Text(first,101,L"原生待办 · 保留全部功能");Click(first,120);Text(first,121,L"输入法、子任务与备注");Click(first,122);Text(first,105,L"本机保存");
    Selection(first,118,4);Selection(first,119,2);Click(first,142);Date(first,114,2026,11,16);Text(first,115,L"2");
    Click(first,119);const auto choice=Picker(first);Expect(choice!=nullptr,"repeat selector opens the existing native picker");
    Click(choice,502);Expect(!Picker(first)&&SendMessageW(Field(first,119),CB_GETCURSEL,0,0)==2,"native picker callback keeps weekday selection");
    if(argc>1){std::filesystem::create_directories(argv[1]);Shot(first,std::filesystem::path(argv[1])/"native-todo.bmp");}
    Click(first,106);Expect(!sheet.IsOpen()&&!IsWindow(first)&&IsWindowEnabled(owner),"save immediately releases the form and enables its owner");
    auto tasks=repository.Read().tasks;Expect(tasks.size()==1&&tasks[0].subtasks.size()==1,"real controls save title and sub-step through existing repository");
    Expect(tasks[0].notes==u8"本机保存"&&tasks[0].color==todo::Color::Orange,"notes and colour preserved");
    Expect(tasks[0].recurrence&&tasks[0].recurrence->frequency==todo::Frequency::Weekdays,"weekday recurrence preserved");
    Expect(tasks[0].reminder.leadMilliseconds==900000,"selected reminder persisted");
    Expect(tasks[0].schedule->startAt==At(2026,11,16)&&tasks[0].schedule->endAt==At(2026,11,18),"all-day multi-day schedule preserved");
    open(tasks[0]);auto edit=sheet.Handle();Text(edit,101,L"仅改名称");Click(edit,106);tasks=repository.Read().tasks;
    Expect(tasks[0].subtasks.size()==1&&tasks[0].recurrence->frequency==todo::Frequency::Weekdays&&tasks[0].schedule->endAt==At(2026,11,18),"editing only a title retains sub-steps, recurrence and schedule");
    open(tasks[0]);edit=sheet.Handle();Click(edit,113);Date(edit,114,2026,11,20);Date(edit,124,2026,11,20,23,30);Date(edit,125,2026,11,20,0,30);Text(edit,115,L"1");Click(edit,111);Click(edit,106);tasks=repository.Read().tasks;
    Expect(tasks[0].completed&&tasks[0].schedule->endAt==At(2026,11,21,0,30),"timed overnight edit and completion share the same command validation");
    const auto writes=saved;
    for(int i=0;i<8;++i){TodoSheetWindow::SetEnglish(i%2!=0);open();Text(sheet.Handle(),101,L"cancelled draft");
        if(i<3){RECT rect{};GetWindowRect(sheet.Handle(),&rect);const unsigned dpi=i==0?96:i==1?144:192;SendMessageW(sheet.Handle(),WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&rect));Expect(IsWindow(Field(sheet.Handle(),101)),"DPI change retains native input controls");}
        Click(sheet.Handle(),107);Expect(!sheet.IsOpen()&&IsWindowEnabled(owner),"repeated close releases every native form");}
    Expect(saved==writes&&Children()==children,"cancelled drafts do not save or start child processes");
    open();const auto dying=sheet.Handle();DestroyWindow(owner);Pump();Expect(!sheet.IsOpen()&&!IsWindow(dying),"owner destruction releases native controls");
    std::cout<<"NATIVE_TODO_PASS opens="<<elapsed.size()<<" firstMs="<<elapsed.front()<<" repeatMinMs="<<*std::min_element(elapsed.begin()+1,elapsed.end())<<" maxMs="<<*std::max_element(elapsed.begin(),elapsed.end())<<" browserChildren=0\n";
    apartment.Close();SetThreadDesktop(previousDesktop);CloseDesktop(desktop);return 0;
}
