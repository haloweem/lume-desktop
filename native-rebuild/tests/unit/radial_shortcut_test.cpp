#include <windows.h>
#include <array>
#include <memory>
#include <cstdlib>
#include <iostream>
#include <vector>

// Inspect the hook's consumed-key lifetime without installing a global hook.
#define private public
#include "platform/windows/radial/radial_shortcut.h"
#undef private
namespace {
struct Posted { HWND target; UINT message; };
std::vector<Posted> posted;
HWND testHit=nullptr;
bool cursorAvailable=true;
unsigned testModifiers=0,drains=0;
BOOL WINAPI TestPostMessage(HWND target,UINT message,WPARAM,LPARAM){posted.push_back({target,message});return TRUE;}
BOOL WINAPI TestPostThreadMessage(DWORD,UINT,WPARAM,LPARAM){++drains;return TRUE;}
BOOL WINAPI TestGetCursorPos(POINT* point){*point={0,0};return cursorAvailable;}
HWND WINAPI TestWindowFromPoint(POINT){return testHit;}
SHORT WINAPI TestGetAsyncKeyState(int key){
    const unsigned mask=key==VK_MENU?1u:key==VK_CONTROL?2u:key==VK_SHIFT?4u:key==VK_LWIN?8u:0u;
    return (testModifiers&mask)?static_cast<SHORT>(0x8000):0;
}
LRESULT WINAPI TestCallNextHookEx(HHOOK,int,WPARAM,LPARAM){return 23;}
}
// The linked static runtime need not extract its shortcut object: this TU
// compiles that implementation against deterministic, non-global boundaries.
#define PostMessageW TestPostMessage
#define PostThreadMessageW TestPostThreadMessage
#define GetCursorPos TestGetCursorPos
#define WindowFromPoint TestWindowFromPoint
#define GetAsyncKeyState TestGetAsyncKeyState
#define CallNextHookEx TestCallNextHookEx
#include "platform/windows/radial/radial_shortcut.cpp"
#undef CallNextHookEx
#undef GetAsyncKeyState
#undef WindowFromPoint
#undef GetCursorPos
#undef PostThreadMessageW
#undef PostMessageW

using namespace lume::platform::windows;
void Expect(bool ok,const char* name){if(!ok){std::cerr<<name<<'\n';std::exit(1);}}
void HookReleaseRegression(){
    static_assert(RadialShortcut::ReleaseMessage==WM_APP+0x6d4);
    RadialShortcut shortcut;
    auto& hook=*shortcut.impl_;
    RadialShortcut::Impl::current=&hook;
    const auto owner=reinterpret_cast<HWND>(static_cast<ULONG_PTR>(1));
    const auto popup=reinterpret_cast<HWND>(static_cast<ULONG_PTR>(2));
    hook.target=owner;hook.enabled=true;testHit=GetShellWindow();
    const auto key=[&](UINT message,unsigned code,bool injected=false){
        KBDLLHOOKSTRUCT input{};input.vkCode=code;input.flags=injected?LLKHF_INJECTED:0;
        return RadialShortcut::Impl::Keyboard(HC_ACTION,message,reinterpret_cast<LPARAM>(&input));
    };
    const auto release=[&]{
        Expect(posted.size()==1&&posted.front().target==owner&&posted.front().message==WM_APP+0x6d4,
               "consumed physical key-up posts one distinct release dismissal");
        posted.clear();
    };
    Expect(key(WM_KEYDOWN,VK_TAB)==1,"desktop trigger down is consumed");
    Expect(shortcut.NeedsRelease(),"hook publishes outstanding consumed trigger to screenshot session");
    Expect(posted.size()==1&&posted.front().message==RadialShortcut::InvokeMessage,"down posts invocation once");
    posted.clear();hook.popup=popup;testHit=popup;
    Expect(key(WM_KEYDOWN,VK_TAB)==1&&posted.empty(),"repeat over popup never reinvokes");
    cursorAvailable=false;testModifiers=4;
    Expect(key(WM_KEYUP,VK_TAB)==1,"trigger release is consumed away from desktop");release();
    Expect(!shortcut.NeedsRelease(),"physical release clears outstanding screenshot trigger");
    Expect(key(WM_KEYUP,VK_TAB)==23&&posted.empty(),"duplicate release is neither consumed nor dismissed");
    Expect(key(WM_KEYUP,'Q')==23&&posted.empty(),"unrelated release cannot dismiss");
    cursorAvailable=true;testModifiers=0;
    Expect(key(WM_KEYDOWN,VK_TAB)==23&&posted.empty(),"popup is not genuine desktop for a fresh trigger");
    key(WM_KEYUP,VK_TAB);
    testHit=GetShellWindow();
    Expect(key(WM_SYSKEYDOWN,VK_TAB)==1,"system keydown invokes on desktop");posted.clear();
    Expect(key(WM_SYSKEYUP,VK_TAB,true)==23&&posted.empty()&&hook.gate.NeedsRelease(),"injected up cannot retire physical trigger");
    hook.paused=true;testHit=nullptr;testModifiers=15;hook.shortcut=512+'Q';
    Expect(key(WM_SYSKEYUP,VK_TAB)==1,"release survives pause, modifiers and shortcut change");release();
    hook.paused=false;testHit=GetShellWindow();testModifiers=2;
    Expect(key(WM_KEYDOWN,'Q')==1,"custom Control shortcut invokes");posted.clear();
    testModifiers=0;key(WM_KEYUP,VK_CONTROL);
    Expect(posted.empty()&&hook.gate.NeedsRelease(),"modifier release does not dismiss trigger");
    hook.Disable();drains=0;
    Expect(key(WM_KEYDOWN,'Q')==1&&posted.empty(),"disabled repeat stays consumed");
    Expect(key(WM_KEYUP,'Q')==1&&!hook.gate.NeedsRelease(),"disabled trigger up drains consumption");release();
    Expect(drains==1,"disabled hook can retire after physical release");
    hook.enabled=true;hook.target=owner;hook.shortcut=VK_TAB;testHit=GetShellWindow();
    Expect(key(WM_KEYDOWN,VK_TAB)==1,"new hold before shutdown is consumed");posted.clear();
    shortcut.Stop();
    Expect(!hook.gate.NeedsRelease()&&!shortcut.NeedsRelease()&&!hook.target.load(),"shutdown clears consumed state without waiting");
    Expect(key(WM_KEYUP,VK_TAB)==23&&posted.empty(),"shutdown never manufactures a release dismissal");
    RadialShortcut::Impl::current=nullptr;
}
int main(){
    HookReleaseRegression();
    RadialKeyGate gate;
    auto d=gate.Key(VK_TAB,0,true,false,false,true,VK_TAB);
    Expect(!d.suppress&&!d.invoke&&!d.dismiss,"Tab over application passes through");
    d=gate.Key(VK_TAB,0,false,false,true,true,VK_TAB);
    Expect(!d.suppress&&!d.invoke&&!d.dismiss,"unconsumed desktop up cannot dismiss");
    d=gate.Key(VK_TAB,0,true,true,true,true,VK_TAB);
    Expect(!d.suppress&&!d.invoke&&!d.dismiss,"injected keys cannot invoke");
    d=gate.Key(VK_TAB,0,true,false,true,true,VK_TAB);
    Expect(d.suppress&&d.invoke&&!d.dismiss,"desktop initial down invokes");
    Expect(gate.NeedsRelease(),"disable must retain hook for an outstanding consumed down");
    d=gate.Key(VK_TAB,0,true,false,false,true,VK_TAB);
    Expect(d.suppress&&!d.invoke&&!d.dismiss,"held-key repeat stays consumed even off desktop");
    d=gate.Key(VK_TAB,0,false,false,false,false,'Q');
    Expect(d.suppress&&!d.invoke&&d.dismiss,"matching release dismisses after config and pointer change");
    Expect(!gate.NeedsRelease(),"release permits disabled hook retirement");
    d=gate.Key(VK_TAB,0,false,false,true,true,VK_TAB);
    Expect(!d.suppress&&!d.invoke&&!d.dismiss,"duplicate gate release cannot dismiss twice");
    d=gate.Key(VK_TAB,4,true,false,true,true,VK_TAB);
    Expect(!d.suppress&&!d.invoke,"modifier mismatch passes");
    gate.Key(VK_TAB,4,false,false,true,true,VK_TAB);
    d=gate.Key('Q',2,true,false,true,true,512+'Q');
    Expect(d.suppress&&d.invoke,"custom Control shortcut invokes");
    d=gate.Key('Q',0,false,false,false,false,VK_TAB);
    Expect(d.suppress&&!d.invoke&&d.dismiss,"custom trigger release ignores modifiers and eligibility");
    HWND fake=CreateWindowExW(0,L"STATIC",L"not a desktop",WS_POPUP,0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Expect(fake&&!IsRadialDesktopWindow(fake),"ordinary native window is not desktop");
    Expect(!IsRadialDesktopWindow(nullptr),"null target rejected");
    const HWND desktop=GetShellWindow();
    Expect(desktop&&IsRadialDesktopWindow(desktop),"actual Explorer desktop root is accepted");
    DestroyWindow(fake);
    std::cout<<"PASS radial hold/release, hook delivery, pairing and desktop exclusion\n";
}
