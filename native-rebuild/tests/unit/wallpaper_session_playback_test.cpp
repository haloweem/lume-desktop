#include "apps/shell/wallpaper_session.h"
#include "core/wallpaper/library.h"
#include "core/wallpaper/playback_preferences.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/mpv_process_controller.h"
#include "vendor/nlohmann/json.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <winioctl.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
using Json=nlohmann::json;
using Session=lume::shell::WallpaperSession;
using Action=lume::ui::WallpaperAction;
namespace fs=std::filesystem;
int failures=0;
Json evidence={{"verification_only",true},{"checks",Json::array()}};
fs::path phaseOutput;
DWORD resourceHoldMs=0;
std::map<DWORD,HANDLE> ownedHandles;
void Expect(bool pass,const char* label) {
    std::cout<<(pass?"PASS: ":"FAIL: ")<<label<<std::endl;
    if(!pass)++failures;
    evidence["checks"].push_back({{"check",label},{"passed",pass}});
}
std::string Utf8(const std::wstring& value) {
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),
        static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(static_cast<std::size_t>((std::max)(0,size)),0);
    if(size>0)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),
        static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);
    return result;
}
void Pump(DWORD duration) {
    const auto end=GetTickCount64()+duration;
    do {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        Sleep(5);
    }while(GetTickCount64()<end);
}
bool Wait(const std::function<bool()>& condition,DWORD timeout=12000) {
    const auto end=GetTickCount64()+timeout;
    do {Pump(5);if(condition())return true;}while(GetTickCount64()<end);
    return false;
}
Json State(Session& session) {
    const auto text=Utf8(session.State());
    return text.empty()?Json{}:Json::parse(text,nullptr,false);
}
Json Screen(Session& session,std::uint64_t display) {
    const auto state=State(session);
    if(state.is_object()&&state.contains("displays")&&state["displays"].is_array())
        for(const auto& item:state["displays"])if(item.value("id",std::string{})==std::to_string(display))return item;
    return {};
}
bool HasScreenState(Session& session,std::uint64_t display,const std::string& state) {
    const auto screen=Screen(session,display);
    return screen.is_object()&&screen.value("state",std::string{})==state;
}
std::vector<DWORD> EmbeddedPlayers(HWND parent) {
    std::vector<DWORD> result;
    EnumChildWindows(parent,[](HWND child,LPARAM context)->BOOL {
        DWORD pid=0;GetWindowThreadProcessId(child,&pid);
        auto& values=*reinterpret_cast<std::vector<DWORD>*>(context);
        if(pid!=GetCurrentProcessId()&&std::find(values.begin(),values.end(),pid)==values.end())values.push_back(pid);
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
std::vector<DWORD> OwnedProcesses() {
    std::vector<DWORD> result;
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return {MAXDWORD};
    std::vector<std::pair<DWORD,DWORD>> processes;
    PROCESSENTRY32W process{};process.dwSize=sizeof(process);
    if(Process32FirstW(snapshot,&process))do {
        processes.push_back({process.th32ProcessID,process.th32ParentProcessID});
    }while(Process32NextW(snapshot,&process));
    CloseHandle(snapshot);
    std::set<DWORD> descendants{GetCurrentProcessId()};
    for(auto it=ownedHandles.begin();it!=ownedHandles.end();) {
        if(WaitForSingleObject(it->second,0)==WAIT_OBJECT_0) {
            CloseHandle(it->second);it=ownedHandles.erase(it);
        }else {descendants.insert(it->first);++it;}
    }
    bool changed=true;
    while(changed) {
        changed=false;
        for(const auto& item:processes)if(descendants.count(item.second)&&!descendants.count(item.first)) {
            descendants.insert(item.first);changed=true;
        }
    }
    for(const auto pid:descendants)if(pid!=GetCurrentProcessId()) {
        if(!ownedHandles.count(pid)) {
            const auto handle=OpenProcess(SYNCHRONIZE,FALSE,pid);
            if(handle)ownedHandles.emplace(pid,handle);
        }
        result.push_back(pid);
    }
    return result;
}
void PublishPhase(const Json& value) {
    if(!phaseOutput.empty()) {
        const auto temporary=phaseOutput.wstring()+L".tmp";
        {std::ofstream stream{fs::path(temporary)};stream<<value.dump(2);}
        Expect(MoveFileExW(temporary.c_str(),phaseOutput.c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE,
            "resource phase marker is atomically published");
    }
}
void Phase(const char* name,Session* session=nullptr) {
    Json value={{"phase",name},{"tick_ms",GetTickCount64()},{"root_pid",GetCurrentProcessId()},
        {"owned_pids",OwnedProcesses()},{"state",session?State(*session):Json{}}};
    evidence["phases"].push_back(value);
    PublishPhase(value);
    if(resourceHoldMs) {
        Pump(resourceHoldMs);
        // Subsequent crash injection, switching and cleanup must not pollute
        // the steady-state resource samples under the preceding phase name.
        value["phase"]="transition";value["tick_ms"]=GetTickCount64();PublishPhase(value);
    }
}
bool TerminateOwnedPlayer(DWORD pid,HWND parent,const fs::path& executable) {
    const auto embedded=EmbeddedPlayers(parent);
    const auto owned=OwnedProcesses();
    if(std::find(embedded.begin(),embedded.end(),pid)==embedded.end()||
        std::find(owned.begin(),owned.end(),pid)==owned.end())return false;
    const auto handle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,pid);
    if(!handle)return false;
    std::array<wchar_t,32768> path{};DWORD length=static_cast<DWORD>(path.size());
    std::error_code ec;
    const bool matches=QueryFullProcessImageNameW(handle,0,path.data(),&length)&&
        fs::equivalent(fs::path(path.data()),executable,ec)&&!ec;
    const bool terminated=matches&&TerminateProcess(handle,23)&&WaitForSingleObject(handle,1500)==WAIT_OBJECT_0;
    CloseHandle(handle);return terminated;
}
HWND SessionWindow() {
    HWND result=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM context)->BOOL {
        wchar_t name[128]{};
        GetClassNameW(window,name,128);
        if(wcscmp(name,L"LumeNativeWallpaperSession")==0) {
            *reinterpret_cast<HWND*>(context)=window;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
struct Probe {
    HANDLE pipe=INVALID_HANDLE_VALUE;
    std::string buffered;
    std::uint64_t serial=700000;
    ~Probe(){Close();}
    void Close(){if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;buffered.clear();}
    bool Open(DWORD pid) {
        Close();
        WIN32_FIND_DATAW entry{};
        const auto prefix=L"\\\\.\\pipe\\lume-native-mpv-"+std::to_wstring(GetCurrentProcessId())+L"-*";
        const auto finder=FindFirstFileW(prefix.c_str(),&entry);
        if(finder==INVALID_HANDLE_VALUE)return false;
        do {
            const auto name=L"\\\\.\\pipe\\"+std::wstring(entry.cFileName);
            const auto handle=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,
                OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
            if(handle==INVALID_HANDLE_VALUE)continue;
            ULONG owner=0;
            if(GetNamedPipeServerProcessId(handle,&owner)&&owner==pid){pipe=handle;break;}
            CloseHandle(handle);
        }while(FindNextFileW(finder,&entry));
        FindClose(finder);return pipe!=INVALID_HANDLE_VALUE;
    }
    bool Transfer(bool write,void* data,DWORD size,DWORD& transferred) {
        OVERLAPPED operation{};operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!operation.hEvent)return false;
        auto success=write?WriteFile(pipe,data,size,&transferred,&operation):
            ReadFile(pipe,data,size,&transferred,&operation);
        if(!success&&GetLastError()==ERROR_IO_PENDING) {
            const auto end=GetTickCount64()+1500;
            while(WaitForSingleObject(operation.hEvent,0)==WAIT_TIMEOUT&&GetTickCount64()<end)Pump(5);
            if(WaitForSingleObject(operation.hEvent,0)==WAIT_OBJECT_0)
                success=GetOverlappedResult(pipe,&operation,&transferred,FALSE);
            else {
                CancelIoEx(pipe,&operation);DWORD ignored=0;
                GetOverlappedResult(pipe,&operation,&ignored,TRUE);
            }
        }
        CloseHandle(operation.hEvent);return success!=FALSE;
    }
    Json Property(const char* name) {
        if(pipe==INVALID_HANDLE_VALUE)return {};
        const auto id=++serial;
        auto request=Json{{"command",Json::array({"get_property",name})},{"request_id",id}}.dump()+"\n";
        DWORD written=0;if(!Transfer(true,request.data(),static_cast<DWORD>(request.size()),written))return {};
        const auto end=GetTickCount64()+2000;
        while(GetTickCount64()<end) {
            const auto newline=buffered.find('\n');
            if(newline==std::string::npos) {
                std::array<char,4096> block{};DWORD read=0;
                if(!Transfer(false,block.data(),static_cast<DWORD>(block.size()),read)||!read)return {};
                buffered.append(block.data(),read);if(buffered.size()>65536)return {};continue;
            }
            const auto response=Json::parse(buffered.substr(0,newline),nullptr,false);
            buffered.erase(0,newline+1);
            if(response.is_object()&&response.value("request_id",std::uint64_t{0})==id)
                return response.value("error",std::string{})=="success"&&response.contains("data")?response["data"]:Json{};
        }
        return {};
    }
};
void Send(Session& session,Action action,std::uint64_t display,const std::string& id={},bool flag=false,int volume=0) {
    lume::ui::WallpaperPageCommand command;
    command.action=action;command.display=display;command.id=id;command.flag=flag;command.volume=volume;
    session.HandlePage(command);
}
bool Setup(const fs::path& root,const fs::path& video,std::uint64_t display,
           std::string& first,std::string& second) {
    lume::wallpaper::Library library(root.u8string());
    if(!library.Load().ok)return false;
    const auto a=library.ImportLocal(video.u8string());
    const auto b=library.ImportLocal(video.u8string());
    if(!a.ok||!b.ok)return false;
    first=a.id;second=b.id;
    lume::platform::windows::LocalSettingsFileStore files;
    lume::wallpaper::PlaybackPreferencesStore store(files);
    lume::wallpaper::PlaybackPreferences preferences;
    preferences.displays.push_back({display,first,true,0,true});
    return store.Save((root/L"preferences.json").u8string(),preferences);
}
struct ReadGate {
    HANDLE file=INVALID_HANDLE_VALUE;
    OVERLAPPED operation{};
    explicit ReadGate(const fs::path& path) {
        operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        DWORD returned=0;
        const bool requested=file!=INVALID_HANDLE_VALUE&&operation.hEvent&&
            !DeviceIoControl(file,FSCTL_REQUEST_OPLOCK_LEVEL_1,nullptr,0,nullptr,0,
                &returned,&operation)&&GetLastError()==ERROR_IO_PENDING;
        Expect(requested,"hold only the isolated fixture file with an actual oplock");
    }
    ~ReadGate(){Release();}
    bool Blocked()const{return operation.hEvent&&WaitForSingleObject(operation.hEvent,0)==WAIT_OBJECT_0;}
    void Release() {
        if(file!=INVALID_HANDLE_VALUE) {
            CancelIoEx(file,&operation);DWORD ignored=0;
            GetOverlappedResult(file,&operation,&ignored,TRUE);
            CloseHandle(file);file=INVALID_HANDLE_VALUE;
        }
        if(operation.hEvent){CloseHandle(operation.hEvent);operation.hEvent=nullptr;}
    }
};
Json BalancedProperties(Probe& probe) {
    Json state;
    for(const auto* name:{"vf","scale","cscale","dscale","tscale","pause","volume","panscan",
        "video-frame-info","mpv-version"})state[name]=probe.Property(name);
    return state;
}
bool MatchesBalanced(const Json& state,bool enabled) {
    if(!state.is_object()||!state["vf"].is_array())return false;
    unsigned filters=0;
    for(const auto& filter:state["vf"])if(filter.value("label","")=="lume-balanced") {
        ++filters;
        if(filter.value("name","")!="fps"||!filter.value("enabled",true)||!filter.contains("params")||
            filter["params"].value("@0",filter["params"].value("fps",""))!="30")return false;
    }
    return filters==(enabled?1u:0u)&&state["scale"]=="bilinear"&&state["cscale"]=="bilinear"&&
        state["dscale"]==(enabled?"bilinear":"mitchell")&&state["tscale"]==(enabled?"linear":"oversample");
}
void BalancedRestore(const fs::path& executable,const fs::path& video,const fs::path& output,
    const std::vector<lume::platform::windows::MpvDisplay>& displays,HWND parent,
    bool initial,const char* gateName) {
    const std::string name=std::string(initial?"enabled-":"disabled-")+gateName;
    const auto root=output/name;
    fs::create_directories(root);
    std::string first,second;
    if(!Setup(root,video,displays.front().id,first,second)){Expect(false,"seed balanced restore fixture");return;}
    lume::wallpaper::Library library(root.u8string());
    if(!library.Load().ok){Expect(false,"reload the isolated fixture");return;}
    lume::platform::windows::LocalSettingsFileStore files;
    lume::wallpaper::PlaybackPreferencesStore store(files);
    lume::wallpaper::PlaybackPreferences preferences;
    preferences.pauseFullscreen=true;
    for(const auto& display:displays)preferences.displays.push_back({display.id,first,true,0,false});
    Expect(store.Save((root/"preferences.json").u8string(),preferences),"seed all displays with independent pause and fit");
    std::string saved;
    Expect(files.Read((root/"preferences.json").u8string(),saved),"read initial isolated playback preferences");
    std::unique_ptr<ReadGate> gate;
    if(std::string(gateName)=="library")gate=std::make_unique<ReadGate>(root/"library.json");
    if(std::string(gateName)=="media")gate=std::make_unique<ReadGate>(fs::u8path(library.Find(first)->localPath));
    lume::shell::WallpaperSessionOptions options;
    options.root=root.u8string();options.executable=executable.wstring();
    options.verification=true;options.verificationParent=parent;options.balancedMode=initial;
    Session session(GetModuleHandleW(nullptr),options,[](const auto&){});
    const auto started=GetTickCount64();
    Expect(session.Restore(),"restore actual session before settings worker confirmation");
    if(gate)Expect(Wait([&]{return gate->Blocked();}),"restore reaches the controlled real file-open boundary");
    const auto requested=GetTickCount64();
    auto result=std::async(std::launch::async,[&]{return session.SetBalancedMode(initial);});
    // A file gate keeps startup busy beyond the admission deadline without
    // changing the player's executable, arguments, IPC replies or policy.
    bool finished=Wait([&]{return result.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;},2800);
    const auto confirmed=GetTickCount64();
    const bool applied=finished&&result.get();
    const auto blockedPids=OwnedProcesses();
    if(gate)gate->Release();
    if(!finished)result.get();
    Expect(finished&&applied,"same restored policy is confirmed while the restore actor is busy");
    const bool ready=Wait([&]{
        return std::all_of(displays.begin(),displays.end(),[&](const auto& display){
            return HasScreenState(session,display.id,"paused");
        });
    },30000);
    Expect(ready,"all real players eventually load the fixture under the restored policy");
    Json record={{"name",name},{"requested_ms",requested-started},{"confirmation_ms",confirmed-requested},
        {"confirmed",applied},{"ready",ready},{"restore_ms",GetTickCount64()-started},
        {"blocked_owned_pids",blockedPids},{"state",State(session)},{"players",Json::array()}};
    auto pids=EmbeddedPlayers(parent);
    Expect(pids.size()==displays.size(),"one isolated real mpv for each restored display");
    for(const auto pid:pids) {
        Probe probe;
        const bool opened=probe.Open(pid);
        Expect(opened,"probe opens only this test's uniquely prefixed owned-player IPC");
        if(!opened)continue;
        const auto state=BalancedProperties(probe);
        record["players"].push_back({{"pid",pid},{"properties",state}});
        Expect(MatchesBalanced(state,initial),"actual mpv filter and all four samplers match the confirmed policy");
        Expect(state["pause"]==true&&state["volume"]==0&&state["panscan"]==0.0,
            "initial policy confirmation preserves manual pause, silent volume and contain fit");
    }
    if(ready)for(const bool enabled:{!initial,initial,initial}) {
        auto change=std::async(std::launch::async,[&session,enabled]{return session.SetBalancedMode(enabled);});
        const bool done=Wait([&]{return change.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;},10000);
        const bool ok=change.get();
        Expect(done&&ok,"real multi-display balanced transition returns success");
        Json transition={{"enabled",enabled},{"success",ok},{"players",Json::array()}};
        for(const auto pid:pids) {
            Probe probe;
            if(!probe.Open(pid)){Expect(false,"reopen owned player for transition evidence");continue;}
            const auto state=BalancedProperties(probe);
            transition["players"].push_back({{"pid",pid},{"properties",state}});
            Expect(MatchesBalanced(state,enabled),"runtime transition really applies the original 30fps and sampling policy");
            Expect(state["pause"]==true&&state["volume"]==0&&state["panscan"]==0.0,
                "real transition preserves unrelated playback settings");
        }
        record["transitions"].push_back(std::move(transition));
    }
    std::string after;
    Expect(files.Read((root/"preferences.json").u8string(),after)&&after==saved,
        "policy confirmation never rewrites independent playback preferences");
    session.Shutdown();
    Expect(!session.SetBalancedMode(initial),"shutdown rejects even an already confirmed policy");
    Expect(Wait([]{return OwnedProcesses().empty();},2000)&&GetWindow(parent,GW_CHILD)==nullptr,
        "balanced fixture cleans only its own complete process tree and windows");
    evidence["balanced_restore"].push_back(std::move(record));
}
}
int main(int argc,char** argv) {
    if(argc!=4&&argc!=5) {
        std::cerr<<"Usage: wallpaper-session-playback-test mpv.exe fixture-video.mp4 output-directory [resource-phase-seconds|--balanced-regression]\n";return 2;
    }
    const auto executable=fs::absolute(fs::u8path(argv[1]));
    const auto video=fs::absolute(fs::u8path(argv[2]));
    const auto output=fs::absolute(fs::u8path(argv[3]));
    const bool balancedRegression=argc==5&&std::string(argv[4])=="--balanced-regression";
    if(argc==5&&!balancedRegression) {
        const auto seconds=std::strtoul(argv[4],nullptr,10);
        if(seconds<3||seconds>30)return 2;
        resourceHoldMs=static_cast<DWORD>(seconds*1000);
        phaseOutput=output/L"wallpaper-session-phase.json";
    }
    const auto root=output/(L"session-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    fs::create_directories(root);
    const auto displays=lume::platform::windows::EnumerateMpvDisplays();
    Expect(!displays.empty(),"enumerated display without modifying Explorer");
    if(displays.empty())return 1;
    const auto display=displays.front().id;
    std::string first,second;
    Expect(Setup(root,video,display,first,second),"import two isolated cleartext media and save manual pause");
    if(first.empty()||second.empty())return 1;
    const auto parent=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Session playback verification",
        WS_POPUP,-30000,-30000,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Expect(parent!=nullptr,"created owned off-screen parent");
    if(!parent)return 1;
    ShowWindow(parent,SW_SHOWNOACTIVATE);
    lume::shell::WallpaperSessionOptions options;
    options.root=root.u8string();options.executable=executable.wstring();
    options.verification=true;options.verificationParent=parent;
    evidence["executable"]=executable.u8string();evidence["fixture"]=video.u8string();
    evidence["root_pid"]=GetCurrentProcessId();
    evidence["scope"]="owned 640x360 off-screen parent, real session and player descendants; not desktop/compositor acceptance";
    if(balancedRegression) {
        BalancedRestore(executable,video,root,displays,parent,true,"none");
        BalancedRestore(executable,video,root,displays,parent,true,"media");
        BalancedRestore(executable,video,root,displays,parent,true,"library");
        BalancedRestore(executable,video,root,displays,parent,false,"library");
        DestroyWindow(parent);
        evidence["residual-pids"]=OwnedProcesses();
        for(const auto& item:ownedHandles)CloseHandle(item.second);
        ownedHandles.clear();
        evidence["failures"]=failures;evidence["library-root"]=root.u8string();
        std::ofstream(output/L"balanced-restore-evidence.json")<<evidence.dump(2);
        return failures?1:0;
    }
    Phase("baseline");
    {
        unsigned notifications=0;
        Session session(GetModuleHandleW(nullptr),options,[&](const std::wstring&){++notifications;});
        Expect(session.Restore(),"restore session asynchronously");
        Expect(session.HasWork(),"Restore immediately retains startup work before the first snapshot");
        const auto restored=Wait([&]{return HasScreenState(session,display,"paused");});
        Expect(restored,"saved manual pause restored in session snapshot");
        evidence["restore-state"]=State(session);
        auto players=EmbeddedPlayers(parent);
        Expect(players.size()==1,"one real mpv owned by the isolated session");
        Probe probe;
        Expect(!players.empty()&&probe.Open(players.front()),"independent real mpv IPC probe");
        Expect(probe.Property("pause")==Json(true),"manual pause is actually applied in mpv");
        Phase("restored-paused",&session);
        const auto dispatch=GetTickCount64();
        Send(session,Action::Apply,display,second);
        Expect(GetTickCount64()-dispatch<100,"apply dispatch does not block UI");
        Expect(session.HasWork(),"queued Apply retains session work before the actor processes it");
        Expect(Wait([&]{auto item=Screen(session,display);return item.is_object()&&
            item.value("wallpaperId",std::string{})==second&&item.value("state",std::string{})=="playing";}),
            "apply second imported video publishes playing");
        Expect(probe.Property("pause")==Json(false),"apply after manual pause really resumes mpv");
        Expect(EmbeddedPlayers(parent)==players,"session apply reuses the same real player PID");
        evidence["switch-pid-before"]=players;
        evidence["switch-pid-after"]=EmbeddedPlayers(parent);
        Phase("playing",&session);
        const auto hidden=SessionWindow();
        Expect(hidden!=nullptr,"found only this thread's private session notification window");
        if(hidden) {
            SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
            Expect(Wait([&]{const auto item=Screen(session,display);return item.is_object()&&
                item.value("autoPaused",false);}),"simulated own-window suspend enables automatic pause");
            Expect(probe.Property("pause")==Json(true),"automatic pause reaches real mpv");
            Send(session,Action::Fit,display,{},false);
            Expect(Wait([&]{const auto item=Screen(session,display);return item.is_object()&&
                item.value("fit",std::string{})=="contain";}),"fit update completed while automatically paused");
            Send(session,Action::Volume,display,{},false,0);
            Pump(100);
            auto item=Screen(session,display);
            Expect(item.is_object()&&item.value("autoPaused",false)&&probe.Property("pause")==Json(true),
                "fit and zero-volume updates preserve automatic pause");
            SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
            Expect(Wait([&]{return HasScreenState(session,display,"playing");}),"power resume clears automatic pause");
            Expect(probe.Property("pause")==Json(false),"automatic resume really resumes mpv");
            Send(session,Action::Pause,display);
            Expect(Wait([&]{return HasScreenState(session,display,"paused");}),"manual pause re-applied");
            SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
            Pump(80);
            SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
            Pump(100);
            Expect(HasScreenState(session,display,"paused")&&probe.Property("pause")==Json(true),
                "automatic recovery never overrides manual pause");
        }
        Phase("manual-paused",&session);
        // Crash only an embedded child whose actual executable matches the
        // supplied verification copy. Keep handles to every observed descendant
        // so even an orphan cannot disappear from the residual-process check.
        Json crashes=Json::array();
        for(unsigned attempt=0;attempt<4;++attempt) {
            players=EmbeddedPlayers(parent);
            if(players.size()!=1)break;
            const auto previous=State(session);
            const auto revision=previous.value("revision",std::uint64_t{0});
            probe.Close();
            const auto started=GetTickCount64();
            const bool killed=TerminateOwnedPlayer(players.front(),parent,executable);
            Expect(killed,"fault injection terminates only the owned verification player");
            if(!killed)break;
            const bool settled=Wait([&] {
                const auto current=State(session);
                const auto children=EmbeddedPlayers(parent);
                const auto screen=Screen(session,display);
                return current.is_object()&&current.value("revision",std::uint64_t{0})>revision&&
                    ((children.size()==1&&children!=players&&HasScreenState(session,display,"paused"))||
                     (attempt>=3&&children.empty()&&screen.is_object()&&screen.value("state",std::string{})=="failed"));
            },12000);
            const auto replacement=EmbeddedPlayers(parent);
            crashes.push_back({{"attempt",attempt+1},{"terminated_pid",players.front()},
                {"replacement_pids",replacement},{"settled",settled},
                {"elapsed_ms",GetTickCount64()-started},{"state",State(session)}});
            Expect(settled,"player crash settles as recovered or explicitly failed, never stale playing/paused");
            if(replacement.empty()) {
                Expect(attempt>=3,"recovery remains available before the bounded retry limit");
                break;
            }
            Expect(replacement.size()==1&&replacement!=players,"crash recovery creates exactly one replacement PID");
            Expect(probe.Open(replacement.front()),"independent probe reconnects only to replacement child");
            Expect(probe.Property("pause")==Json(true),"crash recovery preserves manual pause in actual mpv");
            Expect(probe.Property("panscan")==Json(0.0),"crash recovery preserves contain fit in actual mpv");
            Expect(probe.Property("volume")==Json(0.0),"crash recovery preserves mute-volume preference");
            if(attempt==0)Phase("recovered-paused",&session);
        }
        evidence["crashes"]=std::move(crashes);
        probe.Close();
        Send(session,Action::Stop,display);
        Expect(Wait([&]{return HasScreenState(session,display,"stopped")&&OwnedProcesses().empty();}),
            "page Stop clears the complete owned process tree");
        Expect(!session.HasWork()&&GetWindow(parent,GW_CHILD)==nullptr,
            "page Stop clears retained work and every native container");
        Phase("stopped",&session);
        for(unsigned cycle=0;cycle<3;++cycle) {
            Send(session,Action::Apply,display,cycle%2?second:first);
            Expect(session.HasWork(),"Apply from stopped retains work before asynchronous launch");
            Expect(Wait([&]{return HasScreenState(session,display,"playing")&&EmbeddedPlayers(parent).size()==1;}),
                "repeated Apply starts exactly one player");
            const auto active=OwnedProcesses();
            Send(session,Action::StopAll,display);
            Expect(Wait([&]{return HasScreenState(session,display,"stopped")&&OwnedProcesses().empty();}),
                "repeated StopAll leaves no owned child or descendant");
            evidence["start-stop-cycles"].push_back({{"cycle",cycle+1},{"active_pids",active},
                {"residual_pids",OwnedProcesses()},{"residual_container",GetWindow(parent,GW_CHILD)!=nullptr}});
        }
        Phase("after-three-cycles",&session);
        Send(session,Action::Apply,display,second);
        Expect(Wait([&]{return HasScreenState(session,display,"playing")&&EmbeddedPlayers(parent).size()==1;}),
            "player restarts after repeated stop and fault exhaustion");
        Phase("restarted-playing",&session);
        evidence["final-state"]=State(session);
        const auto before=GetTickCount64();
        session.Shutdown();
        const auto elapsed=GetTickCount64()-before;
        evidence["active-shutdown-ms"]=elapsed;
        Expect(elapsed<2000,"active session Shutdown completes within two seconds");
        Expect(!session.HasWork()&&Wait([]{return OwnedProcesses().empty();},1000),
            "shutdown clears HasWork and the complete owned process tree");
        Expect(GetWindow(parent,GW_CHILD)==nullptr,"active shutdown destroys every owned container");
        Phase("shutdown",&session);
    }
    {
        unsigned notifications=0;
        Session session(GetModuleHandleW(nullptr),options,[&](const std::wstring&){++notifications;});
        Expect(session.Restore(),"start second session for startup cancellation");
        // Pump until actor creates its container, before waiting for a loaded
        // first frame. The cancellation intentionally happens before a final
        // UI snapshot where possible; record that fact rather than assuming it.
        const auto observed=Wait([&]{return GetWindow(parent,GW_CHILD)!=nullptr||notifications!=0;},2000);
        Expect(observed,"startup actor reached its owned container");
        evidence["cancel-before-initial-snapshot"]=notifications==0;
        const auto before=GetTickCount64();
        session.Shutdown();
        const auto elapsed=GetTickCount64()-before;
        evidence["startup-shutdown-ms"]=elapsed;
        Expect(elapsed<2000,"Shutdown during startup completes within two seconds");
        Expect(!session.HasWork()&&Wait([]{return OwnedProcesses().empty();},1000),
            "cancelled startup leaves no owned mpv child or retained work");
        Expect(GetWindow(parent,GW_CHILD)==nullptr,"cancelled startup destroys its container");
    }
    DestroyWindow(parent);
    evidence["residual-pids"]=OwnedProcesses();
    for(const auto& item:ownedHandles)CloseHandle(item.second);
    ownedHandles.clear();
    evidence["failures"]=failures;evidence["library-root"]=root.u8string();
    std::ofstream(output/L"wallpaper-session-playback-evidence.json")<<evidence.dump(2);
    return failures?1:0;
}
