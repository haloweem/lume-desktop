// Integration tests drive only windows created on this test's own UI thread.
// No production test hooks, user preferences, or desktop-wide input injection.
#include "core/clock/clock_controller.h"
#include "core/settings/json_settings_codec.h"
#include "core/settings/settings_store.h"
#include "core/ui/clock_page_state.h"
#include "platform/windows/clock_runtime.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/widget_ui/resource.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace lume;
using namespace lume::platform::windows;
void Check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
HWND OwnWindow(const wchar_t* name,HWND owner=nullptr) {
    struct Search { const wchar_t* name; HWND owner; HWND result; } search{name,owner,nullptr};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM value)->BOOL {
        auto& found=*reinterpret_cast<Search*>(value); wchar_t name[128]{};
        GetClassNameW(window,name,128);
        if(!std::wcscmp(name,found.name) && (!found.owner || GetWindow(window,GW_OWNER)==found.owner)) {
            found.result=window; return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
HWND Clock() { return OwnWindow(L"LumeNativeDesktopClock"); }
std::string Encoded(const settings::SettingsSnapshot& state,const settings::ClockSettings& clock) {
    settings::JsonSettingsCodec codec; std::string text;
    auto snapshot=state;
    snapshot.clock=clock;
    Check(codec.Encode(snapshot,text),"encode test preferences"); return text;
}
enum class ColorInput { Hex, Rgb, AlphaOnly, HexAlphaSlider, RgbAlphaSlider };
struct NativeMenuSearch {
    HWND owner;
    std::vector<HWND> windows;
};
std::vector<HWND> NativeMenus(HWND owner) {
    NativeMenuSearch search{owner,{}};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM value)->BOOL {
        auto& search=*reinterpret_cast<NativeMenuSearch*>(value);DWORD process=0;
        if(GetWindowThreadProcessId(window,&process)!=GetCurrentThreadId()||process!=GetCurrentProcessId())return TRUE;
        wchar_t name[32]{};GetClassNameW(window,name,32);
        if(std::wcscmp(name,L"#32768")||!IsWindowVisible(window))return TRUE;
        search.windows.push_back(window);
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.windows;
}
void PostMenuKey(HWND owner,UINT key) {
    const LPARAM scan=static_cast<LPARAM>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC))<<16;
    PostMessageW(owner,WM_KEYDOWN,key,scan|1);PostMessageW(owner,WM_KEYUP,key,scan|0xc0000001u);
}
struct PopupDriver {
    int rootRow;
    int childRow=-1;
    bool color=false;
    bool position=false;
    bool accept=true;
    bool cancelMenu=false;
    bool scrollSizeToEnd=false;
    ColorInput colorInput=ColorInput::Hex;
    int stage=0;
    HWND root=nullptr;
    ULONGLONG deadline=GetTickCount64()+5000;
    bool completed=false;
    bool timedOut=false;
    UINT command=0;
    bool submenuPending=false;
    bool selectionPosted=false;
    bool editorActionPosted=false;
    UINT rootMovesRemaining=0;
    UINT childMovesRemaining=0;
};
PopupDriver* activePopup=nullptr;
UINT PopupCommand(const PopupDriver& popup) {
    if(popup.rootRow==0)return 15u+static_cast<UINT>(popup.childRow);
    if(popup.rootRow==1)return popup.scrollSizeToEnd?IDM_LUME_WIDGET_SIZE_FIRST+25:IDM_LUME_WIDGET_SIZE_FIRST+static_cast<UINT>(popup.childRow);
    if(popup.rootRow==2)return popup.childRow==3?IDM_LUME_WIDGET_CENTER:IDM_LUME_WIDGET_POSITION;
    if(popup.rootRow==4)return IDM_LUME_WIDGET_COLOR;
    if(popup.rootRow==5)return 11;
    if(popup.rootRow==6)return IDM_LUME_WIDGET_LOCK;
    if(popup.rootRow==8)return IDM_CLOSESKIN;
    return 0;
}
UINT RootIndex(const PopupDriver& popup) {
    switch(popup.rootRow){case 0:return 1;case 1:return 2;case 2:return 3;case 4:return 4;case 5:return 5;case 6:return 6;case 8:return 7;default:return 99;}
}
UINT ChildIndex(const PopupDriver& popup) {
    if(popup.rootRow==2)return popup.childRow==3?2:static_cast<UINT>(popup.childRow);
    return popup.scrollSizeToEnd?25:static_cast<UINT>(popup.childRow);
}
void CALLBACK DrivePopup(HWND,UINT,UINT_PTR timer,DWORD) {
    auto& p=*activePopup;
    if(GetTickCount64()>p.deadline) {
        p.timedOut=true;KillTimer(nullptr,timer);EndMenu();
        for(const auto* name:{L"LumeWidgetColorPickerWindow",L"LumeWidgetPositionEditorWindow"}) {
            if(HWND window=OwnWindow(name)) PostMessageW(window,WM_CLOSE,0,0);
        }
        return;
    }
    if(p.stage==0) {
        const auto menus=NativeMenus(Clock());if(menus.empty())return;p.root=menus.front();p.stage=1;
        if(p.cancelMenu){PostMenuKey(Clock(),VK_ESCAPE);p.selectionPosted=true;p.stage=4;return;}
        p.command=PopupCommand(p);if(!p.command||RootIndex(p)==99){
            p.timedOut=true;KillTimer(nullptr,timer);EndMenu();return;}
        p.rootMovesRemaining=RootIndex(p);p.submenuPending=p.rootRow==0||p.rootRow==1||p.rootRow==2;
        PostMenuKey(Clock(),VK_HOME);p.selectionPosted=true;return;
    } else if(p.stage==1) {
        if(p.rootMovesRemaining){PostMenuKey(Clock(),VK_DOWN);--p.rootMovesRemaining;return;}
        if(p.submenuPending){PostMenuKey(Clock(),VK_RIGHT);p.stage=2;return;}
        PostMenuKey(Clock(),VK_RETURN);p.stage=(p.color||p.position)?3:4;return;
    } else if(p.stage==2 && p.submenuPending) {
        const auto menus=NativeMenus(Clock());if(menus.size()<2)return;
        p.childMovesRemaining=ChildIndex(p);PostMenuKey(Clock(),VK_HOME);p.stage=5;return;
    } else if(p.stage==5 && p.submenuPending) {
        if(p.childMovesRemaining){PostMenuKey(Clock(),VK_DOWN);--p.childMovesRemaining;return;}
        PostMenuKey(Clock(),VK_RETURN);p.submenuPending=false;p.stage=(p.color||p.position)?3:4;return;
    } else if(p.stage==3 && (p.color || p.position)) {
        HWND editor=OwnWindow(p.color?L"LumeWidgetColorPickerWindow":L"LumeWidgetPositionEditorWindow",Clock());
        if(!editor)return;
        if(p.editorActionPosted)return;
        if(p.color) {
            // Real EDIT controls emit their own EN_CHANGE notifications. Never
            // repair their order with a synthetic notification to the picker.
            if(p.colorInput==ColorInput::Hex || p.colorInput==ColorInput::HexAlphaSlider) {
                SetWindowTextW(GetDlgItem(editor,5101),L"#1267AB");
            } else if(p.colorInput==ColorInput::Rgb || p.colorInput==ColorInput::RgbAlphaSlider) {
                SetWindowTextW(GetDlgItem(editor,5102),L"45");
                SetWindowTextW(GetDlgItem(editor,5103),L"89");
                SetWindowTextW(GetDlgItem(editor,5104),L"170");
            }
            if(p.colorInput==ColorInput::HexAlphaSlider || p.colorInput==ColorInput::RgbAlphaSlider) {
                RECT bounds{};GetClientRect(editor,&bounds);
                // Picker size may be fitted to the monitor as well as DPI.
                const double scale=bounds.right/320.0;
                const int sliderX=p.colorInput==ColorInput::RgbAlphaSlider?225:160;
                const LPARAM point=MAKELPARAM(static_cast<int>(std::lround(sliderX*scale)),static_cast<int>(std::lround(362*scale)));
                SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,point);
                SendMessageW(editor,WM_LBUTTONUP,0,point);
            } else {
                SetWindowTextW(GetDlgItem(editor,5105),p.colorInput==ColorInput::AlphaOnly?L"64":p.colorInput==ColorInput::Rgb?L"96":L"128");
            }
            SendMessageW(editor,WM_KEYDOWN,p.accept?VK_RETURN:VK_ESCAPE,0);
        } else {
            SetWindowTextW(GetDlgItem(editor,5201),L"420");
            SetWindowTextW(GetDlgItem(editor,5202),L"260");
            SendMessageW(editor,WM_COMMAND,p.accept?IDOK:IDCANCEL,0);
        }
        p.editorActionPosted=true;p.stage=4;
    } else if((p.color || p.position) && p.editorActionPosted) {
        const auto editor=OwnWindow(p.color?L"LumeWidgetColorPickerWindow":L"LumeWidgetPositionEditorWindow",Clock());
        if(!editor&&NativeMenus(Clock()).empty()){p.completed=true;KillTimer(nullptr,timer);}
    } else if(p.selectionPosted && NativeMenus(Clock()).empty()) {
        p.completed=true;KillTimer(nullptr,timer);
    }
}
void Menu(PopupDriver action) {
    Check(Clock()!=nullptr,"clock must exist before menu");
    action.command=PopupCommand(action);action.deadline=GetTickCount64()+5000;
    activePopup=&action;
    const auto timer=SetTimer(nullptr,0,25,DrivePopup);
    Check(timer!=0,"create bounded popup driver");
    PostMessageW(Clock(),WM_CONTEXTMENU,0,MAKELPARAM(80,80));
    const auto deadline=GetTickCount64()+5500;
    while(!action.completed&&!action.timedOut&&GetTickCount64()<deadline){MSG message{};
        if(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){PostQuitMessage(static_cast<int>(message.wParam));break;}
            TranslateMessage(&message);DispatchMessageW(&message);}else Sleep(1);}
    if(!action.completed&&!action.timedOut){action.timedOut=true;EndMenu();}
    KillTimer(nullptr,timer);activePopup=nullptr;
    Check(!action.timedOut && action.completed,"real menu/editor sequence completed before deadline");
    Check(NativeMenus(Clock()).empty()&&!OwnWindow(L"LumeWidgetColorPickerWindow")&&
          !OwnWindow(L"LumeWidgetPositionEditorWindow"),"menu and editors release their windows");
    Check(GetCapture()==nullptr,"menu releases mouse capture");
}
std::filesystem::path Executable() {
    wchar_t name[32768]{};Check(GetModuleFileNameW(nullptr,name,32768)!=0,"resolve test executable");return name;
}
int RestoreChild(const std::string& path) {
    LocalSettingsFileStore files;settings::JsonSettingsCodec codec;settings::SettingsStore store(files,codec);
    const auto loaded=store.Load(path);
    NativeClockRuntime runtime(GetModuleHandleW(nullptr),[](clock::Action){});
    clock::ClockController controller(loaded.clock,runtime,[](const settings::ClockSettings&){return false;});
    Check(controller.Restore()&&controller.Running()&&Clock(),"new process restores native clock from file");
    Check(files.Write(path+".restored",Encoded(loaded,controller.Snapshot())),"write fresh-process readback");
    controller.Shutdown();Check(!Clock(),"child releases clock");return 0;
}
void VerifyRestart(const std::filesystem::path& path,const std::string& expected) {
    const auto command=L"\""+Executable().wstring()+L"\" --restore \""+path.wstring()+L"\"";
    std::wstring mutableCommand=command;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    Check(CreateProcessW(nullptr,mutableCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"launch fresh test process");
    const auto wait=WaitForSingleObject(process.hProcess,15000);
    DWORD exitCode=1;
    if(wait!=WAIT_OBJECT_0) {TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,2000);}
    GetExitCodeProcess(process.hProcess,&exitCode);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    Check(wait==WAIT_OBJECT_0&&exitCode==0,"fresh process restores and exits cleanly");
    LocalSettingsFileStore files;std::string actual;
    Check(files.Read(path.string()+".restored",actual)&&actual==expected,"all style preferences survive actual process restart");
}
void Run() {
    const auto output=std::getenv("LUME_CLOCK_PROBE_OUTPUT");
    const auto directory=(output?std::filesystem::path(output):Executable().parent_path()/"clock-interaction-artifacts")/std::to_string(GetCurrentProcessId());
    std::filesystem::create_directories(directory);
    const auto path=directory/"preferences.json";
    LocalSettingsFileStore files;settings::JsonSettingsCodec codec;settings::SettingsStore store(files,codec);
    clock::ClockController* active=nullptr;bool callbackOk=true;int saves=0,openSettings=0;
    auto preferences=store.Load(path.string());
    const auto saveClock=[&](const settings::ClockSettings& value) {
        ++saves;
        auto next=preferences;
        next.clock=value;
        if(!store.Save(path.string(),next)) return false;
        preferences=std::move(next);
        return true;
    };
    NativeClockRuntime runtime(GetModuleHandleW(nullptr),[&](clock::Action action){
        if(action.type==clock::ActionType::OpenSettings) {++openSettings;return;}
        callbackOk=active&&active->Handle(action)&&callbackOk;
    });
    clock::ClockController controller(preferences.clock,runtime,saveClock);active=&controller;
    Check(controller.Restore()&&!controller.Running(),"initial disabled state");
    Check(controller.Handle({clock::ActionType::Enable,1}),"enable native clock");
    const auto beforeFailedSave=Encoded(preferences,controller.Snapshot());
    const auto blockedTemporary=std::filesystem::path(path.string()+".tmp");
    Check(std::filesystem::create_directory(blockedTemporary),"block only the test preferences temporary file");
    Check(!controller.Handle({clock::ActionType::Enable,0}),"real settings write failure is reported");
    Check(std::filesystem::remove(blockedTemporary),"release the test write-failure fixture");
    Check(controller.Running()&&std::string(controller.Error())=="save"&&
          Encoded(preferences,controller.Snapshot())==beforeFailedSave&&
          Encoded(preferences,preferences.clock)==beforeFailedSave,
        "failed write restores the native clock and preserves module and application snapshots");
    const auto afterFailedSave=store.Load(path.string());
    Check(Encoded(afterFailedSave,afterFailedSave.clock)==beforeFailedSave,"failed write preserves persisted settings");
    for(int style=0;style<3;++style) {
        Menu({0,style});
        Check(controller.Snapshot().style==clock::StyleName(static_cast<clock::Style>(style)),"menu changes actual clock style");
        const int beforeCancel=saves;const auto before=Encoded(preferences,controller.Snapshot());
        PopupDriver cancel{0};cancel.cancelMenu=true;Menu(cancel);
        PopupDriver color{4};color.color=true;color.accept=false;Menu(color);
        PopupDriver position{2,0};position.position=true;position.accept=false;Menu(position);
        Check(saves==beforeCancel&&Encoded(preferences,controller.Snapshot())==before,"menu/color/position cancellation performs no save");
        const auto initialColor=controller.Snapshot().Active().color;
        color.accept=true;color.colorInput=ColorInput::AlphaOnly;Menu(color);
        Check(controller.Snapshot().Active().color==initialColor&&controller.Snapshot().Active().alpha==64,"alpha-only input preserves the initial color");
        color.colorInput=ColorInput::Hex;
        color.accept=true;Menu(color);
        Check(controller.Snapshot().Active().color==0x1267AB&&controller.Snapshot().Active().alpha==128&&controller.Snapshot().Active().customColor,"HEX then alpha input preserves both values in persisted clock state");
        color.colorInput=ColorInput::Rgb;Menu(color);
        Check(controller.Snapshot().Active().color==0x2D59AA&&controller.Snapshot().Active().alpha==96,"RGB then alpha input preserves both values");
        color.colorInput=ColorInput::HexAlphaSlider;Menu(color);
        Check(controller.Snapshot().Active().color==0x1267AB&&std::abs(controller.Snapshot().Active().alpha-128)<=1,"alpha slider preserves pending HEX input");
        color.colorInput=ColorInput::RgbAlphaSlider;Menu(color);
        Check(controller.Snapshot().Active().color==0x2D59AA&&std::abs(controller.Snapshot().Active().alpha-191)<=1,"alpha slider preserves pending RGB input");
        position.accept=true;Menu(position);
        Check(controller.Snapshot().Active().x==420&&controller.Snapshot().Active().y==260,"coordinate confirmation reaches persisted state");
        Menu({1,0});Check(controller.Snapshot().Active().scale==50,"size submenu lower bound 50 percent");
        PopupDriver largest{1,0};largest.scrollSizeToEnd=true;Menu(largest);
        Check(controller.Snapshot().Active().scale==300,"scroll size submenu upper bound 300 percent");
        Check(controller.Handle({clock::ActionType::Scale,100}),"return to 100 percent for drag checks");
        Menu({6});Check(controller.Snapshot().Active().locked,"menu locks clock");
        SendMessageW(Clock(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));
        Check(GetCapture()!=Clock(),"locked clock refuses drag capture");
        Menu(position);Check(controller.Snapshot().Active().x==420,"locked clock still accepts coordinate editor");
        Menu({6});Check(!controller.Snapshot().Active().locked,"menu unlocks clock");
        const int beforeDrag=saves;
        SendMessageW(Clock(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));
        Check(GetCapture()==Clock(),"unlocked clock captures drag");
        SendMessageW(Clock(),WM_LBUTTONUP,0,MAKELPARAM(10,10));
        Check(saves==beforeDrag+1&&GetCapture()==nullptr,"drag release persists once and releases capture");
        const int beforeAbortedDrag=saves;
        SendMessageW(Clock(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));ReleaseCapture();
        Check(saves==beforeAbortedDrag,"lost capture cancels drag without persisting");
        Menu({2,3});
        RECT bounds{};GetWindowRect(Clock(),&bounds);MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(Clock(),MONITOR_DEFAULTTONEAREST),&monitor);
        Check(std::abs((bounds.left+bounds.right)-(monitor.rcWork.left+monitor.rcWork.right))<=2&&
              std::abs((bounds.top+bounds.bottom)-(monitor.rcWork.top+monitor.rcWork.bottom))<=2,"center command centers visible native bounds");
        Menu({5});Check(openSettings==style+1,"open settings routed once without closing clock");
        Check(ui::ClockPageState(controller).find(L"\"running\":true")!=std::wstring::npos,"page snapshot reflects running native state");
        std::cout<<"PASS style "<<clock::StyleName(static_cast<clock::Style>(style))<<": menus, cancel/confirm, scale bounds, locks, drag lifecycle, center, settings route\n";
    }
    Check(callbackOk,"all native actions accepted by controller");
    const auto saved=store.Load(path.string());
    Check(saved.schemaVersion==3&&saved.widgets.empty(),"clock transactions preserve the existing schema 3 format");
    const auto expected=Encoded(preferences,controller.Snapshot());controller.Shutdown();Check(!Clock(),"shutdown destroys native surface");
    VerifyRestart(path,expected);
    NativeClockRuntime restoredRuntime(GetModuleHandleW(nullptr),[&](clock::Action action){callbackOk=active->Handle(action)&&callbackOk;});
    preferences=store.Load(path.string());
    clock::ClockController restored(preferences.clock,restoredRuntime,saveClock);active=&restored;
    Check(restored.Restore()&&restored.Running(),"restore parent after child restart check");
    Menu({8});
    Check(!restored.Running()&&!restored.Snapshot().enabled&&!store.Load(path.string()).clock.enabled,"menu close synchronizes runtime, state and persisted file");
    Check(ui::ClockPageState(restored).find(L"\"enabled\":false")!=std::wstring::npos,"closed component is disabled in page snapshot");
    Check(callbackOk,"close action accepted");restored.Shutdown();Check(!Clock(),"final native resources released");
    std::cout<<"PASS disk persistence, fresh-process restart, close synchronization. Evidence: "<<directory.string()<<'\n';
}
}
int main(int argc,char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {if(argc==3&&std::string(argv[1])=="--restore")return RestoreChild(argv[2]);Run();return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
