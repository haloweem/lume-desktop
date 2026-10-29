#include "apps/shell/desktop_application.h"
#include "core/wallpaper/library.h"
#include "platform/windows/mpv_process_controller.h"
#include "vendor/nlohmann/json.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
using Json=nlohmann::json;
namespace fs=std::filesystem;
bool passed=true;
void Check(bool value,const char* reason) {
    std::cout<<(value?"PASS: ":"FAIL: ")<<reason<<std::endl;
    if(!value)passed=false;
}
std::string Utf8(const std::wstring& value) {
    const auto count=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string out(static_cast<std::size_t>((std::max)(count,0)),0);
    if(count)WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),count,nullptr,nullptr);
    return out;
}
std::wstring Wide(const std::string& value) {
    const auto count=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring out(static_cast<std::size_t>((std::max)(count,0)),0);
    if(count)MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),count);
    return out;
}
struct Child {
    DWORD id=0,parent=0;HANDLE process=nullptr;std::wstring name;
    std::uint64_t created=0;
};
std::uint64_t CreatedAt(HANDLE process) {
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(process,&created,&exited,&kernel,&user))return 0;
    return (static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}
class Children {
public:
    ~Children(){for(auto& child:children_)CloseHandle(child.process);}
    void Observe() {
        const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot==INVALID_HANDLE_VALUE)return;
        std::vector<PROCESSENTRY32W> entries;
        PROCESSENTRY32W item{};item.dwSize=sizeof(item);
        if(Process32FirstW(snapshot,&item))do{entries.push_back(item);}while(Process32NextW(snapshot,&item));
        CloseHandle(snapshot);
        std::set<DWORD> parents{GetCurrentProcessId()};
        for(const auto& child:children_)if(WaitForSingleObject(child.process,0)==WAIT_TIMEOUT)parents.insert(child.id);
        bool added=true;
        while(added) {
            added=false;
            for(const auto& entry:entries) {
                if(!parents.count(entry.th32ParentProcessID)||parents.count(entry.th32ProcessID))continue;
                const auto process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
                if(!process)continue;
                children_.push_back({entry.th32ProcessID,entry.th32ParentProcessID,process,entry.szExeFile,CreatedAt(process)});
                parents.insert(entry.th32ProcessID);added=true;
            }
        }
    }
    std::size_t Live(const wchar_t* name=nullptr)const {
        return static_cast<std::size_t>(std::count_if(children_.begin(),children_.end(),[&](const auto& child){
            return (!name||_wcsicmp(child.name.c_str(),name)==0)&&WaitForSingleObject(child.process,0)==WAIT_TIMEOUT;
        }));
    }
    Json State()const {
        Json out=Json::array();
        for(const auto& child:children_)out.push_back({{"pid",child.id},{"parent",child.parent},
            {"name",Utf8(child.name)},{"createdFileTime",child.created},
            {"alive",WaitForSingleObject(child.process,0)==WAIT_TIMEOUT}});
        return out;
    }
    std::vector<DWORD> LivePlayerIds()const {
        std::vector<DWORD> result;
        for(const auto& child:children_)
            if(_wcsicmp(child.name.c_str(),L"mpv.exe")==0&&WaitForSingleObject(child.process,0)==WAIT_TIMEOUT)
                result.push_back(child.id);
        return result;
    }
    bool TerminateOwnedPlayer(HWND parent,const fs::path& executable,DWORD& id,std::uint64_t& created)const {
        std::set<DWORD> embedded;
        EnumChildWindows(parent,[](HWND window,LPARAM data)->BOOL {
            DWORD pid=0;GetWindowThreadProcessId(window,&pid);
            if(pid&&pid!=GetCurrentProcessId())reinterpret_cast<std::set<DWORD>*>(data)->insert(pid);
            return TRUE;
        },reinterpret_cast<LPARAM>(&embedded));
        if(embedded.size()!=1)return false;
        for(const auto& child:children_) {
            if(child.parent!=GetCurrentProcessId()||!embedded.count(child.id)||
                _wcsicmp(child.name.c_str(),L"mpv.exe")!=0||!child.created||
                WaitForSingleObject(child.process,0)!=WAIT_TIMEOUT)continue;
            const auto process=OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,child.id);
            if(!process)return false;
            std::array<wchar_t,32768> image{};DWORD length=static_cast<DWORD>(image.size());
            std::error_code error;
            const bool owned=CreatedAt(process)==child.created&&
                QueryFullProcessImageNameW(process,0,image.data(),&length)&&
                fs::equivalent(fs::path(image.data()),executable,error)&&!error;
            const bool terminated=owned&&TerminateProcess(process,31)&&WaitForSingleObject(process,1500)==WAIT_OBJECT_0;
            CloseHandle(process);
            if(terminated){id=child.id;created=child.created;}
            return terminated;
        }
        return false;
    }
private:std::vector<Child> children_;
};
struct Probe {
    lume::shell::DesktopApplication* app=nullptr;
    Children children;
    std::ofstream log;
    Json screen;
    std::string display,first,second;
    ULONGLONG deadline=0,at=0;
    unsigned dwell=1000;
    int stage=0;
    bool sawFirst=false,sawPause=false,sawSecond=false;
    bool faultRecovery=false,faultInjected=false,sawFaultFailed=false,faultRecovered=false;
    bool recordedFaultWait=false;
    ULONGLONG faultCoverAt=0,faultFitAt=0,faultAt=0;
    unsigned faultWaitingTicks=0;
    DWORD failedPlayer=0;
    std::uint64_t failedPlayerCreated=0;
    HWND wallpaperParent=nullptr;
    fs::path playerExecutable;
    void Record(const char* phase) {
        children.Observe();
        log<<Json{{"phase",phase},{"pid",GetCurrentProcessId()},{"tickMs",GetTickCount64()},
            {"screen",screen},{"children",children.State()},
            {"faultRecovery",faultRecovery},{"faultInjected",faultInjected},
            {"failedPlayer",failedPlayer},{"failedPlayerCreatedFileTime",failedPlayerCreated},
            {"faultCoverTickMs",faultCoverAt},{"faultFitTickMs",faultFitAt},{"faultTickMs",faultAt},
            {"sawFaultFailed",sawFaultFailed},{"faultWaitingTicks",faultWaitingTicks},
            {"faultRecovered",faultRecovered}}.dump()<<'\n';
        log.flush();
    }
    void Message(const std::wstring& text) {
        const auto json=Json::parse(Utf8(text),nullptr,false);
        if(!json.is_object()||json.value("type",std::string{})!="wallpaper-state")return;
        for(const auto& value:json["displays"])if(value["id"]==display)screen=value;
        if(screen.is_null())return;
        const auto id=screen.value("wallpaperId",std::string{});
        const auto state=screen.value("state",std::string{});
        if(id==first&&state=="playing")sawFirst=true;
        if(id==first&&state=="paused")sawPause=true;
        if(id==second&&state=="playing")sawSecond=true;
        if(faultRecovery&&stage==1&&id==second&&state=="playing") {
            if(screen.value("fit",std::string{})=="cover"&&!faultCoverAt)faultCoverAt=GetTickCount64();
            if(screen.value("fit",std::string{})=="contain"&&faultCoverAt&&!faultFitAt)faultFitAt=GetTickCount64();
        }
        if(faultInjected&&id==second&&state=="failed")sawFaultFailed=true;
    }
    void Tick() {
        if(GetTickCount64()>deadline){Check(false,"real desktop wallpaper deadline");app->Exit();return;}
        children.Observe();
        if(stage==0&&app->ManagerReady()&&sawFirst&&sawPause&&sawSecond&&
            screen.value("fit",std::string{})=="contain") {
            Check(children.Live(L"mpv.exe")==1,"real WebView commands start exactly one owned player");
            Record("manager-playing");stage=1;at=GetTickCount64();
        }else if(stage==1&&((!faultRecovery&&GetTickCount64()-at>=dwell)||(faultRecovery&&faultFitAt))) {
            if(faultRecovery) {
                Check(faultFitAt>=faultCoverAt&&faultFitAt-faultCoverAt<1500,
                    "fresh cover/contain command pair rearms retry cooldown after the resource dwell");
                Record("fault-recovery-armed");
            }
            app->CloseManager();stage=2;at=GetTickCount64();
        }else if(stage==2&&!app->ManagerHandle()&&!children.Live(L"msedgewebview2.exe")) {
            Check(children.Live(L"mpv.exe")==1,"closing manager releases WebView but keeps wallpaper playing");
            Record("manager-closed-playing");
            if(faultRecovery) {
                // Kill only after WebView exits and well before the five-second
                // cooldown. A slow close must fail this timing gate instead of
                // silently testing an immediately recoverable crash.
                const bool inCooldown=faultCoverAt&&faultFitAt&&GetTickCount64()-faultCoverAt<2000;
                Check(inCooldown,"manager fully closes while the freshly reset five-second retry cooldown remains active");
                if(!inCooldown){Record("fault-timing-failed");app->Exit();return;}
                stage=20;faultAt=GetTickCount64();
                faultInjected=children.TerminateOwnedPlayer(wallpaperParent,playerExecutable,failedPlayer,failedPlayerCreated);
                Check(faultInjected,"fault injection matches retained child handle, creation time, executable and owned parent");
                Record("fault-injected-manager-closed");
                if(!faultInjected){app->Exit();return;}
            }else {stage=3;at=GetTickCount64();}
        }else if(stage==20) {
            if(sawFaultFailed) {
                ++faultWaitingTicks;
                if(!recordedFaultWait) {
                    Check(!app->ManagerHandle()&&!children.Live(L"msedgewebview2.exe")&&!app->ClockRunning()&&!app->CalendarRunning(),
                        "Shell message loop remains alive during failed wallpaper retry without a manager or native widgets");
                    recordedFaultWait=true;Record("fault-recovery-waiting");
                }
            }
            const auto players=children.LivePlayerIds();
            if(players.size()==1&&players.front()!=failedPlayer&&
                screen.value("wallpaperId",std::string{})==second&&screen.value("state",std::string{})=="playing") {
                Check(sawFaultFailed&&faultWaitingTicks>0,"Shell survives the failed state until delayed automatic recovery");
                Check(screen.value("fit",std::string{})=="contain","delayed recovery preserves the selected wallpaper fit");
                Check(!app->ManagerHandle()&&!children.Live(L"msedgewebview2.exe"),
                    "new player PID recovers before reopening the management page");
                faultRecovered=true;Record("fault-recovered-playing");stage=3;at=GetTickCount64();
            }else if(GetTickCount64()-faultAt>15000) {
                Check(false,"delayed automatic recovery completes within fifteen seconds");
                Record("fault-recovery-timeout");app->Exit();
            }
        }else if(stage==3&&GetTickCount64()-at>=dwell) {
            app->Open(lume::shell::ManagerPage::Home);stage=4;
        }else if(stage==4&&app->ManagerReady()) {
            Check(screen.value("wallpaperId",std::string{})==second&&screen.value("fit",std::string{})=="contain",
                "reopened manager receives actual persisted wallpaper state");
            Record("manager-reopened-playing");
            app->Dispatch(lume::ui::PageCommand::ClockEnable);
            app->Dispatch(lume::ui::PageCommand::CalendarEnable);
            app->Dispatch(lume::ui::PageCommand::PerformanceEnable);
            app->CloseManager();stage=5;
        }else if(stage==5&&!app->ManagerHandle()&&!children.Live(L"msedgewebview2.exe")) {
            Check(app->ClockRunning()&&app->CalendarRunning()&&children.Live(L"mpv.exe")==1,
                "wallpaper continues with independent native clock/calendar/performance components");
            Record("components-playing");stage=6;at=GetTickCount64();
        }else if(stage==6&&GetTickCount64()-at>=dwell) {
            Record("tray-exit");stage=7;app->OnTray(lume::platform::windows::TrayAction::Exit);
        }
    }
};
Probe* active=nullptr;
void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD) {
    try{if(active)active->Tick();}
    catch(const std::exception& error){Check(false,error.what());if(active)active->app->Exit();}
}
void Pump(unsigned milliseconds) {
    const auto until=GetTickCount64()+milliseconds;
    do {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        Sleep(10);
    }while(GetTickCount64()<until);
}
}
int main(int argc,char** argv) {
    if(argc<4||argc>6||(argc==6&&std::string(argv[5])!="--fault-recovery")){
        std::cerr<<"Usage: lume-desktop-wallpaper-test mpv.exe video-or-protected-library fresh-output-directory [dwell-ms] [--fault-recovery]\n";
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto root=fs::absolute(fs::u8path(argv[3]));
    if(fs::exists(root)){std::cerr<<"Output directory must be new\n";return 2;}
    fs::create_directories(root);
    const auto displays=lume::platform::windows::EnumerateMpvDisplays();
    if(displays.empty()){Check(false,"actual monitor available");return 1;}
    lume::wallpaper::Library library((root/"wallpapers").u8string());
    Check(library.Load().ok,"create isolated wallpaper library");
    const auto fixture=fs::absolute(fs::u8path(argv[2]));
    lume::wallpaper::LibraryResult first,second;
    Json fixtureEvidence={{"source",fixture.u8string()},{"offscreen",true}};
    if(fs::is_directory(fixture)) {
        lume::wallpaper::Library source(fixture.u8string());
        Check(source.Load().ok,"load verified encrypted official fixture metadata");
        const auto entry=std::find_if(source.Items().begin(),source.Items().end(),[](const auto& item){
            return item.protectedMedia&&item.item.kind==lume::wallpaper::MediaKind::Video;
        });
        if(entry==source.Items().end()){Check(false,"official fixture contains an encrypted video");return 1;}
        fs::create_directories(fs::u8path(library.Root())/"media");
        fs::copy_file(fixture/"cache-key.bin",fs::u8path(library.Root())/"cache-key.bin");
        auto item=entry->item;
        const auto firstPath=fs::u8path(library.Root())/"media"/(item.id+".apk");
        fs::copy_file(fs::u8path(entry->localPath),firstPath);
        first=library.RegisterDownload(item,firstPath.u8string());
        item.id="verified-switch";
        const auto secondPath=fs::u8path(library.Root())/"media"/(item.id+".apk");
        fs::copy_file(fs::u8path(entry->localPath),secondPath);
        second=library.RegisterDownload(item,secondPath.u8string());
        fixtureEvidence["encrypted"]=true;fixtureEvidence["width"]=item.width;
        fixtureEvidence["height"]=item.height;fixtureEvidence["mediaBytes"]=item.sizeBytes;
    }else {
        first=library.ImportLocal(fixture.u8string());second=library.ImportLocal(fixture.u8string());
        fixtureEvidence["encrypted"]=false;
    }
    Check(first.ok&&second.ok,"import real media for desktop integration");
    if(!passed)return 1;
    const auto& bounds=displays.front().bounds;
    fixtureEvidence["viewportWidth"]=bounds.right-bounds.left;
    fixtureEvidence["viewportHeight"]=bounds.bottom-bounds.top;
    std::ofstream(root/"fixture.json")<<fixtureEvidence.dump(2);
    const auto parent=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Owned wallpaper verification parent",
        WS_POPUP,-30000,-30000,bounds.right-bounds.left,bounds.bottom-bounds.top,
        nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(parent!=nullptr,"create isolated offscreen parent without changing Explorer");
    if(!parent)return 1;
    ShowWindow(parent,SW_SHOWNOACTIVATE);
    Probe probe;probe.display=std::to_string(displays.front().id);probe.first=first.id;probe.second=second.id;
    if(argc>=5)probe.dwell=static_cast<unsigned>((std::max)(1000,(std::min)(30000,std::atoi(argv[4]))));
    probe.faultRecovery=argc==6;probe.wallpaperParent=parent;
    probe.playerExecutable=fs::absolute(fs::u8path(argv[1]));
    probe.log.open(root/"lifecycle.jsonl");probe.deadline=GetTickCount64()+60000+4*probe.dwell+(probe.faultRecovery?20000:0);
    lume::shell::DesktopApplicationOptions options;
    options.verification=true;
    options.clockPreferencesPath=(root/"clock.json").u8string();
    options.calendarPreferencesPath=(root/"calendar.json").u8string();
    options.performancePreferencesPath=(root/"performance.json").u8string();
    options.wallpaperOptions={library.Root(),fs::absolute(fs::u8path(argv[1])).wstring(),true,parent};
    options.onMessage=[&](const auto& message){probe.Message(message);};
    const auto configuration=Json{{"display",probe.display},{"first",first.id},{"second",second.id},
        {"faultRecovery",probe.faultRecovery},{"dwell",probe.dwell}}.dump();
    options.readyScript=Wide("(()=>{const config="+configuration+R"JS(;
      let phase=0,arm=0;
      const send=(action,fields={})=>chrome.webview.postMessage({version:1,scope:"wallpaper",action,...fields});
      chrome.webview.addEventListener("message",event=>{
        const state=event.data;if(state?.type!=="wallpaper-state")return;
        const screen=state.displays.find(item=>item.id===config.display);if(!screen)return;
        if(config.faultRecovery&&arm===2&&screen.wallpaperId===config.second&&screen.state==="playing"&&screen.fit==="cover"){
          arm=3;send("fit",{display:config.display,value:"contain"});return;
        }
        if(config.faultRecovery&&arm===3&&screen.wallpaperId===config.second&&screen.state==="playing"&&screen.fit==="contain"){
          arm=4;return;
        }
        if(screen.wallpaperId===config.second&&screen.state==="playing"&&screen.fit==="contain"){
          phase=9;
          if(config.faultRecovery&&arm===0){
            arm=1;setTimeout(()=>{arm=2;send("fit",{display:config.display,value:"cover"});},config.dwell);
          }
          return;
        }
        if(phase===0){phase=1;send("apply",{id:config.first,display:config.display});}
        else if(phase===1&&screen.wallpaperId===config.first&&screen.state==="playing"){
          phase=2;send("pause",{display:config.display});
        }else if(phase===2&&screen.state==="paused"){phase=3;send("apply",{id:config.second,display:config.display});}
        else if(phase===3&&screen.wallpaperId===config.second&&screen.state==="playing"){
          phase=4;send("fit",{display:config.display,value:"contain"});
        }
      });
      send("snapshot");
    })();)JS");
    lume::shell::DesktopApplication app(GetModuleHandleW(nullptr),std::move(options));probe.app=&app;
    active=&probe;const auto timer=SetTimer(nullptr,0,50,Tick);
    Check(timer!=0,"start integration observer");
    app.Open(lume::shell::ManagerPage::Home);
    const auto result=app.Run();
    KillTimer(nullptr,timer);active=nullptr;
    Check(result==0&&probe.stage==7,"complete WebView playback and native component lifecycle");
    if(probe.faultRecovery)
        Check(probe.faultInjected&&probe.sawFaultFailed&&probe.faultWaitingTicks>0&&probe.faultRecovered,
            "complete closed-manager retry-cooldown crash regression");
    const auto until=GetTickCount64()+8000;
    do{probe.children.Observe();if(!probe.children.Live())break;Pump(50);}while(GetTickCount64()<until);
    Check(!probe.children.Live(),"tray exit releases complete observed WebView and mpv process tree");
    Check(!app.ManagerHandle()&&!app.ClockRunning()&&!app.CalendarRunning()&&GetWindow(parent,GW_CHILD)==nullptr,
        "tray exit releases every owned manager/component/wallpaper window");
    probe.Record("all-exited");
    DestroyWindow(parent);
    return passed?0:1;
}
