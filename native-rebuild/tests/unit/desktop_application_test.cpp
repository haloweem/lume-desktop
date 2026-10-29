#include "apps/shell/desktop_application.h"
#include "core/calendar/calendar_preferences_store.h"
#include "core/settings/settings_store.h"
#include "core/settings/json_settings_codec.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/widget_ui/resource.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <cwchar>
#include <iostream>
#include <map>
#include <vector>
#include <cmath>

namespace {
using namespace lume;
using namespace lume::shell;
using P=ui::PageCommand;
bool passed=true;
void Check(bool ok,const char* reason) {if(!ok){passed=false;std::cerr<<"FAIL: "<<reason<<std::endl;}}
ULONGLONG Time(FILETIME value){ULARGE_INTEGER n{};n.LowPart=value.dwLowDateTime;n.HighPart=value.dwHighDateTime;return n.QuadPart;}
struct Child {DWORD pid,parent;ULONGLONG created;HANDLE handle;};
// Keep open handles plus creation times: an exited/reparented child or reused PID
// cannot disappear from the evidence simply because a later snapshot omits it.
class Descendants {
public:
    Descendants(){FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);rootCreated_=Time(c);}
    ~Descendants(){for(auto& c:children_)CloseHandle(c.handle);}
    void Observe(){
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE){Check(false,"enumerate process tree");return;}
        PROCESSENTRY32W item{};item.dwSize=sizeof(item);std::vector<PROCESSENTRY32W> entries;
        if(Process32FirstW(snapshot,&item))do{entries.push_back(item);}while(Process32NextW(snapshot,&item));
        CloseHandle(snapshot);
        bool added=true;while(added){added=false;for(const auto& p:entries){
            ULONGLONG parentCreated=0;
            if(p.th32ParentProcessID==GetCurrentProcessId())parentCreated=rootCreated_;
            else for(const auto& c:children_)if(c.pid==p.th32ParentProcessID && WaitForSingleObject(c.handle,0)==WAIT_TIMEOUT)parentCreated=c.created;
            if(!parentCreated)continue;
            HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p.th32ProcessID);if(!process)continue;
            FILETIME c{},e{},k{},u{};if(!GetProcessTimes(process,&c,&e,&k,&u)){CloseHandle(process);continue;}
            const auto created=Time(c);bool known=false;for(const auto& child:children_)if(child.pid==p.th32ProcessID&&child.created==created)known=true;
            if(known||created<parentCreated){CloseHandle(process);continue;}
            children_.push_back({p.th32ProcessID,p.th32ParentProcessID,created,process});added=true;
        }}
    }
    std::size_t Live()const{std::size_t n=0;for(const auto& c:children_)if(WaitForSingleObject(c.handle,0)==WAIT_TIMEOUT)++n;return n;}
    std::size_t Count()const{return children_.size();}
    void Write(std::ostream& out)const{out<<'[';bool comma=false;for(const auto& c:children_){if(comma)out<<',';comma=true;out<<"{\"pid\":"<<c.pid<<",\"parent\":"<<c.parent<<",\"created\":"<<c.created<<",\"alive\":"<<(WaitForSingleObject(c.handle,0)==WAIT_TIMEOUT?"true":"false")<<'}';}out<<']';}
private:ULONGLONG rootCreated_=0;std::vector<Child> children_;
};
HWND OwnWindow(const wchar_t* wanted,HWND owner=nullptr){
    struct Search{const wchar_t* name;HWND owner,result=nullptr;}s{wanted,owner};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL{auto& s=*reinterpret_cast<Search*>(data);wchar_t name[100]{};GetClassNameW(hwnd,name,100);if(std::wstring(name)==s.name&&(!s.owner||GetWindow(hwnd,GW_OWNER)==s.owner)){s.result=hwnd;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&s));return s.result;
}
struct NativeMenuSearch{HWND owner;std::vector<HWND> windows;};
std::vector<HWND> NativeMenus(HWND owner){
    NativeMenuSearch search{owner,{}};
    EnumWindows([](HWND window,LPARAM value)->BOOL{auto& s=*reinterpret_cast<NativeMenuSearch*>(value);DWORD process=0;
        if(GetWindowThreadProcessId(window,&process)!=GetCurrentThreadId()||process!=GetCurrentProcessId())return TRUE;
        wchar_t name[32]{};GetClassNameW(window,name,32);if(std::wcscmp(name,L"#32768")||!IsWindowVisible(window))return TRUE;
        s.windows.push_back(window);
        return TRUE;},reinterpret_cast<LPARAM>(&search));
    return search.windows;
}
void PostMenuKey(HWND owner,UINT key){const LPARAM scan=static_cast<LPARAM>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC))<<16;PostMessageW(owner,WM_KEYDOWN,key,scan|1);PostMessageW(owner,WM_KEYUP,key,scan|0xc0000001u);}
struct MenuDriver{HWND owner=nullptr;UINT command=0;unsigned rootDown=0,rootMovesRemaining=0;bool clicked=false,timedOut=false;unsigned stage=0;UINT_PTR timer=0;ULONGLONG deadline=0;};
MenuDriver* menuDriver=nullptr;
void CALLBACK MenuTimer(HWND,UINT,UINT_PTR id,DWORD){
    if(!menuDriver)return;
    auto& driver=*menuDriver;
    if(GetTickCount64()>driver.deadline){driver.timedOut=true;KillTimer(nullptr,id);EndMenu();return;}
    const auto menus=NativeMenus(driver.owner);
    if(driver.stage==0){if(menus.empty())return;driver.rootMovesRemaining=driver.rootDown;PostMenuKey(driver.owner,VK_HOME);driver.stage=1;}
    else if(driver.stage==1){if(driver.rootMovesRemaining){PostMenuKey(driver.owner,VK_DOWN);--driver.rootMovesRemaining;return;}PostMenuKey(driver.owner,VK_RETURN);driver.stage=2;}
    else if(menus.empty()){driver.clicked=true;KillTimer(nullptr,id);}
}
void BeginMenuDriver(MenuDriver& driver,HWND owner,UINT command,bool postContext,unsigned rootDown){
    driver={};driver.owner=owner;driver.command=command;driver.rootDown=rootDown;driver.deadline=GetTickCount64()+5000;menuDriver=&driver;driver.timer=SetTimer(nullptr,0,25,MenuTimer);Check(driver.timer!=0,"start native menu keyboard driver");
    if(postContext)Check(PostMessageW(owner,WM_CONTEXTMENU,0,MAKELPARAM(80,80))!=FALSE,"queue native context menu");
}
void SettingsMenu(HWND owner,bool calendar){
    MenuDriver driver;BeginMenuDriver(driver,owner,11,true,calendar?3u:5u);const auto deadline=GetTickCount64()+5500;
    while(!driver.clicked&&!driver.timedOut&&GetTickCount64()<deadline){MSG message{};if(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){PostQuitMessage(static_cast<int>(message.wParam));break;}TranslateMessage(&message);DispatchMessageW(&message);}else Sleep(1);}
    if(!driver.clicked&&!driver.timedOut){driver.timedOut=true;EndMenu();}if(driver.timer)KillTimer(nullptr,driver.timer);menuDriver=nullptr;Check(driver.clicked&&!driver.timedOut,"native settings menu command completed");
}
struct Probe {
    DesktopApplication* app=nullptr;
    Descendants descendants;
    std::ofstream log;
    ULONGLONG deadline=0,at=0;
    int ready=0,stage=0,mode=0;
    bool sawClock=false,sawCalendar=false,sawNavigation=false;
    bool menuBusy=false;
    MenuDriver menuAction;
    ManagerPage expectedPage=ManagerPage::Calendar;
    HWND calendar=nullptr,clock=nullptr;
    std::vector<std::wstring> messages;
    std::string calendarPath,clockPath;
    void Record(const char* phase){
        descendants.Observe();PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
        Check(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=FALSE,"read own memory");
        DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);
        log<<"{\"phase\":\""<<phase<<"\",\"round\":"<<ready<<",\"pid\":"<<GetCurrentProcessId()<<",\"privateBytes\":"<<memory.PrivateUsage<<",\"workingSet\":"<<memory.WorkingSetSize<<",\"handles\":"<<handles
           <<",\"gdi\":"<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<",\"user\":"<<GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)
           <<",\"trackedProcessHandles\":"<<descendants.Count()<<",\"clockRunning\":"<<(app->ClockRunning()?"true":"false")<<",\"calendarRunning\":"<<(app->CalendarRunning()?"true":"false")<<",\"children\":";descendants.Write(log);log<<"}\n";log.flush();
    }
    void Message(const std::wstring& message){
        messages.push_back(message);log<<"{\"message\":";
        for(const wchar_t character:message)log.put(static_cast<char>(character));
        log<<"}\n";
        log.flush();
        if(message.find(L"\"type\":\"settings\"")!=std::wstring::npos)sawClock=true;
        if(message.find(L"\"type\":\"calendar-settings\"")!=std::wstring::npos && message.find(L"\"result\":\"snapshot\"")!=std::wstring::npos){
            sawCalendar=true;
            // Material startup also publishes snapshots, including before the
            // WebView handshake. They are not manager open/close rounds.
            if(mode==5&&stage==0&&app->ManagerReady()){stage=1;Record("ready-callback-close");SendMessageW(app->ManagerHandle(),WM_CLOSE,0,0);}
        }
        if(message.find(L"\"type\":\"navigate\"")!=std::wstring::npos){
            Check(sawClock&&sawCalendar,"Ready sends both states before navigation");
            if(!sawNavigation){++ready;at=GetTickCount64();}
            sawNavigation=true;
            Check(message.find(expectedPage==ManagerPage::Calendar?L"\"page\":\"calendar\"":L"\"page\":\"clock\"")!=std::wstring::npos,"navigation reaches requested component");
        }
    }
    void Tick(){
        if(GetTickCount64()>deadline){Check(false,"desktop scenario deadline");app->Exit();return;}
        descendants.Observe();
        if(menuDriver&&menuDriver->timedOut){menuDriver=nullptr;Check(false,"native menu keyboard driver deadline");return;}
        if(menuDriver&&menuDriver->clicked)menuDriver=nullptr;
        if(menuBusy)return;
        if(mode==6||mode==7){
            if(stage==0&&(mode==7||app->ManagerReady())){
                const auto tray=OwnWindow(L"LumeNativeApplicationTray");
                Check(tray!=nullptr,"installer exit targets only this application tray");
                if(!tray){app->Exit();return;}
                Check(mode!=7||!app->ManagerHandle(),"hidden installer exit never opens the manager");
                stage=1;Record("installer-exit-request");
                PostThreadMessageW(GetWindowThreadProcessId(tray,nullptr),WM_QUIT,0,0);
            }else if(stage==1&&!app->ManagerHandle()){
                stage=2;Record("installer-exit-retained-loop");
                PostThreadMessageW(GetCurrentThreadId(),WM_QUIT,0,0);
            }
            return;
        }
        if(mode==4){
            const auto owner=OwnWindow(L"LumeNativeDesktopClock");
            if(stage==0&&app->ManagerReady()){
                stage=1;Record("clock-menu-opening");PostMessageW(owner,WM_CONTEXTMENU,0,MAKELPARAM(80,80));
            }else if(stage==1){
                const auto picker=OwnWindow(L"LumeWidgetColorPickerWindow",owner);
                if(picker){stage=3;Record("clock-native-editor");app->OnTray(platform::windows::TrayAction::Exit);}
                else if(!menuDriver)BeginMenuDriver(menuAction,owner,IDM_LUME_WIDGET_COLOR,false,4);
            }
            return;
        }
        if(mode==5){
            if(stage==1&&!app->ManagerHandle()){stage=2;app->Dispatch(P::CalendarDisable);}
            return;
        }
        if(mode>=2){
            const auto picker=OwnWindow(L"LumeWidgetColorPickerWindow",app->ManagerHandle());
            if(stage==0&&picker){stage=1;Record("native-editor");if(mode==2)app->CloseManager();else app->OnTray(platform::windows::TrayAction::Exit);at=GetTickCount64();}
            if(mode==2&&stage==1&&!app->ManagerHandle()){
                Check(app->CalendarRunning(),"owner close during page callback keeps calendar");stage=2;Record("editor-owner-closed");app->Dispatch(P::CalendarDisable);
            }
            return;
        }
        if(stage==0 && app->ManagerReady() && GetTickCount64()-at>=150){
            Check(sawClock&&sawCalendar&&sawNavigation,"real ready produces independent snapshots and queued navigation");
            if(!calendar){calendar=app->CalendarHandle();clock=OwnWindow(L"LumeNativeDesktopClock");}
            Check(calendar&&app->CalendarHandle()==calendar&&app->CalendarRunning(),"same native calendar survives manager lifetimes");
            Check(app->ClockRunning()==(mode==1),"expected independent clock runtime");
            if(mode==1&&ready==1){
                platform::windows::LocalSettingsFileStore files;std::string before,after;
                files.Read(clockPath,before);const auto calendarBlocked=std::filesystem::u8path(calendarPath+".tmp");std::filesystem::create_directory(calendarBlocked);
                app->Dispatch(P::CalendarSplit);files.Read(clockPath,after);
                Check(before==after&&app->ClockRunning()&&app->CalendarRunning(),"calendar save failure rolls back without altering clock");
                Check(messages.back().find(L"\"error\":\"save\"")!=std::wstring::npos,"calendar failed save routed to calendar page");std::filesystem::remove(calendarBlocked);
                files.Read(calendarPath,before);const auto clockBlocked=std::filesystem::u8path(clockPath+".tmp");std::filesystem::create_directory(clockBlocked);
                expectedPage=ManagerPage::Clock;app->Dispatch(P::ClockMond);files.Read(calendarPath,after);
                Check(before==after&&app->ClockRunning()&&app->CalendarRunning(),"clock save failure rolls back without altering calendar");
                std::filesystem::remove(clockBlocked);
            }
            Record("manager-open");stage=1;app->CloseManager();at=GetTickCount64();return;
        }
        if(stage==1&&!app->ManagerHandle()){
            Check(app->CalendarRunning()&&IsWindow(calendar),"calendar keeps actual background loop alive");
            if(descendants.Live())return;
            Check(descendants.Count()>0,"observed real WebView descendants");Record("manager-closed-children-exited");
            if(ready<3){
                stage=0;sawClock=sawCalendar=sawNavigation=false;
                expectedPage=mode==1&&ready==1?ManagerPage::Clock:ManagerPage::Calendar;
                menuBusy=true;SettingsMenu(expectedPage==ManagerPage::Calendar?app->CalendarHandle():OwnWindow(L"LumeNativeDesktopClock"),expectedPage==ManagerPage::Calendar);menuBusy=false;
                return;
            }
            stage=2;
            if(mode==1){app->Dispatch(P::CalendarDisable);Check(app->ClockRunning()&&!app->CalendarRunning(),"calendar close preserves clock and background loop");at=GetTickCount64();}
            else app->Dispatch(P::CalendarDisable);
            return;
        }
        if(stage==2&&mode==1&&GetTickCount64()-at>=100){
            Check(app->ClockRunning(),"clock remains alive after a background message cycle");Record("clock-after-calendar-close");
            app->Dispatch(P::CalendarEnable);app->Dispatch(P::ClockDisable);Check(!app->ClockRunning()&&app->CalendarRunning(),"clock close preserves calendar and background loop");at=GetTickCount64();stage=3;
        }else if(stage==3&&mode==1&&GetTickCount64()-at>=100){
            Check(app->CalendarRunning(),"calendar remains alive after a background message cycle");Record("calendar-after-clock-close");app->OnTray(platform::windows::TrayAction::Exit);stage=4;
        }
    }
};
Probe* active=nullptr;
void CALLBACK Timer(HWND,UINT,UINT_PTR,DWORD){try{if(active)active->Tick();}catch(const std::exception& e){Check(false,e.what());if(active)active->app->Exit();}}
void WaitChildren(Probe& p){const auto end=GetTickCount64()+12000;while(p.descendants.Live()&&GetTickCount64()<end){p.descendants.Observe();MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}}MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);}Check(p.descendants.Live()==0,"all retained WebView child identities exited");p.Record("application-exited");}
void Scenario(const std::filesystem::path& root,int mode){
    const auto dir=root/std::to_wstring(mode);std::filesystem::create_directories(dir);
    platform::windows::LocalSettingsFileStore files;calendar::CalendarPreferencesStore calendarStore(files);
    calendar::Preferences prefs;prefs.enabled=true;prefs.material=calendar::Material::FrostedGlass;prefs.profiles[0]={20,20,600,400,true};
    const auto calendarPath=(dir/L"calendar-preferences.json").u8string();Check(calendarStore.Save(calendarPath,prefs),"seed isolated calendar");
    settings::JsonSettingsCodec codec;settings::SettingsStore clockStore(files,codec);settings::SettingsSnapshot settings;settings.clock.enabled=mode==1||mode==4;
    settings.clock.profiles[1].x=321;settings.clock.profiles[1].y=123;
    const auto clockPath=(dir/L"ui-preferences.json").u8string();Check(clockStore.Save(clockPath,settings),"seed isolated clock");
    std::string calendarBefore;files.Read(calendarPath,calendarBefore);
    Probe probe;probe.mode=mode;probe.calendarPath=calendarPath;probe.clockPath=clockPath;probe.log.open(dir/L"lifecycle.jsonl");probe.deadline=GetTickCount64()+75000;
    DesktopApplicationOptions options;options.clockPreferencesPath=clockPath;options.calendarPreferencesPath=calendarPath;options.verification=true;
    options.startHidden=mode==7;
    options.onMessage=[&](const auto& value){probe.Message(value);};
    if(mode==2||mode==3)options.readyScript=L"chrome.webview.postMessage('v1|calendar|highlightColor')";
    DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));probe.app=&app;
    if(mode!=7)app.Open(ManagerPage::Calendar);
    active=&probe;const auto timer=SetTimer(nullptr,0,40,Timer);Check(timer!=0,"start own lifecycle timer");
    const int result=app.Run();KillTimer(nullptr,timer);active=nullptr;
    MSG quit{};Check(!PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE),"application consumes its own quit before next independent instance");
    Check(result==0,"actual desktop application run exits cleanly");Check(!app.ClockRunning()&&!app.CalendarRunning()&&!app.ManagerHandle(),"all owned UI cleaned after run");
    if(mode<2)Check(probe.ready==3,"three manager open/close rounds completed");
    else if(mode==4)Check(probe.stage==3,"clock native menu editor reached tray exit");
    else Check(probe.stage>=1,"real page protocol opened before close or exit");
    WaitChildren(probe);
    if(mode==1){const auto saved=clockStore.Load(clockPath);Check(saved.schemaVersion==3&&saved.clock.profiles[1].x==321&&saved.clock.profiles[1].y==123,"clock save retains unrelated schema3 theme profile fields");}
    if(mode==3){std::string after;files.Read(calendarPath,after);Check(after==calendarBefore,"tray exit during real WebView modal never persists color");}
    std::cout<<"desktop scenario "<<mode<<" ready="<<probe.ready<<" childrenTracked="<<probe.descendants.Count()<<" childrenAlive="<<probe.descendants.Live()<<" log="<<(dir/L"lifecycle.jsonl").u8string()<<std::endl;
}
}
int main(int argc,char** argv){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);const auto root=std::filesystem::path(exe).parent_path()/L"desktop-application-artifacts"/std::to_wstring(GetCurrentProcessId());
    const int first=argc>1?std::atoi(argv[1]):0;
    for(int mode=first;mode<(argc>1?first+1:8)&&passed;++mode)Scenario(root,mode);
    return passed?0:1;
}
