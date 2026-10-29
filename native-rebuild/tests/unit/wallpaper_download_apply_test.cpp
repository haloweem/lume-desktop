#include "apps/shell/wallpaper_session.h"
#include "core/wallpaper/catalog.h"
#include "core/wallpaper/library.h"
#include "platform/windows/mpv_process_controller.h"
#include "platform/windows/wallpaper/download.h"
#include "vendor/nlohmann/json.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {
using Json=nlohmann::json;
using Session=lume::shell::WallpaperSession;
using Action=lume::ui::WallpaperAction;
namespace fs=std::filesystem;
namespace net=lume::platform::windows::wallpaper;
int failures=0;
Json evidence={{"verification_only",true},{"checks",Json::array()}};

void Check(bool pass,const char* reason) {
    std::cout<<(pass?"PASS: ":"FAIL: ")<<reason<<std::endl;
    evidence["checks"].push_back({{"check",reason},{"passed",pass}});
    if(!pass)++failures;
}
std::string Utf8(const std::wstring& value) {
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),
        static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(count<=0)return {};
    std::string result(static_cast<std::size_t>(count),0);
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),
        result.data(),count,nullptr,nullptr);
    return result;
}
void Pump(DWORD milliseconds) {
    const auto until=GetTickCount64()+milliseconds;
    do {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        Sleep(5);
    }while(GetTickCount64()<until);
}
Json State(Session& session) {
    const auto text=Utf8(session.State());
    return text.empty()?Json{}:Json::parse(text,nullptr,false);
}
Json Screen(const Json& state,std::uint64_t display) {
    if(state.is_object()&&state.contains("displays")&&state["displays"].is_array())
        for(const auto& item:state["displays"])
            if(item.value("id",std::string{})==std::to_string(display))return item;
    return {};
}
bool ScreenIs(const Json& state,std::uint64_t display,const char* expected,const std::string& id={}) {
    const auto screen=Screen(state,display);
    return screen.is_object()&&screen.value("state",std::string{})==expected&&
        (id.empty()||screen.value("wallpaperId",std::string{})==id);
}
void Send(Session& session,Action action,std::uint64_t display=0,const std::string& id={}) {
    lume::ui::WallpaperPageCommand command;
    command.action=action;command.display=display;command.id=id;session.HandlePage(command);
}
std::vector<DWORD> EmbeddedPlayers(HWND parent) {
    std::set<DWORD> players;
    EnumChildWindows(parent,[](HWND child,LPARAM data)->BOOL {
        DWORD pid=0;GetWindowThreadProcessId(child,&pid);
        if(pid&&pid!=GetCurrentProcessId())reinterpret_cast<std::set<DWORD>*>(data)->insert(pid);
        return TRUE;
    },reinterpret_cast<LPARAM>(&players));
    return {players.begin(),players.end()};
}
struct Children {
    std::map<DWORD,HANDLE> retained;
    bool complete=true;
    ~Children(){for(const auto& item:retained)CloseHandle(item.second);}
    std::vector<DWORD> Observe() {
        const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot==INVALID_HANDLE_VALUE){complete=false;return {MAXDWORD};}
        std::vector<std::pair<DWORD,DWORD>> entries;
        PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
        if(Process32FirstW(snapshot,&entry))do {
            entries.push_back({entry.th32ProcessID,entry.th32ParentProcessID});
        }while(Process32NextW(snapshot,&entry));
        else complete=false;
        CloseHandle(snapshot);
        std::set<DWORD> live{GetCurrentProcessId()};
        for(auto it=retained.begin();it!=retained.end();) {
            if(WaitForSingleObject(it->second,0)==WAIT_OBJECT_0) {
                CloseHandle(it->second);it=retained.erase(it);
            }else {live.insert(it->first);++it;}
        }
        bool added=true;
        while(added) {
            added=false;
            for(const auto& item:entries)if(live.count(item.second)&&!live.count(item.first)) {
                const auto handle=OpenProcess(SYNCHRONIZE,FALSE,item.first);
                if(!handle){complete=false;continue;}
                retained.emplace(item.first,handle);live.insert(item.first);added=true;
            }
        }
        live.erase(GetCurrentProcessId());
        return {live.begin(),live.end()};
    }
};
bool Wait(Children& children,const std::function<bool()>& condition,DWORD timeout=12000) {
    const auto until=GetTickCount64()+timeout;
    do {Pump(10);children.Observe();if(condition())return true;}while(GetTickCount64()<until);
    return false;
}
struct Probe {
    HANDLE pipe=INVALID_HANDLE_VALUE;
    std::string pending;
    std::uint64_t serial=900000;
    ~Probe(){if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);}
    bool Open(DWORD pid) {
        WIN32_FIND_DATAW entry{};
        const auto pattern=L"\\\\.\\pipe\\lume-native-mpv-"+std::to_wstring(GetCurrentProcessId())+L"-*";
        const auto finder=FindFirstFileW(pattern.c_str(),&entry);
        if(finder==INVALID_HANDLE_VALUE)return false;
        do {
            const auto name=L"\\\\.\\pipe\\"+std::wstring(entry.cFileName);
            const auto candidate=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,
                OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
            if(candidate==INVALID_HANDLE_VALUE)continue;
            ULONG owner=0;
            if(GetNamedPipeServerProcessId(candidate,&owner)&&owner==pid){pipe=candidate;break;}
            CloseHandle(candidate);
        }while(FindNextFileW(finder,&entry));
        FindClose(finder);return pipe!=INVALID_HANDLE_VALUE;
    }
    bool Transfer(bool write,void* data,DWORD size,DWORD& transferred) {
        OVERLAPPED operation{};operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!operation.hEvent)return false;
        auto done=write?WriteFile(pipe,data,size,&transferred,&operation):
            ReadFile(pipe,data,size,&transferred,&operation);
        if(!done&&GetLastError()==ERROR_IO_PENDING) {
            const auto until=GetTickCount64()+1500;
            while(WaitForSingleObject(operation.hEvent,0)==WAIT_TIMEOUT&&GetTickCount64()<until)Pump(5);
            if(WaitForSingleObject(operation.hEvent,0)==WAIT_OBJECT_0)
                done=GetOverlappedResult(pipe,&operation,&transferred,FALSE);
            else {
                CancelIoEx(pipe,&operation);DWORD ignored=0;
                GetOverlappedResult(pipe,&operation,&ignored,TRUE);
            }
        }
        CloseHandle(operation.hEvent);return done!=FALSE;
    }
    Json Property(const char* name) {
        if(pipe==INVALID_HANDLE_VALUE)return {};
        const auto id=++serial;
        auto request=Json{{"command",Json::array({"get_property",name})},{"request_id",id}}.dump()+"\n";
        DWORD written=0;
        if(!Transfer(true,request.data(),static_cast<DWORD>(request.size()),written)||written!=request.size())return {};
        const auto until=GetTickCount64()+2000;
        while(GetTickCount64()<until) {
            const auto newline=pending.find('\n');
            if(newline==std::string::npos) {
                std::array<char,4096> bytes{};DWORD read=0;
                if(!Transfer(false,bytes.data(),static_cast<DWORD>(bytes.size()),read)||!read)return {};
                pending.append(bytes.data(),read);if(pending.size()>65536)return {};continue;
            }
            const auto reply=Json::parse(pending.substr(0,newline),nullptr,false);pending.erase(0,newline+1);
            if(reply.is_object()&&reply.value("request_id",std::uint64_t{0})==id)
                return reply.value("error",std::string{})=="success"&&reply.contains("data")?reply["data"]:Json{};
        }
        return {};
    }
};
struct Parent {
    HWND window=nullptr;
    ~Parent(){if(window)DestroyWindow(window);}
};
bool EncryptedOnly(const fs::path& root) {
    Json files=Json::array();bool valid=true;
    for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file()) {
        const auto relative=entry.path().lexically_relative(root);
        auto extension=entry.path().extension().u8string();
        std::transform(extension.begin(),extension.end(),extension.begin(),
            [](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(extension==".mp4"||extension==".webm"||extension==".mkv"||extension==".mov"||extension==".avi"||
            extension==".m4v"||extension==".part"||extension==".tmp")valid=false;
        files.push_back({{"path",relative.u8string()},{"bytes",entry.file_size()}});
    }
    evidence["isolated-files"]=std::move(files);return valid;
}
int Finish(const fs::path& output,int code) {
    evidence["failures"]=failures;
    std::ofstream stream(output/"download-apply-evidence.json");
    stream<<evidence.dump(2);stream.close();
    return stream.fail()?1:code;
}
int Run(const fs::path& executable,const fs::path& output,const std::string& id) {
    evidence["executable"]=executable.u8string();evidence["official-id"]=id;
    const auto root=output/"wallpapers";
    std::atomic<bool> cancel{false};std::mutex mutex;std::condition_variable finished;bool complete=false;
    std::thread deadline([&] {
        std::unique_lock<std::mutex> lock(mutex);
        if(!finished.wait_for(lock,std::chrono::seconds(40),[&]{return complete;}))cancel=true;
    });
    const auto catalog=net::FetchCatalog(cancel);
    {std::lock_guard<std::mutex> lock(mutex);complete=true;}
    finished.notify_one();deadline.join();
    evidence["catalog-error"]=catalog.error;evidence["catalog-fetch-cancelled"]=cancel.load();
    const auto found=std::find_if(catalog.items.begin(),catalog.items.end(),
        [&](const auto& item){return item.id==id&&item.kind==lume::wallpaper::MediaKind::Video;});
    Check(catalog.error.empty()&&catalog.videoCatalogLoaded&&found!=catalog.items.end(),
        "live official catalog supplies the requested video without a network error");
    if(!catalog.error.empty()||!catalog.videoCatalogLoaded||found==catalog.items.end()) {
        evidence["blocked-stage"]="live-catalog";
        std::cerr<<"FIXTURE BLOCKED: live official video catalog unavailable or ID missing: "<<catalog.error<<'\n';
        return Finish(output,1);
    }
    const auto item=*found;
    evidence["official-source"]={{"id",item.id},{"url",item.mediaUrl},{"width",item.width},
        {"height",item.height},{"bytes",item.sizeBytes},{"sha256",item.sha256}};
    lume::wallpaper::CatalogCache cache(root.u8string());
    lume::wallpaper::CatalogResult seed;seed.videoCatalogLoaded=true;seed.items.push_back(item);
    Check(cache.Save(seed).ok,"seed only real official metadata into the fresh catalog cache");
    lume::wallpaper::Library before(root.u8string());
    Check(before.Load().ok&&before.Items().empty()&&!fs::exists(root/"library.json"),
        "library and downloaded media are not prepopulated");
    if(failures)return Finish(output,1);
    const auto displays=lume::platform::windows::EnumerateMpvDisplays();
    Check(!displays.empty(),"enumerate actual displays without changing the desktop");
    if(displays.empty())return Finish(output,1);
    Parent parent;
    parent.window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Download apply verification",
        WS_POPUP,-30000,-30000,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(parent.window!=nullptr,"create a test-owned off-screen parent");
    if(!parent.window)return Finish(output,1);
    ShowWindow(parent.window,SW_SHOWNOACTIVATE);
    lume::shell::WallpaperSessionOptions options{root.u8string(),executable.wstring(),true,parent.window};
    Children children;
    // Intentionally no notification callback or manager window. Download/apply
    // intent must complete while the UI is absent; state is queried explicitly.
    Session session(GetModuleHandleW(nullptr),options,{});
    Check(session.Restore(),"restore Session with no management-page notification consumer");
    Check(Wait(children,[&]{
        const auto state=State(session);return state.is_object()&&state.value("catalogStatus",std::string{})=="ready";
    }),"Session loads the seeded official catalog");
    Check(State(session).value("library",Json::array()).empty(),"Session starts with an empty media library");
    const auto display=displays.front().id;
    const auto started=GetTickCount64();
    Send(session,Action::Apply,display,id);
    Check(GetTickCount64()-started<100,"Apply dispatch returns without waiting for the network");
    Check(session.HasWork(),"download-and-apply intent retains Session work immediately");
    bool sawRunning=false;
    const bool settled=Wait(children,[&] {
        const auto state=State(session);
        if(!state.is_object())return false;
        const auto download=state.value("download",Json::object());
        const auto status=download.value("status",std::string{});
        sawRunning=sawRunning||status=="running";
        return ScreenIs(state,display,"playing",id)||status=="failed"||status=="cancelled"||
            (!state.value("error",std::string{}).empty()&&status!="running");
    },40000);
    auto state=State(session);
    evidence["download-apply-elapsed-ms"]=GetTickCount64()-started;
    evidence["observed-download-running"]=sawRunning;evidence["download-apply-state"]=state;
    const bool playing=settled&&ScreenIs(state,display,"playing",id);
    Check(playing,"uncached Apply downloads encrypted media and automatically starts playback");
    const auto download=state.value("download",Json::object());
    Check(download.value("id",std::string{})==id&&download.value("status",std::string{})=="complete"&&
        download.value("error",std::string{}).empty()&&download.value("received",std::uint64_t{0})==item.sizeBytes,
        "download finishes complete with the official media byte count");
    if(!playing) {
        const auto failure=download.value("error",std::string{});
        const bool networkBlocked=failure.find("WALLPAPER_HTTP_")==0;
        evidence["blocked-stage"]=networkBlocked?"live-download-fixture":"download-or-apply";
        if(networkBlocked)std::cerr<<"FIXTURE BLOCKED: current live media network failed: "<<failure<<'\n';
        std::cerr<<"LIVE DOWNLOAD/APPLY FAILED (not skipped): "<<state.dump()<<'\n';
        session.Shutdown();
        Check(Wait(children,[&]{return children.Observe().empty();},2000),"failed live fixture still releases all children");
        Check(children.complete&&!session.HasWork()&&GetWindow(parent.window,GW_CHILD)==nullptr,
            "failed live fixture leaves no retained work or native container");
        Check(EncryptedOnly(root),"failed live fixture leaves no cleartext video or staging files");
        return Finish(output,1);
    }
    lume::wallpaper::Library persisted(root.u8string());
    Check(persisted.Load().ok,"reload the actual registered library from disk");
    const auto entry=persisted.Find(id);
    Check(entry&&entry->protectedMedia&&!entry->imported&&fs::u8path(entry->localPath).extension()==".apk"&&
        fs::is_regular_file(fs::u8path(entry->localPath)),"Apply registers an encrypted official library entry");
    std::string readerError;
    auto reader=entry?net::EncryptedMediaReader::Open(root.u8string(),entry->localPath,readerError):nullptr;
    Check(reader&&reader->Size()==item.sizeBytes,"registered ciphertext opens through the protected reader at the verified size");
    reader.reset();
    Check(EncryptedOnly(root),"download-and-apply never creates a cleartext video file");
    auto players=EmbeddedPlayers(parent.window);
    Check(players.size()==1,"single-display Apply embeds exactly one real player under the owned parent");
    {
        Probe probe;
        Check(players.size()==1&&probe.Open(players.front()),"connect an independent probe only to the owned real player");
        const auto path=probe.Property("path");
        Check(path.is_string()&&path.get<std::string>().find("http://127.0.0.1:")==0,
            "real mpv reads the Session protected-media loopback capability");
        const auto time=probe.Property("time-pos");Pump(350);
        const auto advanced=probe.Property("time-pos");
        Check(time.is_number()&&advanced.is_number()&&time!=advanced,"downloaded official video timeline really advances");
        Send(session,Action::Pause,display);
        Check(Wait(children,[&]{return ScreenIs(State(session),display,"paused",id);}),"Pause updates the actual display state");
        Check(probe.Property("pause")==Json(true),"Pause reaches real mpv after download-and-apply");
        evidence["single-display-player-pids"]=players;
    }
    Send(session,Action::Stop,display);
    Check(Wait(children,[&]{return ScreenIs(State(session),display,"stopped")&&children.Observe().empty()&&!session.HasWork();}),
        "per-display Stop exits the complete owned player tree");
    Check(GetWindow(parent.window,GW_CHILD)==nullptr&&!session.HasWork(),"per-display Stop removes its native container and work");
    const auto ciphertext=entry?fs::u8path(entry->localPath):fs::path{};
    const auto previousWrite=ciphertext.empty()?fs::file_time_type{}:fs::last_write_time(ciphertext);
    Send(session,Action::ApplyAll,0,id);
    bool downloadedAgain=false;
    Check(Wait(children,[&] {
        const auto all=State(session);
        if(all.is_object())downloadedAgain=downloadedAgain||
            all.value("download",Json::object()).value("status",std::string{})=="running";
        return std::all_of(displays.begin(),displays.end(),
            [&](const auto& target){return ScreenIs(all,target.id,"playing",id);});
    },static_cast<DWORD>(12000+10000*displays.size())),
        "cached ApplyAll returns playing for every actual enumerated display");
    evidence["apply-all-state"]=State(session);
    const auto allPlayers=EmbeddedPlayers(parent.window);
    Check(allPlayers.size()==displays.size(),"ApplyAll creates one owned embedded player per actual display");
    Check(!downloadedAgain&&(!ciphertext.empty()&&fs::last_write_time(ciphertext)==previousWrite),
        "cached ApplyAll reuses the same protected file without a second download");
    for(const auto& target:displays)
        Check(ScreenIs(State(session),target.id,"playing",id),"ApplyAll exposes a successful per-display result");
    evidence["apply-all-player-pids"]=allPlayers;
    evidence["display-count"]=displays.size();
    Send(session,Action::StopAll);
    Check(Wait(children,[&] {
        const auto stopped=State(session);
        return std::all_of(displays.begin(),displays.end(),
            [&](const auto& target){return ScreenIs(stopped,target.id,"stopped");})&&children.Observe().empty();
    }),"StopAll removes every owned display player and descendant");
    const auto shutdown=GetTickCount64();session.Shutdown();
    evidence["shutdown-ms"]=GetTickCount64()-shutdown;
    evidence["residual-pids"]=children.Observe();
    Check(children.complete&&children.Observe().empty()&&!session.HasWork(),
        "Shutdown leaves zero observed descendants and no retained Session work");
    Check(GetWindow(parent.window,GW_CHILD)==nullptr,"Shutdown leaves zero native child containers");
    Check(EncryptedOnly(root),"final isolated library contains no cleartext video or incomplete download");
    return Finish(output,failures?1:0);
}
}
int main(int argc,char** argv) {
    if(argc<3||argc>4) {
        std::cerr<<"Usage: wallpaper-download-apply-test mpv.exe fresh-output-directory [official-id]\n";return 2;
    }
    const auto executable=fs::absolute(fs::u8path(argv[1]));
    const auto output=fs::absolute(fs::u8path(argv[2]));
    if(fs::exists(output)||!fs::is_regular_file(executable)) {
        std::cerr<<"A valid verification player and a new output directory are required\n";return 2;
    }
    fs::create_directories(output);
    try {return Run(executable,output,argc==4?argv[3]:"scrolling-through-eyes");}
    catch(const std::exception& error) {
        Check(false,"live download/apply test raised an exception");evidence["exception"]=error.what();
        std::cerr<<error.what()<<'\n';return Finish(output,1);
    }
}
