#include "apps/shell/dynamic_island_session.h"
#include "platform/windows/dynamic_island/island_window.h"
#include "platform/windows/widget_ui/window_layer_policy.h"
#include "platform/windows/dynamic_island/island_preview.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <gdiplus.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <thread>
#include <shlobj.h>
#include "platform/windows/dynamic_island/compact_widgets.h"

using namespace lume;
using ui::PageCommand;
namespace {
void Check(bool ok,const char* reason){if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}}
struct Files final:settings::IFileStore {
    std::map<std::string,std::string> values;
    bool fail=false;
    bool Read(const std::string& p,std::string& out)const override{const auto i=values.find(p);if(i==values.end())return false;out=i->second;return true;}
    bool Exists(const std::string& p)const override{return values.count(p)!=0;}
    bool Write(const std::string& p,const std::string& value)override{if(fail)return false;values[p]=value;return true;}
    bool Rename(const std::string& a,const std::string& b)override{if(fail||!Exists(a))return false;values[b]=values[a];values.erase(a);return true;}
    bool Remove(const std::string& p)override{values.erase(p);return true;}
};
unsigned syntheticHoverLeaves=0;
void Pump(unsigned ms,HWND syntheticHover=nullptr){
    const auto end=GetTickCount64()+ms;
    do{
        MSG m{};
        while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){
            // The physical pointer stays on the user's input desktop. This
            // fixture owns its synthetic enter/leave events on the isolated desktop.
            if(syntheticHover&&m.hwnd==syntheticHover&&m.message==WM_MOUSELEAVE){++syntheticHoverLeaves;continue;}
            TranslateMessage(&m);DispatchMessageW(&m);
        }
        MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
    }while(GetTickCount64()<end);
}
void Click(HWND hwnd,int x,int y){const auto dpi=static_cast<int>(GetDpiForWindow(hwnd));const auto at=MAKELPARAM(MulDiv(x,dpi,96),MulDiv(y,dpi,96));SendMessageW(hwnd,WM_LBUTTONDOWN,0,at);SendMessageW(hwnd,WM_LBUTTONUP,0,at);}
thread_local HWND menuOwner=nullptr;
LRESULT CALLBACK PreventMenuActivation(int code,WPARAM w,LPARAM l){
    // This fixture runs on a desktop that is never made the input desktop.
    if(code==HCBT_ACTIVATE)return 1;
    return CallNextHookEx(nullptr,code,w,l);
}
void CALLBACK SelectSettings(HWND,UINT,UINT_PTR timer,DWORD){
    KillTimer(nullptr,timer);
    HWND menu=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM value)->BOOL{
        wchar_t name[64]{};GetClassNameW(hwnd,name,64);
        if(std::wstring(name)==L"LumeWidgetMenuWindow"&&GetWindow(hwnd,GW_OWNER)==menuOwner){
            *reinterpret_cast<HWND*>(value)=hwnd;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&menu));
    Check(menu&&IsWindowVisible(menu),"explicit island context menu is visible");
    // Settings, Reset and Close are the menu's final three commands.
    PostMessageW(menu,WM_KEYDOWN,VK_END,1);
    PostMessageW(menu,WM_KEYDOWN,VK_UP,1);PostMessageW(menu,WM_KEYDOWN,VK_UP,1);
    PostMessageW(menu,WM_KEYDOWN,VK_RETURN,1);
}
nlohmann::json State(const shell::DynamicIslandSession& s){return nlohmann::json::parse(s.State());}
std::vector<HWND> IslandWindows(){
    std::vector<HWND> result;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL{
        wchar_t name[80]{};GetClassNameW(hwnd,name,80);
        if(std::wstring_view(name)==L"LumeWindowsDynamicIsland")reinterpret_cast<std::vector<HWND>*>(data)->push_back(hwnd);
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
bool OnScreen(HWND hwnd,const platform::windows::IslandMonitor& screen){
    RECT rect{};return GetWindowRect(hwnd,&rect)&&std::abs((rect.left+rect.right)-(screen.work.left+screen.work.right))<=2;
}
void SaveFrame(const platform::windows::WidgetBitmap& frame,const wchar_t* name){
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto root=std::filesystem::path(exe).parent_path()/L"verification"/L"dynamic-island";
    std::filesystem::create_directories(root);
    Gdiplus::Bitmap image(frame.width,frame.height,static_cast<INT>(frame.width*4),PixelFormat32bppPARGB,
                         reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(frame.pixels.data())));
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    Check(image.Save((root/name).c_str(),&png)==Gdiplus::Ok,"save actual native foreground");
}
void CheckExpansionModes(){
    Files files;
    unsigned settingsOpened=0;
    shell::DynamicIslandSessionOptions options;options.preferencesPath="expansion-mode.json";
    options.openSettings=[&]{++settingsOpened;};
    {
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr),options,&files);
        Check(session.Restore(),"restore the default expansion mode");
        session.HandlePage(PageCommand::IslandEnable);session.HandlePage(PageCommand::IslandAutoHideOff);Pump(60);
        const auto hwnd=session.Handle();
        const auto move=[&]{const int dpi=static_cast<int>(GetDpiForWindow(hwnd));SendMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM(MulDiv(32,dpi,96),MulDiv(10,dpi,96)));};
        move();Pump(380,hwnd);
        Check(State(session)["hover"]==false&&State(session)["expanded"]==false,"click mode does not expand on mouse hover");
        Click(hwnd,32,10);Check(State(session)["expanded"]==true,"click mode expands the idle island on a real click");
        session.HandlePage(PageCommand::IslandCollapse);SendMessageW(hwnd,WM_MOUSELEAVE,0,0);
        session.HandlePage(PageCommand::IslandHoverOn);move();Pump(380,hwnd);
        Check(State(session)["expanded"]==true,"hover mode expands without a media session");
        SendMessageW(hwnd,WM_MOUSELEAVE,0,0);Pump(520);
        Check(State(session)["expanded"]==false,"a panel opened by hover collapses after the pointer leaves");
        const auto widget=static_cast<PageCommand>(static_cast<int>(PageCommand::IslandLeftWidgetFirst)+static_cast<int>(dynamic_island::CompactWidget::DayProgress));
        session.HandlePage(widget);move();Pump(380,hwnd);
        Check(State(session)["expanded"]==true,"hover mode also expands the compact widget without music");
        session.HandlePage(PageCommand::IslandCollapse);SendMessageW(hwnd,WM_MOUSELEAVE,0,0);move();
        session.HandlePage(PageCommand::IslandHoverOff);SendMessageW(hwnd,WM_TIMER,3,0);
        Check(State(session)["hover"]==false&&State(session)["expanded"]==false,"switching to click mode cancels a pending hover expansion");
        const auto previous=files.values["expansion-mode-display.json"];files.fail=true;
        session.HandlePage(PageCommand::IslandHoverOn);SendMessageW(hwnd,WM_TIMER,3,0);
        Check(State(session)["hover"]==false&&State(session)["expanded"]==false&&State(session)["error"]=="save"&&
            files.values["expansion-mode-display.json"]==previous,"failed mode save restores both the running interaction and the previous file");
        files.fail=false;session.HandlePage(PageCommand::IslandHoverOn);Pump(380,hwnd);
        Check(State(session)["expanded"]==true,"enabling hover mode while the pointer is already inside starts expansion");
        session.HandlePage(PageCommand::IslandResetAppearance);
        Check(State(session)["hover"]==true,"appearance reset preserves the chosen expansion mode");
        session.Shutdown();
    }
    shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),options,&files);
    Check(restored.Restore()&&State(restored)["hover"]==true,"hover expansion preference survives restart");
    const auto hwnd=restored.Handle();const int dpi=static_cast<int>(GetDpiForWindow(hwnd));
    SendMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM(MulDiv(32,dpi,96),MulDiv(10,dpi,96)));Pump(380,hwnd);
    Check(State(restored)["expanded"]==true&&settingsOpened==0,"restored hover mode expands within the island without opening settings");
    restored.HandlePage(PageCommand::IslandReset);
    Check(State(restored)["hover"]==false,"restore defaults returns to click expansion");
    restored.Shutdown();
    std::cout<<"isolated hover: ignored "<<syntheticHoverLeaves<<" unrelated physical-pointer leave events\n";
}
void Scenario(){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization");
    CheckExpansionModes();
    {
        using namespace platform::windows;
        using W=dynamic_island::CompactWidget;
        CompactData values;values.time={};values.time.wYear=2026;values.time.wMonth=11;values.time.wDay=3;
        values.time.wDayOfWeek=2;values.time.wHour=12;values.time.wMinute=34;
        values.battery=57;values.disk=39;values.hasBattery=true;values.plugged=true;values.files=2;
        dynamic_island::Weather weather;weather.available=true;weather.hasRange=true;
        weather.low=12;weather.high=20;weather.temperature=18;weather.condition=L"多云";weather.humidity=65;
        weather.windSpeed=12.96;weather.windDirection=90;weather.uv=3;weather.pressure=1023;
        weather.visibility=21;weather.apparentTemperature=17;weather.precipitation=48;
        weather.sunrise=L"06:09";weather.sunset=L"17:31";
        performance::Snapshot metrics;metrics.monotonicMillis=GetTickCount64();metrics.cpu=48;metrics.memory=62;
        metrics.download=1024;metrics.upload=512;
        for(int id=1;id<static_cast<int>(W::Count);++id){
            const auto value=ReadCompact(static_cast<W>(id),values,weather,metrics,false);
            Check(value.available&&!value.text.empty(),"every offered mini widget has a real data-backed display path");
        }
        Check(ReadCompact(W::TemperatureRange,values,weather,metrics,false).text==L"12°–20°"&&
              ReadCompact(W::Cpu,values,weather,metrics,false).text==L"48%"&&
              ReadCompact(W::Humidity,values,weather,metrics,false).text==L"65%",
              "weather and performance projections use their true source values");
        WidgetCanvas compactCanvas;WidgetBitmap compactFrame;
        Check(compactCanvas.Begin(600,420,144),"compact comparison canvas starts");
        for(int id=1;id<static_cast<int>(W::Count);++id){
            const float x=static_cast<float>((id-1)%5)*120,y=static_cast<float>((id-1)/5)*70;
            compactCanvas.Rounded({x,y,110,36},18,0x000000);
            DrawCompact(compactCanvas,static_cast<W>(id),{x+8,y+1,94,34},values,weather,metrics,false,1);
            compactCanvas.Text(std::to_wstring(id),{x,y+38,110,18},10,0xffffff,1,false,WidgetTextAlign::Center);
        }
        Check(compactCanvas.Finish(compactFrame),"compact comparison canvas finishes");
        SaveFrame(compactFrame,L"compact-reference-gallery.png");
        for(const auto kind:{W::DayProgress,W::WeekProgress,W::MonthProgress,W::QuarterProgress,W::YearProgress,W::Cpu,W::Memory,W::Disk,W::CpuMemory})
          for(const unsigned dpi:{96u,144u})for(const bool right:{false,true}){
            Check(compactCanvas.Begin(98,34,dpi),"meter regression canvas starts");
            DrawCompact(compactCanvas,kind,{0,0,98,34},values,weather,metrics,false,1,right);
            Check(compactCanvas.Finish(compactFrame),"meter regression canvas finishes");
            bool visible=false,extra=false;
            for(unsigned y=0;y<compactFrame.height;++y)for(unsigned x=0;x<compactFrame.width;++x){
                if(!(compactFrame.pixels[static_cast<std::size_t>(y)*compactFrame.width+x]>>24))continue;
                visible=true;const float dip=static_cast<float>(x)*96/static_cast<float>(dpi);
                extra|=right?dip<75:dip>23;
            }
            Check(visible&&!extra,"one aligned ring contains the number without an extra icon or duplicate value");
        }
        weather={};values.battery.reset();values.hasBattery=false;metrics.monotonicMillis=1;
        Check(!ReadCompact(W::TemperatureRange,values,weather,metrics,false).available&&
              !ReadCompact(W::Battery,values,weather,metrics,false).available&&
              !ReadCompact(W::Cpu,values,weather,metrics,false).available,
              "missing weather battery and stale performance samples never fabricate readings");
    }
    std::uint64_t now=1000;
    dynamic_island::Model model;model.StartFocus(now);Check(model.RemainingSeconds()==1500,"25 minute timer");
    model.Tick(now+999);Check(model.RemainingSeconds()==1500,"round up remaining seconds");
    model.Tick(now+1500000);Check(!model.Focusing()&&model.Finished(),"sleep or delayed tick completes once");
    Check(!model.Tick(now+1600000),"completion not repeated");model.CancelFocus();Check(!model.Finished(),"dismiss completion");
    dynamic_island::Media media{7,"fixture",{},L"A long song title for native layout verification",L"Native fixture",true,true,35,180};
    auto invalid=media;invalid.position=std::numeric_limits<double>::infinity();invalid.duration=-1;model.Update(invalid);
    Check(model.Playback().position==0&&model.Playback().duration==0,"nonfinite timeline normalized");
    Files files;int notifications=0,controls=0;bool weatherActive=false;dynamic_island::Action last=dynamic_island::Action::Tick;
    shell::DynamicIslandSessionOptions options;
    options.preferencesPath="island.json";options.monotonicMillis=[&]{return now;};
        options.notify=[&](const auto&){++notifications;};
    options.control=[&](auto action,const auto& projection){Check(projection.session==7&&projection.id=="fixture","control retains native identity");last=action;++controls;};
    options.weatherActive=[&](bool active){weatherActive=active;};
    options.weather=[] {dynamic_island::Weather value;value.enabled=value.available=value.hasRange=true;
        value.low=12;value.high=20;value.temperature=18;value.humidity=65;value.condition=L"多云";return value;};
    const auto foreground=GetForegroundWindow();
    {
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr),options,&files);
        Check(session.Restore()&&!session.Running()&&files.values.empty(),"disabled restore has no window or file writes");
        Check(!session.HandlePage(PageCommand::DockEnable),"unrelated commands rejected");
        Check(session.HandlePage(PageCommand::IslandEnable).has_value()&&session.Running(),"enable creates window");
        Check(!weatherActive,"island without a weather widget leaves the shared weather worker idle");
        Check((GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)==0,"island stays in the desktop layer by default");
        Pump(900);Check(IsWindowVisible(session.Handle()),"idle capsule is visible by default");
        Check(GetForegroundWindow()==foreground,"enable never takes focus");
        Check(SendMessageW(session.Handle(),WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,"click never activates");
        const auto original=files.values["island.json"];files.fail=true;
        session.HandlePage(PageCommand::IslandDisable);
        Check(session.Running()&&session.Snapshot().enabled&&files.values["island.json"]==original,"failed disable restores live window and original preferences");
        Check(!weatherActive,"failed disable preserves the idle weather worker");
        Check(State(session)["error"]=="save","save failure reported");
        files.fail=false;session.HandlePage(PageCommand::IslandLock);Check(session.Snapshot().locked,"lock persisted");
        session.Update(media);const HWND hwnd=session.Handle();
        Pump(900);Check(IsWindowVisible(hwnd),"playback keeps the island visible");
        RECT r{};GetClientRect(hwnd,&r);const int dpi=static_cast<int>(GetDpiForWindow(hwnd));
        Check(r.right==MulDiv(236,dpi,96)&&r.bottom==MulDiv(34,dpi,96),"compact capsule settles at its playing size");
        {
            const auto left=static_cast<PageCommand>(static_cast<int>(PageCommand::IslandLeftWidgetFirst)+20);
            const auto right=static_cast<PageCommand>(static_cast<int>(PageCommand::IslandRightWidgetFirst)+1);
            Check(session.HandlePage(left).has_value()&&session.HandlePage(right).has_value(),"independent side selections reach real session");
            Check(weatherActive,"a selected weather widget requests shared weather data");
            Pump(900);GetClientRect(session.Handle(),&r);
            Check(r.right==MulDiv(360,dpi,96)&&State(session)["leftWidget"]==20&&State(session)["rightWidget"]==1,
                  "compact island widens for the two real mini widgets");
            const auto saved=nlohmann::json::parse(files.values["island-display.json"]);
            Check(saved["leftWidget"]=="temperatureRange"&&saved["rightWidget"]=="lunar",
                  "left and right widget identities persist independently");
            const auto preview=nlohmann::json::parse(*session.HandlePage(PageCommand::IslandPreviewCompact));
            Check(preview["leftWidget"]==20&&preview["rightWidget"]==1&&
                  preview["readings"][19]["text"]=="12°–20°"&&preview["readings"][19]["available"]==true,
                  "actual native preview returns selected side values from the shared weather reading");
            session.HandlePage(PageCommand::IslandPreviewStop);
            files.fail=true;
            session.HandlePage(PageCommand::IslandLeftWidgetFirst);
            Check(State(session)["leftWidget"]==20&&State(session)["error"]=="save",
                  "failed side save restores the actual running selection");
            files.fail=false;
            session.HandlePage(PageCommand::IslandLeftWidgetFirst);
            session.HandlePage(PageCommand::IslandRightWidgetFirst);
            Check(!weatherActive,"clearing weather slots releases the shared weather worker");
            Pump(900);GetClientRect(session.Handle(),&r);
            Check(r.right==MulDiv(236,dpi,96),"clearing both slots restores the original compact width");
        }
        Click(hwnd,100,15);Pump(900);
        Check(State(session)["expanded"]==true,"real capsule click expands");
        PostMessageW(hwnd,WM_APP+0x6e3,0,0);Pump(60);Check(State(session)["expanded"]==false,"outside click collapses");
        Click(hwnd,100,15);Pump(900);Check(State(session)["expanded"]==true,"capsule expands again");
        GetClientRect(hwnd,&r);Check(r.right==MulDiv(372,dpi,96)&&r.bottom==MulDiv(156,dpi,96),"spring settles at the expanded size");
        Click(hwnd,186,128);Check(controls==1&&last==dynamic_island::Action::Toggle,"real play pause button routes narrow control");
        Click(hwnd,114,128);Click(hwnd,258,128);Check(controls==3&&last==dynamic_island::Action::Next,"previous next route controls");
        Check(State(session)["hideFullscreen"]==true&&State(session)["size"]=="standard","default display options");
        Check(State(session)["shape"]=="capsule","legacy preferences retain floating capsule by default");
        session.HandlePage(PageCommand::IslandShapeStandard);Pump(900);
        {RECT at{};GetWindowRect(session.Handle(),&at);MONITORINFO monitor{sizeof(monitor)};
         GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
         Check(State(session)["shape"]=="standard"&&at.top==monitor.rcMonitor.top,"attached shape applies at the actual screen top");}
        {files.fail=true;const auto saved=files.values["island-display.json"];
         session.HandlePage(PageCommand::IslandShapeCapsule);Pump(900);
         Check(State(session)["shape"]=="standard"&&State(session)["error"]=="save"&&files.values["island-display.json"]==saved,"failed shape save rolls back native shape and file");files.fail=false;}
        session.HandlePage(PageCommand::IslandSizeLarge);session.HandlePage(PageCommand::IslandHoverOn);
        Check(State(session)["size"]=="large"&&State(session)["hover"]==true&&files.values.count("island-display.json"),"display options persisted beside preferences");
        {   files.fail=true;const auto saved=files.values["island-display.json"];
            session.HandlePage(PageCommand::IslandTopmostOn);
            Check(State(session)["error"]=="save"&&State(session)["topmost"]==false&&session.Running()&&
                  !(GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&files.values["island-display.json"]==saved,
                  "failed display save keeps the previous window and file");
            files.fail=false;}
        session.HandlePage(PageCommand::IslandTopmostOff);
        Check(session.Running()&&!(GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),"desktop layer option recreates a non-topmost window");
        session.HandlePage(PageCommand::IslandTopmostOn);session.HandlePage(PageCommand::IslandSizeStandard);session.HandlePage(PageCommand::IslandHoverOff);
        session.HandlePage(PageCommand::IslandHideFullscreenOff);
        Check(State(session)["hideFullscreen"]==false&&State(session)["topmost"]==true&&
              (GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),"island-only layering and fullscreen choices apply to the actual native window");
        session.HandlePage(PageCommand::IslandHideFullscreenOn);
        session.HandlePage(PageCommand::IslandMaterialFrosted);Check(State(session)["material"]=="frosted-glass","material persisted");
        session.HandlePage(PageCommand::IslandMaterialNone);
        session.HandlePage(PageCommand::IslandCollapse);session.HandlePage(PageCommand::IslandAutoHideOn);
        Check(State(session)["autoHide"]==true,"auto-hide persisted");
        Pump(5200);{RECT rect{};GetWindowRect(session.Handle(),&rect);MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&info);
            Check(rect.bottom>info.rcMonitor.top&&rect.bottom<=info.rcMonitor.top+MulDiv(8,dpi,96),"idle island tucks to a sliver at the screen top");}
        session.HandlePage(PageCommand::IslandAutoHideOff);
        SendMessageW(hwnd,WM_CONTEXTMENU,0,0);
        session.HandlePage(PageCommand::IslandDisable);
        Check(!session.Running()&&!weatherActive,"disable releases window and shared weather consumer");
        Check(GetForegroundWindow()==foreground,"native controls preserve foreground");
        for(int i=0;i<12;++i){session.HandlePage(PageCommand::IslandEnable);const auto old=session.Handle();session.HandlePage(PageCommand::IslandDisable);Check(!IsWindow(old),"repeat lifecycle releases HWND");}
        session.HandlePage(PageCommand::IslandEnable);session.Shutdown();
        Check(!session.Running()&&!weatherActive&&!session.HandlePage(PageCommand::IslandEnable),"shutdown releases shared weather and cannot resurrect");
    }
    {
        shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),options,&files);
        Check(restored.Restore()&&restored.Running()&&restored.Snapshot().locked,"enabled and locked survive restart");
        Check(State(restored)["topmost"]==true&&State(restored)["hideFullscreen"]==true&&State(restored)["hover"]==false&&
              State(restored)["autoHide"]==false&&State(restored)["material"]=="none"&&State(restored)["shape"]=="standard",
              "display options including unchecked auto-hide survive restart");restored.Shutdown();
        files.values["broken.json"]="bad data";options.preferencesPath="broken.json";
        shell::DynamicIslandSession broken(GetModuleHandleW(nullptr),options,&files);
        Check(broken.Restore()&&!broken.Running()&&files.values["broken.json"]=="bad data","corrupt data not overwritten on read");
        broken.HandlePage(PageCommand::IslandEnable);Check(files.values["broken.json.corrupt"]=="bad data"&&broken.Running(),"explicit save isolates corrupt file");
    }
    {
        Files legacyFiles;
        auto legacyOptions=options;legacyOptions.preferencesPath="legacy.json";
        const auto display=nlohmann::json{{"version",1},{"topmost",false},{"autoHide",false},
            {"size","large"},{"material","liquid-glass"},
            {"customization",{{"customIcons",true},{"overview",true},{"left",{{"widget","clock"},{"style","capsule"}}}}}}.dump()+std::string(4500,' ');
        legacyFiles.values["legacy-display.json"]=display;
        shell::DynamicIslandSession legacy(GetModuleHandleW(nullptr),legacyOptions,&legacyFiles);
        Check(legacy.Restore()&&!legacy.Running(),"legacy side/workspace settings never activate a disabled island");
        const auto state=State(legacy);
        Check(!state.contains("customization")&&state["size"]=="large"&&state["material"]=="liquid-glass"&&
              state["topmost"]==false&&state["autoHide"]==false,"legacy customization is ignored while ordinary display settings survive");
        Check(legacyFiles.values["legacy-display.json"]==display,"loading legacy settings does not rewrite user data");
        Check(legacy.HandlePage(PageCommand::IslandSizeSmall).has_value(),"ordinary settings remain editable");
        const auto saved=nlohmann::json::parse(legacyFiles.values["legacy-display.json"]);
        Check(!saved.contains("customization")&&saved["size"]=="small"&&saved["topmost"]==false&&saved["autoHide"]==false,
              "next explicit save retires customization and preserves ordinary settings");
        Check(legacy.HandlePage(PageCommand::IslandResetAppearance).has_value(),"appearance reset is supported while disabled");
        const auto reset=nlohmann::json::parse(legacyFiles.values["legacy-display.json"]);
        Check(reset["shape"]=="capsule"&&reset["material"]=="none"&&reset["size"]=="standard"&&
              reset["topmost"]==false&&reset["autoHide"]==false,
              "appearance reset restores only shape material and size and preserves visibility preferences");
    }
    {
        using namespace platform::windows;
        Files screenFiles;
        auto hardware=ConnectedIslandMonitors();Check(!hardware.empty(),"read the connected display inventory");
        auto first=hardware.front();first.primary=true;
        auto second=first;second.device=L"verification-screen-2";second.primary=false;
        const LONG width=first.bounds.right-first.bounds.left;
        OffsetRect(&second.bounds,-width,0);OffsetRect(&second.work,-width,0);
        auto third=first;third.device=L"verification-screen-3";third.primary=false;
        const LONG height=first.bounds.bottom-first.bounds.top;
        OffsetRect(&third.bounds,0,-height);OffsetRect(&third.work,0,-height);
        std::vector<IslandMonitor> connected{first,second};
        unsigned inventoryReads=0;
        shell::DynamicIslandSessionOptions screenOptions;screenOptions.preferencesPath="screen-policy.json";
        screenOptions.monitors=[&]{++inventoryReads;return connected;};
        const auto select=[](int id){return static_cast<PageCommand>(static_cast<int>(PageCommand::IslandScreenFirst)+id);};
        {
            shell::DynamicIslandSession session(GetModuleHandleW(nullptr),screenOptions,&screenFiles);
            Check(session.Restore()&&!session.Running()&&State(session)["screens"]==nlohmann::json({1,2}),"disabled settings list the current screens without creating windows");
            session.HandlePage(select(1));Check(!session.Running(),"display policy can be saved while the island is disabled");
            session.HandlePage(PageCommand::IslandEnable);session.HandlePage(PageCommand::IslandAutoHideOff);Pump(160);
            Check(IslandWindows().size()==1&&OnScreen(session.Handle(),first),"screen 1 creates one island centered on that screen");
            session.HandlePage(select(2));Pump(100);
            Check(IslandWindows().size()==1&&OnScreen(session.Handle(),second)&&State(session)["screen"]==2,"screen 2 moves the actual native island");
            const auto saved=screenFiles.values["screen-policy-display.json"];screenFiles.fail=true;
            session.HandlePage(select(0));Pump(80);
            Check(IslandWindows().size()==1&&OnScreen(session.Handle(),second)&&State(session)["screen"]==2&&State(session)["error"]=="save"&&
                screenFiles.values["screen-policy-display.json"]==saved,"failed all-screen save restores the old window group and file");
            screenFiles.fail=false;session.HandlePage(select(0));Pump(120);
            const auto all=IslandWindows();
            Check(all.size()==2&&std::any_of(all.begin(),all.end(),[&](HWND hwnd){return OnScreen(hwnd,first);})&&
                std::any_of(all.begin(),all.end(),[&](HWND hwnd){return OnScreen(hwnd,second);}),"all screens creates a real island on each target");
            const auto reads=inventoryReads;
            for(const HWND hwnd:all)SendMessageW(hwnd,WM_TIMER,1,0);
            Check(inventoryReads==reads+1,"multiple islands poll the display inventory only once per tick");
            const auto other=*std::find_if(all.begin(),all.end(),[&](HWND hwnd){return hwnd!=session.Handle();});
            SendMessageW(other,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
            Check(State(session)["expanded"]==true,"the second island accepts normal expansion input");
            session.HandlePage(PageCommand::IslandCollapse);
            const auto allSaved=screenFiles.values["screen-policy-display.json"];
            connected={first};session.HandlePage(PageCommand::Ready);Pump(80);
            Check(IslandWindows().size()==1&&State(session)["screens"]==nlohmann::json({1})&&screenFiles.values["screen-policy-display.json"]==allSaved,
                "disconnect removes duplicate windows without rewriting the all-screen preference");
            connected={first,second,third};session.HandlePage(PageCommand::Ready);Pump(100);
            Check(IslandWindows().size()==3&&State(session)["screens"]==nlohmann::json({1,2,3}),"a newly connected screen receives its own island in all-screen mode");
            session.HandlePage(select(2));Pump(80);const auto selectedSaved=screenFiles.values["screen-policy-display.json"];
            connected={first};session.HandlePage(PageCommand::Ready);Pump(80);
            Check(IslandWindows().size()==1&&OnScreen(session.Handle(),first)&&State(session)["screen"]==2&&screenFiles.values["screen-policy-display.json"]==selectedSaved,
                "an unavailable selected screen falls back to the primary screen and retains the saved choice");
            connected={first,second};session.HandlePage(PageCommand::Ready);Pump(80);
            Check(OnScreen(session.Handle(),second),"reconnecting the selected screen restores its island");
            session.HandlePage(select(64));
            Check(State(session)["error"]=="screen"&&State(session)["screen"]==2&&screenFiles.values["screen-policy-display.json"]==selectedSaved,
                "a stale screen selection is rejected without changing the saved policy");
            session.Shutdown();Check(IslandWindows().empty(),"shutting down releases the entire display group");
        }
        shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),screenOptions,&screenFiles);
        Check(restored.Restore()&&State(restored)["screen"]==2&&OnScreen(restored.Handle(),second),"the selected screen survives restart");
        restored.HandlePage(PageCommand::IslandResetAppearance);
        Check(State(restored)["screen"]==2,"appearance reset preserves the display policy");
        restored.HandlePage(PageCommand::IslandReset);Pump(80);
        Check(State(restored)["screen"]==1&&IslandWindows().size()==1&&OnScreen(restored.Handle(),first),"restore defaults returns to the primary screen");
        restored.HandlePage(select(0));Pump(80);
        const auto windows=IslandWindows();Check(windows.size()==2,"all-screen mode can be restored after reset");
        const auto other=*std::find_if(windows.begin(),windows.end(),[&](HWND hwnd){return hwnd!=restored.Handle();});
        SendMessageW(other,WM_CLOSE,0,0);
        Check(!restored.Running()&&IslandWindows().empty(),"closing an island on another screen closes the component once");
    }
    {
        using namespace platform::windows;
        unsigned settingsOpened=0,calendarOpened=0,mediaControls=0;
        IslandWindow window(GetModuleHandleW(nullptr),[&](dynamic_island::Action action){
            using dynamic_island::Action;
            if(action==Action::Settings||action==Action::WeatherSettings||action==Action::PerformanceSettings)++settingsOpened;
            if(action==Action::Toggle||action==Action::Previous||action==Action::Next)++mediaControls;
        });
        widget::Preferences prefs;prefs.enabled=true;
        IslandDisplay display;
        Check(window.Start(prefs,display),"preview window starts");
        window.Update({{},L"09:41",L"2026年10月19日 星期一",0,false,false,false});window.Settle();
        SaveFrame(window.Frame(),L"idle.png");
        auto playing=media;playing.spectrum.fill(.5f);playing.spectrum[2]=.9f;playing.energy=.6f;
        window.Update({playing,L"",L"",0,false,false,false});window.Settle();SaveFrame(window.Frame(),L"compact.png");
        display.leftWidget=dynamic_island::CompactWidget::TemperatureRange;
        display.rightWidget=dynamic_island::CompactWidget::Lunar;
        dynamic_island::Weather compactWeather;compactWeather.available=compactWeather.hasRange=true;
        compactWeather.low=12;compactWeather.high=20;compactWeather.temperature=16;window.SetWeather(compactWeather);
        Check(window.SetDisplay(display),"real native island accepts independent compact side widgets");
        window.Settle();SaveFrame(window.Frame(),L"compact-widgets.png");
        RECT compactRect{};GetClientRect(window.Handle(),&compactRect);
        Check(compactRect.right==MulDiv(360,static_cast<int>(GetDpiForWindow(window.Handle())),96),
              "real compact island leaves separate readable left and right lanes");
        const auto nativePreview=window.Preview(display,false);
        Check(nativePreview.width>0&&nativePreview.pixels.pixels.size()==
              static_cast<std::size_t>(nativePreview.width)*nativePreview.height,
              "the production preview uses the same native compact rendering path");
        SaveFrame(nativePreview.pixels,L"compact-widgets-preview.png");
        IslandPreviewRenderer compactRenderer;
        SaveFrame(compactRenderer.Render(nativePreview,display,{}),L"compact-widgets-scene.png");
        wchar_t readingsExe[32768]{};GetModuleFileNameW(nullptr,readingsExe,32768);
        const auto readingsPath=std::filesystem::path(readingsExe).parent_path()/L"verification/dynamic-island/compact-readings.json";
        const auto readings=window.CompactReadings();
        std::ofstream readingsFile(readingsPath,std::ios::binary);
        for(const wchar_t byte:readings)readingsFile.put(static_cast<char>(byte));
        IslandTodoSource navigation;navigation.openCalendar=[&]{++calendarOpened;};
        window.SetTodoSource(navigation);
        using W=dynamic_island::CompactWidget;
        for(int id=1;id<static_cast<int>(W::Count);++id){
            const auto choice=static_cast<W>(id);
            if(choice==W::Files)continue;  // File tray clicks explicitly request its file menu.
            display.leftWidget=display.rightWidget=choice;
            Check(window.SetDisplay(display),"compact readout applies to both sides");
            window.Update({playing,L"",L"",0,false,false,false});window.Settle();
            for(const int x:{20,340}){
                window.Expand(false);window.Settle();
                Click(window.Handle(),x,18);window.Settle();
                Check(settingsOpened==0&&calendarOpened==0,"ordinary compact clicks never open a settings manager");
                Check(window.Expanded()&&window.Page()==IslandPage::Music,"either readout expands available music controls");
            }
            window.Expand(false);window.Update({{},L"09:41",L"",0,false,false,false});window.Settle();
            for(const int x:{20,240}){
                window.Expand(false);window.Settle();
                Click(window.Handle(),x,18);window.Settle();
                Check(window.Expanded()&&window.Page()==IslandPage::Music&&settingsOpened==0&&calendarOpened==0,
                      "either idle readout opens the empty music view without opening settings");
            }
        }
        window.Update({playing,L"",L"",0,false,false,false});window.Settle();
        {
            const auto hook=SetWindowsHookExW(WH_CBT,PreventMenuActivation,nullptr,GetCurrentThreadId());
            Check(hook!=nullptr,"keep context menu verification on its isolated desktop");
            menuOwner=window.Handle();
            const auto timer=SetTimer(nullptr,0,40,SelectSettings);Check(timer!=0,"arm explicit settings selection");
            SendMessageW(window.Handle(),WM_RBUTTONUP,0,MAKELPARAM(20,18));
            KillTimer(nullptr,timer);menuOwner=nullptr;UnhookWindowsHookEx(hook);
            Check(settingsOpened==1&&calendarOpened==0&&GetCapture()==nullptr,
                  "only the explicit Open settings menu command requests the island manager");
        }
        window.Expand(false);window.Settle();
        display.leftWidget=display.rightWidget=dynamic_island::CompactWidget::None;
        Check(window.SetDisplay(display),"mini widget choices can be cleared without rebuilding other settings");window.Settle();
        window.Expand(true);window.Settle();window.Update({playing,L"",L"",0,false,false,false});
        SaveFrame(window.Frame(),L"music.png");
        {BOOL animation=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0);
         const HWND same=window.Handle();RECT before{},during{},final{};GetWindowRect(same,&before);
         display.shape=IslandShape::Standard;window.SetDisplay(display);Pump(85);GetWindowRect(same,&during);
         SaveFrame(window.Frame(),L"shape-transition.png");
         window.Settle();GetWindowRect(same,&final);
         Check(window.Handle()==same,"shape morph retains one native HWND");
         if(animation)Check(during.top<before.top&&during.top>final.top,"shape morph moves through an intermediate position");
         display.shape=IslandShape::Capsule;window.SetDisplay(display);Pump(45);
         display.shape=IslandShape::Standard;window.SetDisplay(display);window.Settle();}
        {const auto frame=window.Frame();const auto alpha=[&](int x,int y){return frame.pixels[static_cast<std::size_t>(MulDiv(y,GetDpiForWindow(window.Handle()),96))*frame.width+MulDiv(x,GetDpiForWindow(window.Handle()),96)]>>24;};
         Check(alpha(12,1)>200&&alpha(1,18)==0&&alpha(186,70)>200,"attached contour has top shoulder, transparent side, and solid body");SaveFrame(frame,L"shape-standard-expanded.png");}
        window.Expand(false);window.Settle();SaveFrame(window.Frame(),L"shape-standard-compact.png");
        {
            playing.artwork="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAAEnQAABJ0Ad5mH3gAAAAiSURBVChThcihEQAACAAh919aOx88GjP7SCihhBJKKKEEDoMXf4GjQpJVAAAAAElFTkSuQmCC";
            display.autoHide=false;window.SetDisplay(display);
            window.Update({playing,L"",L"",0,false,false,false});window.Settle();
            const int dpi=static_cast<int>(GetDpiForWindow(window.Handle()));
            const auto px=[&](int dip){return MulDiv(dip,dpi,96);};
            const auto frame=window.Frame();
            int minX=frame.width,minY=frame.height,maxX=0,maxY=0,count=0;
            for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){
                const auto p=frame.pixels[static_cast<std::size_t>(y)*frame.width+x];
                if(x<static_cast<unsigned>(px(100))&&((p>>8)&255)>180&&((p>>16)&255)<80&&(p&255)<80){
                    minX=std::min(minX,static_cast<int>(x));maxX=std::max(maxX,static_cast<int>(x));
                    minY=std::min(minY,static_cast<int>(y));maxY=std::max(maxY,static_cast<int>(y));++count;
                }
            }
            Check(minX>=px(16)&&count>px(20)*px(20)&&count>=(maxX-minX+1)*(maxY-minY+1)*.96,
                  "standard compact artwork remains a complete rectangle inside the shoulders");
            SaveFrame(frame,L"shape-standard-artwork.png");
            playing.playing=false;window.Update({playing,L"",L"",0,false,false,false});window.Settle();
            const auto paused=window.Frame();int barPixels=0;
            for(int y=px(6);y<px(28);++y){
                const auto p=paused.pixels[static_cast<std::size_t>(y)*paused.width+px(199)];
                if(((p>>8)&255)>90&&((p>>8)&255)>((p>>16)&255)+40)++barPixels;
            }
            Check(barPixels>=px(6),"standard paused spectrum retains upright bars instead of corner dots");
            SaveFrame(paused,L"shape-standard-paused.png");
            IslandPreviewRenderer renderer;
            wchar_t samplePath[32768]{};
            const DWORD sampleLength=GetEnvironmentVariableW(L"LUME_ISLAND_PREVIEW_SAMPLES",samplePath,32768);
            const auto sampleDirectory=sampleLength&&sampleLength<32768?std::filesystem::path(samplePath):std::filesystem::path{};
            if(!sampleDirectory.empty())std::filesystem::create_directories(sampleDirectory);
            wchar_t wallpaperPath[32768]{};
            const DWORD wallpaperLength=GetEnvironmentVariableW(L"LUME_ISLAND_PREVIEW_WALLPAPER",wallpaperPath,32768);
            const auto sampleWallpaper=wallpaperLength&&wallpaperLength<32768?std::filesystem::path(wallpaperPath):std::filesystem::path{};
            const auto same=window.Handle();
            for(const auto shape:{IslandShape::Standard,IslandShape::Capsule})
              for(const auto size:{IslandSize::Small,IslandSize::Standard,IslandSize::Large})
              for(const auto material:{IslandMaterial::None,IslandMaterial::Liquid,IslandMaterial::Frosted}) {
                auto choice=display;choice.shape=shape;choice.size=size;choice.material=material;
                window.SetDisplay(choice);window.Settle();
                const auto preview=window.Preview(choice,false);
                Check(window.Handle()==same&&!window.Expanded(),"preview never changes live HWND or expansion");
                Check(preview.width>0&&preview.height>0,"offscreen native foreground exists");
                auto changed=playing;changed.title=L"Different desktop song";changed.artist=L"Different artist";changed.artwork.clear();changed.position=90;
                window.Update({changed,L"",L"",0,false,false,false});
                const auto afterMedia=window.Preview(choice,false);
                Check(preview.pixels.pixels==afterMedia.pixels.pixels,"sample preview is independent of desktop media and lyrics");
                window.Update({playing,L"",L"",0,false,false,false});
                const auto scene=renderer.Render(preview,choice,{});
                Check(scene.width==960&&scene.height==300,"shared material shader produces a complete native preview scene");
                if(size==IslandSize::Standard&&shape==IslandShape::Standard)SaveFrame(scene,
                    material==IslandMaterial::None?L"preview-native-black.png":material==IslandMaterial::Liquid?L"preview-native-liquid.png":L"preview-native-frosted.png");
                const auto url=IslandPreviewRenderer::PngDataUrl(scene);
                Check(url.rfind("data:image/png;base64,",0)==0&&url.size()<2800000,"native preview encodes bounded PNG data");
                const auto jpeg=IslandPreviewRenderer::JpegDataUrl(scene);
                Check(jpeg.rfind("data:image/jpeg;base64,",0)==0&&jpeg.size()<200000,"animation frame uses bounded JPEG data");
                const char* shapeName=shape==IslandShape::Standard?"standard":"capsule";
                const char* sizeName=size==IslandSize::Small?"small":size==IslandSize::Large?"large":"standard";
                const char* materialName=material==IslandMaterial::Liquid?"liquid-glass":material==IslandMaterial::Frosted?"frosted-glass":"none";
                if(!sampleDirectory.empty())for(bool expanded:{false,true}) {
                    const auto sample=window.Preview(choice,expanded);
                    const auto wallpaper=!sampleWallpaper.empty()&&std::filesystem::exists(sampleWallpaper)?sampleWallpaper.wstring():std::wstring{};
                    const auto rendered=renderer.Render(sample,choice,wallpaper);
                    const auto filename="island-native-"+std::string(shapeName)+"-"+sizeName+"-"+materialName+"-"+(expanded?"expanded":"compact")+".png";
                    Check(IslandPreviewRenderer::WritePng(rendered,(sampleDirectory/filename).wstring()),"write native reference preview");
                }
              }
            window.ReleasePreview();display.shape=IslandShape::Standard;display.size=IslandSize::Standard;display.material=IslandMaterial::None;
            {
                IslandDisplay from;from.shape=IslandShape::Capsule;
                const auto start=window.Preview(from,false);
                auto to=from;to.shape=IslandShape::Standard;
                auto mid=window.Preview(to,false,1.f/30);
                Check(mid.animating&&mid.shapeMix>0&&mid.shapeMix<1&&mid.topOffset>0&&mid.topOffset<start.topOffset,
                      "preview shape morph has continuous shoulders and top position");
                const auto reversed=window.Preview(from,false,1.f/30);
                Check(reversed.animating&&reversed.shapeMix>0&&reversed.shapeMix<1,"reversal retains intermediate shape");
                to.size=IslandSize::Large;
                unsigned frames=0;
                do {mid=window.Preview(to,true,1.f/30);++frames;}while(mid.animating&&frames<90);
                Check(frames>3&&frames<90&&mid.shapeMix==1&&mid.width>start.width&&mid.height>start.height,
                      "preview geometry transition settles and stops");
                const auto stable=window.Preview(to,true,1.f/30);
                Check(!stable.animating&&stable.pixels.pixels==mid.pixels.pixels,"settled sample needs no recurring frames");
                window.ReleasePreview();
            }
            window.SetDisplay(display);window.Settle();
            display.autoHide=true;window.SetDisplay(display);SendMessageW(window.Handle(),WM_MOUSELEAVE,0,0);Pump(4300);window.Settle();
            RECT tucked{};GetWindowRect(window.Handle(),&tucked);MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
            Check(tucked.bottom>monitor.rcMonitor.top&&tucked.bottom<=monitor.rcMonitor.top+px(8),"standard auto-hide leaves a reachable edge");
            const auto hidden=window.Frame();
            Check(std::none_of(hidden.pixels.begin(),hidden.pixels.end(),[](std::uint32_t p){return (p&0x00ffffff)!=0;}),
                  "tucked edge contains no artwork lyrics or spectrum pixels");
            SaveFrame(hidden,L"shape-standard-tucked.png");
            display.autoHide=false;playing.artwork.clear();playing.playing=true;
            window.SetDisplay(display);window.Update({playing,L"",L"",0,false,false,false});
        }
        display.shape=IslandShape::Capsule;window.SetDisplay(display);window.Expand(true);window.Settle();
        {const auto frame=window.Frame();const auto index=static_cast<std::size_t>(MulDiv(1,GetDpiForWindow(window.Handle()),96))*frame.width+MulDiv(12,GetDpiForWindow(window.Handle()),96);
         Check((frame.pixels[index]>>24)==0,"capsule keeps a convex round top corner");SaveFrame(frame,L"shape-capsule-expanded.png");}
        window.Expand(false);window.Settle();SaveFrame(window.Frame(),L"shape-capsule-compact.png");
        display.size=IslandSize::Small;window.SetDisplay(display);window.Expand(false);window.Settle();
        SaveFrame(window.Frame(),L"compact-small.png");
        display.size=IslandSize::Standard;Check(window.SetDisplay(display),"idle entry uses standard geometry");window.Settle();
        // Idle entry remains available even without a playback session.
        window.Update({{},L"09:41",L"",0,false,false,false});window.Settle();
        const auto clickCentre=[&]{
            RECT bounds{};GetClientRect(window.Handle(),&bounds);
            const int dpi=static_cast<int>(GetDpiForWindow(window.Handle()));
            Click(window.Handle(),MulDiv(bounds.right/2,96,dpi),MulDiv(bounds.bottom/2,96,dpi));window.Settle();
        };
        clickCentre();
        Check(window.Expanded()&&window.Page()==IslandPage::Music,"idle capsule click opens an empty playback view");
        SaveFrame(window.Frame(),L"idle-expanded.png");
        clickCentre();Check(!window.Expanded(),"clicking the idle view again collapses it");
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Expanded()&&window.Page()==IslandPage::Music,"idle capsule wheel opens the same empty playback view");
        const auto beforeEmptyControls=mediaControls;
        Click(window.Handle(),186,128);window.Settle();
        Check(!window.Expanded()&&mediaControls==beforeEmptyControls,"idle view has no invisible playback controls");
        window.Update({playing,L"",L"",0,false,false,false});window.Settle();
        window.Expand(true);window.Settle();
        Check(window.Page()==IslandPage::Music,"music page again after a brief media gap");
        window.Expand(false);window.Settle();
        // A temporary media gap keeps an open panel, then restores its controls.
        window.Expand(true);window.Settle();
        window.Update({{},L"09:41",L"",0,false,false,false});window.Settle();
        Check(window.Expanded()&&window.Page()==IslandPage::Music,"media gap keeps the explicit expansion open");
        window.Update({{},L"09:41",L"",0,false,false,true});window.Settle();
        SaveFrame(window.Frame(),L"idle-expanded-en.png");
        window.Update({playing,L"",L"",0,false,false,false});window.Settle();
        Check(window.Expanded()&&window.Page()==IslandPage::Music,"an open island returns to music once media is back");
        window.Expand(false);window.Settle();
        IslandTodoSource source;
        window.SetTodoSource(source);
        display.size=IslandSize::Standard;window.SetDisplay(display);
        window.Expand(true);window.Settle();
        Check(window.Page()==IslandPage::Music,"music remains the only expanded page without reminders");
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Music,"wheel cannot reopen removed pages");
        display.leftWidget=dynamic_island::CompactWidget::Cpu;
        Check(window.SetDisplay(display),"compact CPU choice starts performance sampling");
        const auto warmDeadline=GetTickCount64()+6000;
        while ((!window.PerformanceReading() || !window.PerformanceReading()->cpu) && GetTickCount64()<warmDeadline) Pump(20);
        Check(window.PerformanceReading() && window.PerformanceReading()->cpu,"compact performance readings stay warm");
        const auto warmStamp=window.PerformanceReading()->monotonicMillis;
        window.Expand(false);Pump(1200);
        Check(window.PerformanceReading() && window.PerformanceReading()->cpu &&
              window.PerformanceReading()->monotonicMillis>warmStamp,"collapsed island keeps rates current");
        window.Expand(true);window.Settle();
        Check(window.PerformanceReading() && window.PerformanceReading()->cpu,"reopening music keeps compact rates");
        display.leftWidget=dynamic_island::CompactWidget::None;
        Check(window.SetDisplay(display),"clearing compact CPU choice applies");
        Check(!window.PerformanceReading(),"unused performance samples are released");
        std::string opened;source.openTask=[&](const std::string& id){opened=id;};
        source.find=[](const std::string& id)->std::optional<IslandTodo>{IslandTodo t;t.id=id;t.title=L"给妈妈打电话";t.time=L"10月20日 19:30";t.notes=L"记得问周末安排";return t;};
        window.SetTodoSource(source);
        window.Expand(false);window.Settle();
        Check(window.ShowNotice("task-7",L"给妈妈打电话",L"19:30"),"island shows a reminder notice");window.Settle();
        SaveFrame(window.Frame(),L"notice.png");
        Check(window.ShowNotice("task-8",L"第二条提醒",L"19:30"),"a second reminder due at the same time is accepted");
        Click(window.Handle(),170,30);window.Settle();
        Check(window.Expanded()&&window.Page()==IslandPage::Reminder&&opened.empty(),"clicking the notice opens the reminder page");
        SaveFrame(window.Frame(),L"reminders.png");
        Click(window.Handle(),200,64);Check(opened=="task-8","the newest reminder is listed first and opens its task");
        Click(window.Handle(),200,108);Check(opened=="task-7","a queued reminder is listed too");
        SendMessageW(window.Handle(),WM_MOUSEHWHEEL,MAKEWPARAM(0,static_cast<WORD>(WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Music,"sideways scrolling wraps to the first page");
        window.Expand(false);window.Settle();
        display.topmost=false;Check(window.SetDisplay(display),"desktop layer option applies");
        Check(!(GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),"a reminder-capable island remains below applications");
        {
            using namespace platform::windows::window_layer;
            Context policy;contextReader=[&]{return policy;};Start();
            display.topmost=true;display.hideFullscreen=true;display.autoHide=false;
            Check(window.SetDisplay(display),"explicit overlay starts in the shared policy");window.Settle();
            Check(IsWindowVisible(window.Handle())&&(GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
                  "actual island window can show above normal applications");
            RECT area{};GetWindowRect(window.Handle(),&area);
            policy.foreignForeground=true;policy.fullscreen.push_back(area);Update(policy);Pump(50);window.Settle();
            Check(!IsWindowVisible(window.Handle()),"actual island hides immediately for fullscreen with the option enabled");
            display.hideFullscreen=false;Check(window.SetDisplay(display),"fullscreen visibility preference applies live");window.Settle();
            Check(IsWindowVisible(window.Handle())&&!Suppressed(window.Handle()),"actual island resumes over ordinary fullscreen when explicitly allowed");
            policy.exclusive=true;Update(policy);Pump(50);window.Settle();
            Check(!IsWindowVisible(window.Handle()),"actual island still yields to exclusive game protection");
            policy={};Update(policy);Pump(1100);window.Settle();
            Check(IsWindowVisible(window.Handle()),"actual island restores after game protection clears");
            display.topmost=false;display.hideFullscreen=true;Check(window.SetDisplay(display),"desktop layer can be restored");window.Settle();
            Check(!(GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),"desktop layer clears native topmost state");
            Stop();contextReader={};
        }
        window.SetTodoSource({});
        window.Stop();Check(window.Frame().pixels.empty(),"stop frees rendered bitmap");
    }
    CoUninitialize();std::cout<<"PASS island model, native input, persistence, rollback, lifecycle and rendering\n";
}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto name=L"LumeIslandTest-"+std::to_wstring(GetCurrentProcessId());
    HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);Check(desktop!=nullptr,"create isolated desktop");
    std::thread runner([&]{Check(SetThreadDesktop(desktop)!=FALSE,"bind isolated desktop");
        Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;Check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"PNG encoder");
        Scenario();Gdiplus::GdiplusShutdown(token);});runner.join();
    Check(CloseDesktop(desktop)!=FALSE,"close isolated desktop");return 0;
}
