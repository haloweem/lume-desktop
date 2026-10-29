#include "apps/shell/desktop_application.h"
#include "apps/shell/todo_session.h"
#include "unique_test_directory.h"
#include "vendor/nlohmann/json.hpp"
#include <iostream>
#include <stdexcept>

using namespace lume;
namespace {
HWND Find(const wchar_t* name) {
    struct Search {const wchar_t* name; HWND result=nullptr;} search{name};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM value)->BOOL{
        auto& s=*reinterpret_cast<Search*>(value);wchar_t type[100]{};GetClassNameW(h,type,100);
        if(std::wstring(type)==s.name){s.result=h;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));return search.result;
}
struct Probe {shell::DesktopApplication* app; ULONGLONG deadline; int stage=0; bool ok=true;}* probe=nullptr;
void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD) {
    auto& p=*probe;
    if(GetTickCount64()>p.deadline){std::cerr<<"timeout stage="<<p.stage<<" manager="<<p.app->ManagerHandle()<<" calendar="<<p.app->CalendarHandle()<<" notice="<<Find(L"LumeNativeReminder")<<'\n';p.ok=false;p.app->Exit();return;}
    if(p.stage==0&&p.app->ManagerReady()) {
        p.ok=p.ok&&!p.app->CalendarRunning()&&!p.app->ClockRunning();
        p.stage=1;p.app->CloseManager();
    } else if(p.stage==1&&!p.app->ManagerHandle()) {
        if(auto notice=Find(L"LumeNativeReminder")) {
            p.ok=p.ok&&!p.app->CalendarRunning()&&IsWindowVisible(notice);
            const auto dpi=GetDpiForWindow(notice);
            const auto point=MAKELPARAM(MulDiv(305,dpi,96),MulDiv(173,dpi,96));
            p.stage=2;SendMessageW(notice,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(notice,WM_LBUTTONUP,0,point);
        }
    } else if(p.stage==2) {
        if(auto editor=Find(L"LumeCalendarTodoEditor")) {
            wchar_t title[100]{};GetWindowTextW(GetDlgItem(editor,101),title,100);
            // EDIT fields may be nested in the scroll surface.
            if(!*title)EnumChildWindows(editor,[](HWND h,LPARAM data)->BOOL{
                if(GetDlgCtrlID(h)==101){GetWindowTextW(h,reinterpret_cast<wchar_t*>(data),100);return FALSE;}return TRUE;
            },reinterpret_cast<LPARAM>(title));
            p.ok=p.ok&&p.app->CalendarRunning()&&std::wstring(title)==L"提醒接续验收";
            p.stage=3;p.app->Exit();
        }
    }
}
}
int main(){try{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto root=UniqueTestDirectory(std::filesystem::path(exe).parent_path()/"desktop-reminder-artifacts");
    shell::DesktopApplicationOptions options;options.verification=true;
    bool statusValid=true;
    options.onMessage=[&](const std::wstring& value){
        const auto state=nlohmann::json::parse(value.begin(),value.end(),nullptr,false);
        if(state.is_discarded()){statusValid=false;return;}
        if(state.value("type","")=="calendar-settings") {
            const auto status=state.value("materialStatus","");
            statusValid=statusValid&&(status=="inactive"||status=="native-starting"||status=="native-ready"||status=="transparent-fallback");
        }
    };
    options.verificationReminderAllowed=[] { return true; };
    options.clockPreferencesPath=(root/"ui-preferences.json").u8string();
    options.calendarPreferencesPath=(root/"calendar-preferences.json").u8string();
    shell::TodoSession seed(options.calendarPreferencesPath);
    todo::Command add;add.scope=seed.Read().scope;add.patch.title=u8"提醒接续验收";
    const auto now=platform::windows::WindowsLocalTime::NowMillis();
    add.patch.schedule=todo::Schedule{now+8000,now+68000};
    add.patch.reminder=todo::ReminderDraft{todo::ReminderKind::Relative,0,0,false};
    if(!seed.Dispatch(add).committed)throw std::runtime_error("seed isolated reminder");
    shell::DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));
    Probe state{&app,GetTickCount64()+25000};probe=&state;
    const auto timer=SetTimer(nullptr,0,30,Tick);if(!timer)throw std::runtime_error("create probe timer");
    const auto result=app.Run();KillTimer(nullptr,timer);probe=nullptr;
    if(result||!statusValid||!state.ok||state.stage!=3||app.CalendarRunning()||app.ClockRunning()||
       app.ManagerHandle()||Find(L"LumeNativeReminder")||Find(L"LumeCalendarTodoEditor"))
        throw std::runtime_error("closed widgets keep reminder host; view opens correct task; exit releases windows");
    std::cout<<"DESKTOP_REMINDER_PASS\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
