#include "apps/shell/calendar_session.h"
#include "platform/windows/local_settings_file_store.h"
#include <windows.h>
#include <filesystem>
#include <functional>
#include <iostream>

namespace {
using namespace lume;
using namespace lume::shell;
using P = ui::PageCommand;
bool passed = true;
void Check(bool ok, const char* message) { if (!ok) { passed=false; std::cerr<<"FAIL: "<<message<<'\n'; } }
bool Has(const std::optional<std::wstring>& text, const wchar_t* part) { return text && text->find(part)!=std::wstring::npos; }
void Pump() { MSG m{}; while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) { if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);} } }
HWND OwnPicker(HWND owner) {
    struct Search { HWND owner, found=nullptr; } search{owner};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL {
        auto& s=*reinterpret_cast<Search*>(data); wchar_t name[80]{}; GetClassNameW(hwnd,name,80);
        if(std::wstring(name)==L"LumeWidgetColorPickerWindow" && GetWindow(hwnd,GW_OWNER)==s.owner) { s.found=hwnd;return FALSE; }return TRUE;
    },reinterpret_cast<LPARAM>(&search)); return search.found;
}
std::function<void(HWND)> editorAction;
HWND editorOwner=nullptr;
ULONGLONG deadline=0;
void CALLBACK EditTimer(HWND,UINT,UINT_PTR id,DWORD) {
    try {
    const HWND picker=OwnPicker(editorOwner);
    if(!picker && GetTickCount64()<deadline) return;
    KillTimer(nullptr,id);
    Check(picker!=nullptr,"real native color picker appeared");
    if(picker) editorAction(picker); else PostQuitMessage(1);
    } catch(const std::exception& error) { Check(false,error.what());PostQuitMessage(1); }
}
std::optional<std::wstring> Edit(CalendarSession& session,HWND owner,P command,std::function<void(HWND)> action) {
    editorOwner=owner; editorAction=std::move(action);deadline=GetTickCount64()+5000;
    Check(SetTimer(nullptr,0,20,EditTimer)!=0,"start isolated modal driver");
    auto result=session.HandlePage(command,owner);editorAction={};return result;
}
void Accept(HWND picker) {
    SetWindowTextW(GetDlgItem(picker,5102),L"17");SetWindowTextW(GetDlgItem(picker,5103),L"34");
    SetWindowTextW(GetDlgItem(picker,5104),L"51");SetWindowTextW(GetDlgItem(picker,5105),L"68");
    SendMessageW(picker,WM_KEYDOWN,VK_RETURN,0);
}
HWND Owner() { return CreateWindowExW(0,L"STATIC",L"isolated calendar session owner",WS_OVERLAPPED,0,0,320,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr); }
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto root=std::filesystem::path(exe).parent_path()/L"calendar-session-artifacts"/std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    const auto path=(root/L"calendar-preferences.json").u8string();
    platform::windows::LocalSettingsFileStore files;
    const auto instance=GetModuleHandleW(nullptr);
    int notifications=0,settingsOpened=0;
    {
        CalendarSession session(instance,path,[&](const std::wstring&,bool){++notifications;},[&]{++settingsOpened;});
        Check(session.Restore() && !session.Running(),"fresh disabled session restores");
        Check(!files.Exists(path) && session.LoadStatus()==calendar::LoadStatus::Fresh,"startup never saves defaults");
        Check(Has(session.HandlePage(P::Ready,nullptr),L"\"result\":\"snapshot\""),"ready snapshot");
        Check(!session.HandlePage(P::ClockEnable,nullptr),"clock command is outside calendar boundary");
        Check(Has(session.HandlePage(P::CalendarEnable,nullptr),L"\"saved\":true") && session.Running(),"enable actual native calendar and persist");Pump();
        Check(session.LoadStatus()==calendar::LoadStatus::Loaded,"only explicit successful save clears load status");
        Check(Has(session.HandlePage(P::CalendarSplit,nullptr),L"\"theme\":\"split\""),"switch split");Pump();
        Check(Has(session.HandlePage(P::CalendarSpacious,nullptr),L"\"theme\":\"spacious\""),"switch spacious");Pump();
        const auto before=session.Snapshot(); std::string bytes;files.Read(path,bytes);
        const auto blocked=std::filesystem::u8path(path+".tmp");std::filesystem::create_directory(blocked);
        Check(Has(session.HandlePage(P::CalendarSplit,nullptr),L"\"error\":\"save\""),"real directory obstruction fails save");
        Check(session.Snapshot().theme==before.theme && session.Running(),"save failure rolls back actual runtime");
        std::string retained;files.Read(path,retained);Check(retained==bytes,"failed write retains main file bytes");
        std::filesystem::remove(blocked);
        HWND owner=Owner();
        const int notificationsBefore=notifications;
        auto cancelled=Edit(session,owner,P::CalendarTextColor,[&](HWND picker){
            Check(!session.HandlePage(P::CalendarHighlightColor,owner),"modal reentry has no response or second editor");
            session.HandleNative({calendar::ActionType::Bounds,5,6,600,400});
            Check(notifications==notificationsBefore,"modal bounds cannot acknowledge pending page command");
            SendMessageW(picker,WM_KEYDOWN,VK_ESCAPE,0);
        });
        Check(Has(cancelled,L"\"result\":\"cancelled\"") && Has(cancelled,L"\"error\":\"save\""),"cancel preserves previous error");
        files.Read(path,retained);Check(retained==bytes,"cancel and suppressed bounds perform no save");
        auto accepted=Edit(session,owner,P::CalendarTextColor,Accept);
        Check(Has(accepted,L"#11223344") && Has(accepted,L"\"saved\":true"),"native RGBA confirmation persists exact channels");
        std::filesystem::create_directory(blocked);
        auto failed=Edit(session,owner,P::CalendarHighlightColor,Accept);
        Check(Has(failed,L"\"error\":\"save\"") && session.Snapshot().highlightColor.rgb==0x0a84ff,"native RGBA failed save rolls back");
        std::filesystem::remove(blocked);files.Read(path,bytes);
        auto destroyed=Edit(session,owner,P::CalendarHighlightColor,[&](HWND){DestroyWindow(owner);});
        Check(Has(destroyed,L"\"result\":\"cancelled\""),"owner destruction cancels native editor");
        files.Read(path,retained);Check(bytes==retained,"destroyed owner never saves");
        owner=Owner();
        auto closing=Edit(session,owner,P::CalendarHighlightColor,[&](HWND picker){session.Shutdown();Accept(picker);});
        Check(Has(closing,L"\"result\":\"cancelled\"") && !session.Running(),"shutdown epoch discards accepted modal result");
        files.Read(path,retained);Check(bytes==retained,"shutdown does not save accepted result");DestroyWindow(owner);Pump();
    }
    {
        CalendarSession reloaded(instance,path,{},{});Check(reloaded.Restore() && reloaded.Running(),"new session rebuilds persisted actual runtime");
        Check(reloaded.Snapshot().textColor.rgb==0x112233 && reloaded.Snapshot().textColor.alpha==68,"reopened RGBA");
        reloaded.Shutdown();
    }
    files.Write(path,"bad-primary");
    {
        CalendarSession recovered(instance,path,{},{});recovered.Restore();
        Pump();
        Check(recovered.LoadStatus()==calendar::LoadStatus::RecoveredBackup && files.Exists(path+".corrupt") && !files.Exists(path),"backup startup isolates bad main without replacing it");
        HWND owner=Owner();auto cancelled=Edit(recovered,owner,P::CalendarTextColor,[](HWND picker){SendMessageW(picker,WM_CLOSE,0,0);});
        Check(Has(cancelled,L"\"loadStatus\":\"recovered-backup\""),"cancel retains recovered notice");
        recovered.HandleNative({calendar::ActionType::Bounds,41,42,620,420});
        Check(recovered.LoadStatus()==calendar::LoadStatus::Loaded && files.Exists(path),"user bounds saves recovery and clears notice");
        Check(Has(recovered.HandlePage(P::CalendarDisable,nullptr),L"\"loadStatus\":\"loaded\""),"explicit save remains loaded");DestroyWindow(owner);
    }
    files.Write(path,"bad-main-again");files.Write(path+".bak","bad-backup");
    {
        CalendarSession failed(instance,path,{},{});Check(failed.Restore(),"failed load can restore disabled safe runtime");
        Check(failed.LoadStatus()==calendar::LoadStatus::Failed && !files.Exists(path),"failed load never overwrites corrupt evidence");
        Check(failed.State().find(L"\"error\":\"load\"")!=std::wstring::npos,"load failure visible to page");
        Check(Has(failed.HandlePage(P::CalendarSplit,nullptr),L"\"loadStatus\":\"loaded\""),"explicit save repairs failed configuration");
    }
    // A loaded file lacking position still receives the runtime's posted initial
    // bounds, whereas backup recovery remains read-only until a user action.
    calendar::CalendarPreferencesStore seed(files);calendar::Preferences noPosition;noPosition.enabled=true;
    const auto automaticPath=(root/L"automatic-bounds.json").u8string();Check(seed.Save(automaticPath,noPosition),"seed loaded automatic position case");
    {
        CalendarSession session(instance,automaticPath,{},{});Check(session.Restore(),"restore loaded unpositioned native calendar");Pump();
        Check(session.Snapshot().Active()->hasPosition && seed.Load(automaticPath).value.Active()->hasPosition,"loaded startup automatic bounds persists position");
    }
    Check(seed.Save(automaticPath,noPosition),"seed recovery automatic position case");
    files.Write(automaticPath+".bak",[&]{std::string out;calendar::CalendarPreferencesCodec c;c.Encode(noPosition,out);return out;}());files.Write(automaticPath,"bad");
    {
        CalendarSession session(instance,automaticPath,{},{});Check(session.Restore(),"restore recovered unpositioned native calendar");Pump();
        Check(session.LoadStatus()==calendar::LoadStatus::RecoveredBackup&&!files.Exists(automaticPath),"recovered startup automatic bounds never recreates main file");
        session.HandleNative({calendar::ActionType::Bounds,30,40,600,400});
        Check(session.LoadStatus()==calendar::LoadStatus::Loaded&&files.Exists(automaticPath),"user bounds after ignored startup placement saves recovery");
    }
    const auto local=root/L"local-app-data";std::filesystem::create_directories(local);
    wchar_t original[32768]{};const DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",original,32768);
    SetEnvironmentVariableW(L"LOCALAPPDATA",local.c_str());
    Check(platform::windows::GetCalendarPreferencesPath()==(local/L"Lume"/L"calendar-preferences.json").u8string(),"calendar path isolated from unchanged clock path");
    Check(platform::windows::GetUiPreferencesPath()==(local/L"Lume"/L"ui-preferences.json").u8string(),"clock filename unchanged");
    SetEnvironmentVariableW(L"LOCALAPPDATA",length?original:nullptr);
    std::cout<<"calendar_session_test artifacts="<<root.u8string()<<" result="<<(passed?"passed":"failed")<<'\n';
    return passed?0:1;
}
