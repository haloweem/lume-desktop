#include "apps/shell/wallpaper_session.h"
#include "apps/shell/wallpaper_resource_history.h"
#include "core/wallpaper/library.h"
#include "core/wallpaper/playback_preferences.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/mpv_process_controller.h"
#include "vendor/nlohmann/json.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <winioctl.h>
#include <wtsapi32.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
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
    DWORD connectedPid=0;
    std::string buffered;
    std::uint64_t serial=700000;
    ~Probe(){Close();}
    void Close(){if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;connectedPid=0;buffered.clear();}
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
        FindClose(finder);if(pipe!=INVALID_HANDLE_VALUE)connectedPid=pid;return pipe!=INVALID_HANDLE_VALUE;
    }
    bool Follow(HWND parent){const auto players=EmbeddedPlayers(parent);return players.size()==1&&
        (connectedPid==players.front()||Open(players.front()));}
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
    Json Request(const Json& command) {
        if(pipe==INVALID_HANDLE_VALUE)return {};
        const auto id=++serial;
        auto request=Json{{"command",command},{"request_id",id}}.dump()+"\n";
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
            if(response.is_object()&&response.value("request_id",std::uint64_t{0})==id){
                if(response.value("error",std::string{})!="success")return {};
                if(command.is_array()&&!command.empty()&&command[0]=="set_property")return Json(true);
                return response.value("data",Json{});
            }
        }
        return {};
    }
    Json Property(const char* name) {return Request(Json::array({"get_property",name}));}
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
void CoverageResumeStability(const fs::path& executable,const fs::path& video,const fs::path& output,
                             std::uint64_t display,HWND parent) {
    const auto root=output/"coverage-stability";
    fs::create_directories(root);
    std::string first,second;
    Expect(Setup(root,video,display,first,second),"seed isolated seamless coverage fixture");
    std::atomic<bool> covered{false};
    lume::shell::WallpaperSessionOptions options;
    options.root=root.u8string();options.executable=executable.wstring();options.verification=true;
    options.verificationParent=parent;options.balancedMode=true;
    options.verificationFullscreen=[&](std::uint64_t){return covered.load();};
    // Keep production's 30-second parking threshold. The longer pause below
    // must cross the old 3-second rebuild coalescing interval without parking.
    Session session(GetModuleHandleW(nullptr),options,[](const auto&){});
    Expect(session.Restore()&&Wait([&]{return HasScreenState(session,display,"paused");}),
        "coverage fixture restores a manually paused real player");
    const auto hidden=SessionWindow();
    const auto players=EmbeddedPlayers(parent);
    Probe probe;
    Expect(hidden&&players.size()==1&&probe.Open(players.front()),
        "coverage fixture connects only to its owned player and notification window");
    if(!hidden||players.size()!=1){session.Shutdown();return;}
    Send(session,Action::Resume,display);
    Expect(Wait([&]{return HasScreenState(session,display,"playing")&&probe.Property("pause")==Json(false);}),
        "coverage fixture starts the actual video timeline");
    Pump(3200);
    for(const DWORD hold:{250u,3500u}) {
        covered=true;SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);
        Expect(Wait([&]{return Screen(session,display).value("autoPaused",false)&&probe.Property("pause")==Json(true);}),
            "fullscreen coverage pauses the existing player before return to desktop");
        const auto pausedPosition=probe.Property("time-pos");
        Pump(hold);
        Expect(EmbeddedPlayers(parent)==players,
            "brief and longer ordinary fullscreen coverage retain the same player before parking");
        covered=false;SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);
        Expect(Wait([&]{return HasScreenState(session,display,"playing")&&probe.Follow(parent)&&
            probe.Property("pause")==Json(false);}),"returning to desktop resumes the attached player's actual timeline");
        const auto resumedPosition=probe.Property("time-pos");
        Expect(EmbeddedPlayers(parent)==players,
            "ordinary fullscreen return never replaces a healthy player or exposes its static fallback");
        Expect(pausedPosition.is_number()&&resumedPosition.is_number()&&
            std::abs(resumedPosition.get<double>()-pausedPosition.get<double>())<1.5,
            "resuming fullscreen coverage preserves playback position");
    }
    const auto beforeManual=EmbeddedPlayers(parent);
    Send(session,Action::Pause,display);
    Expect(Wait([&]{return HasScreenState(session,display,"paused")&&probe.Property("pause")==Json(true);}),
        "manual pause reaches the same real player");
    Pump(3500);Send(session,Action::Resume,display);
    Expect(Wait([&]{return HasScreenState(session,display,"playing")&&probe.Property("pause")==Json(false);})&&
        EmbeddedPlayers(parent)==beforeManual,"manual pause and resume also preserve player identity");
    const auto advancing=[&] {
        if (!probe.Follow(parent) || probe.Property("pause")!=Json(false))return false;
        const auto before=probe.Property("time-pos");
        Pump(230);
        const auto after=probe.Property("time-pos");
        return before.is_number() && after.is_number() && std::abs(after.get<double>()-before.get<double>())>.02;
    };
    const auto prefsBytes=[&] {
        std::ifstream file(root/L"preferences.json",std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
    };
    const auto playingPrefs=prefsBytes();
    for(unsigned i=0;i<3;++i){
        session.SetGameIdle(true);Pump(100);session.SetGameIdle(false);Pump(120);
    }
    Expect(Wait(advancing),"three rapid game switches leave actual video time advancing");
    session.SetGameIdle(true);
    Expect(Wait([&]{return EmbeddedPlayers(parent).empty() && OwnedProcesses().empty();},8000),
        "a game on another display releases the real decoder process");
    // Hold only the fixture's imported media unavailable for the first
    // restart. Restore it without sending any command to Lume.
    probe.Close();
    const auto media=root/"media"/(first+".mp4");
    const auto held=root/(first+".held");
    std::error_code ec;
    fs::rename(media,held,ec);
    Expect(!ec,"temporarily withhold owned test media");
    session.SetGameIdle(false);
    Expect(Wait([&]{return HasScreenState(session,display,"failed");}),"first resume fails while media is unavailable");
    fs::rename(held,media,ec);
    Expect(!ec,"restore owned test media");
    Expect(Wait(advancing,15000) && HasScreenState(session,display,"playing"),
        "retry resumes actual video time without opening settings");
    session.SetGameIdle(true);
    Expect(Wait([&]{return EmbeddedPlayers(parent).empty();},6000),"second game session releases player");
    // Simulate the foreground observer losing its final exit notification.
    Expect(Wait(advancing,15000) && HasScreenState(session,display,"playing"),
        "expired game lease independently restarts playback after a missed exit");
    Expect(prefsBytes()==playingPrefs,"game transitions preserve saved playback settings");
    session.SetGameIdle(false);
    Send(session,Action::Pause,display);
    Expect(Wait([&]{return HasScreenState(session,display,"paused");}),"manually pause before game");
    session.SetGameIdle(true);
    Expect(Wait([&]{return EmbeddedPlayers(parent).empty() && OwnedProcesses().empty();},8000),
        "manually paused decoder is released too");
    session.SetGameIdle(false);
    Expect(Wait([&]{return probe.Follow(parent)&&HasScreenState(session,display,"paused")&&probe.Property("pause")==Json(true);}),
        "manually paused wallpaper stays paused after game");
    probe.Close();session.Shutdown();
    Expect(Wait([&]{return EmbeddedPlayers(parent).empty()&&OwnedProcesses().empty();},5000),
        "seamless coverage fixture releases its complete owned process tree");
}
void EarlyResumeRecovery(const fs::path& executable,const fs::path& video,const fs::path& output,
                         std::uint64_t display,HWND parent) {
    // A newly started player is not proof that its GPU resources survived a
    // later display-off/lock. Exercise that ordering inside the coalescing
    // window, rather than waiting 3.2 seconds before the first invalidation.
    for(const std::string scenario:{"early-lock","early-display-off","early-manual-pause"}) {
        const auto root=output/scenario;
        fs::create_directories(root);
        std::string first,second;
        Expect(Setup(root,video,display,first,second),"seed early-resume media and saved manual preference");
        if(first.empty())continue;
        const bool manual=scenario=="early-manual-pause";
        const bool displayPower=scenario=="early-display-off";
        lume::shell::WallpaperSessionOptions options;
        options.root=root.u8string();options.executable=executable.wstring();
        options.verification=true;options.verificationParent=parent;
        Session session(GetModuleHandleW(nullptr),options,[](const auto&){});
        const auto restoreStarted=GetTickCount64();
        Expect(session.Restore()&&Wait([&]{return HasScreenState(session,display,"paused");}),
            "early-resume restores one real manually paused player");
        const auto hidden=SessionWindow();
        Probe probe;
        Expect(hidden&&EmbeddedPlayers(parent).size()==1&&probe.Follow(parent),
            "early-resume connects only to its owned player and event window");
        if(!hidden||!probe.Follow(parent)){session.Shutdown();continue;}
        if(!manual) {
            Send(session,Action::Resume,display);
            Expect(Wait([&]{return HasScreenState(session,display,"playing")&&probe.Property("pause")==Json(false);}),
                "early-resume starts the real video timeline before invalidation");
        }
        const auto savedPreferences=[&] {
            std::ifstream file(root/L"preferences.json",std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
        }();
        struct DisplayPower {GUID setting;DWORD length;DWORD value;};
        DisplayPower power{GUID_CONSOLE_DISPLAY_STATE,sizeof(DWORD),0};
        const auto inactive=[&] {
            if(displayPower) {
                power.value=0;
                SendMessageW(hidden,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&power));
            }else SendMessageW(hidden,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
        };
        const auto resume=[&] {
            if(displayPower) {
                power.value=1;
                SendMessageW(hidden,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&power));
            }else SendMessageW(hidden,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
        };
        auto lastFreshAt=restoreStarted;
        for(unsigned cycle=0;cycle<2;++cycle) {
            const auto before=EmbeddedPlayers(parent);
            inactive();
            Expect(Wait([&]{return probe.Property("pause")==Json(true)&&
                (manual||Screen(session,display).value("autoPaused",false));}),
                "early invalidation actually pauses the owned player without parking");
            const auto elapsed=GetTickCount64()-lastFreshAt;
            const bool early=elapsed<3000;
            Expect(early,"new invalidation occurs inside three seconds of the previous fresh player");
            resume();
            const bool refreshed=Wait([&]{return EmbeddedPlayers(parent).size()==1&&EmbeddedPlayers(parent)!=before&&
                HasScreenState(session,display,manual?"paused":"playing")&&probe.Follow(parent)&&
                probe.Property("pause")==Json(manual);},6000);
            Expect(refreshed,"each early lock or display-off cycle requires fresh GPU resources after returning");
            const auto after=EmbeddedPlayers(parent);
            lastFreshAt=GetTickCount64();
            evidence["early_resume"].push_back({{"scenario",scenario},{"cycle",cycle},
                {"inactiveAfterFreshMs",elapsed},{"early",early},{"refreshed",refreshed},
                {"beforePids",before},{"afterPids",after},{"state",Screen(session,display)}});
            if(!refreshed)break;
            // These are follow-up notifications from the same return. They
            // must not cause another cold start without a new inactive edge.
            SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
            SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMRESUMESUSPEND,0);
            SendMessageW(hidden,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
            Pump(300);
            Expect(EmbeddedPlayers(parent)==after,
                "related resume notifications coalesce after this invalidation was already rebuilt");
        }
        const auto actualPause=probe.Property("pause");
        Expect(actualPause==Json(manual)&&HasScreenState(session,display,manual?"paused":"playing"),
            "early resource refresh preserves the user's manual versus automatic pause policy");
        std::ifstream savedFile(root/L"preferences.json",std::ios::binary);
        const std::string afterPreferences{std::istreambuf_iterator<char>(savedFile),std::istreambuf_iterator<char>()};
        Expect(afterPreferences==savedPreferences,"power and lock cycles leave the saved playback preference untouched");
        probe.Close();session.Shutdown();
        Expect(Wait([&]{return EmbeddedPlayers(parent).empty()&&OwnedProcesses().empty()&&GetWindow(parent,GW_CHILD)==nullptr;},5000),
            "early-resume fixture releases every owned player and container");
    }
}
void ResumeRecovery(const fs::path& executable,const fs::path& video,const fs::path& output,
                    std::uint64_t display,HWND parent) {
    const auto root=output/"resume-policy";
    fs::create_directories(root);
    std::string first,second;
    Expect(Setup(root,video,display,first,second),"seed isolated resume policy fixture");
    std::atomic<bool> covered{false};
    lume::shell::WallpaperSessionOptions options;
    options.root=root.u8string();options.executable=executable.wstring();options.verification=true;
    options.verificationParent=parent;options.balancedMode=true;
    options.verificationFullscreen=[&](std::uint64_t){return covered.load();};
    options.verificationRecoveryMilliseconds=1500;
    const auto historyDirectory=root/"diagnostic-history";
    const auto historyStarted=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    options.verificationResourceHistoryDirectory=historyDirectory.u8string();
    options.verificationResourceHistoryIntervalMilliseconds=1000;
    Session session(GetModuleHandleW(nullptr),options,[](const auto&){});
    Expect(session.Restore()&&Wait([&]{return HasScreenState(session,display,"paused");}),"resume fixture restores saved manual pause");
    const auto hidden=SessionWindow();
    Expect(hidden!=nullptr,"resume fixture uses only its own notification window");
    if(!hidden){session.Shutdown();return;}
    Probe probe;
    auto players=EmbeddedPlayers(parent);
    Expect(players.size()==1&&probe.Open(players.front()),"resume fixture connects to its real owned mpv");
    if(players.size()!=1){session.Shutdown();return;}
    Send(session,Action::Resume,display);
    Expect(Wait([&]{return HasScreenState(session,display,"playing")&&probe.Property("pause")==Json(false);}),"manual resume starts the actual timeline");
    Expect(probe.Request(Json::array({"set_property","pause",true}))==Json(true),"inject player pause drift without changing Lume intent");
    SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);
    Expect(Wait([&]{return probe.Property("pause")==Json(false);}),"a lost pause acknowledgement cannot strand playback on returning to desktop");
    Send(session,Action::Pause,display);
    Expect(Wait([&]{return HasScreenState(session,display,"paused")&&probe.Property("pause")==Json(true);}),"manual pause is confirmed before drift test");
    Expect(probe.Request(Json::array({"set_property","pause",false}))==Json(true),"inject unintended player resume");
    SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);
    Expect(Wait([&]{return probe.Property("pause")==Json(true);}),"health reconciliation preserves the user's manual pause");
    Send(session,Action::Resume,display);
    Expect(Wait([&]{return probe.Property("pause")==Json(false);}),"playing fixture is ready for fullscreen policy");
    covered=true;SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);
    Expect(Wait([&]{return Screen(session,display).value("autoPaused",false)&&probe.Property("pause")==Json(true);}),"covering game automatically pauses the actual player");
    covered=false;SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);
    Expect(Wait([&]{return HasScreenState(session,display,"playing")&&probe.Follow(parent)&&probe.Property("pause")==Json(false);}),"returning to desktop clears automatic pause and resumes mpv");
    Pump(3200);const auto beforeShortLock=EmbeddedPlayers(parent);
    SendMessageW(hidden,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
    Expect(Wait([&]{return Screen(session,display).value("autoPaused",false);}),"short lock records automatic pause before parking deadline");
    SendMessageW(hidden,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
    Expect(Wait([&]{return EmbeddedPlayers(parent)!=beforeShortLock&&HasScreenState(session,display,"playing")&&probe.Follow(parent);}),
        "short unlock rebuilds stale GPU resources even before the parking deadline");
    const auto rebuilt=EmbeddedPlayers(parent);
    SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);Pump(800);
    Expect(EmbeddedPlayers(parent)==rebuilt,"related resume and unlock notifications coalesce into one resource rebuild");
    for(unsigned attempt=0;attempt<4;++attempt){
        players=EmbeddedPlayers(parent);probe.Close();
        const auto revision=State(session).value("revision",std::uint64_t{0});
        Expect(players.size()==1&&TerminateOwnedPlayer(players.front(),parent,executable),"terminate only the isolated player to exercise repeated recovery");
        const auto previous=players;
        Expect(Wait([&]{const auto next=EmbeddedPlayers(parent);return next.size()==1&&next!=previous&&
            State(session).value("revision",std::uint64_t{0})>revision&&HasScreenState(session,display,"playing");},12000),
            "a fourth failure is cooled down and recovered instead of permanently abandoning wallpaper");
    }
    players=EmbeddedPlayers(parent);
    Expect(players.size()==1&&probe.Open(players.front()),"repeated recovery retains one player");
    covered=true;SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);
    Expect(Wait([&]{return EmbeddedPlayers(parent).empty()&&HasScreenState(session,display,"paused");},8000),
        "long game coverage parks only that display's decoder");
    probe.Close();Pump(2200);
    Expect(EmbeddedPlayers(parent).empty(),"a still-covered desktop does not repeatedly rebuild its parked player");
    covered=false;SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);
    Expect(Wait([&]{return EmbeddedPlayers(parent).size()==1&&HasScreenState(session,display,"playing");},8000),
        "leaving a long game rebuilds the wallpaper on return to desktop");
    players=EmbeddedPlayers(parent);
    Expect(players.size()==1&&probe.Open(players.front()),"game return reconnects to the one replacement player");
    const auto beforePark=players;
    SendMessageW(hidden,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
    Expect(Wait([&]{return EmbeddedPlayers(parent).empty()&&HasScreenState(session,display,"paused");},8000),
        "long lock parks the decoder while retaining the logical paused wallpaper");
    Expect(session.HasWork(),"parked wallpaper keeps a resume owner without a child process");
    probe.Close();
    SendMessageW(hidden,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
    Expect(Wait([&]{return EmbeddedPlayers(parent).size()==1&&HasScreenState(session,display,"playing");},8000),
        "unlock rebuilds a parked player without a page command");
    players=EmbeddedPlayers(parent);
    Expect(players.size()==1&&players!=beforePark&&probe.Open(players.front()),"unlock owns a fresh player and pipe");
    const auto firstTime=probe.Property("time-pos");Pump(450);const auto lastTime=probe.Property("time-pos");
    Expect(probe.Property("pause")==Json(false)&&firstTime.is_number()&&lastTime.is_number()&&firstTime!=lastTime,
        "unlocked wallpaper has an advancing real video timeline");
    Send(session,Action::Pause,display);
    Expect(Wait([&]{return HasScreenState(session,display,"paused")&&probe.Property("pause")==Json(true);}),"manual pause is persisted before parking");
    SendMessageW(hidden,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
    Expect(Wait([&]{return EmbeddedPlayers(parent).empty();},8000),"manual-paused video also releases its decoder on long lock");
    const auto manualUnlockRevision=State(session).value("revision",std::uint64_t{0});
    probe.Close();SendMessageW(hidden,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
    Expect(Wait([&]{return EmbeddedPlayers(parent).size()==1&&
        State(session).value("revision",std::uint64_t{0})>manualUnlockRevision&&
        HasScreenState(session,display,"paused");},8000),
        "unlock restores a manually paused wallpaper without overriding its preference");
    // A child HWND and IPC pipe can appear before StartPlayback confirms its
    // final pause policy. The previous parked snapshot was also paused, so
    // require a completed publication above and retry the actual readback.
    Expect(Wait([&]{return probe.Follow(parent)&&probe.Property("pause")==Json(true);}),
        "manual pause survives the parked process replacement");
    Send(session,Action::Resume,display);
    Expect(Wait([&]{return probe.Property("pause")==Json(false);}),"resume fixture is playing before display-power ordering check");
    struct DisplayPower {GUID setting;DWORD length;DWORD value;};
    DisplayPower power{GUID_CONSOLE_DISPLAY_STATE,sizeof(DWORD),0};
    SendMessageW(hidden,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&power));
    SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
    Expect(Wait([&]{return EmbeddedPlayers(parent).empty()&&HasScreenState(session,display,"paused");},8000),
        "automatic system resume does not override a display that is still powered off");
    probe.Close();power.value=1;
    SendMessageW(hidden,WM_POWERBROADCAST,PBT_POWERSETTINGCHANGE,reinterpret_cast<LPARAM>(&power));
    Expect(Wait([&]{return EmbeddedPlayers(parent).size()==1&&HasScreenState(session,display,"playing");},8000),
        "display-on notification independently resumes a parked wallpaper");
    // Allow two low-frequency intervals for an owned replacement's CPU delta;
    // real display power and the user's running wallpaper remain untouched.
    Pump(2200);
    probe.Close();session.Shutdown();
    Expect(Wait([&]{return EmbeddedPlayers(parent).empty()&&OwnedProcesses().empty();},5000),"resume policy fixture leaves no child process or container");
    const auto history=lume::shell::ReadWallpaperResourceHistory(historyDirectory);
    bool sampled=false,evented=false,paused=false,parked=false,cpu=false;
    std::set<std::pair<DWORD,std::string>> historicalPlayers;
    const auto runPrefix=std::to_string(GetCurrentProcessId())+'-';
    std::size_t begin=0;
    while(begin<history.records.size()){
        const auto end=history.records.find('\n',begin);
        const auto record=Json::parse(history.records.substr(begin,end==std::string::npos?std::string::npos:end-begin),nullptr,false);
        if(record.is_object()&&record.value("atUnixMs",std::uint64_t{0})>=historyStarted&&
            record.value("runId",std::string{}).rfind(runPrefix,0)==0&&record.contains("processes")&&record["processes"].is_array()){
            const bool sample=record.value("type",std::string{})=="sample";
            sampled=sampled||sample;evented=evented||record.value("type",std::string{})=="event";
            for(const auto& process:record["processes"]){
                paused=paused||process.value("state",std::string{})=="paused"||process.value("manualPaused",false)||process.value("systemPaused",false);
                parked=parked||process.value("parked",false)||process.value("state",std::string{})=="parked";
                const auto pid=process.value("pid",DWORD{0});
                if(pid)historicalPlayers.emplace(pid,process.value("createdFileTime",std::string{}));
                if(sample&&pid&&process.contains("cpuMachinePercent")&&process["cpuMachinePercent"].is_number()&&
                    process.contains("cpuIntervalMs")&&process["cpuIntervalMs"].is_number_unsigned()&&process["cpuIntervalMs"].get<std::uint64_t>()>0){
                    const auto value=process["cpuMachinePercent"].get<double>();cpu=cpu||(std::isfinite(value)&&value>=0&&value<=100);
                }
            }
        }
        if(end==std::string::npos)break;
        begin=end+1;
    }
    Expect(sampled&&evented,"real session writes minute-history samples and immediate context events into its isolated directory");
    Expect(paused&&parked,"resource history retains actual automatic/manual pause and parked policy transitions");
    Expect(historicalPlayers.size()>=5&&history.summaryState.value("pidReplacements",std::size_t{0})>=4,
        "resource history links repeated owned PID replacements back to the same display");
    Expect(cpu,"resource history contains a real normalized CPU delta for an owned player");
    const auto exports=output/"resource-history-export";fs::create_directories(exports);
    const auto save=[&](const char* name,const std::string& bytes){std::ofstream file(exports/name,std::ios::binary);file<<bytes;return file.good();};
    const std::array<bool,4> exported{{save("wallpaper-resources.jsonl",history.records),save("wallpaper-resources.csv",history.csv),
        save("wallpaper-resource-summary.txt",history.summary),save("wallpaper-resource-summary.json",history.summaryState.dump(2))}};
    Expect(std::all_of(exported.begin(),exported.end(),[](bool saved){return saved;}),
        "four real-session resource history exports are saved as inspectable evidence");
    evidence["resource_history"]=history.summaryState;
    evidence["resource_history_live_run"]={{"runPrefix",runPrefix},{"startedAtUnixMs",historyStarted},{"playerIdentities",historicalPlayers.size()}};
}
void WatchdogRecovery(const fs::path& executable,const fs::path& video,const fs::path& base,
                      std::uint64_t display,HWND parent) {
    for(const std::string scenario:{"resume-during-recovery","still-covered","manual-pause","playing"}) {
        const auto root=base/scenario;
        fs::create_directories(root);
        std::string first,second;
        Expect(Setup(root,video,display,first,second),"seed isolated watchdog media and preferences");
        if(first.empty())continue;
        std::atomic<bool> armed{false},entered{false},release{false};
        lume::shell::WallpaperSessionOptions options;
        options.root=root.u8string();options.executable=executable.wstring();
        options.verification=true;options.verificationParent=parent;
        options.verificationPrivateBytes=[&](std::uint64_t) -> std::uint64_t {
            if(!armed.exchange(false))return 0;
            entered=true;
            const auto end=GetTickCount64()+5000;
            while(!release&&GetTickCount64()<end)Sleep(1);
            return (scenario=="playing"?2048ull:1024ull)<<20;
        };
        Session session(GetModuleHandleW(nullptr),options,[](const std::wstring&){});
        Expect(session.Restore(),"watchdog session starts asynchronously");
        Expect(Wait([&]{return HasScreenState(session,display,"paused");}),"watchdog fixture restores manual pause");
        if(scenario!="manual-pause") {
            Send(session,Action::Resume,display);
            Expect(Wait([&]{return HasScreenState(session,display,"playing");}),"watchdog fixture starts playback");
        }
        Send(session,Action::Fit,display,{},false);
        Expect(Wait([&]{return Screen(session,display).value("fit",std::string{})=="contain";}),
            "watchdog fixture records non-default fit");
        const auto hidden=SessionWindow();
        Expect(hidden!=nullptr,"watchdog fixture locates only its own event window");
        const bool automatic=scenario=="resume-during-recovery"||scenario=="still-covered";
        if(automatic) {
            SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
            Expect(Wait([&]{return Screen(session,display).value("autoPaused",false);}),
                "automatic pause is tracked before memory protection");
        }
        const auto before=EmbeddedPlayers(parent);
        const auto saved=State(session);
        armed=true;SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);
        Expect(Wait([&]{return entered.load();}),"memory protection reaches controlled threshold sample");
        if(scenario=="resume-during-recovery")
            SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
        release=true;
        Expect(Wait([&]{const auto current=EmbeddedPlayers(parent);return current.size()==1&&current!=before&&
            State(session).value("revision",std::uint64_t{0})>saved.value("revision",std::uint64_t{0});}),
            "unchanged memory threshold restarts exactly one owned player");
        const auto after=EmbeddedPlayers(parent);
        Probe probe;
        Expect(after.size()==1&&probe.Open(after.front()),"watchdog probe connects to replacement mpv");
        if(scenario=="still-covered") {
            Expect(Screen(session,display).value("autoPaused",false)&&probe.Property("pause")==Json(true),
                "recovery keeps automatic pause tracked while coverage remains");
            SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
        } else if(scenario=="manual-pause") {
            SendMessageW(hidden,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
            Pump(2200);
            Expect(HasScreenState(session,display,"paused")&&probe.Property("pause")==Json(true),
                "watchdog and automatic resume never override manual pause");
        }
        const bool shouldPlay=scenario!="manual-pause";
        if(shouldPlay) {
            Expect(Wait([&]{return probe.Follow(parent)&&probe.Property("pause")==Json(false);},6000),
                "returning to desktop resumes actual mpv after watchdog recovery");
            // IPC acknowledges pause before the actor publishes its snapshot.
            // Require both to converge instead of sampling that narrow gap.
            Expect(Wait([&]{return HasScreenState(session,display,"playing")&&
                !Screen(session,display).value("autoPaused",true)&&probe.Property("pause")==Json(false);},6000),
                "public state agrees with resumed player");
        }
        const auto timeBefore=probe.Property("time-pos");Pump(400);
        const auto timeAfter=probe.Property("time-pos");
        Expect(timeBefore.is_number()&&timeAfter.is_number()&&((timeBefore!=timeAfter)==shouldPlay),
            "real video timeline follows automatic versus manual pause policy");
        Expect(probe.Property("panscan")==Json(0.0)&&probe.Property("volume")==Json(0.0),
            "memory recovery preserves fit and volume");
        evidence["watchdog"].push_back({{"scenario",scenario},{"beforePids",before},{"afterPids",after},
            {"snapshot",Screen(session,display)},{"actualPause",probe.Property("pause")},
            {"timeBefore",timeBefore},{"timeAfter",timeAfter}});
        probe.Close();session.Shutdown();
        Expect(Wait([]{return OwnedProcesses().empty();},2000)&&GetWindow(parent,GW_CHILD)==nullptr,
            "watchdog fixture cleans up its player and windows");
    }
}
}
int main(int argc,char** argv) {
    if(argc!=4&&argc!=5) {
        std::cerr<<"Usage: wallpaper-session-playback-test mpv.exe fixture-video.mp4 output-directory [resource-phase-seconds|--balanced-regression|--watchdog-regression|--resume-regression|--early-resume-regression]\n";return 2;
    }
    const auto executable=fs::absolute(fs::u8path(argv[1]));
    const auto video=fs::absolute(fs::u8path(argv[2]));
    const auto output=fs::absolute(fs::u8path(argv[3]));
    const bool balancedRegression=argc==5&&std::string(argv[4])=="--balanced-regression";
    const bool watchdogRegression=argc==5&&std::string(argv[4])=="--watchdog-regression";
    const bool gameRegression=argc==5&&std::string(argv[4])=="--game-regression";
    const bool resumeRegression=argc==5&&std::string(argv[4])=="--resume-regression";
    const bool earlyResumeRegression=argc==5&&std::string(argv[4])=="--early-resume-regression";
    if(argc==5&&!gameRegression&&!balancedRegression&&!watchdogRegression&&!resumeRegression&&!earlyResumeRegression) {
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
    options.verificationRecoveryMilliseconds=3000;
    evidence["executable"]=executable.u8string();evidence["fixture"]=video.u8string();
    evidence["root_pid"]=GetCurrentProcessId();
    evidence["scope"]="owned 640x360 off-screen parent, real session and player descendants; not desktop/compositor acceptance";
    if(earlyResumeRegression) {
        EarlyResumeRecovery(executable,video,root,display,parent);
        DestroyWindow(parent);
        evidence["residual-pids"]=OwnedProcesses();
        for(const auto& item:ownedHandles)CloseHandle(item.second);
        ownedHandles.clear();evidence["failures"]=failures;
        std::ofstream(output/L"early-resume-evidence.json")<<evidence.dump(2);
        return failures?1:0;
    }
    if(resumeRegression||gameRegression) {
        CoverageResumeStability(executable,video,root,display,parent);
        if(!gameRegression)ResumeRecovery(executable,video,root,display,parent);
        DestroyWindow(parent);
        evidence["residual-pids"]=OwnedProcesses();
        for(const auto& item:ownedHandles)CloseHandle(item.second);
        ownedHandles.clear();evidence["failures"]=failures;
        std::ofstream(output/L"resume-policy-evidence.json")<<evidence.dump(2);
        return failures?1:0;
    }
    if(watchdogRegression) {
        WatchdogRecovery(executable,video,root,display,parent);
        DestroyWindow(parent);
        evidence["residual-pids"]=OwnedProcesses();
        for(const auto& item:ownedHandles)CloseHandle(item.second);
        ownedHandles.clear();
        evidence["failures"]=failures;
        std::ofstream(output/L"watchdog-recovery-evidence.json")<<evidence.dump(2);
        return failures?1:0;
    }
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
                return current.is_object()&&current.value("revision",std::uint64_t{0})>revision&&
                    children.size()==1&&children!=players&&HasScreenState(session,display,"paused");
            },12000);
            const auto replacement=EmbeddedPlayers(parent);
            crashes.push_back({{"attempt",attempt+1},{"terminated_pid",players.front()},
                {"replacement_pids",replacement},{"settled",settled},
                {"elapsed_ms",GetTickCount64()-started},{"state",State(session)}});
            Expect(settled,"repeated player crashes recover after bounded cooldown, never stale playing/paused");
            if(replacement.empty()) {
                Expect(false,"recovery remains available after the third failure");
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
