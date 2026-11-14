#include "apps/shell/calendar_session.h"
#include "apps/shell/todo_session.h"
#include "platform/windows/calendar/calendar_layout.h"
#include "platform/windows/calendar/todo_sheet_window.h"
#include "unique_test_directory.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace lume;
namespace {
void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
HWND Editor(HWND owner){struct Search{HWND owner;HWND found=nullptr;}s{owner};EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p)->BOOL{auto& s=*reinterpret_cast<Search*>(p);wchar_t cls[80]{};GetClassNameW(h,cls,80);if(GetWindow(h,GW_OWNER)==s.owner&&std::wstring(cls)==L"LumeTodoSheetWindow"){s.found=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&s));return s.found;}
void Create(shell::CalendarSession& session){const auto& p=*session.Snapshot().Active();const auto r=platform::windows::CalculateCalendarLayout(session.Snapshot().theme,static_cast<float>(p.width),static_cast<float>(p.height)).create;const auto dpi=GetDpiForWindow(session.Handle());SendMessageW(session.Handle(),WM_LBUTTONDOWN,0,MAKELPARAM(static_cast<int>((r.x+r.width/2)*dpi/96),static_cast<int>((r.y+r.height/2)*dpi/96)));Pump();Check(Editor(session.Handle())!=nullptr,"session opens real todo editor");}
// The editor is the shared WebView task sheet; post its save message directly.
std::string Today(){SYSTEMTIME t{};GetLocalTime(&t);char b[16]{};std::snprintf(b,sizeof b,"%04u-%02u-%02u",t.wYear,t.wMonth,t.wDay);return b;}
void Save(HWND editor,const std::string& title){
    platform::windows::TodoSheetWindow::Receive(editor,R"({"version":1,"scope":"todo-editor","action":"save","commands":[{"action":"add","fields":{"title":")"+title+R"(","date":")"+Today()+R"(","allDay":true,"days":1}}]})");Pump();}
std::string Bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
}
int main(){try{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto root=UniqueTestDirectory(std::filesystem::path(exe).parent_path()/"calendar-todo-session-artifacts");
    const auto path=(root/"calendar-preferences.json").u8string();const auto data=root/"todo-data"/"local"/"todo-data.json";
    {shell::CalendarSession session(GetModuleHandleW(nullptr),path,{},{});Check(session.Restore(),"session restores");session.HandlePage(ui::PageCommand::CalendarEnable,nullptr);Pump();Check(session.Running(),"actual calendar enabled");
    Create(session);auto editor=Editor(session.Handle());Save(editor,"真实文件日程");Check(!Editor(session.Handle()),"save closes editor");Check(std::filesystem::exists(data),"real atomic file exists");
    shell::TodoSession inspect(path);Check(inspect.Read().tasks.size()==1&&inspect.Read().tasks[0].title==u8"真实文件日程","disk snapshot matches UI");
    const auto before=Bytes(data);const auto blocked=std::filesystem::path(data.wstring()+L".tmp");std::filesystem::create_directory(blocked);
    Create(session);editor=Editor(session.Handle());Save(editor,"失败后重试");Check(Editor(session.Handle())==editor&&Bytes(data)==before,"real disk failure preserves draft and committed bytes");
    std::filesystem::remove(blocked);Save(editor,"失败后重试");Check(!Editor(session.Handle()),"retry succeeds");
    session.HandlePage(ui::PageCommand::CalendarDisable,nullptr);Check(!session.Running(),"disable closes calendar");session.HandlePage(ui::PageCommand::CalendarEnable,nullptr);Pump();Create(session);session.CancelEditor();Check(!Editor(session.Handle()),"shell editor cancellation");
    Create(session);session.Shutdown();Pump();Check(!session.Running(),"shutdown releases all child windows");}
    {shell::CalendarSession restored(GetModuleHandleW(nullptr),path,{},{});Check(restored.Restore()&&restored.Running(),"restart restores enabled calendar");shell::TodoSession inspect(path);Check(inspect.Read().tasks.size()==2,"restart restores both committed tasks");restored.Shutdown();}
    // Unknown versions must not be silently repaired by opening the real form.
    {std::ofstream f(data,std::ios::binary|std::ios::trunc);f<<"{\"version\":999,\"tasks\":[],\"outbox\":[]}";}
    const auto unknown=Bytes(data);
    {shell::CalendarSession session(GetModuleHandleW(nullptr),path,{},{});Check(session.Restore(),"unknown todo version leaves calendar usable");Create(session);auto editor=Editor(session.Handle());Save(editor,"不可覆盖未来版本");Check(Editor(session.Handle())==editor&&Bytes(data)==unknown,"unknown version leaves editor error and file intact");session.Shutdown();}
    std::cout<<"CALENDAR_TODO_SESSION_PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
