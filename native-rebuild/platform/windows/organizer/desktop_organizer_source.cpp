#include "platform/windows/organizer/desktop_organizer_source.h"
#include "platform/windows/organizer/organizer_text.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
namespace lume::platform::windows {
namespace {
constexpr UINT Changed=WM_APP+0x6c1,Ready=WM_APP+0x6c2;
constexpr UINT_PTR ScanTimer=1,ReconcileTimer=2;
std::string Id(){
    GUID guid{};wchar_t text[40]{};
    if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,text,40))return {};
    return OrganizerUtf8(std::wstring_view(text+1,36));
}
bool Application(const std::filesystem::path& target){
    const auto extension=OrganizerPathKey(target.extension().u8string());
    return extension==".exe"||extension==".com"||extension==".bat"||extension==".cmd"||
        extension==".msi"||extension==".appref-ms"||extension==".url";
}
bool ShortcutApplication(const std::filesystem::path& path){
    IShellLinkW* link=nullptr;
    if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))))return true;
    IPersistFile* file=nullptr;bool application=true;
    if(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))){
        if(SUCCEEDED(file->Load(path.c_str(),STGM_READ))){
            wchar_t raw[32768]{};
            if(SUCCEEDED(link->GetPath(raw,32768,nullptr,SLGP_RAWPATH))&&*raw){
                // Classify by the stored target only: never probe a network or
                // disconnected shortcut target while reading the desktop.
                application=Application(raw);
            }
        }
        file->Release();
    }
    link->Release();return application;
}
organizer::DesktopSnapshot ReadDesktop(const std::atomic_bool& stop,const std::vector<std::wstring>& roots){
    organizer::DesktopSnapshot result;result.complete=true;std::set<std::string> paths;
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::vector<std::wstring> directories=roots;
    if(directories.empty())for(const auto* folderId:{&FOLDERID_Desktop,&FOLDERID_PublicDesktop}){
        PWSTR raw=nullptr;
        if(FAILED(SHGetKnownFolderPath(*folderId,KF_FLAG_DONT_VERIFY,nullptr,&raw))||!raw)result.complete=false;
        else directories.emplace_back(raw);
        CoTaskMemFree(raw);
    }
    for(const auto& root:directories){
        if(stop){result.complete=false;break;}
        const std::filesystem::path directory(root);
        std::error_code error;
        if(!std::filesystem::exists(directory,error)&&!error)continue;
        std::filesystem::directory_iterator it(directory,error),end;
        for(;!error&&it!=end;it.increment(error)){
            if(stop){result.complete=false;break;}
            const auto path=it->path();const auto attributes=GetFileAttributesW(path.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES){result.complete=false;continue;}
            if(attributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM))continue;
            const auto normalized=organizer::NormalizeLocalPath(OrganizerUtf8(path.wstring()));
            if(!normalized||!paths.insert(OrganizerPathKey(*normalized)).second)continue;
            if(result.entries.size()>=organizer::MaxItems*organizer::MaxCollections){result.complete=false;break;}
            const auto extension=OrganizerPathKey(path.extension().u8string());
            const bool directoryEntry=(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
            const auto kind=directoryEntry?organizer::ItemKind::Folder:
                extension==".lnk"?organizer::ItemKind::Shortcut:organizer::ItemKind::File;
            const auto id=Id();if(id.empty()){result.complete=false;break;}
            result.entries.push_back({{id,*normalized,organizer::DisplayName(*normalized,kind),kind,true},
                !directoryEntry&&(extension==".lnk"?ShortcutApplication(path):Application(path))});
        }
        if(error)result.complete=false;
    }
    std::sort(result.entries.begin(),result.entries.end(),[](const auto& a,const auto& b){
        return OrganizerSearchKey(a.item.name)<OrganizerSearchKey(b.item.name);
    });
    if(SUCCEEDED(initialized))CoUninitialize();
    return result;
}
}
struct DesktopOrganizerSource::Impl {
    Impl(HINSTANCE instance,Receive receive,std::vector<std::wstring> roots):instance(instance),receive(std::move(receive)),roots(std::move(roots)){}
    ~Impl(){Stop();}
    static LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self&&message==Changed){SetTimer(hwnd,ScanTimer,250,nullptr);return 0;}
        if(self&&message==WM_TIMER){
            if(w==ScanTimer)KillTimer(hwnd,ScanTimer);
            if(w==ScanTimer||w==ReconcileTimer)self->Request();
            return 0;
        }
        if(self&&message==Ready){
            organizer::DesktopSnapshot snapshot;
            {std::lock_guard lock(self->mutex);if(!self->latest)return 0;snapshot=std::move(*self->latest);self->latest.reset();}
            if(!self->stop&&self->receive)self->receive(std::move(snapshot));
            return 0;
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
    bool Start(){
        if(window)return true;
        WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.hInstance=instance;cls.lpfnWndProc=Procedure;cls.lpszClassName=L"LumeDesktopOrganizerSource";
        if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        window=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,this);
        if(!window)return false;
        std::vector<PIDLIST_ABSOLUTE> identifiers;
        if(roots.empty()){
            for(const auto* id:{&FOLDERID_Desktop,&FOLDERID_PublicDesktop}){
                PIDLIST_ABSOLUTE pidl=nullptr;SHGetKnownFolderIDList(*id,0,nullptr,&pidl);if(pidl)identifiers.push_back(pidl);
            }
        }else for(const auto& root:roots){
            PIDLIST_ABSOLUTE pidl=nullptr;SHParseDisplayName(root.c_str(),nullptr,&pidl,0,nullptr);if(pidl)identifiers.push_back(pidl);
        }
        std::vector<SHChangeNotifyEntry> registrations;for(const auto id:identifiers)registrations.push_back({id,FALSE});
        if(!registrations.empty())watch=SHChangeNotifyRegister(window,SHCNRF_ShellLevel|SHCNRF_InterruptLevel,
            SHCNE_CREATE|SHCNE_DELETE|SHCNE_MKDIR|SHCNE_RMDIR|SHCNE_RENAMEITEM|SHCNE_RENAMEFOLDER|SHCNE_UPDATEDIR|SHCNE_ATTRIBUTES,
            Changed,static_cast<int>(registrations.size()),registrations.data());
        for(const auto id:identifiers)CoTaskMemFree(id);
        stop=false;requested=true;
        try{worker=std::thread([this]{Run();});}catch(...){Stop();return false;}
        if(!SetTimer(window,ReconcileTimer,30000,nullptr)){Stop();return false;}
        return true;
    }
    void Request(){std::lock_guard lock(mutex);requested=true;wake.notify_one();}
    void Run(){
        for(;;){
            {std::unique_lock lock(mutex);wake.wait(lock,[this]{return stop||requested;});if(stop)break;requested=false;}
            organizer::DesktopSnapshot snapshot;
            try{snapshot=ReadDesktop(stop,roots);}catch(...){snapshot.complete=false;}
            {std::lock_guard lock(mutex);if(stop)break;latest=std::move(snapshot);}
            PostMessageW(window,Ready,0,0);
        }
    }
    void Stop(){
        {std::lock_guard lock(mutex);stop=true;}wake.notify_one();if(worker.joinable())worker.join();
        if(watch){SHChangeNotifyDeregister(watch);watch=0;}
        if(window){DestroyWindow(window);window=nullptr;}
        latest.reset();requested=false;
    }
    HINSTANCE instance;Receive receive;HWND window=nullptr;ULONG watch=0;
    const std::vector<std::wstring> roots;
    std::atomic_bool stop{true};std::thread worker;std::mutex mutex;std::condition_variable wake;
    bool requested=false;std::optional<organizer::DesktopSnapshot> latest;
};
DesktopOrganizerSource::DesktopOrganizerSource(HINSTANCE instance,Receive receive,std::vector<std::wstring> roots):impl_(std::make_unique<Impl>(instance,std::move(receive),std::move(roots))){}
DesktopOrganizerSource::~DesktopOrganizerSource()=default;
bool DesktopOrganizerSource::Start(){return impl_->Start();}
void DesktopOrganizerSource::Refresh(){if(impl_->window)impl_->Request();}
void DesktopOrganizerSource::Stop(){impl_->Stop();}
bool DesktopOrganizerSource::Running()const{return impl_->window!=nullptr;}
}
