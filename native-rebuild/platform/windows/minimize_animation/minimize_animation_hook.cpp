// Native adapter for the MIT macos-minimize-animation 3.1.2 genie engines.
// The mesh formula and classic scanline mapping retain the original defaults.
// See NOTICE.md for source attribution and intentional lifecycle changes.
#include "platform/windows/minimize_animation/minimize_animation_ipc.h"
#include "platform/windows/lume_window_ids.h"
#include <windows.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <shellapi.h>
#include <uiautomation.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>
#include "platform/windows/dock/dock_slots.h"

using namespace lume::minimize_animation;
using namespace lume::platform::windows::minimize_detail;
namespace {
constexpr wchar_t BypassProperty[]=L"Lume.MinimizeAnimation.Bypass.v1";
constexpr wchar_t OwnedProperty[]=L"Lume.MinimizeAnimation.Active.v1";
struct Connection {
    HANDLE mapping=nullptr, stop=nullptr, owner=nullptr,host=nullptr;
    SharedControl* state=nullptr;
    ProcessLease* lease=nullptr;
    ~Connection() {
        if (state) UnmapViewOfFile(state);
        for (HANDLE h:{mapping,stop,owner,host}) if (h) CloseHandle(h);
    }
    bool Open() {
        mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,ObjectName(NativeBits,L"Control").c_str());
        if (!mapping) return false;
        state=static_cast<SharedControl*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControl)));
        if (!state || state->version!=ProtocolVersion || state->size!=sizeof(SharedControl)) return false;
        stop=OpenEventW(SYNCHRONIZE,FALSE,ObjectName(NativeBits,L"Stop").c_str());
        owner=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,state->ownerPid);
        host=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(Read(state->hostPid)));
        FILETIME created{},exited{},kernel{},user{};
        const bool valid=stop && owner && host && GetProcessTimes(owner,&created,&exited,&kernel,&user) &&
            static_cast<LONG>(created.dwLowDateTime)==state->ownerTimeLow &&
            static_cast<LONG>(created.dwHighDateTime)==state->ownerTimeHigh;
        if (!valid) return false;
        if (!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) return false;
        const LONG pid=static_cast<LONG>(GetCurrentProcessId());
        for (auto& candidate:state->processes) if (Read(candidate.pid)==pid &&
            candidate.createdLow==static_cast<LONG>(created.dwLowDateTime) &&
            candidate.createdHigh==static_cast<LONG>(created.dwHighDateTime)) {lease=&candidate;break;}
        if (!lease) {
            for (auto& candidate:state->processes) {
                if (InterlockedCompareExchange(&candidate.pid,-1,0)==0) {
                    candidate.createdLow=static_cast<LONG>(created.dwLowDateTime);
                    candidate.createdHigh=static_cast<LONG>(created.dwHighDateTime);
                    InterlockedExchange(&candidate.pid,pid);lease=&candidate;break;
                }
            }
        }
        if (!lease) {
            // A long-running feature can outlive many applications. Recycle
            // only previously observed, exited process leases when the bounded
            // table fills; this is not a system-wide process scan.
            for (auto& candidate:state->processes) {
                const LONG previous=Read(candidate.pid);
                if (previous<=0) continue;
                const LONG low=candidate.createdLow,high=candidate.createdHigh;
                HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,
                                           static_cast<DWORD>(previous));
                bool exited=!process && GetLastError()==ERROR_INVALID_PARAMETER;
                if (process) {
                    FILETIME oldCreated{},oldEnded{},oldKernel{},oldUser{};
                    exited=WaitForSingleObject(process,0)==WAIT_OBJECT_0 ||
                        (GetProcessTimes(process,&oldCreated,&oldEnded,&oldKernel,&oldUser) &&
                         (static_cast<LONG>(oldCreated.dwLowDateTime)!=low ||
                          static_cast<LONG>(oldCreated.dwHighDateTime)!=high));
                    CloseHandle(process);
                }
                if (!exited || InterlockedCompareExchange(&candidate.pid,-1,previous)!=previous) continue;
                if (candidate.createdLow!=low || candidate.createdHigh!=high) {
                    InterlockedExchange(&candidate.pid,previous);continue;
                }
                InterlockedExchange(&candidate.entries,0);InterlockedExchange(&candidate.workers,0);
                candidate.createdLow=static_cast<LONG>(created.dwLowDateTime);
                candidate.createdHigh=static_cast<LONG>(created.dwHighDateTime);
                InterlockedExchange(&candidate.pid,pid);lease=&candidate;break;
            }
        }
        return lease!=nullptr;
    }
    bool Stopping() const {
        return !Read(state->enabled) || WaitForSingleObject(stop,0)!=WAIT_TIMEOUT ||
            WaitForSingleObject(owner,0)!=WAIT_TIMEOUT || WaitForSingleObject(host,0)!=WAIT_TIMEOUT;
    }
};
struct Entry {
    std::shared_ptr<Connection> connection;
    explicit Entry(std::shared_ptr<Connection> link):connection(std::move(link)) {
        InterlockedIncrement(&connection->lease->entries);
    }
    ~Entry() {
        InterlockedDecrement(&connection->lease->entries);
    }
};
struct Bitmap {
    HDC dc=nullptr; HBITMAP bitmap=nullptr; HGDIOBJ previous=nullptr;
    void* bits=nullptr; int width=0,height=0;
    ~Bitmap() { Reset(); }
    void Reset() {
        if (dc && previous) SelectObject(dc,previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        dc=nullptr;bitmap=nullptr;previous=nullptr;bits=nullptr;width=height=0;
    }
    bool Create(int w,int h,std::size_t limit=MaxSnapshotBytes) {
        Reset(); if (!SnapshotBytes(w,h,limit)) return false;
        dc=CreateCompatibleDC(nullptr); if (!dc) return false;
        BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;
        bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
        if (!bitmap || !bits) { Reset(); return false; }
        previous=SelectObject(dc,bitmap);
        if (!previous || previous==HGDI_ERROR) { Reset(); return false; }
        width=w;height=h; return true;
    }
};
enum class Kind { Minimize, Restore, Launch };
struct Animation {
    std::shared_ptr<Connection> link;
    Preferences settings;
    HWND target=nullptr;
    Kind kind=Kind::Minimize;
    HMODULE module=nullptr;
    HANDLE cancel=nullptr, captured=nullptr, firstFrame=nullptr;
    Bitmap image;
    RECT rect{};
    std::atomic_bool ready{false};
    std::atomic_bool capturing{false};
    bool ownsCloak=false, transitionsChanged=false;
    BOOL previousTransitions=FALSE;
    bool deferred=false, revealedTaskbar=false;
    HWND previousForeground=nullptr;
    std::atomic_uint presented{0};
    bool Stopping() const {
        return link->Stopping() || WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT || !IsWindow(target);
    }
    bool Wait(DWORD ms) const {
        HANDLE events[]={cancel,link->stop,link->owner,link->host};
        return WaitForMultipleObjects(4,events,FALSE,ms)==WAIT_TIMEOUT && !Stopping();
    }
    bool Hide() {
        DWORD cloaked=0;
        if (FAILED(DwmGetWindowAttribute(target,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) || cloaked)
            return false;
        BOOL cloak=TRUE;
        ownsCloak=SUCCEEDED(DwmSetWindowAttribute(target,DWMWA_CLOAK,&cloak,sizeof(cloak)));
        return ownsCloak;
    }
    void SuppressTransitions() {
        if (SUCCEEDED(DwmGetWindowAttribute(target,DWMWA_TRANSITIONS_FORCEDISABLED,
                                           &previousTransitions,sizeof(previousTransitions))) &&
            !previousTransitions) {
            BOOL disabled=TRUE;
            transitionsChanged=SUCCEEDED(DwmSetWindowAttribute(target,DWMWA_TRANSITIONS_FORCEDISABLED,
                                                               &disabled,sizeof(disabled)));
        }
    }
    void RestoreWindow() {
        if (!IsWindow(target) || GetPropW(target,OwnedProperty)!=reinterpret_cast<HANDLE>(this)) return;
        DWORD pid=0; GetWindowThreadProcessId(target,&pid);
        if (pid!=GetCurrentProcessId()) return;
        if (ownsCloak) {
            BOOL cloak=FALSE;
            DwmSetWindowAttribute(target,DWMWA_CLOAK,&cloak,sizeof(cloak));
            ownsCloak=false;
        }
        if (transitionsChanged) {
            DwmSetWindowAttribute(target,DWMWA_TRANSITIONS_FORCEDISABLED,
                                  &previousTransitions,sizeof(previousTransitions));
            transitionsChanged=false;
        }
    }
    ~Animation() {
        RestoreWindow();
        if (GetPropW(target,OwnedProperty)==reinterpret_cast<HANDLE>(this)) RemovePropW(target,OwnedProperty);
        for (HANDLE h:{cancel,captured,firstFrame}) if (h) CloseHandle(h);
    }
};
std::mutex activeMutex;
std::map<HWND,std::shared_ptr<Animation>> active;
std::set<HWND> seen;
struct Registration {
    std::shared_ptr<Animation> animation;
    bool released=false;
    ~Registration() {
        if (released) return;
        std::lock_guard<std::mutex> lock(activeMutex);
        const auto found=active.find(animation->target);
        if (found!=active.end() && found->second.get()==animation.get()) active.erase(found);
    }
};
struct RenderSlot {
    HANDLE mutexes[4]{};
    HANDLE acquired=nullptr;
    RenderSlot() {
        for (unsigned index=0;index<4;++index) {
            const auto name=L"Local\\Lume.MinimizeAnimation.v1.RenderSlot."+std::to_wstring(index);
            mutexes[index]=CreateMutexW(nullptr,FALSE,name.c_str());
            if (!mutexes[index]) return;
        }
        const auto result=WaitForMultipleObjects(4,mutexes,FALSE,0);
        if (result>=WAIT_OBJECT_0 && result<WAIT_OBJECT_0+4) acquired=mutexes[result-WAIT_OBJECT_0];
        if (result>=WAIT_ABANDONED_0 && result<WAIT_ABANDONED_0+4) acquired=mutexes[result-WAIT_ABANDONED_0];
    }
    ~RenderSlot() {
        if (acquired) ReleaseMutex(acquired);
        for (HANDLE handle:mutexes) if (handle) CloseHandle(handle);
    }
};

bool Eligible(HWND target) {
    DWORD pid=0; GetWindowThreadProcessId(target,&pid);
    if (pid!=GetCurrentProcessId() || GetAncestor(target,GA_ROOT)!=target) return false;
    const auto style=GetWindowLongPtrW(target,GWL_STYLE);
    const auto ex=GetWindowLongPtrW(target,GWL_EXSTYLE);
    if ((style&WS_CHILD) || (ex&WS_EX_TOOLWINDOW) || GetPropW(target,BypassProperty)) return false;
    wchar_t name[96]{};
    GetClassNameW(target,name,96);
    if (!wcscmp(name,L"Shell_TrayWnd") || !wcscmp(name,L"Shell_SecondaryTrayWnd") ||
        !wcscmp(name,L"Progman") || !wcscmp(name,L"WorkerW")) return false;
    return true;
}
// Called on the target GUI thread, including restore's post-WM_WINDOWPOSCHANGED
// hook. PrintWindow therefore cannot deadlock waiting for this same GUI thread.
bool Capture(Animation& a) {
    RECT raw{}; if (!GetWindowRect(a.target,&raw)) return false;
    a.rect=raw;
    RECT extended{};
    if (SUCCEEDED(DwmGetWindowAttribute(a.target,DWMWA_EXTENDED_FRAME_BOUNDS,&extended,sizeof(extended))))
        a.rect=extended;
    const int w=a.rect.right-a.rect.left,h=a.rect.bottom-a.rect.top;
    const int rawW=raw.right-raw.left,rawH=raw.bottom-raw.top;
    if (w<40 || h<40 || !SnapshotBytes(rawW,rawH) || !a.image.Create(w,h)) return false;
    Bitmap buffer; if (!buffer.Create(rawW,rawH)) return false;
    if (!PrintWindow(a.target,buffer.dc,2 /*PW_RENDERFULLCONTENT*/)) return false;
    const int ox=a.rect.left-raw.left,oy=a.rect.top-raw.top;
    if (ox<0 || oy<0 || ox+w>rawW || oy+h>rawH) return false;
    GdiFlush();
    auto* source=static_cast<const unsigned char*>(buffer.bits);
    auto* destination=static_cast<unsigned char*>(a.image.bits);
    for (int y=0;y<h;++y) {
        std::memcpy(destination+static_cast<std::size_t>(y)*w*4,
            source+(static_cast<std::size_t>(y+oy)*rawW+ox)*4,static_cast<std::size_t>(w)*4);
        for (int x=0;x<w;++x) destination[(static_cast<std::size_t>(y)*w+x)*4+3]=255;
    }
    InterlockedIncrement(&a.link->state->captures);
    return true;
}
// The Dock shows minimized windows as they looked here. The mapping stays open
// while the window lives; closing the last handle (window destroyed or this
// DLL unloaded) removes it, and the Dock falls back to DWM's thumbnail.
std::map<HWND,HANDLE> snapshots;  // guarded by activeMutex
void PublishSnapshot(HWND target,const Bitmap& image) {
    namespace slots=lume::platform::windows::dock_slots;
    if (!image.bits || image.width<=0 || image.height<=0) return;
    HANDLE mapping=nullptr;
    {
        std::lock_guard<std::mutex> lock(activeMutex);
        if (const auto found=snapshots.find(target);found!=snapshots.end()) mapping=found->second;
    }
    if (!mapping) {
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,slots::SnapshotBytes,
                                   slots::SnapshotName(reinterpret_cast<std::uintptr_t>(target)).c_str());
        if (!mapping) return;
        std::lock_guard<std::mutex> lock(activeMutex);
        snapshots[target]=mapping;
    }
    auto* view=static_cast<unsigned char*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,slots::SnapshotBytes));
    if (!view) return;
    auto& header=*reinterpret_cast<slots::SnapshotHeader*>(view);
    const float fit=std::min(1.f,static_cast<float>(slots::SnapshotSide)/std::max(image.width,image.height));
    const int w=std::max(1,static_cast<int>(image.width*fit)),h=std::max(1,static_cast<int>(image.height*fit));
    InterlockedIncrement(&header.serial);  // odd while writing
    auto* out=reinterpret_cast<std::uint32_t*>(view+sizeof(slots::SnapshotHeader));
    const auto* in=static_cast<const unsigned char*>(image.bits);
    // Box filter: average the source pixels each output pixel covers.
    for (int y=0;y<h;++y) {
        const int y0=y*image.height/h,y1=std::max(y0+1,(y+1)*image.height/h);
        for (int x=0;x<w;++x) {
            const int x0=x*image.width/w,x1=std::max(x0+1,(x+1)*image.width/w);
            unsigned sum[3]{},count=0;
            for (int sy=y0;sy<y1;sy+=std::max(1,(y1-y0)/4))
                for (int sx=x0;sx<x1;sx+=std::max(1,(x1-x0)/4)) {
                    const auto* p=in+(static_cast<std::size_t>(sy)*image.width+sx)*4;
                    sum[0]+=p[0];sum[1]+=p[1];sum[2]+=p[2];++count;
                }
            out[static_cast<std::size_t>(y)*w+x]=0xff000000u|((sum[2]/count)<<16)|((sum[1]/count)<<8)|(sum[0]/count);
        }
    }
    header.width=w;header.height=h;
    InterlockedIncrement(&header.serial);
    UnmapViewOfFile(view);
}
HWND Taskbar(HMONITOR monitor) {
    HWND tray=FindWindowW(L"Shell_TrayWnd",nullptr);
    if (tray && MonitorFromWindow(tray,MONITOR_DEFAULTTOPRIMARY)==monitor) return tray;
    HWND next=nullptr;
    while ((next=FindWindowExW(nullptr,next,L"Shell_SecondaryTrayWnd",nullptr)))
        if (MonitorFromWindow(next,MONITOR_DEFAULTTONULL)==monitor) return next;
    return tray;
}
// Same title/process matching signals as the source mod, with bounded UIA
// transactions. This runs only after the first ghost frame, never while the
// minimize hook is waiting for that frame.
float TaskbarButtonX(Animation& a,HWND tray,float fallback) {
    if (!tray || a.link->state->targetThread || a.Stopping()) return fallback;
    wchar_t titleBuffer[512]{};
    if (!SendMessageTimeoutW(a.target,WM_GETTEXT,512,reinterpret_cast<LPARAM>(titleBuffer),
                            SMTO_ABORTIFHUNG|SMTO_BLOCK,60,nullptr)) return fallback;
    std::wstring title=titleBuffer;
    std::transform(title.begin(),title.end(),title.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    std::wstring process=wcsrchr(path,L'\\') ? wcsrchr(path,L'\\')+1 : path;
    const auto dot=process.rfind(L'.');if (dot!=std::wstring::npos) process.resize(dot);
    std::transform(process.begin(),process.end(),process.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    const std::wstring hint=process==L"chrome"?L"google chrome":process==L"msedge"?L"microsoft edge":
        process==L"firefox"?L"mozilla firefox":process;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if (FAILED(com)) return fallback;
    IUIAutomation* automation=nullptr;IUIAutomation2* timed=nullptr;
    IUIAutomationElement* root=nullptr;IUIAutomationCondition* button=nullptr;
    IUIAutomationCondition* list=nullptr;IUIAutomationCondition* both=nullptr;
    IUIAutomationElementArray* elements=nullptr;
    float result=fallback;int best=0;
    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,
                                  IID_IUIAutomation,reinterpret_cast<void**>(&automation))) &&
        SUCCEEDED(automation->QueryInterface(IID_IUIAutomation2,reinterpret_cast<void**>(&timed)))) {
        timed->put_ConnectionTimeout(100);timed->put_TransactionTimeout(100);
        if (SUCCEEDED(automation->ElementFromHandle(tray,&root))) {
            VARIANT type{};type.vt=VT_I4;type.lVal=UIA_ButtonControlTypeId;
            automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&button);
            type.lVal=UIA_ListItemControlTypeId;
            automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&list);
            if (button && list) automation->CreateOrCondition(button,list,&both);
            if (both && SUCCEEDED(root->FindAll(TreeScope_Descendants,both,&elements))) {
                int count=0;elements->get_Length(&count);
                const auto deadline=GetTickCount64()+80;
                for (int index=0;index<std::min(count,256) && GetTickCount64()<deadline && !a.Stopping();++index) {
                    IUIAutomationElement* element=nullptr;BSTR name=nullptr;
                    if (SUCCEEDED(elements->GetElement(index,&element))) {
                        if (SUCCEEDED(element->get_CurrentName(&name)) && name) {
                            std::wstring text=name;
                            std::transform(text.begin(),text.end(),text.begin(),
                                [](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
                            int score=0;
                            if (!text.empty() && !title.empty()) {
                                if (text==title) score+=1000;
                                if (title.find(text)!=std::wstring::npos) score+=500;
                                if (text.find(title)!=std::wstring::npos) score+=500;
                            }
                            if (!process.empty() && text.find(process)!=std::wstring::npos) score+=400;
                            if (hint!=process && text.find(hint)!=std::wstring::npos) score+=900;
                            if (score>best) {
                                RECT bounds{};
                                if (SUCCEEDED(element->get_CurrentBoundingRectangle(&bounds)) &&
                                    bounds.right>bounds.left) {
                                    best=score;result=(bounds.left+bounds.right)*.5f;
                                }
                            }
                        }
                        if (name) SysFreeString(name);
                        element->Release();
                    }
                }
            }
        }
    }
    if (elements) elements->Release();
    if (both) both->Release();
    if (list) list->Release();
    if (button) button->Release();
    if (root) root->Release();
    if (timed) timed->Release();
    if (automation) automation->Release();
    CoUninitialize();
    return result;
}
// With Lume's Dock running on the monitor, windows fly into its right end,
// where the minimized window's thumbnail appears, instead of the taskbar.
// The bar is 100 DIPs tall; icons are 58 with 21 below and 24 of padding.
bool DockTarget(HMONITOR monitor,HWND window,Rect& out) {
    HWND bar=FindWindowW(lume::window_ids::DockBarClass,nullptr);
    RECT r{};
    if (!bar || !GetWindowRect(bar,&r) || r.right<=r.left || r.bottom<=r.top ||
        MonitorFromWindow(bar,MONITOR_DEFAULTTONULL)!=monitor) return false;
    // The window's own thumbnail, else its program's icon, as the Dock publishes them.
    wchar_t path[32768]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,32768);
    RECT slot{};
    if (lume::platform::windows::dock_slots::Find(window,length ? lume::platform::windows::dock_slots::ProgramKey(path) : 0,slot)) {
        out={static_cast<float>(slot.left),static_cast<float>(slot.top),
             static_cast<float>(slot.right-slot.left),static_cast<float>(slot.bottom-slot.top)};
        return true;
    }
    // A window the Dock does not show yet joins at the right end of its windows.
    const float k=static_cast<float>(r.bottom-r.top)/100.f, icon=58.f*k;
    // The bar grows by one slot, half on each side, when the thumbnail arrives.
    const float centre=static_cast<float>(r.right)+(icon+10.f*k)/2-24.f*k-icon/2;
    const float height=icon*.82f;
    out={centre-icon/2,static_cast<float>(r.bottom)-21.f*k-height,icon,height};
    return true;
}
struct TargetGeometry { Rect window,dock; RECT canvas; HWND tray=nullptr; };
TargetGeometry GeometryFor(Animation& a) {
    TargetGeometry result{};
    result.window={static_cast<float>(a.rect.left),static_cast<float>(a.rect.top),
                   static_cast<float>(a.image.width),static_cast<float>(a.image.height)};
    const HMONITOR monitor=a.settings.multiMonitor ? MonitorFromWindow(a.target,MONITOR_DEFAULTTONEAREST) :
        MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{}; info.cbSize=sizeof(info);
    if (!GetMonitorInfoW(monitor,&info)) {
        info.rcMonitor={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
        info.rcWork=info.rcMonitor;
    }
    result.tray=Taskbar(monitor);
    RECT taskbar{}; const bool hasTray=result.tray && GetWindowRect(result.tray,&taskbar);
    DWORD alignment=1,bytes=sizeof(alignment);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                 L"TaskbarAl",RRF_RT_REG_DWORD,nullptr,&alignment,&bytes);
    float x=alignment ? (info.rcMonitor.left+info.rcMonitor.right)*.5f : info.rcMonitor.left+160.f;
    POINT cursor{};
    if (GetCursorPos(&cursor) && hasTray && PtInRect(&taskbar,cursor)) x=static_cast<float>(cursor.x);
    const float dpi=static_cast<float>(GetDpiForWindow(a.target))/96.f;
    const float y=hasTray && taskbar.bottom-taskbar.top < taskbar.right-taskbar.left ?
        static_cast<float>(taskbar.top) : info.rcMonitor.bottom-70.f*dpi;
    // A thread-scoped fixture never changes Explorer state. Its deterministic
    // dock still exercises the complete non-affine renderer on a real HWND.
    result.dock={x-11.f,y,22.f,33.f*dpi};
    if (!a.link->state->targetThread && DockTarget(monitor,a.target,result.dock)) result.tray=nullptr;
    else if (a.settings.engine==Engine::Classic) result.dock.y=static_cast<float>(info.rcMonitor.bottom);
    const float margin=result.window.width/2+8;
    const LONG virtualLeft=GetSystemMetrics(SM_XVIRTUALSCREEN);
    const LONG virtualTop=GetSystemMetrics(SM_YVIRTUALSCREEN);
    const LONG virtualRight=virtualLeft+GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const LONG virtualBottom=virtualTop+GetSystemMetrics(SM_CYVIRTUALSCREEN);
    result.canvas={
        std::max(virtualLeft,std::min(info.rcMonitor.left,static_cast<LONG>(std::floor(result.window.x-margin)))),
        std::max(virtualTop,static_cast<LONG>(std::floor(std::min(result.window.y,result.dock.y)-8))),
        std::min(virtualRight,std::max(info.rcMonitor.right,static_cast<LONG>(
            std::ceil(result.window.x+result.window.width+margin)))),
        std::min(virtualBottom,static_cast<LONG>(std::ceil(std::max(result.window.y+result.window.height,
                                                            result.dock.y)+8)))};
    return result;
}
struct Surface {
    Bitmap canvas; HWND ghost=nullptr;
    std::vector<float> rows;
    HMODULE d2dModule=nullptr;
    ID2D1Factory* factory=nullptr; ID2D1DCRenderTarget* target=nullptr;
    ID2D1Bitmap* source=nullptr; ID2D1BitmapBrush* brush=nullptr;
    ~Surface() {
        if (brush) brush->Release();
        if (source) source->Release();
        if (target) target->Release();
        if (factory) factory->Release();
        if (d2dModule) FreeLibrary(d2dModule);
        if (ghost) DestroyWindow(ghost);
    }
    bool Create(Animation& a,const TargetGeometry& g) {
        const int w=g.canvas.right-g.canvas.left,h=g.canvas.bottom-g.canvas.top;
        if (!canvas.Create(w,h,MaxCanvasBytes)) return false;
        ghost=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
            L"STATIC",L"LumeMinimizeGhost",WS_POPUP,g.canvas.left,g.canvas.top,w,h,nullptr,nullptr,nullptr,nullptr);
        if (!ghost) return false;
        if (a.settings.engine==Engine::Classic) {rows.resize(static_cast<std::size_t>(a.image.height)+1);return true;}
        d2dModule=LoadLibraryExW(L"d2d1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!d2dModule) return false;
        using FactoryProc=HRESULT(WINAPI*)(D2D1_FACTORY_TYPE,REFIID,const D2D1_FACTORY_OPTIONS*,void**);
        const auto createFactory=Procedure<FactoryProc>(d2dModule,"D2D1CreateFactory");
        if (!createFactory || FAILED(createFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory),nullptr,reinterpret_cast<void**>(&factory)))) return false;
        // A DC target is copied to a layered DIB every frame. Software rendering
        // avoids a private D3D device/driver cache in every hooked application,
        // while preserving the exact Direct2D mesh and bitmap interpolation.
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),
            96,96,D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
        if (FAILED(factory->CreateDCRenderTarget(&properties,&target))) return false;
        const auto bitmapProperties=D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(target->CreateBitmap(D2D1::SizeU(a.image.width,a.image.height),
            a.image.bits,a.image.width*4,bitmapProperties,&source))) return false;
        return SUCCEEDED(target->CreateBitmapBrush(source,D2D1::BitmapBrushProperties(
            D2D1_EXTEND_MODE_CLAMP,D2D1_EXTEND_MODE_CLAMP,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR),&brush));
    }
    bool Present(const TargetGeometry& g,BYTE alpha) {
        POINT destination{g.canvas.left,g.canvas.top},zero{0,0};
        SIZE size{canvas.width,canvas.height};
        BLENDFUNCTION blend{AC_SRC_OVER,0,alpha,AC_SRC_ALPHA};
        if (!UpdateLayeredWindow(ghost,nullptr,&destination,&size,canvas.dc,&zero,0,&blend,ULW_ALPHA)) return false;
        ShowWindow(ghost,SW_SHOWNOACTIVATE); return true;
    }
};
bool ModernFrame(Surface& s,const TargetGeometry& g,float progress) {
    constexpr int tiles=20;
    D2D1_POINT_2F grid[tiles+1][tiles+1]{};
    float left=1e9f,right=-1e9f,top=1e9f,bottom=-1e9f;
    for (int y=0;y<=tiles;++y) for (int x=0;x<=tiles;++x) {
        const auto v=LampVertex(static_cast<float>(x)/tiles,static_cast<float>(y)/tiles,progress,g.window,g.dock);
        auto& point=grid[y][x]; point={v.x-g.canvas.left,v.y-g.canvas.top};
        left=std::min(left,point.x);right=std::max(right,point.x);
        top=std::min(top,point.y);bottom=std::max(bottom,point.y);
    }
    if (right-left<2 || bottom-top<2) return false;
    const RECT rect{0,0,s.canvas.width,s.canvas.height};
    if (FAILED(s.target->BindDC(s.canvas.dc,&rect))) return false;
    s.target->BeginDraw(); s.target->Clear(D2D1::ColorF(0,0,0,0));
    s.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    ID2D1RoundedRectangleGeometry* outline=nullptr;
    const float radius=std::min(8.f,std::min(right-left,bottom-top)/2);
    const auto rounded=D2D1::RoundedRect(D2D1::RectF(left,top,right,bottom),radius,radius);
    s.factory->CreateRoundedRectangleGeometry(rounded,&outline);
    if (outline) {
        auto layer=D2D1::LayerParameters();
        layer.geometricMask=outline;layer.maskAntialiasMode=D2D1_ANTIALIAS_MODE_ALIASED;
        s.target->PushLayer(layer,nullptr);
    }
    const float sw=g.window.width/tiles,sh=g.window.height/tiles;
    for (int y=0;y<tiles;++y) for (int x=0;x<tiles;++x) {
        const auto a=grid[y][x],b=grid[y][x+1],c=grid[y+1][x],d=grid[y+1][x+1];
        const D2D1_POINT_2F center{(a.x+b.x+c.x+d.x)/4,(a.y+b.y+c.y+d.y)/4};
        const float qw=std::max(std::abs(b.x-a.x),std::abs(d.x-c.x));
        const float qh=std::max(std::abs(c.y-a.y),std::abs(d.y-b.y));
        const float amount=std::clamp(std::min(qw,qh)*.04f,.15f,.35f);
        const auto bloat=[&](D2D1_POINT_2F p) {
            const float dx=p.x-center.x,dy=p.y-center.y,length=std::sqrt(dx*dx+dy*dy);
            if (length>.001f) {p.x+=dx/length*amount;p.y+=dy/length*amount;} return p;
        };
        ID2D1PathGeometry* quad=nullptr;ID2D1GeometrySink* sink=nullptr;
        if (FAILED(s.factory->CreatePathGeometry(&quad))) continue;
        if (SUCCEEDED(quad->Open(&sink))) {
            sink->BeginFigure(bloat(a),D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(bloat(b));sink->AddLine(bloat(d));sink->AddLine(bloat(c));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);sink->Close();sink->Release();
            const float sx=x*sw,sy=y*sh;
            const float m11=(b.x-a.x)/sw,m12=(b.y-a.y)/sw;
            const float m21=(c.x-a.x)/sh,m22=(c.y-a.y)/sh;
            s.brush->SetTransform(D2D1::Matrix3x2F(m11,m12,m21,m22,
                a.x-sx*m11-sy*m21,a.y-sx*m12-sy*m22));
            s.target->FillGeometry(quad,s.brush);
        }
        quad->Release();
    }
    if (outline) {s.target->PopLayer();outline->Release();}
    return SUCCEEDED(s.target->EndDraw());
}
bool ClassicFrame(Surface& s,Animation& a,const TargetGeometry& g,float progress) {
    const int w=a.image.width,h=a.image.height;
    auto* pixels=static_cast<unsigned char*>(s.canvas.bits);
    const auto* source=static_cast<const unsigned char*>(a.image.bits);
    std::memset(pixels,0,static_cast<std::size_t>(s.canvas.width)*s.canvas.height*4);
    auto& rows=s.rows;
    const float dockX=g.dock.x+g.dock.width/2;
    for (int y=0;y<=h;++y) rows[y]=ClassicRow(static_cast<float>(y)/h,progress,g.window,dockX,g.dock.y).y;
    int segment=0;
    for (int y=0;y<s.canvas.height;++y) {
        const float screenY=y+g.canvas.top+.5f;
        if (screenY<rows[0] || screenY>=rows[h]) continue;
        while (segment<h-1 && rows[segment+1]<=screenY) ++segment;
        const float delta=rows[segment+1]-rows[segment];
        const float fraction=delta>.0001f ? (screenY-rows[segment])/delta : 0;
        const float v=(segment+fraction)/h;
        const auto strip=ClassicRow(v,progress,g.window,dockX,g.dock.y);
        const float width=std::max(1.f,strip.width),left=strip.center-width*.5f-g.canvas.left;
        const int srcY=std::clamp(static_cast<int>(v*h),0,h-1);
        const int first=std::max(0,static_cast<int>(left));
        const int last=std::min(s.canvas.width,static_cast<int>(left+width)+1);
        for (int x=first;x<last;++x) {
            const float u=(x+.5f-left)/width; if (u<0 || u>=1) continue;
            const int srcX=std::clamp(static_cast<int>(u*w),0,w-1);
            std::memcpy(pixels+(static_cast<std::size_t>(y)*s.canvas.width+x)*4,
                        source+(static_cast<std::size_t>(srcY)*w+srcX)*4,4);
        }
    }
    return true;
}
void Render(Animation& a) {
    RenderSlot slot;
    if (!slot.acquired) return;
    if (a.kind!=Kind::Minimize) {
        HANDLE events[]={a.captured,a.cancel,a.link->stop,a.link->owner,a.link->host};
        if (WaitForMultipleObjects(5,events,FALSE,400)!=WAIT_OBJECT_0 || !a.ready || a.Stopping()) return;
    }
    auto geometry=GeometryFor(a);
    Surface surface; if (!surface.Create(a,geometry)) return;
    LARGE_INTEGER frequency{},start{},now{};
    QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
    bool done=false;
    while (!a.Stopping()) {
        QueryPerformanceCounter(&now);
        const double elapsed=static_cast<double>(now.QuadPart-start.QuadPart)*1000/frequency.QuadPart;
        const float raw=std::min(1.f,static_cast<float>(elapsed/a.settings.durationMs));
        const bool modern=a.settings.engine==Engine::Modern;
        const float eased=modern ? raw*raw*(3-2*raw) : raw;
        const float progress=a.kind==Kind::Minimize ? eased : 1-eased;
        const bool drawn=modern ? ModernFrame(surface,geometry,progress) : ClassicFrame(surface,a,geometry,progress);
        if (drawn && !a.Stopping()) {
            const BYTE alpha=modern ? 255 : static_cast<BYTE>(std::clamp((1-progress)/.2f,0.f,1.f)*255);
            if (!surface.Present(geometry,alpha)) break;
            ++a.presented;InterlockedIncrement(&a.link->state->frames);
            if (a.presented==1) {
                if (a.deferred && !a.Hide()) break;
                SetEvent(a.firstFrame);
                geometry.dock.x=TaskbarButtonX(a,geometry.tray,geometry.dock.x+11)-11;
            }
        }
        if (raw>=1) {done=true;break;}
        // Only active animations wake at 8ms. No polling thread remains idle.
        if (!a.Wait(8)) break;
    }
    if (done && a.presented) InterlockedIncrement(&a.link->state->completed);
    else if (a.Stopping()) InterlockedIncrement(&a.link->state->cancelled);
    if (a.deferred && !a.Stopping()) {
        const int extra=a.settings.unhideDurationMs-a.settings.durationMs;
        if (extra>0) a.Wait(static_cast<DWORD>(extra));
        if (!a.Stopping()) {
            SetPropW(a.target,BypassProperty,reinterpret_cast<HANDLE>(1));
            SendMessageTimeoutW(a.target,WM_SYSCOMMAND,SC_MINIMIZE,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,250,nullptr);
            RemovePropW(a.target,BypassProperty);
        }
    }
    if (a.revealedTaskbar && a.previousForeground && IsWindow(a.previousForeground) &&
        !IsIconic(a.previousForeground)) SetForegroundWindow(a.previousForeground);
    // Reveal the real restore before Surface destroys the ghost, matching the
    // source engine's handoff order and avoiding a compositor-frame gap.
    a.RestoreWindow();
}
DWORD WINAPI Worker(void* value) {
    std::unique_ptr<std::shared_ptr<Animation>> box(static_cast<std::shared_ptr<Animation>*>(value));
    auto a=*box;box.reset();
    const auto module=a->module;
    try { Render(*a); } catch (...) { InterlockedIncrement(&a->link->state->failed); }
    a->RestoreWindow();
    SetEvent(a->firstFrame);
    if (!a->presented) InterlockedIncrement(&a->link->state->failed);
    {
        std::lock_guard<std::mutex> lock(activeMutex);
        const auto found=active.find(a->target);
        if (found!=active.end() && found->second.get()==a.get()) active.erase(found);
    }
    InterlockedDecrement(&a->link->lease->workers);
    a.reset();
    FreeLibraryAndExitThread(module,0);
}
// Return true only for a successfully started deferred minimize: CBT then
// cancels the original transition while the worker owns its eventual replay.
bool Start(HWND target,Kind kind) {
    if (!Eligible(target)) return false;
    auto link=std::make_shared<Connection>();if (!link->Open()) return false;
    Entry entry(link);
    Preferences preferences;
    if (link->Stopping() || !ReadSettings(*link->state,preferences) || !preferences.enabled ||
        (link->state->targetProcess && static_cast<DWORD>(link->state->targetProcess)!=GetCurrentProcessId()))
        return false;
    if ((kind==Kind::Restore && !preferences.restore) || (kind==Kind::Launch && !preferences.launch)) return false;
    {
        std::lock_guard<std::mutex> lock(activeMutex);
        if (auto it=active.find(target);it!=active.end()) {SetEvent(it->second->cancel);return false;}
    }
    auto a=std::make_shared<Animation>(); a->link=std::move(link);a->settings=preferences;
    a->target=target;a->kind=kind;
    a->cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    a->captured=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    a->firstFrame=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!a->cancel || !a->captured || !a->firstFrame) return false;
    if (!SetPropW(target,OwnedProperty,reinterpret_cast<HANDLE>(a.get()))) return false;
    {
        std::lock_guard<std::mutex> lock(activeMutex);
        active.emplace(target,a);
    }
    Registration registration{a};
    if (kind==Kind::Minimize) {
        DWORD cloaked=0;
        DwmGetWindowAttribute(target,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        if (cloaked || !IsWindowVisible(target) || IsIconic(target) || !Capture(*a)) return false;
        a->ready=true;
        PublishSnapshot(target,a->image);
    } else {
        if (!a->Hide()) return false;
        if (kind==Kind::Launch) {a->ready=Capture(*a);SetEvent(a->captured);if (!a->ready)return false;}
    }
    Rect dockTarget{};
    const auto targetMonitor=preferences.multiMonitor ? MonitorFromWindow(target,MONITOR_DEFAULTTONEAREST) :
        MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
    // Windows going into the Dock do not need the hidden taskbar revealed.
    if (kind==Kind::Minimize && preferences.unhideTaskbar && !a->link->state->targetThread &&
        !DockTarget(targetMonitor,target,dockTarget)) {
        const auto monitor=MonitorFromWindow(target,MONITOR_DEFAULTTONEAREST);
        HWND tray=Taskbar(monitor);
        APPBARDATA data{};data.cbSize=sizeof(data);data.hWnd=tray;
        // Lume's "completely hidden" taskbar is hidden, transparent and click-
        // through; revealing it would flash it until Lume hides it again.
        const bool lumeHidden=tray && (!IsWindowVisible(tray) ||
            (GetWindowLongPtrW(tray,GWL_EXSTYLE)&WS_EX_TRANSPARENT));
        if (tray && !lumeHidden && (SHAppBarMessage(ABM_GETSTATE,&data)&ABS_AUTOHIDE)) {
            HWND next=GetWindow(target,GW_HWNDNEXT);
            while (next) {
                if (next!=tray && next!=target && IsWindowVisible(next) && !IsIconic(next) &&
                    GetAncestor(next,GA_ROOT)==next && GetWindowTextLengthW(next)>0 &&
                    !(GetWindowLongPtrW(next,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)) {
                    a->previousForeground=next;break;
                }
                next=GetWindow(next,GW_HWNDNEXT);
            }
            a->revealedTaskbar=SetForegroundWindow(tray)!=FALSE;
            a->deferred=a->revealedTaskbar;
        }
    }
    a->SuppressTransitions();
    if (a->Stopping()) return false;
    auto box=std::make_unique<std::shared_ptr<Animation>>(a);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&Worker),&a->module)) return false;
    InterlockedIncrement(&a->link->lease->workers);
    InterlockedExchange(&a->link->state->lastTargetPid,static_cast<LONG>(GetCurrentProcessId()));
    HANDLE thread=CreateThread(nullptr,0,Worker,box.get(),0,nullptr);
    if (!thread) {
        InterlockedDecrement(&a->link->lease->workers);
        FreeLibrary(a->module);return false;
    }
    box.release();registration.released=true;
    CloseHandle(thread);
    if (kind==Kind::Minimize) InterlockedIncrement(&a->link->state->minimized);
    else if (kind==Kind::Restore) InterlockedIncrement(&a->link->state->restored);
    else InterlockedIncrement(&a->link->state->launched);
    {
        std::lock_guard<std::mutex> lock(activeMutex);
        if (seen.size()<4096) seen.insert(target);
    }
    if (kind==Kind::Minimize) {
        HANDLE events[]={a->firstFrame,a->link->stop,a->link->owner};
        if (WaitForMultipleObjects(3,events,FALSE,200)!=WAIT_OBJECT_0 || !a->presented) {
            SetEvent(a->cancel);return false;
        }
    }
    return a->deferred;
}
void Shown(HWND hwnd,UINT message,LPARAM parameter) {
    if (!Eligible(hwnd)) return;
    if (message!=WM_WINDOWPOSCHANGED) return;
    std::shared_ptr<Animation> restore;
    {
        std::lock_guard<std::mutex> lock(activeMutex);
        const auto found=active.find(hwnd);
        if (found!=active.end() && found->second->kind==Kind::Restore &&
            WaitForSingleObject(found->second->captured,0)==WAIT_TIMEOUT) restore=found->second;
    }
    if (restore && !IsIconic(hwnd) && IsWindowVisible(hwnd) && !restore->capturing.exchange(true)) {
        Entry entry(restore->link);
        restore->ready=Capture(*restore);SetEvent(restore->captured);return;
    }
    const auto* position=reinterpret_cast<const WINDOWPOS*>(parameter);
    if (position && (position->flags&SWP_SHOWWINDOW) && IsWindowVisible(hwnd) &&
        !IsIconic(hwnd) && (GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_CAPTION)) {
        bool fresh=false;
        {
            std::lock_guard<std::mutex> lock(activeMutex);
            fresh=seen.size()<4096 && seen.insert(hwnd).second;
        }
        if (fresh) Start(hwnd,Kind::Launch);
    }
}
}
extern "C" __declspec(dllexport) LRESULT CALLBACK LumeMinimizeCbt(int code,WPARAM w,LPARAM l) {
    if (code==HCBT_MINMAX) {
        const auto hwnd=reinterpret_cast<HWND>(w);
        const auto command=ShowCommand(static_cast<std::uintptr_t>(l));
        try {
            if (command==SW_MINIMIZE || command==SW_SHOWMINIMIZED || command==SW_SHOWMINNOACTIVE) {
                if (Start(hwnd,Kind::Minimize)) return 1;
            } else if (IsIconic(hwnd) && (command==SW_RESTORE || command==SW_SHOWNORMAL ||
                                        command==SW_SHOWMAXIMIZED || command==SW_SHOW))
                Start(hwnd,Kind::Restore);
        } catch (...) {}
    } else if (code==HCBT_DESTROYWND) {
        const auto hwnd=reinterpret_cast<HWND>(w);
        std::lock_guard<std::mutex> lock(activeMutex);
        if (const auto found=active.find(hwnd);found!=active.end()) SetEvent(found->second->cancel);
        seen.erase(hwnd);
        if (const auto snapshot=snapshots.find(hwnd);snapshot!=snapshots.end()) {CloseHandle(snapshot->second);snapshots.erase(snapshot);}
    }
    return CallNextHookEx(nullptr,code,w,l);
}
extern "C" __declspec(dllexport) LRESULT CALLBACK LumeMinimizeShown(int code,WPARAM w,LPARAM l) {
    if (code>=0 && l) {
        const auto* message=reinterpret_cast<const CWPRETSTRUCT*>(l);
        try { Shown(message->hwnd,message->message,message->lParam); } catch (...) {}
    }
    return CallNextHookEx(nullptr,code,w,l);
}
