#include "apps/shell/radial_session.h"
#include "platform/windows/radial/radial_shortcut.h"
#include "platform/windows/radial/radial_window.h"
#include "platform/windows/screenshot/screenshot_editor.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <map>
#include <cstdlib>
#include <iostream>
namespace {
std::array<bool,256> testKeys{};
bool testPendingRelease=false,testDesktop=true;
unsigned testInputBatches=0;
ULONGLONG testTick=100;
SHORT WINAPI TestGetAsyncKeyState(int key){return testKeys.at(static_cast<unsigned>(key))?static_cast<SHORT>(0x8000):0;}
ULONGLONG WINAPI TestGetTickCount64(){return testTick;}
bool TestIsRadialDesktopPoint(POINT){return testDesktop;}
}
namespace lume::platform::windows {
namespace capture {
class TestScreenshotEditor final {
public:
    explicit TestScreenshotEditor(HINSTANCE){}
    bool Start(){++testInputBatches;return true;}
    void Stop(){}
    bool Active()const{return false;}
};
}
// The production session can exercise its non-verification release wait without
// installing hooks or ever sending global keyboard input.
class TestRadialShortcut final {
public:
    bool Start(HWND,unsigned,bool=false){running_=true;return true;}
    void Configure(unsigned){}
    void Visible(HWND){}
    void Disable(){running_=false;}
    void Pause(bool){}
    void Stop(){running_=false;testPendingRelease=false;}
    bool Running()const{return running_;}
    bool NeedsRelease()const{return testPendingRelease;}
private:
    bool running_=false;
};
}
#define RadialShortcut TestRadialShortcut
#define ScreenshotEditor TestScreenshotEditor
#define GetAsyncKeyState TestGetAsyncKeyState
#define GetTickCount64 TestGetTickCount64
#define IsRadialDesktopPoint TestIsRadialDesktopPoint
#include "apps/shell/radial_session.cpp"
#undef IsRadialDesktopPoint
#undef GetTickCount64
#undef ScreenshotEditor
#undef GetAsyncKeyState
#undef RadialShortcut

using namespace lume;
void Expect(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
struct Files final:settings::IFileStore {
    std::map<std::string,std::string> values;bool fail=false;
    bool Read(const std::string& p,std::string& out)const override {const auto it=values.find(p);if(it==values.end())return false;out=it->second;return true;}
    bool Exists(const std::string& p)const override{return values.count(p)!=0;}
    bool Write(const std::string& p,const std::string& value)override{if(fail)return false;values[p]=value;return true;}
    bool Rename(const std::string& from,const std::string& to)override{if(fail||!Exists(from))return false;values[to]=values[from];values.erase(from);return true;}
    bool Remove(const std::string& p)override{values.erase(p);return true;}
};
void ScreenshotReleaseRegression(){
    Files files;
    shell::RadialSessionOptions options;options.preferencesPath="held-radial-test.json";
    shell::RadialSession session(GetModuleHandleW(nullptr),options,{}, {},&files);
    Expect(session.HandlePage(ui::PageCommand::RadialEnable).has_value()&&session.Running(),"isolated screenshot runtime starts");
    const auto click=[&]{
        SendMessageW(session.Handle(),platform::windows::RadialShortcut::InvokeMessage,0,0);
        const auto p=radial::SlotPosition(radial::ScreenshotSlot);
        const auto dpi=GetDpiForWindow(session.Handle());
        const auto location=MAKELPARAM(static_cast<int>(p.x*dpi/96),static_cast<int>(p.y*dpi/96));
        SendMessageW(session.Handle(),WM_LBUTTONDOWN,0,location);
        SendMessageW(session.Handle(),WM_LBUTTONUP,0,location);
        Expect(!IsWindowVisible(session.Handle()),"screenshot selection hides before release wait");
    };
    testPendingRelease=true;testKeys[VK_TAB]=true;click();
    SendMessageW(session.Handle(),WM_TIMER,101,0);
    Expect(testInputBatches==0,"screenshot must wait while the Tab trigger is held");
    testKeys[VK_TAB]=false;
    SendMessageW(session.Handle(),WM_TIMER,101,0);
    Expect(testInputBatches==0,"consumed trigger must release even when async key state is clear");
    testPendingRelease=false;testKeys[VK_SHIFT]=true;
    SendMessageW(session.Handle(),WM_TIMER,101,0);
    Expect(testInputBatches==0,"screenshot still waits for modifiers");
    testKeys[VK_SHIFT]=false;
    SendMessageW(session.Handle(),WM_TIMER,101,0);
    SendMessageW(session.Handle(),WM_TIMER,101,0);
    Expect(testInputBatches==1,"screenshot submits once after physical input release");
    const auto custom=static_cast<ui::PageCommand>(static_cast<int>(ui::PageCommand::RadialShortcutFirst)+512+'Q'-1);
    session.HandlePage(custom);testKeys['Q']=true;click();
    SendMessageW(session.Handle(),WM_TIMER,101,0);
    Expect(testInputBatches==1,"custom trigger also delays screenshot");
    testKeys['Q']=false;testPendingRelease=true;
    testTick+=2001;
    SendMessageW(session.Handle(),WM_TIMER,101,0);
    Expect(testInputBatches==1,"release timeout must not inject over a consumed trigger");
    testPendingRelease=false;
    SendMessageW(session.Handle(),WM_TIMER,101,0);
    Expect(testInputBatches==1,"timed-out screenshot cannot run late");
    click();session.HandlePage(ui::PageCommand::RadialDisable);
    Expect(!session.Handle()&&testInputBatches==1,"disable cancels pending screenshot");
    session.Shutdown();testKeys={};testInputBatches=0;testTick=100;
}
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ScreenshotReleaseRegression();
    Files files;bool desktop=true;unsigned screenshots=0;bool shortcutSettings=false;HWND screenshotOwner=nullptr;
    shell::RadialSessionOptions options;options.preferencesPath="radial-test.json";options.verification=true;
    options.desktop=[&](POINT){return desktop;};options.screenshot=[&]{Expect(!IsWindowVisible(screenshotOwner),"screenshot callback always sees hidden popup");++screenshots;return true;};
    shell::RadialSession session(GetModuleHandleW(nullptr),options,[](const auto&,bool){},[&](bool shortcut){shortcutSettings=shortcut;},&files);
    Expect(session.Restore()&&!session.Running(),"radial starts disabled");
    Expect(session.HandlePage(ui::PageCommand::RadialEnable).has_value()&&session.Running(),"enable persists and starts isolated runtime");
    screenshotOwner=session.Handle();
    Expect(files.Exists(options.preferencesPath),"preferences atomically committed");
    session.HandlePage(ui::PageCommand::RadialFrostedGlass);Expect(session.Snapshot().frosted,"material persisted");
    files.fail=true;session.HandlePage(ui::PageCommand::RadialLiquidGlass);Expect(session.Snapshot().frosted,"save failure rolls back material");files.fail=false;
    const auto custom=static_cast<ui::PageCommand>(static_cast<int>(ui::PageCommand::RadialShortcutFirst)+512+'Q'-1);
    session.HandlePage(custom);Expect(session.Snapshot().shortcutCode==512+'Q',"custom shortcut saved");
    session.HandlePage(ui::PageCommand::RadialCaptureBegin);
    SendMessageW(session.Handle(),platform::windows::RadialShortcut::InvokeMessage,0,0);
    Expect(!IsWindowVisible(session.Handle()),"recording cannot be interrupted by an existing desktop shortcut");
    session.HandlePage(ui::PageCommand::RadialCaptureEnd);
    SendMessageW(session.Handle(),platform::windows::RadialShortcut::InvokeMessage,0,0);
    Expect(IsWindowVisible(session.Handle()),"desktop invocation opens native popup");
    SendMessageW(session.Handle(),WM_MOUSEMOVE,0,MAKELPARAM(160,46));
    SendMessageW(session.Handle(),platform::windows::RadialShortcut::ReleaseMessage,0,0);
    Expect(!IsWindowVisible(session.Handle())&&screenshots==0,"trigger release hides without executing the hovered screenshot");
    SendMessageW(session.Handle(),platform::windows::RadialShortcut::InvokeMessage,0,0);
    for(int slot=0;slot<10;++slot){
        if(slot==radial::ScreenshotSlot)continue;
        const auto point=radial::SlotPosition(slot);
        const auto dpi=GetDpiForWindow(session.Handle());
        const auto position=MAKELPARAM(static_cast<int>(point.x*dpi/96),static_cast<int>(point.y*dpi/96));
        SendMessageW(session.Handle(),WM_LBUTTONDOWN,0,position);
        SendMessageW(session.Handle(),WM_LBUTTONUP,0,position);
        SendMessageW(session.Handle(),WM_TIMER,101,0);
        Expect(screenshots==0&&IsWindowVisible(session.Handle()),"blue translation and placeholder sectors never execute an action");
    }
    const auto p=radial::SlotPosition(radial::ScreenshotSlot);
    const auto dpi=GetDpiForWindow(session.Handle());
    const auto location=MAKELPARAM(static_cast<int>(p.x*dpi/96),static_cast<int>(p.y*dpi/96));
    SendMessageW(session.Handle(),WM_LBUTTONDOWN,0,location);
    SendMessageW(session.Handle(),WM_LBUTTONUP,0,location);
    SendMessageW(session.Handle(),platform::windows::RadialShortcut::ReleaseMessage,0,0);
    SendMessageW(session.Handle(),platform::windows::RadialShortcut::InvokeMessage,0,0);
    SendMessageW(session.Handle(),WM_TIMER,101,0);
    Expect(screenshots==1&&!IsWindowVisible(session.Handle()),"screenshot dismisses before invoking test port once");
    desktop=false;SendMessageW(session.Handle(),platform::windows::RadialShortcut::InvokeMessage,0,0);
    Expect(!IsWindowVisible(session.Handle()),"queued invocation rechecks desktop");
    Expect(!shortcutSettings,"no settings opened by invocation");
    session.Shutdown();Expect(!session.Running()&&!session.Handle(),"shutdown releases runtime");
    shell::RadialSession restored(GetModuleHandleW(nullptr),options,{}, {},&files);
    Expect(restored.Snapshot().frosted&&restored.Snapshot().shortcutCode==512+'Q',"material and shortcut restored");
    restored.Shutdown();
    auto backup=radial::Preferences{};backup.frosted=true;std::string encoded;Expect(radial::Encode(backup,encoded),"backup encoded");
    files.values[options.preferencesPath]=std::string(4097,'x');files.values[options.preferencesPath+".bak"]=encoded;
    shell::RadialSession oversized(GetModuleHandleW(nullptr),options,{}, {},&files);
    Expect(oversized.Snapshot().frosted,"valid backup recovers oversized main");
    oversized.HandlePage(ui::PageCommand::RadialReset);
    Expect(!oversized.Snapshot().frosted&&files.Exists(options.preferencesPath+".corrupt"),"explicit reset quarantines oversized main and saves defaults");
    oversized.Shutdown();
    CoUninitialize();std::cout<<"PASS radial persistence, recovery, rollback, invocation and screenshot dispatch\n";
}
