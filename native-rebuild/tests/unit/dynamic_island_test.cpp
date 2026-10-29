#include "apps/shell/dynamic_island_session.h"
#include "platform/windows/dynamic_island/island_window.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <gdiplus.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <thread>

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
void Pump(unsigned ms){const auto end=GetTickCount64()+ms;do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<end);}
void Click(HWND hwnd,int x,int y){const auto dpi=static_cast<int>(GetDpiForWindow(hwnd));const auto at=MAKELPARAM(MulDiv(x,dpi,96),MulDiv(y,dpi,96));SendMessageW(hwnd,WM_LBUTTONDOWN,0,at);SendMessageW(hwnd,WM_LBUTTONUP,0,at);}
nlohmann::json State(const shell::DynamicIslandSession& s){return nlohmann::json::parse(s.State());}
void SaveFrame(const platform::windows::WidgetBitmap& frame,const wchar_t* name){
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto root=std::filesystem::path(exe).parent_path()/L"verification"/L"dynamic-island";
    std::filesystem::create_directories(root);
    Gdiplus::Bitmap image(frame.width,frame.height,static_cast<INT>(frame.width*4),PixelFormat32bppPARGB,
                         reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(frame.pixels.data())));
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    Check(image.Save((root/name).c_str(),&png)==Gdiplus::Ok,"save actual native foreground");
}
bool HasLightning(const platform::windows::WidgetBitmap& frame){
    return std::any_of(frame.pixels.begin(),frame.pixels.end(),[](std::uint32_t pixel){
        const auto red=(pixel>>16)&0xff,green=(pixel>>8)&0xff,blue=pixel&0xff;
        return red>green+35&&green>blue+35&&red>100;
    });
}
void Scenario(){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization");
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
    const auto foreground=GetForegroundWindow();
    {
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr),options,&files);
        Check(session.Restore()&&!session.Running()&&files.values.empty(),"disabled restore has no window or file writes");
        Check(!session.HandlePage(PageCommand::DockEnable),"unrelated commands rejected");
        Check(session.HandlePage(PageCommand::IslandEnable).has_value()&&session.Running(),"enable creates window");
        Check(weatherActive,"island enables its shared weather consumer");
        Check((GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,"island shows above windows by default");
        Pump(900);Check(IsWindowVisible(session.Handle()),"idle capsule is visible by default");
        Check(GetForegroundWindow()==foreground,"enable never takes focus");
        Check(SendMessageW(session.Handle(),WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,"click never activates");
        const auto original=files.values["island.json"];files.fail=true;
        session.HandlePage(PageCommand::IslandDisable);
        Check(session.Running()&&session.Snapshot().enabled&&files.values["island.json"]==original,"failed disable restores live window and original preferences");
        Check(weatherActive,"failed disable also restores the shared weather consumer");
        Check(State(session)["error"]=="save","save failure reported");
        files.fail=false;session.HandlePage(PageCommand::IslandLock);Check(session.Snapshot().locked,"lock persisted");
        session.Update(media);const HWND hwnd=session.Handle();
        Pump(900);Check(IsWindowVisible(hwnd),"playback keeps the island visible");
        RECT r{};GetClientRect(hwnd,&r);const int dpi=static_cast<int>(GetDpiForWindow(hwnd));
        Check(r.right==MulDiv(236,dpi,96)&&r.bottom==MulDiv(34,dpi,96),"compact capsule settles at its playing size");
        Click(hwnd,100,15);Pump(900);
        Check(State(session)["expanded"]==true,"real capsule click expands");
        PostMessageW(hwnd,WM_APP+0x6e3,0,0);Pump(60);Check(State(session)["expanded"]==false,"outside click collapses");
        Click(hwnd,100,15);Pump(900);Check(State(session)["expanded"]==true,"capsule expands again");
        GetClientRect(hwnd,&r);Check(r.right==MulDiv(372,dpi,96)&&r.bottom==MulDiv(156,dpi,96),"spring settles at the expanded size");
        Click(hwnd,186,128);Check(controls==1&&last==dynamic_island::Action::Toggle,"real play pause button routes narrow control");
        Click(hwnd,114,128);Click(hwnd,258,128);Check(controls==3&&last==dynamic_island::Action::Next,"previous next route controls");
        Check(State(session)["hideFullscreen"]==true&&State(session)["size"]=="standard","default display options");
        session.HandlePage(PageCommand::IslandSizeLarge);session.HandlePage(PageCommand::IslandHoverOn);
        Check(State(session)["size"]=="large"&&State(session)["hover"]==true&&files.values.count("island-display.json"),"display options persisted beside preferences");
        {   files.fail=true;const auto saved=files.values["island-display.json"];
            session.HandlePage(PageCommand::IslandTopmostOff);
            Check(State(session)["error"]=="save"&&State(session)["topmost"]==true&&session.Running()&&
                  (GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&files.values["island-display.json"]==saved,
                  "failed display save keeps the previous window and file");
            files.fail=false;}
        session.HandlePage(PageCommand::IslandTopmostOff);
        Check(session.Running()&&!(GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),"desktop layer option recreates a non-topmost window");
        session.HandlePage(PageCommand::IslandTopmostOn);session.HandlePage(PageCommand::IslandSizeStandard);session.HandlePage(PageCommand::IslandHoverOff);
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
        Check(State(restored)["topmost"]==true&&State(restored)["hover"]==false&&
              State(restored)["autoHide"]==false&&State(restored)["material"]=="none",
              "display options including unchecked auto-hide survive restart");restored.Shutdown();
        files.values["broken.json"]="bad data";options.preferencesPath="broken.json";
        shell::DynamicIslandSession broken(GetModuleHandleW(nullptr),options,&files);
        Check(broken.Restore()&&!broken.Running()&&files.values["broken.json"]=="bad data","corrupt data not overwritten on read");
        broken.HandlePage(PageCommand::IslandEnable);Check(files.values["broken.json.corrupt"]=="bad data"&&broken.Running(),"explicit save isolates corrupt file");
    }
    {
        using namespace platform::windows;
        IslandWindow window(GetModuleHandleW(nullptr),{});
        widget::Preferences prefs;prefs.enabled=true;
        IslandDisplay display;
        Check(window.Start(prefs,display),"preview window starts");
        window.Update({{},L"09:41",L"2026年10月19日 星期一",0,false,false,false});window.Settle();
        SaveFrame(window.Frame(),L"idle.png");
        auto playing=media;playing.spectrum.fill(.5f);playing.spectrum[2]=.9f;playing.energy=.6f;
        window.Update({playing,L"",L"",0,false,false,false});window.Settle();SaveFrame(window.Frame(),L"compact.png");
        window.Expand(true);window.Settle();window.Update({playing,L"",L"",0,false,false,false});
        SaveFrame(window.Frame(),L"music.png");
        display.size=IslandSize::Small;window.SetDisplay(display);window.Expand(false);window.Settle();
        SaveFrame(window.Frame(),L"compact-small.png");
        // Media drops out for a moment between tracks while collapsed; once it is
        // back the island must open on the music page, not the calendar fallback.
        window.Update({{},L"09:41",L"",0,false,false,false});window.Settle();
        Check(window.Page()==IslandPage::Calendar,"no media falls back to the calendar");
        window.Update({playing,L"",L"",0,false,false,false});window.Settle();
        window.Expand(true);window.Settle();
        Check(window.Page()==IslandPage::Music,"music page again after a brief media gap");
        window.Expand(false);window.Settle();
        // Opened during the gap: the calendar fallback gives way when the song is back.
        window.Update({{},L"09:41",L"",0,false,false,false});window.Settle();
        window.Expand(true);window.Settle();
        window.Update({playing,L"",L"",0,false,false,false});window.Settle();
        Check(window.Page()==IslandPage::Music,"an open island returns to music once media is back");
        window.Expand(false);window.Settle();
        // Calendar and todo pages read the shared todo store through the source.
        int toggles=0;
        IslandTodoSource source;
        source.forDay=[](calendar::Date){return std::vector<IslandTodo>{
            {"a",L"提交周报",L"09:30",true,false,0x6bd89b},{"b",L"和设计评审灵动岛",L"14:00",false,false,0xff6961},{"c",L"买牛奶",L"",false,false,0}};};
        source.hasTodos=[](calendar::Date d){return d.day%5==0;};
        source.toggle=[&](const IslandTodo& t){Check(t.id=="b","toggle routes the clicked todo");++toggles;};
        window.SetTodoSource(source);
        display.size=IslandSize::Standard;window.SetDisplay(display);
        window.Expand(true);window.Settle();
        Check(window.Page()==IslandPage::Music,"music page first while playing");
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Calendar,"wheel moves to the calendar page");
        SaveFrame(window.Frame(),L"calendar.png");
        // The weather page only projects the host's reading; it never fetches.
        dynamic_island::Weather weather;
        weather.enabled=true;weather.available=true;weather.city=L"广州 · 天河区";weather.condition=L"多云";
        weather.temperature=26;weather.hasRange=true;weather.high=29;weather.low=21;weather.code=3;weather.isDay=true;
        weather.hours=6;
        for(std::size_t i=0;i<weather.hourly.size();++i){
            auto& hour=weather.hourly[i];hour.label=std::to_wstring(10+i);hour.temperature=26+static_cast<int>(i%3);
            hour.code=i<2?3:i<4?61:0;
        }
        window.SetWeather(weather);
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Weather,"wheel moves to the weather page");
        SaveFrame(window.Frame(),L"weather.png");
        auto rain=weather;rain.city=L"上海 · 浦东新区";rain.condition=L"中雨";rain.code=61;rain.temperature=19;rain.high=22;rain.low=16;
        window.SetWeather(rain);window.Settle();
        SaveFrame(window.Frame(),L"weather-rain.png");
        auto clear=weather;clear.city=L"北京 · 朝阳区";clear.condition=L"晴";clear.code=0;clear.temperature=31;clear.high=34;clear.low=23;
        window.SetWeather(clear);window.Settle();
        SaveFrame(window.Frame(),L"weather-clear.png");
        clear.isDay=false;clear.condition=L"晴";clear.temperature=18;clear.high=34;clear.low=23;
        window.SetWeather(clear);window.Settle();
        SaveFrame(window.Frame(),L"weather-night.png");
        auto unknown=weather;unknown.code=999;unknown.condition=L"霾";
        window.SetWeather(unknown);window.Settle();
        Check(!HasLightning(window.Frame()),"unknown weather code does not render as thunderstorm");
        auto storm=weather;storm.code=95;storm.condition=L"雷暴";
        window.SetWeather(storm);window.Settle();
        Check(HasLightning(window.Frame()),"WMO thunderstorm code renders lightning");
        window.SetWeather({});window.Settle();
        SaveFrame(window.Frame(),L"weather-off.png");
        window.SetWeather(weather);window.Settle();
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Performance,"wheel moves to the performance page");
        {   // The page samples while open; give the 1 s sampler two readings.
            const auto until=GetTickCount64()+2500;
            while(GetTickCount64()<until){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}
            window.Settle();
        }
        SaveFrame(window.Frame(),L"performance.png");
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Todo,"wheel moves to the todo page");
        SaveFrame(window.Frame(),L"todo.png");
        Click(window.Handle(),30,48+29+13);Check(toggles==1,"todo checkbox toggles through the shared store");
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Music,"wheel wraps back to music");
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
        Check(!window.ShowNotice("task-8",L"x",L""),"a desktop-layer island leaves reminders to the popup");
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
