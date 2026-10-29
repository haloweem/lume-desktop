// Explicit isolated developer probe; never shipped and never touches APPDATA.
#include "apps/shell/todo_session.h"
#include "platform/windows/calendar/native_calendar_window.h"
#include "process_memory_snapshot.h"
#include "unique_test_directory.h"
#include <psapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace lume;
using namespace lume::platform::windows;
namespace {
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Pump(DWORD duration){
    const auto end=GetTickCount64()+duration;
    for(;;){
        MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
        const auto now=GetTickCount64();if(now>=end)break;
        MsgWaitForMultipleObjects(0,nullptr,FALSE,static_cast<DWORD>(end-now),QS_ALLINPUT);
    }
}
HWND Field(HWND parent,int id){struct Search{int id;HWND found=nullptr;}s{id};EnumChildWindows(parent,[](HWND h,LPARAM p)->BOOL{auto& s=*reinterpret_cast<Search*>(p);if(GetDlgCtrlID(h)==s.id){s.found=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&s));return s.found;}
void Click(NativeCalendarWindow& w,CalendarRect r){const auto dpi=GetDpiForWindow(w.Handle());SendMessageW(w.Handle(),WM_LBUTTONDOWN,0,MAKELPARAM(static_cast<int>((r.x+r.width/2)*dpi/96),static_cast<int>((r.y+r.height/2)*dpi/96)));Pump(30);}
void Sample(std::ofstream& f,const char* phase,int round){
    PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);Check(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m))!=FALSE,"memory counters");
    DWORD handles=0;Check(GetProcessHandleCount(GetCurrentProcess(),&handles)!=FALSE,"handle counters");
    FILETIME creation{},exit{},kernel{},user{};Check(GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)!=FALSE,"CPU counters");
    const auto ticks=[](FILETIME v){return(static_cast<unsigned long long>(v.dwHighDateTime)<<32)|v.dwLowDateTime;};
    auto process=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);Check(process!=INVALID_HANDLE_VALUE,"process snapshot");PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);unsigned children=0;
    if(Process32FirstW(process,&entry))do{if(entry.th32ParentProcessID==GetCurrentProcessId())++children;}while(Process32NextW(process,&entry));
    CloseHandle(process);
    f<<"{\"phase\":\""<<phase<<"\",\"round\":"<<round<<",\"tickMs\":"<<GetTickCount64()<<",\"privateBytes\":"<<m.PrivateUsage<<",\"workingSetBytes\":"<<m.WorkingSetSize
      <<",\"handles\":"<<handles<<",\"gdi\":"<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<",\"user\":"<<GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)
      <<",\"cpu100ns\":"<<ticks(kernel)+ticks(user)<<",\"children\":"<<children<<"}\n";f.flush();Check(f.good()&&children==0,"write sample and native-only process");
}
}
int main(){try{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);const auto root=UniqueTestDirectory(std::filesystem::path(exe).parent_path()/"verification","calendar-c-resource-");
    std::ofstream samples(root/"samples.jsonl");Sample(samples,"baseline",0);
    {shell::TodoSession session((root/"calendar.json").u8string());const auto now=WindowsLocalTime::NowMillis();const auto today=session.Time().ToLocal(now)->date;
    for(int i=0;i<30;++i){todo::Command c;c.scope=session.Read().scope;c.patch.title=u8"本地日程资源验收 "+std::to_string(i);c.patch.notes=u8"中文备注与可滚动清单";const auto start=*session.Time().FromLocal({today,8+i%12});c.patch.schedule=todo::Schedule{start,start+3600000};Check(session.Dispatch(c).ok,"seed local tasks");}
    NativeCalendarWindow window(GetModuleHandleW(nullptr),[](calendar::Action){},[]{});window.BindTodos(session.Port());calendar::Preferences prefs;prefs.enabled=true;prefs.Active()->hasPosition=true;prefs.Active()->x=100;prefs.Active()->y=60;
    for(int round=1;round<=12;++round){prefs.theme=round%2?calendar::Theme::Spacious:calendar::Theme::Split;Check(window.Configure(prefs)&&window.Start(),"open calendar");Pump(80);Click(window,window.Layout().list);Sample(samples,"agenda",round);
        Check(!window.AgendaLayout().rows.empty(),"agenda rows");Click(window,window.AgendaLayout().rows[0].edit);auto editor=window.TodoEditor().Handle();Check(editor!=nullptr,"open editor");Sample(samples,"editor",round);
        SendMessageW(Field(editor,102),BM_CLICK,0,0);Pump(50);Sample(samples,"picker",round);window.CancelTodoEditor();
        Click(window,window.AgendaLayout().rows[0].edit);editor=window.TodoEditor().Handle();SetWindowTextW(Field(editor,101),(L"已编辑日程 "+std::to_wstring(round)).c_str());SendMessageW(Field(editor,106),BM_CLICK,0,0);Pump(40);Check(!window.TodoEditor().IsOpen(),"commit closes editor");
        Click(window,window.AgendaLayout().rows[0].check);SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);Pump(30);
        window.Stop();Pump(100);Sample(samples,"closed",round);Check(WriteProcessMemorySnapshot(root/"diagnostics.jsonl","closed",round),"resource diagnostics");
    }
    Check(window.Start(),"idle open");Click(window,window.Layout().list);Pump(1000);Sample(samples,"idle",0);for(int second=1;second<=10;++second){Pump(1000);Sample(samples,"idle",second);}
    // Sample() walks the machine process table; exclude that probe overhead
    // from the uninterrupted idle CPU interval, which also spans the 15s timer.
    const auto cpu=[](){FILETIME a{},b{},k{},u{};Check(GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u)!=FALSE,"idle CPU read");return(static_cast<unsigned long long>(k.dwHighDateTime)<<32)+k.dwLowDateTime+(static_cast<unsigned long long>(u.dwHighDateTime)<<32)+u.dwLowDateTime;};
    const auto firstCpu=cpu(),firstTick=GetTickCount64();Pump(16000);const auto lastCpu=cpu(),lastTick=GetTickCount64();
    std::ofstream interval(root/"idle-cpu.json");interval<<"{\"elapsedMs\":"<<lastTick-firstTick<<",\"cpu100ns\":"<<lastCpu-firstCpu<<",\"oneCorePercent\":"<<static_cast<double>(lastCpu-firstCpu)/10000/static_cast<double>(lastTick-firstTick)*100<<"}";
    interval.close();Check(!interval.fail(),"idle CPU interval evidence written");
    window.Stop();}
    Pump(500);Sample(samples,"destroyed",0);std::cout<<"CALENDAR_TODO_RESOURCE_PASS "<<root.u8string()<<'\n';
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
