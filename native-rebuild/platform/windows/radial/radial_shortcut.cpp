#include "platform/windows/radial/radial_shortcut.h"
#include "core/radial/radial_menu.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace lume::platform::windows {
RadialKeyDecision RadialKeyGate::Key(unsigned key,unsigned modifiers,bool down,bool injected,
                                    bool desktop,bool enabled,unsigned shortcut){
    if(key>=held_.size()||injected)return {};
    if(!down){const bool consumed=consumed_[key];held_[key]=consumed_[key]=false;return {consumed,false,consumed};}
    if(held_[key])return {consumed_[key],false};
    held_[key]=true;
    const bool accepted=enabled&&desktop&&key+256*modifiers==shortcut;
    consumed_[key]=accepted;return {accepted,accepted};
}
bool RadialKeyGate::NeedsRelease()const{
    for(const bool key:consumed_)if(key)return true;
    return false;
}
bool IsRadialDesktopWindow(HWND window){
    const HWND shell=GetShellWindow();
    if(!shell||!window)return false;
    DWORD shellProcess=0;GetWindowThreadProcessId(shell,&shellProcess);
    HWND root=GetAncestor(window,GA_ROOT);
    wchar_t name[64]{};GetClassNameW(root,name,64);
    if(wcscmp(name,L"Progman")&&wcscmp(name,L"WorkerW"))return false;
    DWORD process=0;GetWindowThreadProcessId(root,&process);
    if(!shellProcess||process!=shellProcess)return false;
    // Do not accept Explorer folder windows, third-party children or overlays.
    for(HWND current=window;current;current=GetParent(current)){
        GetWindowThreadProcessId(current,&process);
        if(process!=shellProcess)return false;
        GetClassNameW(current,name,64);
        if(wcscmp(name,L"SysListView32")&&wcscmp(name,L"SHELLDLL_DefView")&&
           wcscmp(name,L"Progman")&&wcscmp(name,L"WorkerW"))return false;
        if(current==root)return true;
    }
    return false;
}
bool IsRadialDesktopPoint(POINT point){return IsRadialDesktopWindow(WindowFromPoint(point));}
namespace {
unsigned Modifiers(){
    unsigned value=0;
    if(GetAsyncKeyState(VK_MENU)&0x8000)value|=1;
    if(GetAsyncKeyState(VK_CONTROL)&0x8000)value|=2;
    if(GetAsyncKeyState(VK_SHIFT)&0x8000)value|=4;
    if((GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN))&0x8000)value|=8;
    return value;
}
}
struct RadialShortcut::Impl {
    static constexpr UINT DrainMessage=WM_APP+0x6d3;
    static thread_local Impl* current;
    std::atomic<HWND> target{nullptr},popup{nullptr};
    std::atomic<unsigned> shortcut{9};
    std::atomic<DWORD> threadId{0};
    std::atomic<bool> running{false};
    std::atomic<bool> enabled{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> needsRelease{false};
    std::thread thread;
    std::mutex mutex;std::condition_variable ready;bool started=false;
    RadialKeyGate gate;
    static LRESULT CALLBACK Keyboard(int code,WPARAM message,LPARAM data){
        auto* self=current;
        if(code<0||!self)return CallNextHookEx(nullptr,code,message,data);
        const auto& key=*reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
        const bool up=message==WM_KEYUP||message==WM_SYSKEYUP;
        if(!down&&!up)return CallNextHookEx(nullptr,code,message,data);
        const HWND popup=self->popup.load();
        const bool injected=(key.flags&LLKHF_INJECTED)!=0;
        if(down&&!injected&&popup&&key.vkCode==VK_ESCAPE){
            PostMessageW(self->target.load(),DismissMessage,0,0);
            // Escape is not consumed, so the underlying desktop receives a pair.
        }
        POINT point{};bool desktop=false;
        const unsigned shortcut=self->shortcut.load();
        if(down&&key.vkCode==(shortcut&255)&&GetCursorPos(&point)){
            const HWND hit=WindowFromPoint(point);
            desktop=IsRadialDesktopWindow(hit);
        }
        const auto decision=self->gate.Key(key.vkCode,Modifiers(),down,injected,desktop,
                                          self->enabled.load()&&!self->paused.load(),shortcut);
        self->needsRelease=self->gate.NeedsRelease();
        if(decision.invoke&&self->enabled){const auto owner=self->target.load();if(owner)PostMessageW(owner,InvokeMessage,0,0);}
        // Release is owed for a consumed down, regardless of current invocation eligibility.
        if(decision.dismiss){const auto owner=self->target.load();if(owner)PostMessageW(owner,ReleaseMessage,0,0);}
        if(!self->enabled&&!self->gate.NeedsRelease())PostThreadMessageW(GetCurrentThreadId(),DrainMessage,0,0);
        return decision.suppress?1:CallNextHookEx(nullptr,code,message,data);
    }
    static LRESULT CALLBACK Mouse(int code,WPARAM message,LPARAM data){
        auto* self=current;
        if(code>=0&&self&&(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||message==WM_MBUTTONDOWN)){
            const HWND popup=self->popup.load();
            const auto& mouse=*reinterpret_cast<MSLLHOOKSTRUCT*>(data);
            if(popup&&!GetPropW(popup,L"LumeRadialMenuActive")&&!(mouse.flags&LLMHF_INJECTED)&&WindowFromPoint(mouse.pt)!=popup)
                PostMessageW(self->target.load(),DismissMessage,0,0);
        }
        return CallNextHookEx(nullptr,code,message,data);
    }
    bool Start(HWND owner,unsigned code,bool initialPause){
        if(!owner||!IsWindow(owner)||!radial::ValidShortcut(code))return false;
        {std::lock_guard<std::mutex> lock(mutex);
            if(running){target=owner;shortcut=code;paused=initialPause;enabled=true;return true;}}
        Stop();target=owner;shortcut=code;paused=initialPause;started=false;enabled=true;
        try{thread=std::thread([this]{
            current=this;MSG message{};PeekMessageW(&message,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
            threadId=GetCurrentThreadId();
            const auto keyboard=SetWindowsHookExW(WH_KEYBOARD_LL,Keyboard,GetModuleHandleW(nullptr),0);
            const auto mouse=keyboard?SetWindowsHookExW(WH_MOUSE_LL,Mouse,GetModuleHandleW(nullptr),0):nullptr;
            {std::lock_guard<std::mutex> lock(mutex);running=keyboard&&mouse;started=true;}ready.notify_one();
            if(running)while(GetMessageW(&message,nullptr,0,0)>0){
                if(message.message==DrainMessage){
                    std::lock_guard<std::mutex> lock(mutex);
                    if(!enabled&&!gate.NeedsRelease()){running=false;break;}
                    continue;
                }
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            if(mouse)UnhookWindowsHookEx(mouse);
            if(keyboard)UnhookWindowsHookEx(keyboard);
            target=nullptr;popup=nullptr;needsRelease=false;running=false;threadId=0;current=nullptr;
        });}catch(...){target=nullptr;return false;}
        std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[this]{return started;});
        return running;
    }
    void Stop(){
        enabled=false;paused=false;running=false;popup=nullptr;
        if(thread.joinable()){const auto id=threadId.load();if(id)PostThreadMessageW(id,WM_QUIT,0,0);thread.join();}
        target=nullptr;gate={};needsRelease=false;
    }
    void Disable(){
        enabled=false;popup=nullptr;
        const auto id=threadId.load();if(id)PostThreadMessageW(id,DrainMessage,0,0);
    }
};
thread_local RadialShortcut::Impl* RadialShortcut::Impl::current=nullptr;
RadialShortcut::RadialShortcut():impl_(std::make_unique<Impl>()){}
RadialShortcut::~RadialShortcut(){Stop();}
bool RadialShortcut::Start(HWND target,unsigned code,bool paused){return impl_->Start(target,code,paused);}
void RadialShortcut::Configure(unsigned code){if(radial::ValidShortcut(code))impl_->shortcut=code;}
void RadialShortcut::Visible(HWND popup){impl_->popup=popup;}
void RadialShortcut::Disable(){impl_->Disable();}
void RadialShortcut::Pause(bool paused){impl_->paused=paused;}
void RadialShortcut::Stop(){impl_->Stop();}
bool RadialShortcut::Running()const{return impl_->enabled&&impl_->running;}
bool RadialShortcut::NeedsRelease()const{return impl_->needsRelease;}
}
