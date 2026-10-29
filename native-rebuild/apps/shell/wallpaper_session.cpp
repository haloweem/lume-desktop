#include "apps/shell/wallpaper_session.h"
#include "platform/windows/text_util.h"
#include "core/wallpaper/library.h"
#include "core/wallpaper/playback_preferences.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/mpv_process_controller.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "platform/windows/wallpaper/download.h"
#include "platform/windows/wallpaper/media_server.h"
#include "platform/windows/wallpaper/static_image.h"
#include "platform/windows/wallpaper/system_wallpaper.h"
#include "vendor/nlohmann/json.hpp"
#include <shobjidl.h>
#include <wtsapi32.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <map>
#include <mutex>
#include <set>
#include <thread>
namespace lume::shell {
namespace {
using Json=nlohmann::json;
using namespace platform::windows;
namespace net=platform::windows::wallpaper;
namespace fs=std::filesystem;
using Action=ui::WallpaperAction;
constexpr UINT NotifyMessage=WM_APP+111;
constexpr std::size_t MaxImportFiles=32;
struct ImportPathLess {
    bool operator()(const std::wstring& left,const std::wstring& right)const {
        return CompareStringOrdinal(left.data(),static_cast<int>(left.size()),
            right.data(),static_cast<int>(right.size()),TRUE)==CSTR_LESS_THAN;
    }
};
using lume::text::Wide;
using lume::text::Utf8;
std::string Kind(lume::wallpaper::MediaKind value) {
    return value==lume::wallpaper::MediaKind::Video?"video":"image";
}
std::string ChooseMedia(HWND owner) {
    IFileOpenDialog* dialog=nullptr;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return {};
    const COMDLG_FILTERSPEC filters[]={
        {L"壁纸视频和图片",L"*.mp4;*.webm;*.mkv;*.mov;*.avi;*.jpg;*.jpeg;*.png;*.webp;*.bmp"}
    };
    dialog->SetOptions(FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_FORCEFILESYSTEM|FOS_DONTADDTORECENT);
    dialog->SetFileTypes(1,filters);dialog->SetTitle(L"导入本地壁纸");
    std::string result;
    if(SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item=nullptr;
        if(SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path=nullptr;
            if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){result=Utf8(path);CoTaskMemFree(path);}
            item->Release();
        }
    }
    dialog->Release();return result;
}
bool Fullscreen(const MpvDisplay& display) {
    struct Context {RECT bounds;bool covered=false;};Context context{display.bounds,false};
    EnumWindows([](HWND window,LPARAM data)->BOOL {
        auto& context=*reinterpret_cast<Context*>(data);
        if(!IsWindowVisible(window)||IsIconic(window)||GetWindow(window,GW_OWNER))return TRUE;
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        if(pid==GetCurrentProcessId())return TRUE;
        wchar_t name[128]{};GetClassNameW(window,name,128);
        if(wcscmp(name,L"Progman")==0||wcscmp(name,L"WorkerW")==0||wcscmp(name,L"Shell_TrayWnd")==0||
            wcscmp(name,L"Shell_SecondaryTrayWnd")==0)return TRUE;
        const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
        if(style&(WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT))return TRUE;
        DWORD cloaked=0;DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(cloaked)return TRUE;
        RECT bounds{};if(!GetWindowRect(window,&bounds))return TRUE;
        if(bounds.left<=context.bounds.left+2&&bounds.top<=context.bounds.top+2&&
            bounds.right>=context.bounds.right-2&&bounds.bottom>=context.bounds.bottom-2){context.covered=true;return FALSE;}
        return TRUE;
    },reinterpret_cast<LPARAM>(&context));
    return context.covered;
}
}
struct WallpaperSession::Impl {
    HINSTANCE instance;WallpaperSessionOptions options;Notify notify;
    HWND window=nullptr;HANDLE wake=nullptr,actorFinished=nullptr;HPOWERNOTIFY power=nullptr;
    std::thread actor,network;
    std::atomic<bool> stopping{false},cancel{false},work{false},locked{false},sleeping{false},rejected{false};
    std::atomic<bool> initializing{false};
    std::atomic<unsigned> pendingCommands{0};
    std::atomic<std::uint64_t> importCancellation{0};
    mutable std::mutex queueMutex,snapshotMutex;
    struct Job {std::uint64_t sequence;std::function<void()> run;};
    std::deque<Job> jobs,completions;
    struct BalancedRequest {
        std::mutex mutex;
        std::condition_variable ready;
        enum class Status {Queued,Running,Done};
        Status status=Status::Queued;
        bool result=false;
        const bool enabled;
        const std::chrono::steady_clock::time_point deadline=
            std::chrono::steady_clock::now()+std::chrono::seconds(2);
        explicit BalancedRequest(bool enabled):enabled(enabled){}
        void CancelPending() {
            std::lock_guard<std::mutex> lock(mutex);
            if(status==Status::Queued){status=Status::Done;ready.notify_all();}
        }
    };
    std::vector<std::shared_ptr<BalancedRequest>> balancedRequests;
    bool acceptingBalanced=false,balancedMode=false,balancedConfirmed=false;
    std::uint64_t nextJob=0,downloadGeneration=0;
    Json snapshot=Json::object(),pendingSnapshot=Json::object();
    Json latestProgress;
    bool notifyPending=false,fullNotification=false;
    std::unique_ptr<MpvProcessController> controller;
    WallpaperMediaServer media;
    LocalSettingsFileStore files;
    lume::wallpaper::PlaybackPreferencesStore preferencesStore{files};
    lume::wallpaper::PlaybackPreferences preferences;
    lume::wallpaper::Library library;
    lume::wallpaper::CatalogCache catalogCache;
    std::vector<lume::wallpaper::CatalogItem> catalog;
    std::vector<MpvDisplay> displays;
    std::map<std::uint64_t,std::string> errors;
    std::map<std::uint64_t,unsigned> retries;
    std::map<std::uint64_t,ULONGLONG> retryAfter;
    // Right after sign-in Lume can start before Explorer has a desktop to
    // attach to. Until this tick that case is retried every DesktopRetryMs and
    // does not count as one of the three retries for a failing player.
    ULONGLONG desktopWaitUntil=0;
    static constexpr ULONGLONG DesktopRetryMs=500,DesktopWaitMs=60000;
    static constexpr std::uint64_t MpvSoftMemoryLimit=1024ull<<20,MpvHardMemoryLimit=2048ull<<20;
    static constexpr ULONGLONG HeartbeatMs=10*60*1000;
    ULONGLONG lastHeartbeat=0;
    Json frames;
    bool framesLoaded=false;
    std::map<std::uint64_t,std::uint64_t> lastPrivate;
    bool WaitingForDesktop()const{
        if(GetTickCount64()>=desktopWaitUntil)return false;
        for(const auto& [id,failure]:errors)if(failure=="MPV_DESKTOP_NOT_READY")return true;
        return false;
    }
    std::set<std::uint64_t> automaticPause;
    std::map<std::string,std::string> mediaUrls;
    std::vector<std::uint64_t> pendingApplyTargets;
    std::string previewId,loadStatus="fresh",error,catalogStatus="idle",catalogError;
    Json download={{"id",""},{"status","idle"},{"received",0},{"total",0},{"error",""}};
    bool writable=true,networkBusy=false,playerAvailable=false,catalogDirty=true,libraryDirty=true;
    bool imageCatalogKnown=false,videoCatalogKnown=false;
    std::vector<std::pair<std::string,std::string>> pendingDownloadRecords;
    struct Admission { std::string id, source; std::uint64_t generation = 0; };
    std::vector<Admission> pendingAdmissions;
    std::shared_ptr<bool> admissionAlive = std::make_shared<bool>(true); // UI-thread gate.
    std::uint64_t revision=0;
    Impl(HINSTANCE instance,WallpaperSessionOptions options,Notify notify)
        :instance(instance),options(std::move(options)),notify(std::move(notify)),
        library(this->options.root),catalogCache(this->options.root){}
    std::string SettingsPath()const{return (fs::u8path(options.root)/"preferences.json").u8string();}
    static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self)return DefWindowProcW(window,message,w,l);
        if(message==NotifyMessage) {
            std::wstring update;
            std::vector<std::pair<std::string,std::string>> records;
            std::vector<Admission> admissions;
            {
                std::lock_guard<std::mutex> lock(self->snapshotMutex);
                update=self->fullNotification?Wide(self->pendingSnapshot.dump()):Wide(Json{{"version",1},{"type","wallpaper-progress"},
                    {"download",self->latestProgress}}.dump());
                self->pendingSnapshot=Json::object();
                self->notifyPending=false;self->fullNotification=false;
                records.swap(self->pendingDownloadRecords);
                admissions.swap(self->pendingAdmissions);
            }
            if(!self->stopping&&self->options.downloadCompleted)for(auto& record:records) {
                try {self->options.downloadCompleted(std::move(record.first),std::move(record.second));}
                catch (...) {}
            }
            if (!self->stopping) for (auto& request : admissions) {
                const auto alive = self->admissionAlive;
                const auto id = request.id; const auto generation = request.generation;
                self->options.authorizeDownload(std::move(request.id), std::move(request.source),
                    [self, alive, id, generation](std::string failure) {
                        if (!*alive) return;
                        self->Complete([self, id, generation, failure=std::move(failure)] {
                            if (generation != self->downloadGeneration) return;
                            if (self->cancel || !failure.empty()) {
                                self->networkBusy=false; self->pendingApplyTargets.clear();
                                self->download["status"]=self->cancel || failure=="WALLPAPER_CANCELLED" ? "cancelled" : "failed";
                                self->download["error"]=self->cancel ? "WALLPAPER_CANCELLED" : failure;
                                self->Publish(); return;
                            }
                            const auto item=std::find_if(self->catalog.begin(),self->catalog.end(),[&](const auto& i){return i.id==id;});
                            if(item!=self->catalog.end())self->StartDownload(*item,generation);
                        });
                    });
            }
            if(!self->stopping&&self->notify)self->notify(update);
            return 0;
        }
        if(message==WM_WTSSESSION_CHANGE) {
            if(w==WTS_SESSION_LOCK)self->locked=true;
            if(w==WTS_SESSION_UNLOCK)self->locked=false;
            SetEvent(self->wake);return 0;
        }
        if(message==WM_POWERBROADCAST) {
            if(w==PBT_APMSUSPEND)self->sleeping=true;
            if(w==PBT_APMRESUMEAUTOMATIC||w==PBT_APMRESUMESUSPEND)self->sleeping=false;
            if(w==PBT_POWERSETTINGCHANGE&&l) {
                const auto setting=reinterpret_cast<POWERBROADCAST_SETTING*>(l);
                if(setting->DataLength==sizeof(DWORD)&&IsEqualGUID(setting->PowerSetting,GUID_CONSOLE_DISPLAY_STATE))
                    self->sleeping=(*reinterpret_cast<const DWORD*>(setting->Data)==0);
            }
            SetEvent(self->wake);return TRUE;
        }
        if(message==WM_DISPLAYCHANGE){SetEvent(self->wake);return 0;}
        return DefWindowProcW(window,message,w,l);
    }
    bool Enqueue(std::function<void()> job) {
        std::lock_guard<std::mutex> lock(queueMutex);
        if(stopping||!wake)return false;
        if(jobs.size()>=64){rejected=true;SetEvent(wake);return false;}
        jobs.push_back({++nextJob,std::move(job)});++pendingCommands;SetEvent(wake);return true;
    }
    void CancelBalancedRequests() {
        // queueMutex also serializes admission with Restore/Shutdown.
        acceptingBalanced=false;
        for(const auto& request:balancedRequests)request->CancelPending();
    }
    void ConfirmUnchangedBalancedRequests() {
        // queueMutex is held. Only the actor confirms a policy; startup file
        // I/O and first frames need not finish to acknowledge that same policy.
        if(stopping||!balancedConfirmed)return;
        for(const auto& request:balancedRequests) {
            std::lock_guard<std::mutex> lock(request->mutex);
            if(request->status==BalancedRequest::Status::Done)continue;
            if(request->status!=BalancedRequest::Status::Queued)break;
            if(std::chrono::steady_clock::now()<request->deadline) {
                // A later no-op cannot overtake an earlier actual mode change.
                if(request->enabled!=balancedMode)break;
                request->result=true;
            }
            request->status=BalancedRequest::Status::Done;
            request->ready.notify_all();
        }
    }
    bool SetBalancedMode(bool enabled) {
        auto request=std::make_shared<BalancedRequest>(enabled);
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if(stopping||!acceptingBalanced)return false;
            if(balancedConfirmed&&balancedMode==enabled&&
                std::all_of(balancedRequests.begin(),balancedRequests.end(),[](const auto& pending) {
                    std::lock_guard<std::mutex> lock(pending->mutex);
                    return pending->status==BalancedRequest::Status::Done;
                }))return true;
            if(jobs.size()>=64)return false;
            jobs.push_back({++nextJob,[this,request,enabled] {
                {
                    std::lock_guard<std::mutex> lock(request->mutex);
                    if(request->status!=BalancedRequest::Status::Queued)return;
                    if(stopping||std::chrono::steady_clock::now()>=request->deadline) {
                        request->status=BalancedRequest::Status::Done;request->ready.notify_all();return;
                    }
                    request->status=BalancedRequest::Status::Running;
                    request->ready.notify_all();
                }
                bool applied=false;
                try {applied=controller->SetBalancedMode(enabled);}catch(...){}
                {
                    std::lock_guard<std::mutex> queueLock(queueMutex);
                    if(applied){balancedMode=enabled;balancedConfirmed=true;}
                    {
                        std::lock_guard<std::mutex> lock(request->mutex);
                        request->result=applied;request->status=BalancedRequest::Status::Done;
                    }
                    if(applied)ConfirmUnchangedBalancedRequests();
                }
                request->ready.notify_all();
            }});
            balancedRequests.push_back(request);
            ++pendingCommands;SetEvent(wake);
        }
        bool result=false;
        {
            std::unique_lock<std::mutex> lock(request->mutex);
            request->ready.wait_until(lock,request->deadline,[&]{
                return request->status!=BalancedRequest::Status::Queued;
            });
            if(request->status==BalancedRequest::Status::Queued)
                request->status=BalancedRequest::Status::Done;
            // Once claimed by the actor, only its bounded transaction may
            // decide the result. A caller timeout cannot race a late commit.
            request->ready.wait(lock,[&]{return request->status==BalancedRequest::Status::Done;});
            result=request->result;
        }
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            balancedRequests.erase(std::remove(balancedRequests.begin(),balancedRequests.end(),request),
                balancedRequests.end());
        }
        return result;
    }
    void Complete(std::function<void()> job) {
        std::lock_guard<std::mutex> lock(queueMutex);
        if(stopping||!wake)return;
        // Only one network operation exists; terminal callbacks never share
        // the bounded, user-controlled page-command queue.
        completions.push_back({++nextJob,std::move(job)});++pendingCommands;SetEvent(wake);
    }
    std::wstring Snapshot()const {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        return snapshot.empty()?std::wstring{}:Wide(snapshot.dump());
    }
    bool NotificationPending()const {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        return notifyPending;
    }
    void PublishProgress() {
        bool post=false;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            latestProgress=download;
            snapshot["download"]=download;
            if(fullNotification)pendingSnapshot["download"]=download;
            if(!notifyPending){notifyPending=true;post=true;}
        }
        if(post&&window&&!stopping)PostMessageW(window,NotifyMessage,0,0);
    }
    std::string Url(const lume::wallpaper::LibraryEntry& entry)const {
        const auto found=mediaUrls.find(entry.item.id);
        if(found!=mediaUrls.end())return found->second;
        if(!entry.protectedMedia)return "https://lume-wallpaper.local/"+fs::u8path(entry.localPath).filename().u8string();
        return {};
    }
    lume::wallpaper::LibraryResult ResolveProtected(const lume::wallpaper::LibraryEntry& entry) {
        if(const auto it=mediaUrls.find(entry.item.id);it!=mediaUrls.end())return {true,entry.item.id,it->second,{}};
        std::string failure;
        auto reader=net::EncryptedMediaReader::Open(options.root,entry.localPath,failure);
        if(!reader)return {false,entry.item.id,{},failure};
        WallpaperMediaSource source;source.bytes=reader->Size();
        source.extension=lume::wallpaper::MediaExtension(entry.item);
        if(!source.extension.empty()&&source.extension.front()=='.')source.extension.erase(0,1);
        if(source.extension=="jpg"||source.extension=="jpeg")source.contentType="image/jpeg";
        else if(source.extension=="png")source.contentType="image/png";
        else if(source.extension=="webp")source.contentType="image/webp";
        else if(source.extension=="bmp")source.contentType="image/bmp";
        else if(source.extension=="webm")source.contentType="video/webm";
        else if(source.extension=="mkv")source.contentType="video/x-matroska";
        else source.contentType="video/mp4";
        source.read=[reader](std::uint64_t offset,char* buffer,std::size_t capacity,std::size_t& read){
            return reader->Read(offset,buffer,capacity,read);
        };
        net::StaticImageSource staticSource{source.bytes,source.read};
        auto url=media.Register(entry.item.id,std::move(source));
        if(url.empty())return {false,entry.item.id,{},"WALLPAPER_MEDIA_SERVER"};
        if(entry.item.kind==lume::wallpaper::MediaKind::Image) {
            const auto slash=url.rfind('/');
            const auto prefix=slash==std::string::npos?std::string{}:url.substr(0,slash+1);
            if(!controller->AllowMediaUrlPrefix(prefix)||
                !controller->AllowStaticImageSource(url,std::move(staticSource))) {
                controller->RevokeMediaUrlPrefix(prefix);media.Remove(entry.item.id);
                return {false,entry.item.id,{},"WALLPAPER_MEDIA_CAPABILITY"};
            }
        }
        mediaUrls[entry.item.id]=url;catalogDirty=true;libraryDirty=true;
        return {true,entry.item.id,url,{}};
    }
    void ReleaseMedia() {
        std::set<std::string> retained;
        if(!previewId.empty())retained.insert(previewId);
        for(const auto& pref:preferences.displays)if(controller->IsRunning(pref.display))retained.insert(pref.wallpaperId);
        for(auto it=mediaUrls.begin();it!=mediaUrls.end();) {
            if(!retained.count(it->first)){
                const auto slash=it->second.rfind('/');
                if(slash!=std::string::npos)controller->RevokeMediaUrlPrefix(it->second.substr(0,slash+1));
                media.Remove(it->first);it=mediaUrls.erase(it);catalogDirty=true;libraryDirty=true;
            }
            else ++it;
        }
    }
    void Publish(bool complete=false) {
        if(complete){catalogDirty=true;libraryDirty=true;}
        Json items=Json::array(),local=Json::array(),screens=Json::array();
        if(catalogDirty)for(const auto& item:catalog) {
            const auto stored=library.Find(item.id);
            // A video not downloaded yet previews from its short clip when the
            // catalog has one; the page only shows this URL, never downloads it.
            const auto& remote=item.previewUrl.empty()?item.mediaUrl:item.previewUrl;
            Json record={{"id",item.id},{"title",item.title},{"kind",Kind(item.kind)},{"thumbnail",item.thumbnailUrl},
                {"url",stored?Url(*stored):remote},{"width",item.width},{"height",item.height},{"bytes",item.sizeBytes},
                {"libraryId",stored?item.id:""}};
            if(!item.heroThumbnailUrl.empty())record["heroThumbnail"]=item.heroThumbnailUrl;
            if(item.homeOrder)record["homeOrder"]=*item.homeOrder;
            items.push_back(std::move(record));
        }
        if(libraryDirty)for(const auto& entry:library.Items()) {
            std::error_code ec;
            Json record={{"id",entry.item.id},{"title",entry.item.title},{"kind",Kind(entry.item.kind)},
                {"thumbnail",entry.item.thumbnailUrl},{"url",Url(entry)},{"bytes",entry.item.sizeBytes},
                {"official",!entry.imported},{"width",entry.item.width},{"height",entry.item.height},
                {"missing",!fs::is_regular_file(fs::u8path(entry.localPath),ec)}};
            if(!entry.item.heroThumbnailUrl.empty())record["heroThumbnail"]=entry.item.heroThumbnailUrl;
            if(entry.item.homeOrder)record["homeOrder"]=*entry.item.homeOrder;
            local.push_back(std::move(record));
        }
        for(const auto& display:displays) {
            const auto pref=std::find_if(preferences.displays.begin(),preferences.displays.end(),[&](const auto& p){return p.display==display.id;});
            const bool has=pref!=preferences.displays.end(),running=controller&&controller->IsRunning(display.id);
            const bool paused=has&&(pref->paused||automaticPause.count(display.id));
            screens.push_back({{"id",std::to_string(display.id)},{"name",Utf8(display.name)},
                {"width",display.bounds.right-display.bounds.left},{"height",display.bounds.bottom-display.bounds.top},
                {"primary",display.primary},{"wallpaperId",has?pref->wallpaperId:""},
                {"state",running?(paused?"paused":"playing"):errors[display.id].empty()?"stopped":"failed"},
                {"autoPaused",automaticPause.count(display.id)!=0},{"fit",!has||pref->cover?"cover":"contain"},
                {"volume",has?pref->volume:0},{"error",errors[display.id]}});
        }
        const bool recoveryPending=std::any_of(preferences.displays.begin(),preferences.displays.end(),[&](const auto& pref){
            return retries[pref.display]<3&&
                std::any_of(displays.begin(),displays.end(),[&](const auto& display){return display.id==pref.display;});
        });
        work=networkBusy||recoveryPending||(controller&&!controller->ActiveDisplays().empty());
        Json update={{"version",1},{"type","wallpaper-state"},{"revision",++revision},
            {"status",networkBusy?"busy":"idle"},{"error",error},{"loadStatus",loadStatus},
            {"playerAvailable",playerAvailable},{"catalogStatus",catalogStatus},{"catalogError",catalogError},
            {"displays",std::move(screens)},{"download",download},
            {"pauseFullscreen",preferences.pauseFullscreen}};
        if(catalogDirty){update["catalog"]=std::move(items);catalogDirty=false;}
        if(libraryDirty){update["library"]=std::move(local);libraryDirty=false;}
        if(complete||!snapshot.contains("likedIds"))update["likedIds"]=Json::array();
        bool post=false;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            // Keep a complete snapshot for reopening the page, but only send
            // changed collections across WebView's JSON boundary.
            snapshot.update(update);
            if(notifyPending&&fullNotification)pendingSnapshot.update(update);
            else pendingSnapshot=update;
            latestProgress=download;
            fullNotification=true;if(!notifyPending){notifyPending=true;post=true;}
        }
        if(post&&window&&!stopping)PostMessageW(window,NotifyMessage,0,0);
    }
    bool StartPlayback(const lume::wallpaper::PlaybackPreference& pref) {
        const bool started=StartPlaybackOnce(pref);
        if(started)ApplySignInStill(pref);
        diag::Log("wallpaper",started?"display "+std::to_string(pref.display)+" playing "+pref.wallpaperId:
            "display "+std::to_string(pref.display)+" failed to play "+pref.wallpaperId+": "+errors[pref.display]);
        return started;
    }
    bool StartPlaybackOnce(const lume::wallpaper::PlaybackPreference& pref) {
        const auto entry=library.Find(pref.wallpaperId);
        if(!entry){errors[pref.display]="WALLPAPER_LIBRARY_NOT_FOUND";return false;}
        if(!playerAvailable&&entry->item.kind==lume::wallpaper::MediaKind::Video) {
            errors[pref.display]="WALLPAPER_PLAYER_MISSING";return false;
        }
        if(options.verification&&!options.verificationParent){errors[pref.display]="WALLPAPER_SYSTEM_EFFECTS_DISABLED";return false;}
        auto resolved=library.ResolvePlayablePath(pref.wallpaperId);
        if(!resolved.ok){errors[pref.display]=resolved.error;return false;}
        if(resolved.path.find("http://127.0.0.1:")==0) {
            const auto slash=resolved.path.rfind('/');
            if(slash==std::string::npos||!controller->AllowMediaUrlPrefix(resolved.path.substr(0,slash+1))) {
                errors[pref.display]="WALLPAPER_MEDIA_CAPABILITY";return false;
            }
        }
        const auto monitor=std::find_if(displays.begin(),displays.end(),[&](const auto& d){return d.id==pref.display;});
        const bool autoPause=locked||sleeping||(!options.verification&&(balancedMode||preferences.pauseFullscreen)&&
            monitor!=displays.end()&&Fullscreen(*monitor));
        if(!controller->Start(pref.display,resolved.path)) {
            // Start either restores the previous media or stops its owned player.
            errors[pref.display]=controller->LastError();return false;
        }
        if(!controller->SetFit(pref.display,pref.cover)||
            !controller->SetVolume(pref.display,pref.volume)||
            !controller->Command(pref.display,(pref.paused||autoPause)?
                lume::wallpaper::MpvCommand::Pause:lume::wallpaper::MpvCommand::Resume)) {
            errors[pref.display]=controller->LastError();controller->Stop(pref.display);return false;
        }
        errors[pref.display].clear();
        if(autoPause&&!pref.paused)automaticPause.insert(pref.display);else automaticPause.erase(pref.display);
        return true;
    }
    bool Save(const lume::wallpaper::PlaybackPreferences& next) {
        if(!writable||!preferencesStore.Save(SettingsPath(),next)){error="WALLPAPER_SAVE_FAILED";return false;}
        preferences=next;loadStatus="loaded";error.clear();return true;
    }
    void Control(const ui::WallpaperPageCommand& command) {
        error.clear();
        if(command.action==Action::PauseFullscreen) {
            auto next=preferences;next.pauseFullscreen=command.flag;Save(next);Poll();return;
        }
        if(command.action==Action::StopAll) {
            pendingApplyTargets.clear();
            auto next=preferences;next.displays.clear();
            if(Save(next)){for(auto id:controller->ActiveDisplays())controller->Stop(id);errors.clear();automaticPause.clear();ReleaseMedia();RestoreSystemWallpaper();}
            Publish();return;
        }
        const auto display=std::find_if(displays.begin(),displays.end(),[&](const auto& d){return d.id==command.display;});
        if(display==displays.end()){error="WALLPAPER_DISPLAY_MISSING";Publish();return;}
        if(command.action==Action::Stop) {
            pendingApplyTargets.erase(std::remove(pendingApplyTargets.begin(),pendingApplyTargets.end(),
                command.display),pendingApplyTargets.end());
        }
        auto next=preferences;
        auto pref=std::find_if(next.displays.begin(),next.displays.end(),[&](const auto& p){return p.display==command.display;});
        if(command.action==Action::Apply) {
            if(!library.Find(command.id)){error="WALLPAPER_LIBRARY_NOT_FOUND";Publish();return;}
            if(pref==next.displays.end()){next.displays.push_back({command.display,command.id,false,0,true});pref=next.displays.end()-1;}
            else {pref->wallpaperId=command.id;pref->paused=false;}
            if(!StartPlayback(*pref)){error=errors[command.display];ReleaseMedia();Publish();return;}
            if(!Save(next)) {
                controller->Stop(command.display);
                for(const auto& previous:preferences.displays)if(previous.display==command.display)StartPlayback(previous);
            }
        } else if(pref==next.displays.end()){error="WALLPAPER_NOT_PLAYING";}
        else if(command.action==Action::Stop) {
            next.displays.erase(pref);
            if(Save(next)){controller->Stop(command.display);errors.erase(command.display);automaticPause.erase(command.display);RestoreSystemWallpaper(command.display);}
        } else {
            const auto original=*pref;bool applied=false;
            if(command.action==Action::Pause){pref->paused=true;applied=controller->Command(command.display,lume::wallpaper::MpvCommand::Pause);}
            else if(command.action==Action::Resume){pref->paused=false;applied=controller->Command(command.display,lume::wallpaper::MpvCommand::Resume);}
            else if(command.action==Action::Fit){pref->cover=command.flag;applied=controller->SetFit(command.display,command.flag);}
            else if(command.action==Action::Volume){pref->volume=command.volume;applied=controller->SetVolume(command.display,command.volume);}
            if(!applied)error=controller->LastError();
            else if(!Save(next)) {
                controller->SetFit(original.display,original.cover);controller->SetVolume(original.display,original.volume);
                controller->Command(original.display,(original.paused||automaticPause.count(original.display))?
                    lume::wallpaper::MpvCommand::Pause:lume::wallpaper::MpvCommand::Resume);
            }else if(command.action==Action::Pause||command.action==Action::Resume)automaticPause.erase(command.display);
            else if(command.action==Action::Fit)ApplySignInStill(*pref);
        }
        retries[command.display]=0;retryAfter[command.display]=GetTickCount64()+5000;
        ReleaseMedia();Publish();
    }
    void ApplyOnDisplays(const std::string& id,const std::vector<std::uint64_t>& targets,bool cancelDownload=false) {
        std::string failure;
        for(const auto display:targets) {
            if(stopping||(cancelDownload&&cancel))break;
            ui::WallpaperPageCommand command;command.action=Action::Apply;command.id=id;command.display=display;
            Control(command);
            if(!error.empty()&&failure.empty())failure=error;
        }
        if(!failure.empty()){error=failure;Publish();}
    }
    void RequestApply(const ui::WallpaperPageCommand& command) {
        if(!writable){error="WALLPAPER_SAVE_FAILED";Publish();return;}
        std::vector<std::uint64_t> targets;
        for(const auto& display:displays) {
            if(command.action==Action::ApplyAll||display.id==command.display)targets.push_back(display.id);
        }
        if(targets.empty()){error="WALLPAPER_DISPLAY_MISSING";Publish();return;}
        std::error_code fileError;
        const auto stored=library.Find(command.id);
        if(stored&&fs::is_regular_file(fs::u8path(stored->localPath),fileError)) {
            for(const auto display:targets)pendingApplyTargets.erase(std::remove(
                pendingApplyTargets.begin(),pendingApplyTargets.end(),display),pendingApplyTargets.end());
            ApplyOnDisplays(command.id,targets);
        }else Download(command.id,std::move(targets));
    }
    void Refresh() {
        if(networkBusy){error="WALLPAPER_BUSY";Publish();return;}
        if(network.joinable())network.join();
        networkBusy=true;cancel=false;catalogStatus="loading";catalogError.clear();Publish();
        network=std::thread([this] {
            auto result=net::FetchCatalog(cancel);
            Complete([this,result=std::move(result)]()mutable {
                if(network.joinable())network.join();
                networkBusy=false;catalogError=result.error;
                if(result.imageCatalogLoaded||result.videoCatalogLoaded) {
                    auto merged=std::move(result.items);
                    for(const auto& previous:catalog) {
                        const bool replaced=previous.kind==lume::wallpaper::MediaKind::Image?
                            result.imageCatalogLoaded:result.videoCatalogLoaded;
                        if(!replaced)merged.push_back(previous);
                    }
                    std::set<std::string> names;
                    bool valid=merged.size()<=lume::wallpaper::kMaxLibraryItems;
                    for(const auto& item:merged) {
                        auto id=item.id;
                        std::transform(id.begin(),id.end(),id.begin(),[](unsigned char c){
                            return static_cast<char>(std::tolower(c));
                        });
                        if(!names.insert(id).second){valid=false;break;}
                    }
                    if(!valid) {
                        catalogError="WALLPAPER_CATALOG_CONFLICT";
                        catalogStatus=(imageCatalogKnown||videoCatalogKnown)?"ready":"failed";Publish();return;
                    }
                    catalog=std::move(merged);
                    imageCatalogKnown=imageCatalogKnown||result.imageCatalogLoaded;
                    videoCatalogKnown=videoCatalogKnown||result.videoCatalogLoaded;
                    // Persist the latest complete in-memory snapshot. A
                    // previous write failure must not roll a failed collection
                    // back to an older disk copy during the next refresh.
                    lume::wallpaper::CatalogResult cacheValue{catalog,{},imageCatalogKnown,videoCatalogKnown};
                    const auto saved=catalogCache.Save(cacheValue);
                    if(!saved.ok)catalogError="WALLPAPER_CATALOG_CACHE_SAVE:"+saved.error;
                    catalogDirty=true;catalogStatus="ready";
                }else {
                    catalogStatus=(imageCatalogKnown||videoCatalogKnown)?"ready":"failed";
                    if(imageCatalogKnown||videoCatalogKnown)catalogError="WALLPAPER_CATALOG_OFFLINE:"+result.error;
                }
                Publish();
            });
        });
    }
    void Download(const std::string& id,std::vector<std::uint64_t> applyTargets={}) {
        if(networkBusy){error="WALLPAPER_BUSY";Publish();return;}
        const auto selected=std::find_if(catalog.begin(),catalog.end(),[&](const auto& i){return i.id==id;});
        if(selected==catalog.end()){error="WALLPAPER_CATALOG_NOT_FOUND";Publish();return;}
        std::error_code fileError;
        const auto stored=library.Find(id);
        if(stored&&fs::is_regular_file(fs::u8path(stored->localPath),fileError)){Publish();return;}
        const auto item=*selected;
        if(network.joinable())network.join();
        networkBusy=true;cancel=false;pendingApplyTargets=std::move(applyTargets);
        const auto generation=++downloadGeneration;
        download={{"id",id},{"status","running"},{"received",0},{"total",item.sizeBytes},{"error",""}};Publish();
        if (options.authorizeDownload) {
            auto source=item.mediaUrl;
            constexpr std::string_view cos="https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com";
            if(source.compare(0,cos.size(),cos)==0)source.replace(0,cos.size(),"https://static.lumedesktop.com");
            { std::lock_guard<std::mutex> lock(snapshotMutex); pendingAdmissions.push_back({id,std::move(source),generation}); }
            Publish();return;
        }
        if (!options.verification) {
            networkBusy=false;pendingApplyTargets.clear();download["status"]="failed";
            download["error"]="WALLPAPER_DOWNLOAD_AUTHORIZATION";Publish();return;
        }
        StartDownload(item,generation);
    }
    void StartDownload(lume::wallpaper::CatalogItem item,std::uint64_t generation) {
        network=std::thread([this,item,generation] {
            net::DownloadOptions setup;setup.protectedWriter=net::MakeProtectedWriterFactory(options.root);
            ULONGLONG last=0;
            const auto destination=(fs::u8path(options.root)/"media"/(item.id+".apk")).u8string();
            auto result=net::Download(item,destination,cancel,[this,&last,generation](const net::DownloadProgress& progress){
                const auto now=GetTickCount64();if(now-last<500&&progress.receivedBytes!=progress.totalBytes)return;last=now;
                Enqueue([this,progress,generation]{
                    if(generation!=downloadGeneration||download["status"]!="running")return;
                    download["received"]=progress.receivedBytes;download["total"]=progress.totalBytes;PublishProgress();
                });
            },setup);
            Complete([this,item,result=std::move(result)] {
                if(network.joinable())network.join();
                networkBusy=false;download["received"]=result.receivedBytes;
                const auto applyTargets=std::move(pendingApplyTargets);pendingApplyTargets.clear();
                download["status"]=result.ok?"complete":result.cancelled?"cancelled":"failed";download["error"]=result.error;
                if(result.ok) {
                    const auto registered=library.RegisterDownload(item,result.path);
                    if(!registered.ok){download["status"]="failed";download["error"]=registered.error;}
                    else {
                        catalogDirty=true;libraryDirty=true;
                        if(options.downloadCompleted && !options.authorizeDownload) {
                            auto source=item.mediaUrl;
                            constexpr std::string_view cos="https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com";
                            if(source.compare(0,cos.size(),cos)==0)
                                source.replace(0,cos.size(),"https://static.lumedesktop.com");
                            std::lock_guard<std::mutex> lock(snapshotMutex);
                            pendingDownloadRecords.emplace_back(item.id,std::move(source));
                        }
                        if(!cancel&&!applyTargets.empty())ApplyOnDisplays(item.id,applyTargets,true);
                    }
                }
                Publish();
            });
        });
    }
    void ImportFiles(std::vector<std::string> imported,std::uint64_t requestCancellation) {
        if (imported.empty()) return;
        if (networkBusy) { error="WALLPAPER_BUSY"; Publish(); return; }
        if (network.joinable()) network.join();
        networkBusy=true; cancel=false; error.clear(); Publish();
        network=std::thread([this,imported=std::move(imported),requestCancellation] {
            lume::wallpaper::Library isolated(options.root);
            std::string failure;
            const auto remember=[&](const std::string& value){if(failure.empty())failure=value;};
            const auto cancelled=[&]{
                if(stopping||cancel||requestCancellation!=importCancellation) {
                    cancel=true;remember("WALLPAPER_CANCELLED");return true;
                }
                return false;
            };
            if (!cancelled()) {
                const auto loaded=isolated.Load();
                if (!loaded.ok) remember(loaded.error);
                std::set<std::wstring,ImportPathLess> seen;
                std::size_t importedCount=0;
                for (const auto& path:imported) {
                    if (!loaded.ok||cancelled()) break;
                    try {
                        if(path.empty()||path.find('\0')!=std::string::npos) {
                            remember("WALLPAPER_IMPORT_PATH");continue;
                        }
                        const auto source=fs::absolute(fs::u8path(path)).lexically_normal().make_preferred();
                        if(source.native().size()>32767){remember("WALLPAPER_IMPORT_PATH");continue;}
                        if(!seen.insert(source.native()).second)continue;
                        if(importedCount>=MaxImportFiles){remember("WALLPAPER_IMPORT_LIMIT");break;}
                        if(cancelled())break;
                        const auto result=isolated.ImportLocal(source.u8string(),&cancel);
                        if(result.ok)++importedCount;
                        else {
                            remember(result.error);
                            if(result.error=="WALLPAPER_CANCELLED")break;
                        }
                    }catch(const std::exception&){remember("WALLPAPER_IMPORT_PATH");}
                }
            }
            Complete([this,failure=std::move(failure)] {
                if (network.joinable()) network.join();
                networkBusy=false;
                const auto refreshed=library.Load();
                error=!failure.empty()?failure:refreshed.ok?std::string{}:refreshed.error;
                if (refreshed.ok) {
                    catalogDirty=true;
                    libraryDirty=true;
                }
                Publish();
            });
        });
    }
    void Command(const ui::WallpaperPageCommand& command,const std::string& imported,
        std::uint64_t requestCancellation) {
        error.clear();
        switch(command.action) {
        case Action::Snapshot:Publish(true);break;
        case Action::Refresh:Refresh();break;
        case Action::Download:Download(command.id);break;
        case Action::Apply:case Action::ApplyAll:RequestApply(command);break;
        case Action::Like: {
            const auto stored=library.Find(command.id);
            const bool known=(stored&&!stored->imported)||
                std::any_of(catalog.begin(),catalog.end(),[&](const auto& item){return item.id==command.id;});
            if(!known){error="WALLPAPER_CATALOG_NOT_FOUND";Publish();break;}
            error="WALLPAPER_AUTH_REQUIRED";
            Publish();break;
        }
        case Action::Stats:case Action::EndStats:break;
        case Action::Cancel:cancel=true;pendingApplyTargets.clear();Publish();break;
        case Action::Import: {
            if(imported.empty())break;
            ImportFiles({imported},requestCancellation);
            break;
        }
        case Action::Preview: {
            previewId=command.id;
            if(const auto entry=library.Find(command.id);entry&&entry->protectedMedia) {
                const auto result=ResolveProtected(*entry);if(!result.ok)error=result.error;
            }
            ReleaseMedia();Publish();break;
        }
        case Action::EndPreview:previewId.clear();ReleaseMedia();Publish();break;
        case Action::Remove: {
            if(networkBusy){error="WALLPAPER_BUSY";Publish();break;}
            const bool active=std::any_of(preferences.displays.begin(),preferences.displays.end(),[&](const auto& p){return p.wallpaperId==command.id;});
            if(active||previewId==command.id){error="WALLPAPER_MEDIA_IN_USE";Publish();break;}
            const auto result=library.Remove(command.id);if(!result.ok)error=result.error;
            else {catalogDirty=true;libraryDirty=true;}
            Publish();break;
        }
        default:Control(command);break;
        }
    }
    void Poll() {
        auto current=EnumerateMpvDisplays();bool changed=current.size()!=displays.size();
        if(!changed)for(std::size_t i=0;i<current.size();++i)
            if(current[i].id!=displays[i].id||!EqualRect(&current[i].bounds,&displays[i].bounds)){changed=true;break;}
        displays=std::move(current);
        for(const auto id:controller->ActiveDisplays()) {
            if(std::none_of(displays.begin(),displays.end(),[&](const auto& d){return d.id==id;})){
                controller->Stop(id);automaticPause.erase(id);changed=true;
            }
        }
        for(const auto& pref:preferences.displays) {
            const auto display=std::find_if(displays.begin(),displays.end(),[&](const auto& d){return d.id==pref.display;});
            if(display==displays.end())continue;
            if(!controller->IsRunning(pref.display)) {
                if(errors[pref.display].empty()){
                    errors[pref.display]="WALLPAPER_PLAYER_EXITED";changed=true;
                    const auto code=controller->ExitCode(pref.display);
                    diag::Log("wallpaper","display "+std::to_string(pref.display)+" player exited, code "+
                        (code?diag::ExitCodeText(*code):std::string("unknown"))+", last private "+std::to_string(lastPrivate[pref.display]>>20)+" MB");
                }
                const bool desktop=errors[pref.display]=="MPV_DESKTOP_NOT_READY"&&GetTickCount64()<desktopWaitUntil;
                if((desktop||retries[pref.display]<3)&&GetTickCount64()>=retryAfter[pref.display]) {
                    if(!desktop)++retries[pref.display];
                    retryAfter[pref.display]=GetTickCount64()+(desktop?DesktopRetryMs:5000);
                    StartPlayback(pref);changed=true;
                }
                continue;
            }
            if(!controller->EnsureAttached(pref.display)) {
                errors[pref.display]=controller->LastError();changed=true;
                if(retries[pref.display]<3&&GetTickCount64()>=retryAfter[pref.display]) {
                    ++retries[pref.display];retryAfter[pref.display]=GetTickCount64()+5000;
                    controller->Stop(pref.display);StartPlayback(pref);
                }
                continue;
            }
            if(!errors[pref.display].empty()){errors[pref.display].clear();changed=true;}
            const bool shouldPause=locked||sleeping||(!options.verification&&(balancedMode||preferences.pauseFullscreen)&&Fullscreen(*display));
            // Some GPU drivers make mpv grow without bound over hours of looping
            // (users saw 15 GB overnight). Restart the player once it is clearly
            // past normal use: while hidden past the soft limit, at once past the
            // hard one. Start keeps media, pause, volume and fit.
            if(const auto bytes=controller->PrivateBytes(pref.display)){
                lastPrivate[pref.display]=bytes;
                const bool hidden=locked||sleeping||pref.paused||automaticPause.count(pref.display);
                if(bytes>=(hidden?MpvSoftMemoryLimit:MpvHardMemoryLimit)){
                    diag::Log("wallpaper","display "+std::to_string(pref.display)+" player at "+std::to_string(bytes>>20)+
                        " MB private, restarting ("+(hidden?"hidden":"visible")+")");
                }
                if(bytes>=(hidden?MpvSoftMemoryLimit:MpvHardMemoryLimit)&&controller->Recover(pref.display)){
                    automaticPause.erase(pref.display);changed=true;continue;
                }
            }
            if(!pref.paused&&shouldPause&&!automaticPause.count(pref.display)) {
                if(controller->Command(pref.display,lume::wallpaper::MpvCommand::Pause)){automaticPause.insert(pref.display);changed=true;}
            }else if(!pref.paused&&!shouldPause&&automaticPause.count(pref.display)) {
                if(controller->Command(pref.display,lume::wallpaper::MpvCommand::Resume)){automaticPause.erase(pref.display);changed=true;}
            }
        }
        Heartbeat();
        ReleaseMedia();if(changed)Publish();
    }
    // Windows shows its own desktop wallpaper from sign-in until Lume starts.
    // Make that a still of the Lume wallpaper (a video's first frame), so the
    // desktop looks right at once and the live wallpaper simply takes over.
    // frames.json keeps which still each display has and the user's own
    // wallpaper, put back when the Lume wallpaper is removed.
    fs::path FramesDirectory()const{return fs::u8path(options.root)/"frames";}
    Json& Frames(){
        if(!framesLoaded){
            framesLoaded=true;
            std::ifstream in(FramesDirectory()/"frames.json",std::ios::binary);
            frames=in?Json::parse(in,nullptr,false):Json::object();
            if(!frames.is_object())frames=Json::object();
        }
        return frames;
    }
    void SaveFrames(){
        std::error_code fsError;fs::create_directories(FramesDirectory(),fsError);
        const auto file=FramesDirectory()/"frames.json",temp=FramesDirectory()/"frames.json.tmp";
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<Frames().dump(2);if(!out)return;}
        MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING);
    }
    void ApplySignInStill(const lume::wallpaper::PlaybackPreference& pref){
        if(options.verification)return;
        const auto display=std::find_if(displays.begin(),displays.end(),[&](const auto& d){return d.id==pref.display;});
        if(display==displays.end())return;
        const auto key=pref.wallpaperId+(pref.cover?"|cover":"|fit");
        auto& entry=Frames()[std::to_string(pref.display)];
        if(!entry.is_object())entry=Json::object();
        std::error_code fsError;
        const auto current=fs::u8path(entry.value("image",std::string{}));
        if(entry.value("key",std::string{})==key&&!current.empty()&&fs::is_regular_file(current,fsError))return;
        fs::create_directories(FramesDirectory(),fsError);
        // A new name per still: Windows keeps its cached copy for an unchanged path.
        const auto stamp=std::to_string(GetTickCount64());
        const auto image=FramesDirectory()/("display-"+std::to_string(pref.display)+"-"+stamp+".jpg");
        if(!controller->SaveFrame(pref.display,image.wstring())||!fs::is_regular_file(image,fsError)){
            diag::Log("wallpaper","display "+std::to_string(pref.display)+" sign-in still not captured: "+controller->LastError());
            return;
        }
        if(!entry.contains("original")){
            const auto original=net::SystemWallpaper(display->bounds);
            const auto path=original?Utf8(*original):std::string{};
            // Never remember one of Lume's own stills as the user's wallpaper.
            entry["original"]=path.find(FramesDirectory().u8string())==0?std::string{}:path;
        }
        if(!net::SetSystemWallpaper(display->bounds,image.wstring(),pref.cover)){
            fs::remove(image,fsError);
            diag::Log("wallpaper","display "+std::to_string(pref.display)+" sign-in still could not be set as the Windows wallpaper");
            return;
        }
        if(!current.empty()&&current!=image)fs::remove(current,fsError);
        entry["key"]=key;entry["image"]=image.u8string();
        SaveFrames();
        diag::Log("wallpaper","display "+std::to_string(pref.display)+" sign-in still set to "+pref.wallpaperId);
    }
    // Puts the user's own Windows wallpaper back (one display, or all of them).
    void RestoreSystemWallpaper(std::optional<std::uint64_t> only={}){
        if(options.verification)return;
        auto& all=Frames();
        bool changed=false;
        for(auto item=all.begin();item!=all.end();){
            const auto id=std::strtoull(item.key().c_str(),nullptr,10);
            if(only&&*only!=id){++item;continue;}
            const auto display=std::find_if(displays.begin(),displays.end(),[&](const auto& d){return d.id==id;});
            const auto original=item.value().is_object()?item.value().value("original",std::string{}):std::string{};
            const auto image=item.value().is_object()?item.value().value("image",std::string{}):std::string{};
            std::error_code fsError;
            if(display!=displays.end()&&!original.empty()&&fs::is_regular_file(fs::u8path(original),fsError))
                net::SetSystemWallpaper(display->bounds,Wide(original),true);
            if(!image.empty())fs::remove(fs::u8path(image),fsError);
            item=all.erase(item);changed=true;
        }
        if(changed)SaveFrames();
    }
    // One line every ten minutes: a slow leak shows up as a rising series.
    void Heartbeat() {
        const auto now=GetTickCount64();
        if(lastHeartbeat&&now-lastHeartbeat<HeartbeatMs)return;
        lastHeartbeat=now;
        std::string line;
        for(const auto& pref:preferences.displays){
            line+=" display "+std::to_string(pref.display)+"="+pref.wallpaperId+" "+
                (controller->IsRunning(pref.display)?std::to_string(controller->PrivateBytes(pref.display)>>20)+"MB":std::string("stopped"))+
                (pref.paused||automaticPause.count(pref.display)?" paused":"");
        }
        if(!line.empty())diag::Log("wallpaper","heartbeat"+line+(locked?" locked":"")+(sleeping?" display-off":""));
    }
    void RunActor() {
        controller=std::make_unique<MpvProcessController>(options.executable,options.verificationParent);
        controller->SetCancellation(&stopping);
        if(controller->SetBalancedMode(options.balancedMode)) {
            std::lock_guard<std::mutex> lock(queueMutex);
            balancedMode=options.balancedMode;balancedConfirmed=true;
            ConfirmUnchangedBalancedRequests();
        }
        std::error_code ec;playerAvailable=!options.executable.empty()&&fs::is_regular_file(options.executable,ec);
        library.SetProtectedResolver([this](const auto& entry){return ResolveProtected(entry);});
        const auto loaded=library.Load();
        if(!loaded.ok){error=loaded.error;writable=false;loadStatus="failed";}
        const auto config=preferencesStore.Load(SettingsPath());
        preferences=config.value;writable=writable&&config.writable;
        loadStatus=writable?(library.Recovered()?"recovered":config.status):"failed";
        // Playback first: the saved wallpaper needs only the local library. The
        // online catalog cache (megabytes of JSON) is for the manager page.
        displays=EnumerateMpvDisplays();desktopWaitUntil=GetTickCount64()+DesktopWaitMs;
        for(const auto& pref:preferences.displays) {
            if(stopping)break;
            if(std::any_of(displays.begin(),displays.end(),[&](const auto& d){return d.id==pref.display;})) {
                StartPlayback(pref);
                retryAfter[pref.display]=GetTickCount64()+(errors[pref.display]=="MPV_DESKTOP_NOT_READY"?DesktopRetryMs:5000);
            }
        }
        const auto cached=catalogCache.Load();
        if(cached.ok&&cached.found) {
            catalog=catalogCache.Items();catalogStatus="ready";catalogError="WALLPAPER_CATALOG_CACHED";
            imageCatalogKnown=catalogCache.HasImageCatalog();videoCatalogKnown=catalogCache.HasVideoCatalog();
        }else if(!cached.ok)catalogError=cached.error;
        Publish();
        initializing=false;
        if(!work&&!NotificationPending())Publish();
        while(!stopping) {
            const auto wait=preferences.displays.empty()?INFINITE:WaitingForDesktop()?static_cast<DWORD>(DesktopRetryMs):2000;
            MsgWaitForMultipleObjects(1,&wake,FALSE,wait,QS_ALLINPUT);
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            std::deque<Job> pending;
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                // Terminal callbacks have reserved capacity, not priority:
                // earlier Stop/Cancel jobs must precede a completed download.
                while(!jobs.empty()||!completions.empty()) {
                    auto& source=jobs.empty()?completions:completions.empty()?jobs:
                        jobs.front().sequence<completions.front().sequence?jobs:completions;
                    pending.push_back(std::move(source.front()));source.pop_front();
                }
            }
            for(auto& job:pending){if(stopping)break;job.run();--pendingCommands;}
            if(rejected.exchange(false)&&!stopping){error="WALLPAPER_BUSY";Publish();}
            if(!stopping)Poll();
            // A page can close while a command is queued. Notify again after
            // the final command finishes so Shell can reevaluate keep-alive.
            if(!stopping&&!pending.empty()&&!pendingCommands&&!work&&!NotificationPending())Publish();
        }
        cancel=true;if(network.joinable())network.join();
        for(auto id:controller->ActiveDisplays())controller->Stop(id);
        media.Stop();mediaUrls.clear();controller.reset();work=false;pendingCommands=0;initializing=false;
    }
    bool Restore() {
        if(stopping)return false;
        if(actor.joinable())return true;
        if(options.root.empty())return false;
        std::error_code ec;fs::create_directories(fs::u8path(options.root)/"media",ec);if(ec)return false;
        wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!wake)return false;
        actorFinished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!actorFinished){CloseHandle(wake);wake=nullptr;return false;}
        WNDCLASSEXW klass{};klass.cbSize=sizeof(klass);klass.hInstance=instance;klass.lpfnWndProc=WindowProc;
        klass.lpszClassName=L"LumeNativeWallpaperSession";
        if(!RegisterClassExW(&klass)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){
            CloseHandle(wake);wake=nullptr;CloseHandle(actorFinished);actorFinished=nullptr;return false;
        }
        window=CreateWindowExW(0,klass.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,this);
        if(!window){
            CloseHandle(wake);wake=nullptr;CloseHandle(actorFinished);actorFinished=nullptr;return false;
        }
        if(!options.verification) {
            WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION);
            power=RegisterPowerSettingNotification(window,&GUID_CONSOLE_DISPLAY_STATE,DEVICE_NOTIFY_WINDOW_HANDLE);
        }
        initializing=true;
        try{
            std::lock_guard<std::mutex> lock(queueMutex);
            acceptingBalanced=true;
            actor=std::thread([this] {
            try{RunActor();}catch(...){
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    CancelBalancedRequests();
                }
                cancel=true;if(network.joinable())network.join();
                error="WALLPAPER_RUNTIME_FAILED";work=false;pendingCommands=0;initializing=false;
                if(controller){for(auto id:controller->ActiveDisplays())controller->Stop(id);}
                media.Stop();mediaUrls.clear();
                try{Publish();}catch(...){}
                controller.reset();
            }
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                CancelBalancedRequests();
            }
            SetEvent(actorFinished);
        });}catch(...){Shutdown();return false;}
        return true;
    }
    void Shutdown() {
        *admissionAlive=false;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            stopping=true;cancel=true;CancelBalancedRequests();if(wake)SetEvent(wake);
        }
        if(actor.joinable()) {
            // Destroying an actor-owned child can synchronously notify its
            // UI-thread parent. A plain join would block both threads.
            // Only service sent messages; do not reenter page/timer callbacks
            // or consume WM_QUIT while ownership is being torn down.
            while(MsgWaitForMultipleObjectsEx(1,&actorFinished,INFINITE,QS_SENDMESSAGE,
                MWMO_INPUTAVAILABLE)!=WAIT_OBJECT_0) {
                MSG sent{};PeekMessageW(&sent,nullptr,0,0,PM_NOREMOVE|PM_QS_SENDMESSAGE);
            }
            actor.join();
        }
        if(network.joinable())network.join();
        media.Stop();work=false;pendingCommands=0;initializing=false;
        if(power){UnregisterPowerSettingNotification(power);power=nullptr;}
        if(window){WTSUnRegisterSessionNotification(window);DestroyWindow(window);window=nullptr;}
        if(wake){CloseHandle(wake);wake=nullptr;}
        if(actorFinished){CloseHandle(actorFinished);actorFinished=nullptr;}
        std::lock_guard<std::mutex> lock(queueMutex);jobs.clear();completions.clear();
    }
};
WallpaperSession::WallpaperSession(HINSTANCE instance,WallpaperSessionOptions options,Notify notify)
    :impl_(std::make_unique<Impl>(instance,std::move(options),std::move(notify))){}
WallpaperSession::~WallpaperSession(){Shutdown();}
bool WallpaperSession::Restore(){return impl_->Restore();}
bool WallpaperSession::SetBalancedMode(bool enabled){return impl_->SetBalancedMode(enabled);}
void WallpaperSession::HandlePage(const ui::WallpaperPageCommand& command,HWND owner) {
    if(impl_->stopping)return;
    if(command.action==Action::Cancel){++impl_->importCancellation;impl_->cancel=true;}
    std::string path;
    if(command.action==Action::Import){if(impl_->options.verification)return;path=ChooseMedia(owner);if(path.empty())return;}
    const auto requestCancellation=impl_->importCancellation.load();
    impl_->Enqueue([self=impl_.get(),command,path=std::move(path),requestCancellation]{
        self->Command(command,path,requestCancellation);
    });
}
void WallpaperSession::HandleDroppedFiles(std::vector<std::wstring> paths) {
    if(impl_->stopping||paths.empty())return;
    const auto requestCancellation=impl_->importCancellation.load();
    std::vector<std::string> imported;
    imported.reserve(paths.size());
    for(const auto& path:paths) {
        // Keep invalid inputs in order so the worker reports the first error.
        imported.push_back(path.size()>32767?std::string{}:Utf8(path));
    }
    impl_->Enqueue([self=impl_.get(),imported=std::move(imported),requestCancellation]{
        self->ImportFiles(std::move(imported),requestCancellation);
    });
}
std::wstring WallpaperSession::State()const{return impl_->Snapshot();}
bool WallpaperSession::IsOfficialId(std::string_view id)const {
    if(!ui::ValidWallpaperId(id)||impl_->stopping)return false;
    std::lock_guard<std::mutex> lock(impl_->snapshotMutex);
    for(const auto* key:{"catalog","library"}) {
        const auto records=impl_->snapshot.find(key);
        if(records==impl_->snapshot.end()||!records->is_array())continue;
        for(const auto& item:*records)if(item.value("id",std::string{})==id&&
            (std::string_view(key)=="catalog"||item.value("official",false)))return true;
    }
    return false;
}
std::wstring WallpaperSession::MediaDirectory()const {
    return impl_->options.root.empty()?std::wstring{}:(fs::u8path(impl_->options.root)/"media").wstring();
}
bool WallpaperSession::HasWork()const{return impl_->work||impl_->initializing||impl_->pendingCommands!=0;}
void WallpaperSession::Shutdown(){impl_->Shutdown();}
}
