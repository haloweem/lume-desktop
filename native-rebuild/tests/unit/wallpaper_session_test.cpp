#include "apps/shell/wallpaper_session.h"
#include "core/wallpaper/catalog.h"
#include "core/wallpaper/library.h"
#include "core/wallpaper/playback_preferences.h"
#include "platform/windows/mpv_process_controller.h"
#include "platform/windows/wallpaper/download.h"
#include "vendor/nlohmann/json.hpp"
#include <windows.h>
#include <winioctl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
namespace fs=std::filesystem;
using Json=nlohmann::json;
using Session=lume::shell::WallpaperSession;
using Action=lume::ui::WallpaperAction;
void Expect(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void Pump(unsigned millis) {
    const auto until=GetTickCount64()+millis;
    do {MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(5);}
    while(GetTickCount64()<until);
}
bool Wait(const std::function<bool()>& condition,unsigned timeout=3000) {
    const auto until=GetTickCount64()+timeout;
    do {Pump(1);if(condition())return true;}while(GetTickCount64()<until);
    return false;
}
std::string Utf8(const std::wstring& value) {
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),
        static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    Expect(size>0,"snapshot contains valid UTF-16");
    std::string result(static_cast<std::size_t>(size),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),
        result.data(),size,nullptr,nullptr);
    return result;
}
Json State(const Session& session) {
    const auto text=session.State();
    return text.empty()?Json::object():Json::parse(Utf8(text));
}
Json Screen(const Json& state,std::uint64_t display) {
    if(!state.is_object()||!state.contains("displays")||!state["displays"].is_array())return Json::object();
    for(const auto& item:state["displays"])
        if(item.value("id",std::string{})==std::to_string(display))return item;
    return Json::object();
}
void Write(const fs::path& path,const std::string& value) {
    std::ofstream out(path,std::ios::binary);
    out<<value;
    Expect(bool(out),"write isolated import fixture");
}
std::string Read(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    Expect(bool(in),"read isolated import fixture");
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
struct ReadGate {
    HANDLE file=INVALID_HANDLE_VALUE;
    OVERLAPPED operation{};
    explicit ReadGate(const fs::path& path) {
        operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Expect(operation.hEvent!=nullptr,"create isolated file-read gate event");
        file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(file==INVALID_HANDLE_VALUE){Release();Expect(false,"open isolated file-read gate");}
        DWORD returned=0;
        // A real oplock holds the next file open until the test releases it.
        // This observes the production I/O boundary without sleeps or mocks.
        const auto ok=DeviceIoControl(file,FSCTL_REQUEST_OPLOCK_LEVEL_1,nullptr,0,nullptr,0,
            &returned,&operation);
        const auto error=GetLastError();
        if(ok||error!=ERROR_IO_PENDING){Release();Expect(false,"request isolated file-read oplock");}
    }
    ~ReadGate(){Release();}
    ReadGate(const ReadGate&)=delete;
    ReadGate& operator=(const ReadGate&)=delete;
    bool Blocked()const{return WaitForSingleObject(operation.hEvent,0)==WAIT_OBJECT_0;}
    void Release() {
        if(file!=INVALID_HANDLE_VALUE) {
            CancelIoEx(file,&operation);
            DWORD ignored=0;GetOverlappedResult(file,&operation,&ignored,TRUE);
            CloseHandle(file);file=INVALID_HANDLE_VALUE;
        }
        if(operation.hEvent){CloseHandle(operation.hEvent);operation.hEvent=nullptr;}
    }
};
lume::shell::WallpaperSessionOptions ImportOptions(const fs::path& root) {
    lume::shell::WallpaperSessionOptions options;
    options.root=root.u8string();
    options.executable=(root/"not-a-player.exe").wstring();
    options.verification=true;
    return options;
}
struct ImportFixture {
    fs::path root,sources;
    Session session;
    explicit ImportFixture(const fs::path& base,bool start=true)
        :root(base/"library"),sources(base/"sources"),
        session(GetModuleHandleW(nullptr),ImportOptions(root),[](const auto&){}) {
        fs::create_directories(root);
        fs::create_directories(sources);
        Write(root/"not-a-player.exe","not an executable");
        if(start)Start();
    }
    void Start() {
        Expect(session.Restore(),"restore verification session under explicit isolated root");
        Expect(Wait([&]{return !session.State().empty()&&!session.HasWork();}),
            "isolated import session initializes");
    }
    fs::path Source(const fs::path& name,const std::string& content="import fixture")const {
        const auto path=sources/name;
        Write(path,content);
        return path;
    }
    void Idle() {
        Expect(Wait([&]{return !session.HasWork();}),"import completes without retained worker ownership");
    }
    void Drop(std::vector<std::wstring> paths) {
        const auto revision=State(session).value("revision",std::uint64_t{0});
        const auto before=GetTickCount64();
        session.HandleDroppedFiles(std::move(paths));
        Expect(GetTickCount64()-before<100,"drop dispatch does not wait for filesystem I/O");
        Expect(Wait([&]{return State(session).value("revision",std::uint64_t{0})>revision&&
            !session.HasWork();},10000),"nonempty drop publishes its terminal result");
    }
    std::vector<lume::wallpaper::LibraryEntry> Persisted()const {
        lume::wallpaper::Library library(root.u8string());
        Expect(library.Load().ok,"fresh Library loads the real imported manifest");
        return library.Items();
    }
    void Cancel() {
        lume::ui::WallpaperPageCommand command;command.action=Action::Cancel;
        session.HandlePage(command);
    }
};
void MultiFileImport(const fs::path& base) {
    ImportFixture fixture(base);
    const auto image=fixture.Source("photo.jpg","image payload");
    const auto video=fixture.Source(fs::path(L"\u6211\u7684\u89c6\u9891.mp4"),"video payload");
    fixture.Drop({image.wstring(),video.wstring()});
    const auto state=State(fixture.session);
    const auto stored=fixture.Persisted();
    Expect(state["error"]==""&&state["library"].size()==2&&stored.size()==2,
        "verification imports every supplied image and UTF-16 video into the isolated library");
    Expect(stored[0].imported&&stored[1].imported&&
        stored[0].item.kind==lume::wallpaper::MediaKind::Image&&
        stored[1].item.kind==lume::wallpaper::MediaKind::Video,
        "real import preserves local media kinds and input order");
    Expect(Read(fs::u8path(stored[0].localPath))==Read(image)&&
        Read(fs::u8path(stored[1].localPath))==Read(video),
        "imported copies match untouched original source bytes");
    for(const auto& entry:stored)
        Expect(fs::weakly_canonical(fs::u8path(entry.localPath)).parent_path()==
            fs::weakly_canonical(fixture.root/"media"),"all imported files stay inside the fixture root");
    Expect(!state["displays"].empty(),"verification enumerates a monitor without applying media");
    lume::ui::WallpaperPageCommand command;command.action=Action::Apply;
    command.id=stored[0].item.id;command.display=std::stoull(state["displays"][0]["id"].get<std::string>());
    fixture.session.HandlePage(command);
    Expect(Wait([&]{
        return fixture.session.State().find(L"WALLPAPER_SYSTEM_EFFECTS_DISABLED")!=std::wstring::npos;
    }),"allowing verification imports does not allow system playback");
    command.action=Action::StopAll;fixture.session.HandlePage(command);fixture.Idle();
    fixture.session.Shutdown();
    Session reopened(GetModuleHandleW(nullptr),ImportOptions(fixture.root),[](const auto&){});
    Expect(reopened.Restore(),"reopen isolated imported library");
    Expect(Wait([&]{return State(reopened).contains("library")&&!reopened.HasWork();}),
        "reopened import session initializes");
    Expect(State(reopened)["library"].size()==2,"all imported items survive session restart");
}
void DuplicatePaths(const fs::path& base) {
    ImportFixture fixture(base);
    const auto source=fixture.Source(fs::path(L"\u00e4-photo.jpg"));
    const auto upper=(fixture.sources/fs::path(L"\u00c4-PHOTO.JPG")).wstring();
    fixture.Drop({source.wstring(),source.wstring(),upper,
        (fixture.sources/"."/source.filename()).wstring(),source.generic_wstring(),
        source.lexically_relative(fs::current_path()).wstring()});
    Expect(fixture.Persisted().size()==1&&State(fixture.session)["error"]=="",
        "case, Unicode case, dot, slash and relative aliases import only once per batch");
    fixture.Drop({source.wstring()});
    Expect(fixture.Persisted().size()==2,"deduplication is batch-local, not an implicit content deduplication policy");
}
void InvalidFiles(const fs::path& base) {
    ImportFixture fixture(base);
    const auto bad=fixture.Source("unsupported.txt");
    const auto empty=fixture.Source("empty.png","");
    const auto first=fixture.Source("first.jpg");
    const auto second=fixture.Source("second.mp4");
    fixture.Drop({bad.wstring(),first.wstring(),(fixture.sources/"missing.jpg").wstring(),
        fixture.sources.wstring(),empty.wstring(),second.wstring()});
    Expect(fixture.Persisted().size()==2&&State(fixture.session)["library"].size()==2,
        "unsupported, missing, directory and empty inputs do not stop later valid imports");
    Expect(State(fixture.session)["error"]=="WALLPAPER_IMPORT_FORMAT",
        "only the first invalid file error survives the batch");
    fixture.Drop({(fixture.sources/"missing.jpg").wstring(),fixture.sources.wstring(),empty.wstring()});
    Expect(fixture.Persisted().size()==2&&State(fixture.session)["error"]=="WALLPAPER_IMPORT_MISSING",
        "all-invalid batch reports its first error and leaves previous media intact");
    auto embedded=first.wstring();embedded.push_back(L'\0');embedded+=L"suffix";
    fixture.Drop({L"",std::wstring(32768,L'x'),std::wstring(1,static_cast<wchar_t>(0xd800)),
        embedded,second.wstring()});
    Expect(State(fixture.session)["error"]=="WALLPAPER_IMPORT_PATH"&&fixture.Persisted().size()==3,
        "invalid UTF-16, NUL, empty and oversized paths cannot suppress a later valid file");
}
void ImportLimit(const fs::path& base) {
    for(unsigned variant=0;variant<3;++variant) {
        ImportFixture fixture(base/std::to_string(variant));
        std::vector<std::wstring> paths;
        const unsigned count=variant==0?32:33;
        for(unsigned i=0;i<count;++i)
            paths.push_back(fixture.Source("limit-"+std::to_string(i)+".jpg").wstring());
        paths.insert(paths.begin()+1,paths.front());
        paths.push_back(paths.front());
        if(variant==2)paths.insert(paths.begin(),fixture.Source("invalid.txt").wstring());
        fixture.Drop(paths);
        const char* expected=variant==0?"":variant==1?"WALLPAPER_IMPORT_LIMIT":"WALLPAPER_IMPORT_FORMAT";
        Expect(fixture.Persisted().size()==32,"import limit counts successful unique files, not invalid or duplicate paths");
        Expect(State(fixture.session)["error"]==expected,
            "32 files succeed; overflow is explicit unless an earlier file error already exists");
    }
}
void ImportClearsError(const fs::path& base) {
    ImportFixture fixture(base);
    lume::ui::WallpaperPageCommand command;command.action=Action::Like;command.id="unknown";
    fixture.session.HandlePage(command);fixture.Idle();
    Expect(State(fixture.session)["error"]=="WALLPAPER_CATALOG_NOT_FOUND","seed a previous session error");
    const auto source=fixture.Source("blocked.jpg");
    const auto other=fixture.Source("busy.jpg");
    ReadGate gate(source);
    const auto before=GetTickCount64();
    fixture.session.HandleDroppedFiles({source.wstring()});
    Expect(GetTickCount64()-before<100,"blocked input is never opened by drop dispatch");
    Expect(Wait([&]{return gate.Blocked();}),"worker reaches the gated source read");
    Expect(State(fixture.session)["status"]=="busy"&&State(fixture.session)["error"]=="",
        "starting a batch clears stale error before file I/O completes");
    fixture.session.HandleDroppedFiles({other.wstring()});
    Expect(Wait([&]{return State(fixture.session)["error"]=="WALLPAPER_BUSY";}),
        "a concurrent batch is rejected without starting another worker");
    gate.Release();fixture.Idle();
    Expect(fixture.Persisted().size()==1&&State(fixture.session)["error"]=="",
        "successful completion clears the busy rejection without importing the rejected batch");
}
void CancelQueuedImport(const fs::path& base) {
    ImportFixture fixture(base,false);
    Write(fixture.root/"library.json",R"({"version":1,"items":[]})");
    const auto source=fixture.Source("queued.jpg");
    ReadGate startup(fixture.root/"library.json");
    Expect(fixture.session.Restore(),"start actor with a gated isolated library load");
    Expect(Wait([&]{return startup.Blocked();}),"actor is blocked before dequeuing imports");
    fixture.session.HandleDroppedFiles({source.wstring()});
    fixture.Cancel();
    startup.Release();fixture.Idle();
    Expect(State(fixture.session)["error"]=="WALLPAPER_CANCELLED"&&fixture.Persisted().empty(),
        "cancel before actor dispatch prevents queued imports and reports cancellation");
    fixture.Drop({source.wstring()});
    Expect(fixture.Persisted().size()==1&&State(fixture.session)["error"]=="",
        "a new batch after cancellation starts with a fresh cancellation state");
}
void CancelAtQueueLimit(const fs::path& base) {
    ImportFixture fixture(base,false);
    Write(fixture.root/"library.json",R"({"version":1,"items":[]})");
    const auto source=fixture.Source("queued.jpg");
    ReadGate startup(fixture.root/"library.json");
    Expect(fixture.session.Restore(),"start actor before filling its command queue");
    Expect(Wait([&]{return startup.Blocked();}),"actor load is gated before queue saturation");
    fixture.session.HandleDroppedFiles({source.wstring()});
    lume::ui::WallpaperPageCommand snapshot;snapshot.action=Action::Snapshot;
    for(unsigned i=0;i<63;++i)fixture.session.HandlePage(snapshot);
    fixture.Cancel();
    startup.Release();fixture.Idle();
    Expect(fixture.Persisted().empty(),
        "out-of-band cancellation survives a full queue and cannot be reset by queued import startup");
    fixture.Drop({source.wstring()});
    Expect(fixture.Persisted().size()==1,"new imports recover after a full-queue cancellation");
}
void CompletionKeepsFirstError(const fs::path& base) {
    ImportFixture fixture(base);
    const auto bad=fixture.Source("bad.txt");
    const auto good=fixture.Source("blocked.jpg");
    ReadGate gate(good);
    fixture.session.HandleDroppedFiles({bad.wstring(),good.wstring()});
    Expect(Wait([&]{return gate.Blocked();}),"valid file starts after the first format error");
    const std::string unsupported=R"({"version":999,"items":[]})";
    Write(fixture.root/"library.json",unsupported);
    gate.Release();fixture.Idle();
    Expect(State(fixture.session)["error"]=="WALLPAPER_IMPORT_FORMAT",
        "completion reload failure cannot overwrite the first import error");
    Expect(Read(fixture.root/"library.json")==unsupported&&State(fixture.session)["library"].empty(),
        "an externally changed manifest is preserved and failed copies are not published");
    Expect(fs::is_empty(fixture.root/"media"),"failed import registration cleans its copied media");
}
void CancelPartialImport(const fs::path& base,bool firstError) {
    ImportFixture fixture(base);
    const auto first=fixture.Source("committed.jpg");
    const auto blocked=fixture.Source("blocked.mp4");
    const auto last=fixture.Source("never-started.jpg");
    std::vector<std::wstring> paths{first.wstring(),blocked.wstring(),last.wstring()};
    if(firstError)paths.insert(paths.begin(),fixture.Source("bad.txt").wstring());
    ReadGate gate(blocked);
    fixture.session.HandleDroppedFiles(std::move(paths));
    Expect(Wait([&]{return gate.Blocked();}),"second valid import is held at real file I/O");
    const auto committed=fixture.Persisted();
    Expect(committed.size()==1&&committed[0].item.title=="committed",
        "first file is durably committed before cancellation of the next file");
    fixture.Cancel();gate.Release();fixture.Idle();
    Expect(fixture.Persisted().size()==1&&State(fixture.session)["library"].size()==1,
        "cancellation preserves committed files and skips all remaining files");
    Expect(State(fixture.session)["error"]==(firstError?"WALLPAPER_IMPORT_FORMAT":"WALLPAPER_CANCELLED"),
        "cancellation is reported only when no earlier file error exists");
    Expect(std::distance(fs::directory_iterator(fixture.root/"media"),fs::directory_iterator{})==1,
        "cancelled in-flight file leaves no partial media copy");
    Expect(Read(first)=="import fixture"&&Read(blocked)=="import fixture"&&Read(last)=="import fixture",
        "cancel does not alter original input files");
    fixture.Cancel();fixture.Idle();
    fixture.Drop({last.wstring()});
    Expect(fixture.Persisted().size()==2&&State(fixture.session)["error"]=="",
        "repeated cancellation at idle does not poison the next import");
}
void ShutdownImport(const fs::path& base) {
    ImportFixture fixture(base);
    fixture.session.HandleDroppedFiles({});
    Expect(!fixture.session.HasWork(),"an empty drop is a no-op");
    const auto source=fixture.Source("shutdown.jpg");
    fixture.session.HandleDroppedFiles({source.wstring()});
    fixture.session.Shutdown();
    const auto stored=fixture.Persisted();
    Expect(!fixture.session.HasWork()&&stored.size()<=1,"shutdown drains or cancels the queued import");
    Expect(static_cast<std::size_t>(std::distance(fs::directory_iterator(fixture.root/"media"),
        fs::directory_iterator{}))==stored.size(),"shutdown leaves no unregistered partial media");
    fixture.session.HandleDroppedFiles({source.wstring()});
    Pump(20);
    Expect(!fixture.session.HasWork()&&fixture.Persisted().size()==stored.size(),
        "disposed sessions ignore later drops");
    Session invalid(GetModuleHandleW(nullptr),{},[](const auto&){});
    Expect(!invalid.Restore(),"import cannot create an implicit user configuration root");
    invalid.HandleDroppedFiles({source.wstring()});
    Expect(!invalid.HasWork()&&invalid.State().empty(),"unrestored session cannot write imported files");
}
bool SetBalanced(Session& session,bool enabled) {
    auto result=std::async(std::launch::async,[&]{return session.SetBalancedMode(enabled);});
    const bool finished=Wait([&]{return result.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;},10000);
    Expect(finished,"background settings worker receives a bounded mode result");
    return result.get();
}
void BalancedPreferences(const fs::path& base) {
    ImportFixture fixture(base,false);
    Expect(!SetBalanced(fixture.session,true),"unrestored session rejects mode changes");
    fixture.Start();
    for(bool manual:{false,true}) {
        lume::ui::WallpaperPageCommand command;command.action=Action::PauseFullscreen;command.flag=manual;
        fixture.session.HandlePage(command);fixture.Idle();
        const auto saved=Read(fixture.root/"preferences.json");
        for(bool enabled:{true,true,false,false}) {
            Expect(SetBalanced(fixture.session,enabled),"idle actor confirms repeated balanced changes");
            Expect(State(fixture.session)["pauseFullscreen"]==manual&&
                Read(fixture.root/"preferences.json")==saved,
                "balanced mode never overwrites the independent persisted fullscreen option");
        }
    }
    fixture.session.Shutdown();
    Expect(!SetBalanced(fixture.session,true)&&!SetBalanced(fixture.session,false),
        "shutdown rejects even an idempotent mode request");
}
void BalancedDuringRestore(const fs::path& base) {
    ImportFixture fixture(base,false);
    Write(fixture.root/"library.json",R"({"version":1,"items":[]})");
    ReadGate startup(fixture.root/"library.json");
    Expect(fixture.session.Restore(),"start blocked restore for balanced mode");
    Expect(Wait([&]{return startup.Blocked();}),"restore blocks before queued mode work");
    auto result=std::async(std::launch::async,[&]{return fixture.session.SetBalancedMode(true);});
    Pump(40);
    const bool waiting=result.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready;
    startup.Release();
    Expect(waiting,"mode cannot report success before the restore actor executes it");
    Expect(Wait([&]{return result.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;})&&result.get(),
        "queued mode change returns its real result after restore");
}
void BalancedConfirmedRestore(const fs::path& base) {
    for(const bool initial:{false,true}) {
        const auto root=base/(initial?"enabled":"disabled");
        fs::create_directories(root);
        Write(root/"library.json",R"({"version":1,"items":[]})");
        auto options=ImportOptions(root);options.balancedMode=initial;
        Session session(GetModuleHandleW(nullptr),options,[](const auto&){});
        ReadGate startup(root/"library.json");
        Expect(session.Restore(),"start confirmed-policy fixture");
        Expect(Wait([&]{return startup.Blocked();}),"actor confirms the policy before blocking on library I/O");
        auto result=std::async(std::launch::async,[&]{return session.SetBalancedMode(initial);});
        const bool finished=Wait([&]{return result.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;},2500);
        startup.Release();
        const bool applied=result.get();
        Expect(finished&&applied,"already confirmed policy must not expire behind slow restore I/O");
    }
}
void BalancedConfirmedFullQueue(const fs::path& base) {
    ImportFixture fixture(base,false);
    Write(fixture.root/"library.json",R"({"version":1,"items":[]})");
    ReadGate startup(fixture.root/"library.json");
    Expect(fixture.session.Restore(),"start policy fixture before saturating unrelated page queue");
    Expect(Wait([&]{return startup.Blocked();}),"actor confirms its default before the queue fills");
    lume::ui::WallpaperPageCommand snapshot;snapshot.action=Action::Snapshot;
    for(unsigned i=0;i<64;++i)fixture.session.HandlePage(snapshot);
    const bool unchanged=SetBalanced(fixture.session,false);
    const bool changed=SetBalanced(fixture.session,true);
    startup.Release();fixture.Idle();
    Expect(unchanged&&!changed,"full page queue permits confirmed no-op but still rejects a real policy change");
}
void BalancedRequestOrder(const fs::path& base) {
    ImportFixture fixture(base,false);
    Write(fixture.root/"library.json",R"({"version":1,"items":[]})");
    ReadGate startup(fixture.root/"library.json");
    Expect(fixture.session.Restore(),"start blocked actor for policy request ordering");
    Expect(Wait([&]{return startup.Blocked();}),"actor confirms the disabled baseline");
    auto change=std::async(std::launch::async,[&]{return fixture.session.SetBalancedMode(true);});
    Pump(40);
    auto previous=std::async(std::launch::async,[&]{return fixture.session.SetBalancedMode(false);});
    Pump(40);
    const bool bothPending=change.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready&&
        previous.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready;
    startup.Release();
    Expect(Wait([&]{return previous.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;}),
        "both ordered requests settle after restore");
    const bool changed=change.get(),restored=previous.get();
    Expect(bothPending&&changed&&restored,"confirmed value cannot bypass an earlier outstanding opposite request");
}
void BalancedTimeout(const fs::path& base) {
    ImportFixture fixture(base,false);
    Write(fixture.root/"library.json",R"({"version":1,"items":[]})");
    const auto source=fixture.Source("preserved.jpg");
    ReadGate startup(fixture.root/"library.json");
    Expect(fixture.session.Restore(),"start gated restore for timeout");
    Expect(Wait([&]{return startup.Blocked();}),"actor is held before mode dispatch");
    fixture.session.HandleDroppedFiles({source.wstring()});
    auto result=std::async(std::launch::async,[&]{return fixture.session.SetBalancedMode(true);});
    const auto before=GetTickCount64();
    const bool finished=Wait([&]{return result.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;},4000);
    startup.Release();
    Expect(finished&&!result.get(),"queued mode expires without waiting indefinitely for restore I/O");
    Expect(GetTickCount64()-before<4000,"pending mode timeout is bounded");
    fixture.Idle();
    Expect(fixture.Persisted().size()==1,"mode cancellation cannot clear earlier wallpaper jobs");
    Expect(SetBalanced(fixture.session,false),"actor remains usable after mode timeout");
}
void BalancedShutdown(const fs::path& base) {
    ImportFixture fixture(base,false);
    Write(fixture.root/"library.json",R"({"version":1,"items":[]})");
    ReadGate startup(fixture.root/"library.json");
    Expect(fixture.session.Restore(),"start gated restore for shutdown");
    Expect(Wait([&]{return startup.Blocked();}),"actor is held before shutdown");
    auto result=std::async(std::launch::async,[&]{return fixture.session.SetBalancedMode(true);});
    Pump(40);
    bool cancelledBeforeRelease=false;
    std::thread release([&]{
        cancelledBeforeRelease=result.wait_for(std::chrono::milliseconds(600))==std::future_status::ready;
        startup.Release();
    });
    fixture.session.Shutdown();
    release.join();
    Expect(cancelledBeforeRelease&&!result.get(),"shutdown promptly cancels unexecuted mode work even during restore");
    Expect(!fixture.session.HasWork(),"mode shutdown leaves no queued ownership");
}
struct PlaybackFixture {
    fs::path root,media;
    HWND parent=CreateWindowExW(0,L"STATIC",L"Balanced session verification",
        WS_POPUP,-30000,-30000,320,180,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    std::unique_ptr<Session> session;
    explicit PlaybackFixture(const fs::path& base,bool balanced,const char* extension=".mp4")
        :root(base/"library") {
        Expect(parent!=nullptr,"create an owned playback verification parent");
        fs::create_directories(base);
        const auto source=base/(std::string("source")+extension);
        Write(source,"Controlled session media");
        lume::wallpaper::Library library(root.u8string());
        Expect(library.Load().ok,"load isolated playback library");
        const auto imported=library.ImportLocal(source.u8string());
        Expect(imported.ok,"import stub media using real library");
        media=fs::u8path(imported.path);
        const auto displays=lume::platform::windows::EnumerateMpvDisplays();
        Expect(!displays.empty(),"session playback fixture has a display");
        lume::wallpaper::PlaybackPreferences preferences;
        preferences.pauseFullscreen=true;
        preferences.displays.push_back({displays.front().id,imported.id,true,23,false});
        std::string encoded;
        Expect(lume::wallpaper::EncodePlaybackPreferences(preferences,encoded),"encode independent playback preferences");
        Write(root/"preferences.json",encoded);
        std::array<wchar_t,32768> executable{};
        const auto size=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        const auto stub=fs::path(std::wstring(executable.data(),size)).parent_path()/"lume-mpv-process-controller-test.exe";
        Expect(fs::is_regular_file(stub),"build the sibling mpv controller test to supply the existing IPC stub");
        auto options=ImportOptions(root);options.executable=stub.wstring();
        options.verificationParent=parent;options.balancedMode=balanced;
        session=std::make_unique<Session>(GetModuleHandleW(nullptr),options,[](const auto&){});
    }
    ~PlaybackFixture() {
        if(session)session->Shutdown();
        if(parent)DestroyWindow(parent);
    }
    void Ready()const {
        Expect(Wait([&] {
            const auto state=State(*session);
            if(!state.contains("displays"))return false;
            return std::any_of(state["displays"].begin(),state["displays"].end(),[](const auto& display) {
                return display.value("state","")=="paused";
            });
        },5000),"restored playback reaches the actor-owned IPC stub");
    }
    Json Player()const {
        std::ifstream source(fs::path(media.wstring()+L".state.json"));
        return Json::parse(source,nullptr,false);
    }
    bool Filtered()const {
        const auto state=Player();
        return state.is_object()&&state["vf"].size()==2&&state["vf"][1]["label"]=="lume-balanced";
    }
    std::size_t ModeCommands()const {
        std::ifstream source(media.parent_path()/"commands.jsonl");
        std::string line;std::size_t count=0;
        while(std::getline(source,line)) {
            const auto command=Json::parse(line);
            if(command[0]=="vf"||command[0]=="set")++count;
        }
        return count;
    }
};
void BalancedPlayback(const fs::path& base) {
    PlaybackFixture fixture(base,true);
    Expect(fixture.session->Restore(),"restore initially balanced playback");
    fixture.Ready();
    Expect(fixture.Filtered(),"initial restored settings reach MPV before playback");
    const auto arguments=Json::parse(Read(fixture.media.parent_path()/"arguments.json"));
    for(const auto* option:{"--vf-add=@lume-balanced:fps=30","--scale=bilinear",
        "--cscale=bilinear","--dscale=bilinear","--tscale=linear"})
        Expect(std::find(arguments.begin(),arguments.end(),option)!=arguments.end(),
            "first player starts with all original balanced launch options");
    auto preferences=Read(fixture.root/"preferences.json");
    for(bool enabled:{true,false,false,true,false}) {
        Expect(SetBalanced(*fixture.session,enabled)&&fixture.Filtered()==enabled,
            "background worker mode result agrees with real IPC state");
        const auto state=fixture.Player();
        Expect(state["pause"]==true&&state["properties"]["volume"]==23&&
            state["properties"]["panscan"]==0.0&&state["vf"][0]["label"]=="fixture-other",
            "session mode preserves pause, volume, fit and unrelated filters");
        Expect(State(*fixture.session)["pauseFullscreen"]==true&&
            Read(fixture.root/"preferences.json")==preferences,"runtime mode preserves manual fullscreen preference");
    }
    const auto failure=fs::path(fixture.media.wstring()+L".reject-command.json");
    Write(failure,Json::array({"set","tscale","linear"}).dump());
    Expect(!SetBalanced(*fixture.session,true)&&!fixture.Filtered(),
        "session reports a failed MPV transaction without changing its mode");
    Expect(SetBalanced(*fixture.session,true)&&fixture.Filtered(),"failed session mode can be retried");
}
void FailedStaticSwitchKeepsSessionPlaying(const fs::path& base) {
    PlaybackFixture fixture(base,true,".webp");
    const auto sourceVideo=base/"next.mp4";
    Write(sourceVideo,"video fixture");
    lume::wallpaper::Library library(fixture.root.u8string());
    Expect(library.Load().ok,"load isolated switch library");
    const auto importedVideo=library.ImportLocal(sourceVideo.u8string());
    Expect(importedVideo.ok,"import the next video switch fixture");
    const auto displays=lume::platform::windows::EnumerateMpvDisplays();
    const auto reject=fs::path(fs::u8path(importedVideo.path).wstring()+L".reject-command.json");
    Write(reject,Json::array({"vf","add","@lume-balanced:fps=30"}).dump());
    Expect(fixture.session->Restore(),"restore the static switch session");
    fixture.Ready();
    const auto initial=State(*fixture.session);
    const auto previous=Screen(initial,displays.front().id);
    const auto preferences=Read(fixture.root/"preferences.json");
    lume::ui::WallpaperPageCommand apply;
    apply.action=Action::Apply;apply.id=importedVideo.id;apply.display=displays.front().id;
    fixture.session->HandlePage(apply);
    Expect(Wait([&] {
        return State(*fixture.session).value("revision",std::uint64_t{0})>
            initial.value("revision",std::uint64_t{0});
    },5000),"rejected video switch publishes a terminal snapshot");
    const auto state=State(*fixture.session);
    Expect(state.value("error",std::string{})=="MPV_COMMAND_REJECTED",
        "rejected video switch publishes its original playback error");
    const auto screen=Screen(state,displays.front().id);
    Expect(screen["wallpaperId"]==previous["wallpaperId"]&&screen["state"]=="paused",
        "failed video switch keeps the previous static wallpaper and pause state");
    Expect(Read(fixture.root/"preferences.json")==preferences,
        "failed video switch does not persist an unconfirmed wallpaper");
    Expect(Wait([&] {
        const auto player=fixture.Player();
        return player.is_object()&&player.value("running",false)&&player["pause"]==true&&
            player["properties"]["volume"]==23&&player["properties"]["panscan"]==0.0;
    }),
        "production session leaves the recovered player alive with all previous options");
}
void NativeStaticWithoutPlayer(const fs::path& base,bool official) {
    fs::create_directories(base);
    const auto source=base/"native.bmp";
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    info.biSize=sizeof(info);info.biWidth=2;info.biHeight=2;
    info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=16;
    file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+16;
    {
        std::ofstream image(source,std::ios::binary);
        const std::array<DWORD,4> pixels={0x0000ff00,0x0000ff00,0x0000ff00,0x0000ff00};
        image.write(reinterpret_cast<const char*>(&file),sizeof(file));
        image.write(reinterpret_cast<const char*>(&info),sizeof(info));
        image.write(reinterpret_cast<const char*>(pixels.data()),sizeof(pixels));
        Expect(bool(image),"write owned BMP image fixture");
    }
    const auto root=base/"library";
    lume::wallpaper::Library library(root.u8string());
    Expect(library.Load().ok,"load isolated native static library");
    lume::wallpaper::LibraryResult registered;
    if(official) {
        const auto encrypted=root/"media"/"native.apk";
        fs::create_directories(encrypted.parent_path());
        std::string failure;
        auto writer=lume::platform::windows::wallpaper::MakeProtectedWriterFactory(root.u8string())(
            encrypted.u8string(),failure);
        const auto clear=Read(source);
        Expect(writer&&writer->Append(reinterpret_cast<const std::uint8_t*>(clear.data()),clear.size(),failure)&&
            writer->Finish(failure),"encrypt owned static fixture with production writer");
        lume::wallpaper::CatalogItem item;
        item.id="native-static-fixture";item.title="Native static fixture";
        item.kind=lume::wallpaper::MediaKind::Image;
        item.thumbnailUrl="https://static.lumedesktop.com/lume/native.bmp";
        item.mediaUrl=item.thumbnailUrl;item.width=2;item.height=2;item.sizeBytes=clear.size();
        item.sha256=std::string(64,'a');
        registered=library.RegisterDownload(item,encrypted.u8string());
    } else registered=library.ImportLocal(source.u8string());
    Expect(registered.ok,"register the isolated native static fixture");
    const auto displays=lume::platform::windows::EnumerateMpvDisplays();
    Expect(!displays.empty(),"native static fixture enumerates a display");
    const auto parent=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Native session fixture",
        WS_POPUP,-30000,-30000,320,180,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Expect(parent!=nullptr,"create owned offscreen native parent");
    auto options=ImportOptions(root);options.verificationParent=parent;
    // The executable deliberately does not exist; static decode must never need it.
    options.executable=(root/"missing-mpv.exe").wstring();
    try {
        Session session(GetModuleHandleW(nullptr),options,[](const auto&){});
        Expect(session.Restore(),"restore native static session without a player");
        Expect(Wait([&]{return State(session).contains("library")&&!session.HasWork();}),
            "native static session publishes initial state");
        Expect(State(session)["playerAvailable"]==false,"video player remains accurately unavailable");
        lume::ui::WallpaperPageCommand command;
        command.action=Action::Apply;command.id=registered.id;command.display=displays.front().id;
        session.HandlePage(command);
        Expect(Wait([&]{return Screen(State(session),command.display).value("state","")=="playing";},5000),
            "static image applies through production actor with no mpv executable");
        DWORD childProcess=0;
        const auto host=GetWindow(parent,GW_CHILD);
        GetWindowThreadProcessId(host,&childProcess);
        Expect(host&&childProcess==GetCurrentProcessId()&&GetWindow(host,GW_CHILD)==nullptr,
            "native image owns only an in-process drawing window");
        command.action=Action::Fit;command.flag=false;session.HandlePage(command);
        Expect(Wait([&]{return Screen(State(session),command.display).value("fit","")=="contain";}),
            "native image accepts and persists contain fit");
        command.action=Action::Pause;session.HandlePage(command);
        Expect(Wait([&]{return Screen(State(session),command.display).value("state","")=="paused";}),
            "native image preserves manual pause semantics");
        session.Shutdown();
        Expect(GetWindow(parent,GW_CHILD)==nullptr,"native actor shutdown releases its host");
        Session reopened(GetModuleHandleW(nullptr),options,[](const auto&){});
        Expect(reopened.Restore(),"reopen native static session without a player");
        Expect(Wait([&]{return Screen(State(reopened),command.display).value("state","")=="paused";},5000),
            "native image restores persisted wallpaper and pause without mpv");
        command.action=Action::StopAll;reopened.HandlePage(command);
        Expect(Wait([&]{return Screen(State(reopened),command.display).value("state","")=="stopped"&&
            !reopened.HasWork();}),"native image cancels and releases actor ownership");
        reopened.Shutdown();
        if(official) {
            const auto stored=library.Find(registered.id);
            Expect(stored&&stored->protectedMedia&&Read(fs::u8path(stored->localPath)).substr(0,8)=="LUMEWP02",
                "official native image remains encrypted at rest after apply and restore");
            std::size_t mediaFiles=0;
            for(const auto& entry:fs::directory_iterator(root/"media")) {
                ++mediaFiles;Expect(entry.path().extension()==".apk","protected playback exports no cleartext media");
            }
            Expect(mediaFiles==1,"protected native playback creates no extra cache file");
        }
    } catch(...) {DestroyWindow(parent);throw;}
    DestroyWindow(parent);
}
void BalancedPlaybackTimeout(const fs::path& base) {
    PlaybackFixture fixture(base,false);
    ReadGate startup(fixture.root/"library.json");
    Expect(fixture.session->Restore(),"start a gated restored player");
    Expect(Wait([&]{return startup.Blocked();}),"startup is held before the first frame");
    auto result=std::async(std::launch::async,[&]{return fixture.session->SetBalancedMode(true);});
    const bool finished=Wait([&]{return result.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;},4000);
    startup.Release();
    Expect(finished&&!result.get(),"blocked startup mode request expires");
    fixture.Ready();
    Expect(SetBalanced(*fixture.session,false),"subsequent request is an actor queue barrier");
    Expect(!fixture.Filtered()&&fixture.ModeCommands()==0,
        "expired mode job sends no late filter or sampler commands after restore");
}
void BalancedPlaybackShutdown(const fs::path& base) {
    PlaybackFixture fixture(base,false);
    Expect(fixture.session->Restore(),"start playback before in-flight mode shutdown");
    fixture.Ready();
    Write(fixture.media.parent_path()/"delay-balanced","200");
    auto result=std::async(std::launch::async,[&]{return fixture.session->SetBalancedMode(true);});
    const bool entered=Wait([&]{return fs::is_regular_file(fixture.media.parent_path()/"balanced-entered");});
    fixture.session->Shutdown();
    Expect(entered&&!result.get(),"shutdown interrupts a genuinely running MPV mode transaction");
    Expect(!fixture.Filtered()&&GetWindow(fixture.parent,GW_CHILD)==nullptr&&!fixture.session->HasWork(),
        "shutdown returns only after rollback and owned-player cleanup");
}
void BalancedDuringImport(const fs::path& base) {
    ImportFixture fixture(base);
    const auto source=fixture.Source("blocked.jpg");
    ReadGate read(source);
    fixture.session.HandleDroppedFiles({source.wstring()});
    Expect(Wait([&]{return read.Blocked();}),"network worker is occupied by real import I/O");
    const auto before=GetTickCount64();
    const auto applied=SetBalanced(fixture.session,true);
    const auto elapsed=GetTickCount64()-before;
    const auto state=State(fixture.session);
    read.Release();fixture.Idle();
    Expect(applied&&elapsed<1000&&state["status"]=="busy",
        "mode jobs do not wait for or cancel the independent network worker");
    Expect(fixture.Persisted().size()==1,"network operation finishes normally after mode changes");
}
void SessionLifecycle(const fs::path& root) {
    std::filesystem::create_directories(root);
    lume::wallpaper::CatalogCache cache(root.u8string());
    lume::wallpaper::CatalogResult cached;
    cached.videoCatalogLoaded=true;
    cached.items.push_back({"cached-video","Cached video",lume::wallpaper::MediaKind::Video,
        "https://static.lumedesktop.com/lume/test.jpg","https://static.lumedesktop.com/lume/test.mp4",
        1920,1080,100,std::string(64,'a')});
    cached.items.front().heroThumbnailUrl="https://static.lumedesktop.com/lume/test-hero.jpg";
    cached.items.front().homeOrder=0;
    Expect(cache.Save(cached).ok,"seed validated offline catalog in an isolated library");
    {
        std::ofstream legacy(root/"favorites.json");
        legacy << R"({"version":1,"ids":["cached-video"]})";
    }
    lume::shell::WallpaperSessionOptions options;
    options.root=root.u8string();options.executable=(root/"missing-mpv.exe").wstring();options.verification=true;
    unsigned notifications=0;
    std::wstring lastUpdate;
    lume::shell::WallpaperSession session(GetModuleHandleW(nullptr),options,[&](const std::wstring& update){
        ++notifications;lastUpdate=update;
    });
    Expect(session.Restore(),"restore isolated empty wallpaper library");
    for(unsigned i=0;i<100&&!notifications;++i)Pump(10);
    Expect(notifications>0,"host receives asynchronous initial snapshot");
    const auto wide=session.State();
    const auto state=nlohmann::json::parse(wide);
    Expect(state["type"]=="wallpaper-state"&&state["library"].empty()&&!state["playerAvailable"]&&
        !state["pauseFullscreen"],"fresh snapshot preserves original muted and unthrottled defaults");
    Expect(!session.HasWork(),"empty wallpaper does not keep shell alive");
    Expect(state["likedIds"].empty(),"shared device favorites are not loaded into an account");
    Expect(session.IsOfficialId("cached-video")&&!session.IsOfficialId("missing")&&!session.IsOfficialId("../x"),
        "account routing accepts only known official catalog or downloaded IDs");
    Expect(state["catalog"].size()==1&&state["catalogStatus"]=="ready"&&
        state["catalogError"]=="WALLPAPER_CATALOG_CACHED",
        "startup exposes cached official metadata without a network request or player");
    Expect(state["catalog"][0].value("heroThumbnail","")=="https://static.lumedesktop.com/lume/test-hero.jpg"&&
        state["catalog"][0].value("homeOrder",-1)==0&&
        state["catalog"][0]["thumbnail"]=="https://static.lumedesktop.com/lume/test.jpg",
        "page snapshot carries the ordered hero cover separately from the card thumbnail");
    lume::ui::WallpaperPageCommand command;command.action=lume::ui::WallpaperAction::Like;
    command.id="cached-video";command.flag=true;
    const auto beforeLikeNotifications=notifications;
    session.HandlePage(command);
    for(unsigned i=0;i<100&&notifications==beforeLikeNotifications;++i)Pump(10);
    Expect(notifications>beforeLikeNotifications&&
        lastUpdate.find(L"WALLPAPER_AUTH_REQUIRED")!=std::wstring::npos&&
        lastUpdate.find(L"\"catalog\"")==std::wstring::npos&&
        lastUpdate.find(L"\"library\"")==std::wstring::npos,
        "direct actor likes require native account authorization");
    Expect(session.State().find(L"\"likedIds\":[]")!=std::wstring::npos&&
        std::filesystem::is_regular_file(root/"favorites.json"),
        "legacy favorites file is preserved without uploading or applying it");
    command.flag=false;session.HandlePage(command);Pump(80);
    Expect(session.State().find(L"\"likedIds\":[]")!=std::wstring::npos,
        "unauthorized unlikes cannot change device data");
    command.action=lume::ui::WallpaperAction::Apply;
    Expect(!state["displays"].empty(),"isolated state enumerates an actual monitor without changing it");
    command.id="missing";command.display=std::stoull(state["displays"][0]["id"].get<std::string>());
    const auto before=GetTickCount64();session.HandlePage(command);
    Expect(GetTickCount64()-before<100,"dispatch cannot block UI waiting for a player");
    Pump(100);
    Expect(session.State().find(L"missing")!=std::wstring::npos||
        session.State().find(L"NOT_FOUND")!=std::wstring::npos,"missing media is not marked playing");
    Expect(lastUpdate.find(L"\"catalog\"")==std::wstring::npos&&
        lastUpdate.find(L"\"library\"")==std::wstring::npos,
        "playback-only update does not resend the entire wallpaper catalog and library");
    Expect(session.State().find(L"\"catalog\"")!=std::wstring::npos,
        "reopened management page still receives a complete snapshot");
    command.action=lume::ui::WallpaperAction::Snapshot;
    session.HandlePage(command);Pump(50);
    Expect(lastUpdate.find(L"\"catalog\"")!=std::wstring::npos,
        "explicit page snapshot contains the complete catalog");
    session.Shutdown();session.Shutdown();
    Expect(!session.HasWork(),"shutdown cancels all module ownership");
    Expect(!session.Restore(),"disposed wallpaper session cannot restart stale cancellation state");
    lume::shell::WallpaperSession reopened(GetModuleHandleW(nullptr),options,[](const auto&){});
    Expect(reopened.Restore(),"reopen independent wallpaper session");
    for(unsigned i=0;i<100&&reopened.State().empty();++i)Pump(10);
    Expect(reopened.State().find(L"\"likedIds\":[]")!=std::wstring::npos,
        "a fresh session never reintroduces device-wide likes");
    reopened.Shutdown();
    lume::shell::WallpaperSession invalid(GetModuleHandleW(nullptr),{},[](const auto&){});
    Expect(!invalid.Restore(),"missing explicit config root cannot access installed data");
}
void DownloadAdmission(const fs::path& root) {
    fs::create_directories(root);
    lume::wallpaper::CatalogCache cache(root.u8string());lume::wallpaper::CatalogResult catalog;
    catalog.videoCatalogLoaded=true;catalog.items.push_back({"quota-video","Quota video",lume::wallpaper::MediaKind::Video,
        "https://static.lumedesktop.com/lume/test.jpg",
        "https://static.lumedesktop.com/lume/wallpapers/official/videos/quota.mp4",1920,1080,100,std::string(64,'a')});
    Expect(cache.Save(catalog).ok,"admission test seeds catalog without network");
    auto options=ImportOptions(root);std::function<void(std::string)> complete;unsigned requests=0;
    const auto owner=GetCurrentThreadId();
    options.authorizeDownload=[&](std::string id,std::string source,std::function<void(std::string)> callback) {
        Expect(GetCurrentThreadId()==owner&&id=="quota-video"&&source==catalog.items[0].mediaUrl,"admission reaches UI account port with catalog identity");
        ++requests;complete=std::move(callback);
    };
    Session session(GetModuleHandleW(nullptr),options,[](const auto&){});
    Expect(session.Restore()&&Wait([&]{return !session.State().empty()&&!session.HasWork();}),"admission fixture initialized");
    session.HandlePage({Action::Download,"quota-video"});
    Expect(Wait([&]{return requests==1;}),"download asks for authorization before media");
    Expect(State(session)["download"]["received"]==0&&State(session)["library"].empty()&&fs::is_empty(root/"media"),
        "waiting for quota never writes media bytes or creates library entry");
    complete("WALLPAPER_DOWNLOAD_QUOTA");
    Expect(Wait([&]{return State(session)["download"]["status"]=="failed"&&!session.HasWork();}),"quota denial finishes without network download");
    Expect(State(session)["download"]["error"]=="WALLPAPER_DOWNLOAD_QUOTA"&&State(session)["library"].empty()&&fs::is_empty(root/"media"),
        "third guest and sixth member denial cannot leave any downloadable file");
    session.HandlePage({Action::Download,"quota-video"});
    Expect(Wait([&]{return requests==2;}),"new attempt obtains a new admission");
    session.HandlePage({Action::Cancel});complete({});
    Expect(Wait([&]{return State(session)["download"]["status"]=="cancelled"&&!session.HasWork();}),"cancelled admission cannot start media on late success");
    session.HandlePage({Action::Download,"quota-video"});
    Expect(Wait([&]{return requests==3;}),"shutdown test has pending admission");
    session.Shutdown();complete({});
    Expect(!session.HasWork()&&fs::is_empty(root/"media"),"late authorization after shutdown never touches destroyed actor");
}
}
int main(int argc,char** argv) {
    const auto fixtureRoot=fs::current_path()/"wallpaper-session-fixture";
    const auto base=fixtureRoot/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    fs::create_directories(base);
    unsigned failures=0;
    const auto run=[&](const char* name,const std::function<void(const fs::path&)>& test) {
        try {test(base/name);std::cout<<"PASS: "<<name<<'\n';}
        catch(const std::exception& error){++failures;std::cerr<<"FAIL: "<<name<<": "<<error.what()<<'\n';}
    };
    if(argc==2&&std::string_view(argv[1])=="--download-admission-only") {
        run("download-admission",DownloadAdmission);fs::remove_all(base);return failures?1:0;
    }
    run("session-lifecycle",SessionLifecycle);
    run("multi-file-import",MultiFileImport);
    run("duplicate-paths",DuplicatePaths);
    run("invalid-files",InvalidFiles);
    run("import-limit",ImportLimit);
    run("import-clears-error",ImportClearsError);
    run("cancel-queued-import",CancelQueuedImport);
    run("cancel-at-queue-limit",CancelAtQueueLimit);
    run("cancel-partial-import",[](const auto& root){CancelPartialImport(root,false);});
    run("cancel-keeps-first-error",[](const auto& root){CancelPartialImport(root,true);});
    run("completion-keeps-first-error",CompletionKeepsFirstError);
    run("shutdown-import",ShutdownImport);
    run("balanced-preferences",BalancedPreferences);
    run("download-admission",DownloadAdmission);
    run("balanced-during-restore",BalancedDuringRestore);
    run("balanced-confirmed-restore",BalancedConfirmedRestore);
    run("balanced-confirmed-full-queue",BalancedConfirmedFullQueue);
    run("balanced-request-order",BalancedRequestOrder);
    run("balanced-timeout",BalancedTimeout);
    run("balanced-shutdown",BalancedShutdown);
    run("balanced-playback",BalancedPlayback);
    run("failed-static-switch",FailedStaticSwitchKeepsSessionPlaying);
    run("native-static-no-player",[](const auto& root){NativeStaticWithoutPlayer(root,false);});
    run("native-protected-no-player",[](const auto& root){NativeStaticWithoutPlayer(root,true);});
    run("balanced-playback-timeout",BalancedPlaybackTimeout);
    run("balanced-playback-shutdown",BalancedPlaybackShutdown);
    run("balanced-during-import",BalancedDuringImport);
    try {
        Expect(fs::weakly_canonical(base).parent_path()==fs::weakly_canonical(fixtureRoot),
            "fixture cleanup remains inside this test's explicitly owned root");
        fs::remove_all(base);
    }catch(const std::exception& error){++failures;std::cerr<<"FAIL: fixture-cleanup: "<<error.what()<<'\n';}
    std::cout<<"wallpaper session tests: "<<failures<<" failure(s)\n";
    return failures?1:0;
}
