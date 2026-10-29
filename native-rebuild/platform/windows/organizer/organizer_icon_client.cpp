#include "platform/windows/organizer/organizer_icon_client.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/organizer/organizer_icon_protocol.h"
#include "platform/windows/organizer/shell_open.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace lume::platform::windows {
namespace {
std::atomic_uint hosts{0};
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){Reset();}
    void Reset(HANDLE next=nullptr){if(value)CloseHandle(value);value=next;}
    HANDLE Release(){const auto result=value;value=nullptr;return result;}
};
struct PendingChild {HANDLE process=nullptr,job=nullptr;};
struct PendingChildren {
    std::mutex mutex;std::vector<PendingChild> children;
    ~PendingChildren(){for(const auto child:children){CloseHandle(child.job);CloseHandle(child.process);}}
    unsigned Reap() {
        std::lock_guard<std::mutex> lock(mutex);
        for(auto it=children.begin();it!=children.end();) {
            if(WaitForSingleObject(it->process,0)==WAIT_OBJECT_0) {
                CloseHandle(it->job);CloseHandle(it->process);--hosts;it=children.erase(it);
            } else ++it;
        }
        return static_cast<unsigned>(children.size());
    }
    void Keep(HANDLE process,HANDLE job) {
        std::lock_guard<std::mutex> lock(mutex);children.push_back({process,job});
    }
};
PendingChildren pending;
bool LocalPath(const std::wstring& path) {
    return path.size()>=3&&path.size()<organizer_icon_ipc::MaxPath&&
        ((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&
        path[1]==L':'&&(path[2]==L'\\'||path[2]==L'/')&&path.find(L'\0')==std::wstring::npos;
}
std::wstring DefaultHost() {
    wchar_t path[32768]{};
    const auto size=GetModuleFileNameW(nullptr,path,32768);
    if(!size||size>=32768)return {};
    return (std::filesystem::path(path).parent_path()/L"LumeIconHost.exe").wstring();
}
std::string Narrow(const std::wstring& value) {
    const int size=WideCharToMultiByte(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(size>0?static_cast<std::size_t>(size):0,'\0');
    if(size>0)WideCharToMultiByte(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);
    return result;
}
// The helper keeps slow shell handlers off the widget thread. When it cannot
// start (security software quarantines or blocks the unsigned LumeIconHost.exe,
// a broken install), open the item from Lume itself on a short-lived thread,
// as the desktop would, instead of refusing every item.
bool OpenInProcess(const std::wstring& target,const std::wstring& reason) {
    diag::Log("organizer","icon host unavailable ("+Narrow(reason)+"); opening in process");
    try {
        std::thread([target]{
            if(!OpenShellTarget(target.c_str()))
                diag::Log("organizer","in-process open failed, error "+std::to_string(GetLastError()));
        }).detach();
        return true;
    } catch(...) { return false; }
}
std::wstring QuoteArgument(std::wstring_view value) {
    std::wstring result=L"\"";
    std::size_t slashes=0;
    for(const auto character:value) {
        if(character==L'\\') { ++slashes; continue; }
        if(character==L'"') result.append(slashes*2+1,L'\\');
        else result.append(slashes,L'\\');
        result.push_back(character);slashes=0;
    }
    result.append(slashes*2,L'\\');result.push_back(L'"');
    return result;
}
}
bool OpenOrganizerTarget(const std::wstring& target) {
    if(target.empty()||target.size()>=organizer_icon_ipc::MaxPath)return false;
    const auto host=DefaultHost();
    if(host.empty())return OpenInProcess(target,L"no module path");
    if(GetFileAttributesW(host.c_str())==INVALID_FILE_ATTRIBUTES)
        return OpenInProcess(target,L"LumeIconHost.exe missing, error "+std::to_wstring(GetLastError()));
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    auto command=QuoteArgument(host)+L" --open "+QuoteArgument(target);
    PROCESS_INFORMATION process{};
    const auto created=CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,FALSE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,
        &startup,&process);
    if(!created)return OpenInProcess(target,L"LumeIconHost.exe blocked, error "+std::to_wstring(GetLastError()));
    // Pass on the foreground right, so the opened program comes to the front
    // at once even when the caller is a desktop-level window (the organizer).
    AllowSetForegroundWindow(process.dwProcessId);
    // Do not let the helper reach ShellExecute before the foreground grant.
    if(ResumeThread(process.hThread)==static_cast<DWORD>(-1)){
        TerminateProcess(process.hProcess,1);
        CloseHandle(process.hThread);CloseHandle(process.hProcess);return false;
    }
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    return true;
}

struct OrganizerIconExtractor::Impl {
    explicit Impl(std::wstring executable,unsigned timeoutMs)
        :executable(executable.empty()?DefaultHost():std::move(executable)),
         timeoutMs(std::clamp(timeoutMs,50U,10000U)){}
    ~Impl(){
        if(!Stop()&&process.value) {
            // Kernel I/O cancellation can outlast the bounded shutdown. Keep
            // the actual handles/count visible without a permanent poll thread.
            pending.Keep(process.Release(),job.Release());counted=false;
            ReleaseIpc();
        }
    }
    bool Start() {
        if(stopping&&!Stop())return false;
        if(process.value&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT)return true;
        if(!Stop()||pending.Reap())return false;
        if(executable.empty()||executable.find(L'"')!=std::wstring::npos)return false;
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        mapping.Reset(CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,
            sizeof(organizer_icon_ipc::Message),nullptr));
        request.Reset(CreateEventW(&security,FALSE,FALSE,nullptr));
        reply.Reset(CreateEventW(&security,FALSE,FALSE,nullptr));
        quit.Reset(CreateEventW(&security,TRUE,FALSE,nullptr));
        job.Reset(CreateJobObjectW(nullptr,nullptr));
        if(!mapping.value||!request.value||!reply.value||!quit.value||!job.value){Stop();return false;}
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){
            Stop();return false;
        }
        message=static_cast<organizer_icon_ipc::Message*>(
            MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(organizer_icon_ipc::Message)));
        if(!message){Stop();return false;}
        *message=organizer_icon_ipc::Message{};
        std::vector<HANDLE> inherited{mapping.value,request.value,reply.value,quit.value};
        SIZE_T bytes=0;
        InitializeProcThreadAttributeList(nullptr,2,0,&bytes);
        std::vector<unsigned char> storage(bytes);
        auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(attributes,2,0,&bytes)){Stop();return false;}
        if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited.data(),
            inherited.size()*sizeof(HANDLE),nullptr,nullptr)||
           !UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&job.value,
            sizeof(HANDLE),nullptr,nullptr)){
            DeleteProcThreadAttributeList(attributes);Stop();return false;
        }
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
        startup.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;
        std::wstring command=L"\""+executable+L"\"";
        for(auto handle:inherited)command+=L" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(handle));
        PROCESS_INFORMATION created{};
        const bool started=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
            CREATE_SUSPENDED|CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,
            &startup.StartupInfo,&created)!=FALSE;
        DeleteProcThreadAttributeList(attributes);
        if(!started){Stop();return false;}
        process.Reset(created.hProcess);Handle thread;thread.value=created.hThread;
        pid=created.dwProcessId;counted=true;++hosts;
        // JOB_LIST closes the crash window between creation and assignment.
        if(ResumeThread(thread.value)==static_cast<DWORD>(-1)){
            Stop();return false;
        }
        return true;
    }
    std::shared_ptr<const WidgetBitmap> Read(const std::wstring& path,const std::atomic_bool& cancelled,unsigned pixelSize) {
        if(cancelled||!LocalPath(path)){if(cancelled)Stop();return {};}
        if(!pixelSize||pixelSize>organizer_icon_ipc::MaxSide)return {};
        if(!Start())return {};
        message->result=organizer_icon_ipc::Result::Pending;
        message->width=0;message->height=0;
        message->requestedSide=pixelSize;
        message->pathLength=static_cast<std::uint32_t>(path.size());
        std::copy(path.begin(),path.end(),message->path);message->path[path.size()]=L'\0';
        if(!SetEvent(request.value)){Stop();return {};}
        const auto deadline=GetTickCount64()+timeoutMs;
        const HANDLE waitHandles[]{reply.value,process.value};
        for(;;) {
            if(cancelled||GetTickCount64()>=deadline){Stop();return {};}
            const auto status=WaitForMultipleObjects(2,waitHandles,FALSE,20);
            if(status==WAIT_TIMEOUT)continue;
            if(status!=WAIT_OBJECT_0){Stop();return {};}
            if(message->magic!=organizer_icon_ipc::Magic||message->version!=organizer_icon_ipc::Version){
                Stop();return {};
            }
            if(message->result==organizer_icon_ipc::Result::Missing)return {};
            const auto width=message->width,height=message->height;
            if(message->result!=organizer_icon_ipc::Result::Ready||!width||!height||
               width>organizer_icon_ipc::MaxSide||height>organizer_icon_ipc::MaxSide){
                Stop();return {};
            }
            auto bitmap=std::make_shared<WidgetBitmap>();bitmap->width=width;bitmap->height=height;
            bitmap->pixels.assign(message->pixels,message->pixels+static_cast<std::size_t>(width)*height);
            for(const auto color:bitmap->pixels) {
                const auto alpha=color>>24;
                if(((color>>16)&255)>alpha||((color>>8)&255)>alpha||(color&255)>alpha){Stop();return {};}
            }
            return bitmap;
        }
    }
    bool Stop() {
        if(process.value) {
            stopping=true;
            if(quit.value)SetEvent(quit.value);
            if(WaitForSingleObject(process.value,60)!=WAIT_OBJECT_0) {
                if(!TerminateJobObject(job.value,1))TerminateProcess(process.value,1);
                if(WaitForSingleObject(process.value,2000)!=WAIT_OBJECT_0)return false;
            }
        }
        process.Reset();job.Reset();pid=0;
        if(counted){--hosts;counted=false;}
        ReleaseIpc();stopping=false;return true;
    }
    void ReleaseIpc() {
        if(message){UnmapViewOfFile(message);message=nullptr;}
        mapping.Reset();request.Reset();reply.Reset();quit.Reset();
    }
    std::wstring executable;unsigned timeoutMs;
    Handle mapping,request,reply,quit,job,process;
    organizer_icon_ipc::Message* message=nullptr;
    unsigned pid=0;bool counted=false,stopping=false;
};
OrganizerIconExtractor::OrganizerIconExtractor(std::wstring executable,unsigned timeoutMs)
    :impl_(std::make_unique<Impl>(std::move(executable),timeoutMs)){}
OrganizerIconExtractor::~OrganizerIconExtractor()=default;
std::shared_ptr<const WidgetBitmap> OrganizerIconExtractor::Read(
    const std::wstring& path,const std::atomic_bool& cancelled,unsigned pixelSize){return impl_->Read(path,cancelled,pixelSize);}
bool OrganizerIconExtractor::Stop(){return impl_->Stop();}
unsigned OrganizerIconExtractor::ProcessId()const{return impl_->pid;}
unsigned OrganizerIconExtractor::ActiveHosts(){pending.Reap();return hosts.load();}
unsigned OrganizerIconExtractor::PendingCleanup(){return pending.Reap();}
std::shared_ptr<const WidgetBitmap> ReadOrganizerIcon(
    const std::wstring& path,const std::atomic_bool& cancelled,unsigned pixelSize){
    OrganizerIconExtractor extractor;return extractor.Read(path,cancelled,pixelSize);
}
}
