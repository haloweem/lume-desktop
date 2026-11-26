#include "apps/shell/dynamic_island_session.h"
#include "platform/windows/dynamic_island/island_window.h"
#include "platform/windows/widget_ui/window_layer_policy.h"
#include "platform/windows/dynamic_island/island_preview.h"
#include "platform/windows/dynamic_island/notification_icon.h"
#include "platform/windows/dynamic_island/off_work_voice_captions.h"
#include "platform/windows/dynamic_island/notification_display_policy.h"
#include "platform/windows/dynamic_island/island_performance_card.h"
#include "platform/windows/dynamic_island/island_clock_card.h"
#include "core/calendar/calendar_date.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
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
int IslandPx(int dip,int dpi=96,float scale=1.2f){return MulDiv(dip,static_cast<int>(std::lround(static_cast<float>(dpi)*scale)),96);}
unsigned PreviewPx(int dip,float scale=1.2f){return static_cast<unsigned>(IslandPx(dip,96,scale));}
void Click(HWND hwnd,int x,int y){const auto dpi=static_cast<int>(GetDpiForWindow(hwnd));const auto at=MAKELPARAM(IslandPx(x,dpi),IslandPx(y,dpi));SendMessageW(hwnd,WM_LBUTTONDOWN,0,at);SendMessageW(hwnd,WM_LBUTTONUP,0,at);}
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
    // Settings immediately precedes Close.
    PostMessageW(menu,WM_KEYDOWN,VK_END,1);
    PostMessageW(menu,WM_KEYDOWN,VK_UP,1);
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
void CheckAnimationSettings(){
    using namespace platform::windows;
    Files files;shell::DynamicIslandSessionOptions options;options.preferencesPath="animation.json";
    const auto delay=[](int ms){return ui::ParsePageCommand(L"v1|dynamicIsland|hoverDelayMs|"+std::to_wstring(ms));};
    {
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr),options,&files);
        Check(session.Restore()&&State(session)["hoverDelayMs"]==300&&State(session)["animationSpeed"]=="normal"&&State(session)["bounce"]=="normal",
            "default hover delay aligns with the 50 ms slider grid");
        for(const auto command:{delay(100),delay(320),delay(1000),PageCommand::IslandAnimationFast,PageCommand::IslandAnimationNormal,
            PageCommand::IslandAnimationSlow,PageCommand::IslandBounceSubtle,PageCommand::IslandBounceNormal,PageCommand::IslandBounceBouncy}){
            const auto reply=session.HandlePage(command);
            Check(reply&&nlohmann::json::parse(*reply)["saved"]==true,"animation commands confirm a successful native save");
        }
        const auto saved=files.values["animation-display.json"];
        Check(!session.Running()&&State(session)["hoverDelayMs"]==1000&&State(session)["animationSpeed"]=="slow"&&State(session)["bounce"]=="bouncy",
            "animation preferences can be saved without enabling the island");
        files.fail=true;
        for(const auto command:{delay(100),PageCommand::IslandAnimationFast,PageCommand::IslandBounceSubtle}){
            session.HandlePage(command);
            Check(State(session)["hoverDelayMs"]==1000&&State(session)["animationSpeed"]=="slow"&&State(session)["bounce"]=="bouncy"&&
                State(session)["error"]=="save"&&files.values["animation-display.json"]==saved,"failed animation saves preserve all confirmed values and the file");
        }
        files.fail=false;session.HandlePage(PageCommand::IslandEnable);session.HandlePage(PageCommand::IslandAutoHideOff);
        const auto hwnd=session.Handle();const int dpi=static_cast<int>(GetDpiForWindow(hwnd));
        Click(hwnd,32,10);Check(State(session)["expanded"]==true,"the hover delay does not delay a real click");
        session.HandlePage(PageCommand::IslandCollapse);session.HandlePage(PageCommand::IslandHoverOn);
        const auto move=[&]{SendMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM(MulDiv(32,dpi,96),MulDiv(10,dpi,96)));};
        move();Pump(160,hwnd);Check(!State(session)["expanded"].get<bool>(),"one second hover delay does not expand early");
        session.HandlePage(delay(100));Pump(170,hwnd);
        Check(State(session)["expanded"]==true,"changing the delay while hovered reschedules the real native timer");
        session.HandlePage(PageCommand::IslandCollapse);SendMessageW(hwnd,WM_MOUSELEAVE,0,0);
        session.HandlePage(delay(1000));move();Pump(820,hwnd);
        Check(State(session)["expanded"]==false,"maximum hover delay still waits near the end of the interval");
        Pump(250,hwnd);Check(State(session)["expanded"]==true,"one second hover delay eventually expands");
        session.HandlePage(PageCommand::IslandCollapse);SendMessageW(hwnd,WM_MOUSELEAVE,0,0);
        session.HandlePage(delay(100));move();SendMessageW(hwnd,WM_MOUSELEAVE,0,0);Pump(170);
        Check(State(session)["expanded"]==false,"leaving cancels the minimum-delay hover timer");
        session.HandlePage(delay(1000));session.HandlePage(PageCommand::IslandResetAppearance);
        Check(State(session)["hoverDelayMs"]==1000&&State(session)["animationSpeed"]=="slow"&&State(session)["bounce"]=="bouncy",
            "appearance reset preserves all animation choices");
        session.Shutdown();
    }
    {
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr),options,&files);
        Check(session.Restore()&&State(session)["hoverDelayMs"]==1000&&State(session)["animationSpeed"]=="slow"&&State(session)["bounce"]=="bouncy",
            "animation preferences survive a native session restart");
        session.HandlePage(PageCommand::IslandReset);
        Check(State(session)["hoverDelayMs"]==300&&State(session)["animationSpeed"]=="normal"&&State(session)["bounce"]=="normal",
            "full reset restores the animation defaults");session.Shutdown();
    }
    for(const auto& bad:{nlohmann::json(-1),nlohmann::json(99),nlohmann::json(1001),nlohmann::json(320.5),nlohmann::json("320"),nlohmann::json(true),nlohmann::json(nullptr)}){
        Files invalid;invalid.values["animation-display.json"]=nlohmann::json{{"version",1},{"hoverDelayMs",bad},{"animationSpeed",42},{"bounce","invalid"}}.dump();
        const auto before=invalid.values;
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr),options,&invalid);
        Check(session.Restore()&&State(session)["hoverDelayMs"]==300&&State(session)["animationSpeed"]=="normal"&&State(session)["bounce"]=="normal"&&invalid.values==before,
            "invalid animation fields fall back safely without rewriting preferences");session.Shutdown();
    }
    for(const auto& sample:{std::pair<unsigned,unsigned>{320,300},{440,450},{125,150},{1000,1000}}){
        Files legacy;legacy.values["animation-display.json"]=nlohmann::json{{"version",1},{"hoverDelayMs",sample.first}}.dump();
        const auto original=legacy.values;
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr),options,&legacy);
        Check(session.Restore()&&State(session)["hoverDelayMs"]==sample.second&&legacy.values==original,
            "legacy delay is rounded to the nearest 50 ms without rewriting on load");
        session.HandlePage(delay(440));
        Check(State(session)["hoverDelayMs"]==450&&nlohmann::json::parse(legacy.values["animation-display.json"])["hoverDelayMs"]==450,
            "older protocol values also save on the new delay grid");session.Shutdown();
    }
    BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    IslandWindow window(GetModuleHandleW(nullptr),{});widget::Preferences prefs;prefs.enabled=true;
    IslandDisplay display;display.autoHide=false;display.hideFullscreen=false;
    Check(window.Start(prefs,display),"animation measurement window starts");window.Settle();
    if(animations){
        const auto measure=[&](IslandAnimationSpeed speed,bool expanded){
            display.animationSpeed=speed;Check(window.SetDisplay(display),"animation speed applies to the real HWND");
            window.Expand(!expanded);window.Settle();window.Expand(expanded);Pump(140,window.Handle());
            SendMessageW(window.Handle(),WM_APP+0x6e4,0,0);RECT rect{};GetClientRect(window.Handle(),&rect);window.Settle();return rect.bottom;
        };
        const int slowOpen=measure(IslandAnimationSpeed::Slow,true),fastOpen=measure(IslandAnimationSpeed::Fast,true);
        const int slowClose=measure(IslandAnimationSpeed::Slow,false),fastClose=measure(IslandAnimationSpeed::Fast,false);
        std::cout<<"native animation heights: expand "<<slowOpen<<" / "<<fastOpen<<", collapse "<<slowClose<<" / "<<fastClose<<'\n';
        Check(fastOpen>slowOpen+20&&slowClose>fastClose+20,"speed changes both real expand and collapse trajectories");
        const auto peak=[&](IslandBounce bounce){
            display.animationSpeed=IslandAnimationSpeed::Fast;display.bounce=bounce;window.SetDisplay(display);
            window.Expand(false);window.Settle();window.Expand(true);int peak=0;
            for(int i=0;i<24;++i){Pump(16,window.Handle());RECT rect{};GetClientRect(window.Handle(),&rect);peak=std::max(peak,static_cast<int>(rect.bottom));}
            window.Settle();return peak;
        };
        const auto subtle=peak(IslandBounce::Subtle),bouncy=peak(IslandBounce::Bouncy);
        std::cout<<"native bounce peaks: "<<subtle<<" / "<<bouncy<<'\n';
        Check(bouncy>subtle+10,"bounce amount changes the actual native overshoot");
        for(const auto speed:{IslandAnimationSpeed::Slow,IslandAnimationSpeed::Normal,IslandAnimationSpeed::Fast}){
            display.animationSpeed=speed;display.bounce=IslandBounce::Normal;window.ReleasePreview();
            window.Preview(display,false);IslandPreviewFrame frame;
            for(int i=0;i<7;++i)frame=window.Preview(display,true,.02f);
            const auto opening=frame.height;
            window.Preview(display,true);for(int i=0;i<7;++i)frame=window.Preview(display,false,.02f);
            std::cout<<"preview speed "<<static_cast<int>(speed)<<": "<<opening<<" / "<<frame.height<<'\n';
            Check(speed!=IslandAnimationSpeed::Slow||opening<PreviewPx(100)&&frame.height>PreviewPx(90),"slow preview changes both directions gradually");
            Check(speed!=IslandAnimationSpeed::Fast||opening>PreviewPx(135)&&frame.height<PreviewPx(55),"fast preview changes both directions rapidly");
        }
        for(const auto speed:{IslandAnimationSpeed::Slow,IslandAnimationSpeed::Normal,IslandAnimationSpeed::Fast})
          for(const auto bounce:{IslandBounce::Subtle,IslandBounce::Normal,IslandBounce::Bouncy}){
            display.animationSpeed=speed;display.bounce=bounce;window.ReleasePreview();window.Preview(display,false);
            unsigned previewPeak=0,count=0;IslandPreviewFrame frame;
            do{frame=window.Preview(display,true,.02f);previewPeak=std::max(previewPeak,frame.height);++count;}while(frame.animating&&count<200);
            Check(!frame.animating&&frame.height==PreviewPx(156)&&previewPeak<260,"every speed and bounce pair settles within the drawing bounds");
            if(speed==IslandAnimationSpeed::Normal){
                Check(bounce!=IslandBounce::Subtle||previewPeak<=PreviewPx(158),"subtle preview has minimal overshoot");
                Check(bounce!=IslandBounce::Bouncy||previewPeak>PreviewPx(170),"bouncy preview visibly overshoots");
            }
          }
        window.ReleasePreview();
    }
    window.Stop();
}
void CheckExpandedWidgets(){
    using namespace platform::windows;
    using W=dynamic_island::ExpandedWidget;
    unsigned valid=0,legacyValid=0,clockValid=0,fiveValid=0,countdownValid=0,countdownFiveValid=0;
    for(unsigned code=0;code<=dynamic_island::ExpandedLayoutMaximum;++code){
        std::vector<W> values{W::Music};
        const bool accepted=dynamic_island::DecodeExpandedLayout(code,values);
        const auto command=ui::ParsePageCommand(L"v1|dynamicIsland|expandedLayout|"+std::to_wstring(code));
        Check(accepted==(command!=PageCommand::Invalid),"wire and model agree for every bounded layout");
        if(accepted){
            ++valid;Check(values.size()<=5&&dynamic_island::EncodeExpandedLayout(values)==code,"layout round trips without dropping order");
            if(code<=dynamic_island::ExpandedLayoutLegacyMaximum){
                ++legacyValid;
                unsigned previous=0;
                for(const auto value:values)previous=previous*6+static_cast<unsigned>(value)+1;
                Check(previous==code&&!dynamic_island::HasExpandedWidget(values,W::Clock),"every previous layout keeps its original base-6 meaning");
            }else if(code<=dynamic_island::ExpandedLayoutClockMaximum){
                ++clockValid;
                Check(dynamic_island::HasExpandedWidget(values,W::Clock),"only layouts containing clock use the extension range");
            }else if(code<dynamic_island::ExpandedLayoutCountdownOffset){
                ++fiveValid;
                Check(values.size()==5,"the new permutation range always decodes five distinct widgets");
            }else if(code<dynamic_island::ExpandedLayoutCountdownFiveOffset){
                ++countdownValid;
                Check(dynamic_island::HasExpandedWidget(values,W::Countdown),"new layouts in the base-8 range contain countdown");
            }else{
                ++countdownFiveValid;
                Check(values.size()==5&&dynamic_island::HasExpandedWidget(values,W::Countdown),
                      "new five-widget permutations contain countdown");
            }
        }
        else Check(values==std::vector<W>{W::Music},"bad layout leaves the previous selection untouched");
    }
    Check(valid==3620&&legacyValid==206&&clockValid==311&&fiveValid==720&&countdownValid==583&&countdownFiveValid==1800,
          "old layouts remain unchanged and all countdown layout encodings are canonical");
    std::vector<W> original;
    Check(dynamic_island::DecodeExpandedLayout(53,original)&&original==std::vector<W>{W::Music,W::CalendarWeather,W::Todos},
          "legacy music-calendar-todos layout 53 is unchanged");
    Check(dynamic_island::EncodeExpandedLayout({W::Clock})==1302&&
          dynamic_island::EncodeExpandedLayout({W::Music,W::Clock})==1309,
          "clock-only and mixed clock layouts have the shared versioned encoding");
    Check(dynamic_island::EncodeExpandedLayout({W::Countdown})==4424&&
          dynamic_island::EncodeExpandedLayout({W::Countdown,W::Music})==4474&&
          dynamic_island::EncodeExpandedLayout({W::Countdown,W::Clock,W::Todos,W::System,W::Focus})==dynamic_island::ExpandedLayoutMaximum,
          "countdown layouts use a new range while retaining the five-widget limit");
    Check(ui::ParsePageCommand(L"v1|dynamicIsland|countdownDate|2932896")==PageCommand::IslandCountdownDateLast&&
          ui::ParsePageCommand(L"v1|dynamicIsland|countdownDate|2932897")==PageCommand::Invalid,
          "countdown target dates are bounded by the supported calendar range");
    for(const auto& invalid:std::vector<std::vector<W>>{{W::Clock,W::Clock},{W::Music,W::CalendarWeather,W::Focus,W::System,W::Todos,W::Clock},
          {static_cast<W>(7)},{static_cast<W>(-1)}})
        Check(dynamic_island::EncodeExpandedLayout(invalid)>dynamic_island::ExpandedLayoutMaximum,"invalid widget selections cannot produce a valid code");
    for(const auto* value:{L"01",L"1296",L"1297",L"1303",L"1344",L"4417",L"-1",L"1.0",L"6|extra"})
        Check(ui::ParsePageCommand(std::wstring(L"v1|dynamicIsland|expandedLayout|")+value)==PageCommand::Invalid,"noncanonical layouts are rejected");
    Files files;std::uint64_t now=10000;
    std::vector<IslandTodo> tasks{{"today-one",L"完成新展开态",L"10:00"},{"today-two",L"检查交互细节",L"11:00"}};
    shell::DynamicIslandSessionOptions options;options.preferencesPath="expanded-widgets.json";
    options.monotonicMillis=[&]{return now;};
    options.todos.forDay=[&](calendar::Date){return tasks;};options.todos.writable=[]{return true;};
    const unsigned layout=dynamic_island::EncodeExpandedLayout({W::Focus,W::Todos,W::CalendarWeather,W::System});
    // Focus is hidden during the redesign: saved layouts project
    // to their released members, and hidden commands are refused.
    const unsigned projected=dynamic_island::EncodeExpandedLayout({W::Todos,W::CalendarWeather,W::System});
    const unsigned changedLayout=dynamic_island::EncodeExpandedLayout({W::CalendarWeather,W::Music});
    const unsigned hiddenLayout=dynamic_island::EncodeExpandedLayout({W::Music,W::Focus,W::System});
    const auto choose=[&](unsigned code){return ui::ParsePageCommand(L"v1|dynamicIsland|expandedLayout|"+std::to_wstring(code));};
    for(const bool left:{true,false}){
        Files retiredFiles;auto retiredOptions=options;
        const auto stem=left?"retired-left":"retired-right";
        retiredOptions.preferencesPath=std::string(stem)+".json";
        const auto path=std::string(stem)+"-display.json";
        const auto legacy=nlohmann::json{{"version",1},{"expandedLayout",layout},{"size","large"},{"shape","standard"},
            {"leftWidget",left?"files":"clock"},{"rightWidget",left?"temperatureRange":"files"}}.dump();
        retiredFiles.values[path]=legacy;
        shell::DynamicIslandSession retired(GetModuleHandleW(nullptr),retiredOptions,&retiredFiles);
        Check(retired.Restore(),"restore legacy file-tray settings");
        const auto state=State(retired);
        Check(state["leftWidget"]==(left?0:5)&&state["rightWidget"]==(left?20:0)&&
              state["expandedLayout"]==projected&&state["size"]=="large"&&state["shape"]=="standard",
              "removed file tray becomes None while other widget IDs and appearance survive");
        Check(retiredFiles.values[path]==legacy,"loading the retired selection does not rewrite the settings file");
        const auto command=static_cast<PageCommand>(static_cast<int>(left?PageCommand::IslandLeftWidgetFirst:PageCommand::IslandRightWidgetFirst)+18);
        const auto applied=nlohmann::json::parse(*retired.HandlePage(command));
        const auto saved=nlohmann::json::parse(retiredFiles.values[path]);
        Check(applied["saved"]&&applied[left?"leftWidget":"rightWidget"]==0&&
              saved[left?"leftWidget":"rightWidget"]=="none"&&saved[left?"rightWidget":"leftWidget"]==(left?"temperatureRange":"clock"),
              "legacy file-tray commands save None without shifting or clearing the other side");
        retired.Shutdown();
    }
    files.values["expanded-widgets-display.json"]=nlohmann::json{{"version",1},{"expandedLayout",layout},{"leftWidget","temperatureRange"},{"rightWidget","lunar"}}.dump();
    {
        const auto legacy=files.values["expanded-widgets-display.json"];
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr),options,&files);
        Check(session.Restore()&&State(session)["expandedLayout"]==projected&&State(session)["leftWidget"]==20&&State(session)["rightWidget"]==1,
              "all previously saved compact and expanded widget choices are restored");
        Check(files.values["expanded-widgets-display.json"]==legacy,"loading restored widget choices does not rewrite preferences");
        files.fail=true;session.HandlePage(choose(changedLayout));
        Check(State(session)["expandedLayout"]==projected&&State(session)["error"]=="save"&&files.values["expanded-widgets-display.json"]==legacy,
              "failed expanded-widget save preserves every confirmed selection");
        files.fail=false;session.HandlePage(PageCommand::IslandEnable);session.HandlePage(PageCommand::IslandAutoHideOff);
        const auto changed=nlohmann::json::parse(*session.HandlePage(choose(changedLayout)));
        Check(changed["saved"]&&State(session)["expandedLayout"]==changedLayout,
              "the native session accepts and saves an ordered calendar and music layout");
        const auto refused=nlohmann::json::parse(*session.HandlePage(choose(hiddenLayout)));
        Check(!refused["saved"]&&State(session)["expandedLayout"]==changedLayout&&
              nlohmann::json::parse(files.values["expanded-widgets-display.json"])["expandedLayout"]==changedLayout,
              "layouts with widgets under redesign are refused without changing the saved layout");
        const int rightCommand=static_cast<int>(PageCommand::IslandRightWidgetFirst)+5;
        const auto compact=nlohmann::json::parse(*session.HandlePage(static_cast<PageCommand>(rightCommand)));
        Check(compact["saved"]&&State(session)["leftWidget"]==20&&State(session)["rightWidget"]==5,
              "the native session applies a compact-widget choice to its selected side");
        session.HandlePage(PageCommand::IslandExpand);Pump(1000);
        const auto hwnd=session.Handle();RECT rect{};GetClientRect(hwnd,&rect);const int dpi=static_cast<int>(GetDpiForWindow(hwnd));
        Check(rect.right>IslandPx(372,dpi)&&rect.bottom>IslandPx(156,dpi),
              "restored multi-widget layout expands the native workspace beyond the single music view");
        session.HandlePage(PageCommand::IslandFocus);
        Check(!State(session)["focusing"].get<bool>(),"the hidden focus widget cannot start its timer");
        session.HandlePage(PageCommand::IslandResetAppearance);
        const auto persisted=nlohmann::json::parse(files.values["expanded-widgets-display.json"]);
        Check(persisted["expandedLayout"]==changedLayout&&persisted["leftWidget"]=="temperatureRange"&&persisted["rightWidget"]=="clock",
              "ordinary settings preserve the restored widget selections");
        const auto preview=nlohmann::json::parse(*session.HandlePage(PageCommand::IslandPreviewExpanded));
        const auto previewImage=preview["image"].get<std::string>();
        Check(preview["expandedLayout"]==changedLayout&&preview["leftWidget"]==20&&preview["rightWidget"]==5&&
              preview["error"]==""&&previewImage.rfind("data:image/jpeg;base64,",0)==0,
              "native preview renders the saved expanded and compact widget selections");
        session.HandlePage(choose(0));Check(State(session)["expandedLayout"]==0,"music can explicitly be disabled");
        Check(!State(session)["focusing"].get<bool>(),"removing the focus widget prevents starting its timer");
        session.HandlePage(choose(1));Check(State(session)["expandedLayout"]==1,"music can be enabled again");
        session.Shutdown();
    }
    {
        shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),options,&files);
        Check(restored.Restore()&&State(restored)["expandedLayout"]==1&&State(restored)["leftWidget"]==20&&State(restored)["rightWidget"]==5,
              "expanded and compact widget selections survive restart");
        restored.HandlePage(PageCommand::IslandReset);
        Check(State(restored)["expandedLayout"]==1&&!State(restored)["focusing"].get<bool>(),"full reset restores the default music layout");
        restored.Shutdown();
    }
    {
        Files clockFiles;auto clockOptions=options;clockOptions.preferencesPath="clock-layout.json";
        const auto path="clock-layout-display.json";
        const unsigned clockLayout=dynamic_island::EncodeExpandedLayout({W::Clock,W::CalendarWeather,W::Todos,W::Music});
        const auto old=nlohmann::json{{"version",1},{"expandedLayout",53},{"leftWidget","temperatureRange"},{"rightWidget","lunar"}}.dump();
        clockFiles.values[path]=old;
        {
            shell::DynamicIslandSession session(GetModuleHandleW(nullptr),clockOptions,&clockFiles);
            Check(session.Restore()&&State(session)["expandedLayout"]==53,"adding clock does not migrate a user's existing layout");
            clockFiles.fail=true;
            const auto failed=nlohmann::json::parse(*session.HandlePage(choose(clockLayout)));
            Check(!failed["saved"]&&failed["error"]=="save"&&State(session)["expandedLayout"]==53&&clockFiles.values[path]==old,
                  "failed clock save leaves confirmed and persisted legacy selections intact");
            clockFiles.fail=false;
            const auto saved=nlohmann::json::parse(*session.HandlePage(choose(clockLayout)));
            Check(saved["saved"]&&State(session)["expandedLayout"]==clockLayout&&
                  nlohmann::json::parse(clockFiles.values[path])["expandedLayout"]==clockLayout,
                  "the native session accepts and persists all four released widgets with clock first");
            const auto preview=nlohmann::json::parse(*session.HandlePage(PageCommand::IslandPreviewExpanded));
            Check(preview["expandedLayout"]==clockLayout&&preview["error"]==""&&
                  preview["image"].get<std::string>().rfind("data:image/jpeg;base64,",0)==0,
                  "the native preview carries the saved clock selection");
            const auto before=clockFiles.values[path];
            const auto refused=nlohmann::json::parse(*session.HandlePage(choose(dynamic_island::EncodeExpandedLayout({W::Clock,W::Focus}))));
            Check(!refused["saved"]&&State(session)["expandedLayout"]==clockLayout&&clockFiles.values[path]==before,
                  "clock does not release hidden widgets or overwrite a confirmed layout");
            session.Shutdown();
        }
        {
            shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),clockOptions,&clockFiles);
            Check(restored.Restore()&&State(restored)["expandedLayout"]==clockLayout&&
                  State(restored)["leftWidget"]==20&&State(restored)["rightWidget"]==1,
                  "clock order and compact widgets survive a full session restart");
            restored.HandlePage(choose(dynamic_island::EncodeExpandedLayout({W::Clock})));
            Check(State(restored)["expandedLayout"]==1302,"clock can be the only expanded widget");
            restored.HandlePage(PageCommand::IslandReset);
            Check(State(restored)["expandedLayout"]==1,"full reset returns a clock layout to the existing music default");
            restored.Shutdown();
        }
        for(const auto invalid:{1296,1297,1303,1344,4417}){
            const auto stale=nlohmann::json{{"version",1},{"expandedLayout",invalid},{"leftWidget","lunar"}}.dump();
            clockFiles.values[path]=stale;
            shell::DynamicIslandSession staleSession(GetModuleHandleW(nullptr),clockOptions,&clockFiles);
            Check(staleSession.Restore()&&State(staleSession)["expandedLayout"]==1&&State(staleSession)["leftWidget"]==1&&clockFiles.values[path]==stale,
                  "malformed clock settings fall back without rewriting unrelated user preferences");
            staleSession.Shutdown();
        }
    }
    {
        Files performanceFiles;auto performanceOptions=options;performanceOptions.preferencesPath="performance-layout.json";
        const auto path="performance-layout-display.json";
        const auto old=nlohmann::json{{"version",1},{"expandedLayout",53},{"leftWidget","temperatureRange"},{"rightWidget","lunar"}}.dump();
        performanceFiles.values[path]=old;
        const auto five=dynamic_island::EncodeExpandedLayout({W::System,W::Clock,W::CalendarWeather,W::Todos,W::Music});
        const auto hiddenFive=dynamic_island::EncodeExpandedLayout({W::System,W::Clock,W::CalendarWeather,W::Todos,W::Focus});
        Check(five>=dynamic_island::ExpandedLayoutFiveOffset&&choose(five)!=PageCommand::Invalid,
              "five released widgets fit the existing four-digit page protocol");
        {
            shell::DynamicIslandSession session(GetModuleHandleW(nullptr),performanceOptions,&performanceFiles);
            Check(session.Restore()&&State(session)["expandedLayout"]==53,"performance remains opt-in for existing layouts");
            performanceFiles.fail=true;
            const auto failed=nlohmann::json::parse(*session.HandlePage(choose(five)));
            Check(!failed["saved"]&&failed["error"]=="save"&&State(session)["expandedLayout"]==53&&performanceFiles.values[path]==old,
                  "failed five-widget save preserves the confirmed legacy layout and exact file");
            performanceFiles.fail=false;
            const auto saved=nlohmann::json::parse(*session.HandlePage(choose(five)));
            Check(saved["saved"]&&State(session)["expandedLayout"]==five&&
                  nlohmann::json::parse(performanceFiles.values[path])["expandedLayout"]==five,
                  "performance and all five released widgets persist in their chosen order");
            const auto confirmed=performanceFiles.values[path];
            const auto refused=nlohmann::json::parse(*session.HandlePage(choose(hiddenFive)));
            Check(!refused["saved"]&&State(session)["expandedLayout"]==five&&performanceFiles.values[path]==confirmed,
                  "a five-widget code containing hidden focus is refused without changing preferences");
            const auto preview=nlohmann::json::parse(*session.HandlePage(PageCommand::IslandPreviewExpanded));
            Check(preview["expandedLayout"]==five&&preview["error"]==""&&
                  preview["image"].get<std::string>().rfind("data:image/jpeg;base64,",0)==0,
                  "the native session renders the five-widget performance selection");
            session.Shutdown();
        }
        {
            shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),performanceOptions,&performanceFiles);
            Check(restored.Restore()&&State(restored)["expandedLayout"]==five&&
                  State(restored)["leftWidget"]==20&&State(restored)["rightWidget"]==1,
                  "all five widget choices and compact readings survive restart");
            const auto performanceOnly=dynamic_island::EncodeExpandedLayout({W::System});
            const auto alone=nlohmann::json::parse(*restored.HandlePage(choose(performanceOnly)));
            Check(alone["saved"]&&State(restored)["expandedLayout"]==performanceOnly,
                  "performance can be the only expanded widget");
            restored.HandlePage(PageCommand::IslandReset);
            Check(State(restored)["expandedLayout"]==1,"full reset keeps the existing music default");
            restored.Shutdown();
        }
    }
    {
        Check(IslandDisplay{}.clockStyle==IslandClockStyle::Simple,"existing clock appearance remains the default");
        const auto simple=ui::ParsePageCommand(L"v1|dynamicIsland|clockStyle|simple");
        const auto progress=ui::ParsePageCommand(L"v1|dynamicIsland|clockStyle|progress");
        const auto digital=ui::ParsePageCommand(L"v1|dynamicIsland|clockStyle|digital");
        const auto date=ui::ParsePageCommand(L"v1|dynamicIsland|clockStyle|date");
        const auto flip=ui::ParsePageCommand(L"v1|dynamicIsland|clockStyle|flip");
        const auto pixel=ui::ParsePageCommand(L"v1|dynamicIsland|clockStyle|pixel");
        Check(simple==PageCommand::IslandClockSimple&&progress==PageCommand::IslandClockProgress&&
              digital==PageCommand::IslandClockDigital&&date==PageCommand::IslandClockDate&&
              flip==PageCommand::IslandClockFlip&&pixel==PageCommand::IslandClockPixel,
              "all six clock styles have canonical native page commands");
        for(const auto* invalid:{L"",L"Progress",L"Digital",L"DATE",L"unknown",L"1",L"progress|extra",L"digital|extra",L"date|extra",L"simple ",L"date "})
            Check(ui::ParsePageCommand(std::wstring(L"v1|dynamicIsland|clockStyle|")+invalid)==PageCommand::Invalid,
                  "malformed or unsupported clock styles cannot enter the native session");
        Files styleFiles;auto styleOptions=options;styleOptions.preferencesPath="clock-style.json";
        const auto path="clock-style-display.json";
        const unsigned clockLayout=dynamic_island::EncodeExpandedLayout({W::Music,W::Clock,W::CalendarWeather,W::Todos,W::System});
        const auto legacy=nlohmann::json{{"version",1},{"expandedLayout",clockLayout},{"screen",1},
            {"leftWidget","temperatureRange"},{"rightWidget","lunar"},{"size","large"},{"shape","standard"},
            {"material","none"},{"topmost",true},{"hideFullscreen",false},{"autoHide",false},{"hoverExpand",true},
            {"hoverDelayMs",700},{"animationSpeed","fast"},{"bounce","subtle"}};
        styleFiles.values[path]=legacy.dump();
        {
            shell::DynamicIslandSession session(GetModuleHandleW(nullptr),styleOptions,&styleFiles);
            Check(session.Restore()&&State(session)["clockStyle"]=="simple"&&styleFiles.values[path]==legacy.dump(),
                  "legacy settings restore the original clock without an implicit file rewrite");
            session.HandlePage(PageCommand::IslandEnable);const auto hwnd=session.Handle();
            styleFiles.fail=true;
            const auto failed=nlohmann::json::parse(*session.HandlePage(progress));
            Check(!failed["saved"]&&failed["error"]=="save"&&State(session)["clockStyle"]=="simple"&&
                  styleFiles.values[path]==legacy.dump()&&session.Handle()==hwnd,
                  "failed clock-style save rolls back the live session and keeps the exact old preference file");
            styleFiles.fail=false;
            const auto saved=nlohmann::json::parse(*session.HandlePage(progress));
            const auto persisted=nlohmann::json::parse(styleFiles.values[path]);
            Check(saved["saved"]&&State(session)["clockStyle"]=="progress"&&persisted["clockStyle"]=="progress"&&session.Handle()==hwnd,
                  "progress clock style is applied and saved without replacing the running island");
            bool preserved=true;for(const auto& item:legacy.items())preserved=preserved&&persisted[item.key()]==item.value();
            Check(preserved,"changing clock style preserves layout, compact widgets, display, animation and hover settings");
            const auto preview=nlohmann::json::parse(*session.HandlePage(PageCommand::IslandPreviewExpanded));
            Check(preview["clockStyle"]=="progress"&&preview["expandedLayout"]==clockLayout&&preview["error"]=="",
                  "internal native rendering uses the selected style without changing the widget layout");
            session.HandlePage(PageCommand::IslandPreviewStop);
            session.HandlePage(PageCommand::IslandResetAppearance);
            Check(State(session)["clockStyle"]=="progress"&&State(session)["expandedLayout"]==clockLayout,
                  "appearance reset preserves the independent clock style and enabled widgets");
            session.Shutdown();
        }
        {
            shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),styleOptions,&styleFiles);
            Check(restored.Restore()&&State(restored)["clockStyle"]=="progress"&&State(restored)["expandedLayout"]==clockLayout,
                  "the chosen clock style survives a complete session restart");
            const auto saved=nlohmann::json::parse(*restored.HandlePage(simple));
            Check(saved["saved"]&&State(restored)["clockStyle"]=="simple"&&
                  nlohmann::json::parse(styleFiles.values[path])["clockStyle"]=="simple",
                  "the original clock style can be selected and persisted again");
            restored.HandlePage(progress);restored.HandlePage(PageCommand::IslandReset);
            Check(State(restored)["clockStyle"]=="simple"&&State(restored)["expandedLayout"]==1,
                  "full reset restores the original clock style and default music layout");
            restored.Shutdown();
        }
        for(const auto command:{digital,date,flip,pixel}){
            const std::string name=command==digital?"digital":command==date?"date":command==flip?"flip":"pixel";
            auto previous=legacy;previous["clockStyle"]="progress";const auto previousFile=previous.dump();
            styleFiles.values[path]=previousFile;
            {
                shell::DynamicIslandSession session(GetModuleHandleW(nullptr),styleOptions,&styleFiles);
                Check(session.Restore()&&State(session)["clockStyle"]=="progress",
                      "new clock styles retain an existing progress selection before a deliberate change");
                styleFiles.fail=true;
                const auto failed=nlohmann::json::parse(*session.HandlePage(command));
                Check(!failed["saved"]&&failed["error"]=="save"&&State(session)["clockStyle"]=="progress"&&styleFiles.values[path]==previousFile,
                      "failed digital or date style save preserves the confirmed style and preference file");
                styleFiles.fail=false;
                const auto saved=nlohmann::json::parse(*session.HandlePage(command));
                const auto persisted=nlohmann::json::parse(styleFiles.values[path]);
                Check(saved["saved"]&&State(session)["clockStyle"]==name&&persisted["clockStyle"]==name,
                      "digital and date styles are independently applied and persisted");
                bool preserved=true;for(const auto& item:legacy.items())preserved=preserved&&persisted[item.key()]==item.value();
                Check(preserved,"new clock styles preserve the same five widget IDs, order and unrelated display preferences");
                session.Shutdown();
            }
            {
                const auto confirmed=styleFiles.values[path];
                shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),styleOptions,&styleFiles);
                Check(restored.Restore()&&State(restored)["clockStyle"]==name&&State(restored)["expandedLayout"]==clockLayout&&styleFiles.values[path]==confirmed,
                      "digital and date styles survive restart without rewriting the preference file");
                restored.HandlePage(PageCommand::IslandResetAppearance);
                Check(State(restored)["clockStyle"]==name&&State(restored)["expandedLayout"]==clockLayout,
                      "appearance reset keeps the selected digital or date style and five-component layout");
                restored.HandlePage(PageCommand::IslandReset);
                Check(State(restored)["clockStyle"]=="simple"&&State(restored)["expandedLayout"]==1,
                      "full reset restores the original defaults from either new clock style");
                restored.Shutdown();
            }
        }
        for(const auto& invalid:nlohmann::json::array({"unknown","Progress","Digital","DATE","digital ","date|extra",7,true,nullptr,nlohmann::json::array(),nlohmann::json::object()})){
            auto malformed=legacy;malformed["clockStyle"]=invalid;const auto malformedFile=malformed.dump();styleFiles.values[path]=malformedFile;
            shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),styleOptions,&styleFiles);
            Check(restored.Restore()&&State(restored)["clockStyle"]=="simple"&&State(restored)["expandedLayout"]==clockLayout&&
                  State(restored)["leftWidget"]==20&&styleFiles.values[path]==malformedFile,
                  "invalid clock style falls back safely while preserving the old file and other widget preferences");
            restored.Shutdown();
        }
    }
    widget::Preferences prefs;prefs.enabled=true;IslandDisplay display;display.autoHide=false;
    IslandWindow window(GetModuleHandleW(nullptr),{});Check(window.Start(prefs,display),"native workspace render starts");
    window.Update({{},L"10:08",L"",0,false,false,false});
    window.SetTodoSource(options.todos);
    dynamic_island::Weather weather;weather.available=true;weather.temperature=18;weather.condition=L"多云";window.SetWeather(weather);
    display.expandedWidgets={W::Music,W::CalendarWeather,W::Focus,W::Todos};
    window.SetDisplay(display);window.Expand(true);window.Settle();SaveFrame(window.Frame(),L"expanded-workspace-zh.png");
    const auto before=window.Handle();const auto preview=window.Preview(display,true);
    IslandPreviewRenderer renderer;const auto scene=renderer.Render(preview,display,{});
    Check(scene.width==960&&scene.height==300,"four widget preview fits the complete native scene");
    SaveFrame(scene,L"expanded-workspace-preview.png");
    window.ReleasePreview();
    const auto releasedCompact=window.Preview(IslandDisplay{},false);
    SaveFrame(renderer.Render(releasedCompact,IslandDisplay{},{}),L"release-compact-preview.png");
    window.ReleasePreview();
    const auto releasedMusic=window.Preview(IslandDisplay{},true);
    SaveFrame(renderer.Render(releasedMusic,IslandDisplay{},{}),L"release-music-preview.png");
    window.ReleasePreview();window.Preview(display,true);
    auto reordered=display;std::reverse(reordered.expandedWidgets.begin(),reordered.expandedWidgets.end());
    Check(window.Preview(reordered,true).pixels.pixels!=preview.pixels.pixels&&window.Handle()==before&&window.Expanded(),"preview follows order without changing the desktop window");
    IslandPreviewFrame shrinking;unsigned shrinkFrames=0;
    do{
        shrinking=window.Preview(IslandDisplay{},true,.02f);
        Check(shrinking.width>0&&shrinking.height>0&&shrinking.pixels.pixels.size()==static_cast<std::size_t>(shrinking.width)*shrinking.height,
            "every four-to-one preview frame stays inside the retained canvas");
        ++shrinkFrames;
    }while(shrinking.animating&&shrinkFrames<200);
    Check(!shrinking.animating&&shrinking.width==PreviewPx(372)&&shrinking.height==PreviewPx(156),"four widgets animate back to the standard music layout");
    auto large=display;large.size=IslandSize::Large;large.bounce=IslandBounce::Bouncy;
    window.ReleasePreview();window.Preview(large,false);
    unsigned largeFrames=0;IslandPreviewFrame largeFrame;
    do{
        largeFrame=window.Preview(large,true,.02f);
        Check(largeFrame.width<=960&&largeFrame.height+static_cast<unsigned>(std::ceil(largeFrame.topOffset))<=300,
            "large four-widget bounce stays within the complete preview scene");
        ++largeFrames;
    }while(largeFrame.animating&&largeFrames<200);
    Check(!largeFrame.animating&&largeFrames>1,"large four-widget bounce finishes without stopping on a render error");
    display.expandedWidgets={};window.SetDisplay(display);window.Settle();
    RECT rect{};GetClientRect(before,&rect);Check(window.Expanded()&&rect.bottom==IslandPx(156,static_cast<int>(GetDpiForWindow(before))),"empty layout remains expandable");
    SaveFrame(window.Frame(),L"expanded-workspace-empty.png");
    window.SetMonitor({L"narrow",{0,0,640,900},{0,0,640,900},true});
    display.expandedWidgets={W::Music,W::CalendarWeather,W::Focus,W::System};window.SetDisplay(display);window.Settle();
    GetClientRect(before,&rect);Check(rect.right<=640&&rect.bottom<IslandPx(177,static_cast<int>(GetDpiForWindow(before))),"a narrow monitor keeps the workspace on one fitted row");
    SaveFrame(window.Frame(),L"expanded-workspace-narrow.png");
    display.expandedWidgets={W::CalendarWeather};window.SetMonitor({L"wide",{0,0,1920,1080},{0,0,1920,1080},true});window.SetDisplay(display);window.Settle();
    GetClientRect(before,&rect);
    Check(rect.right>=IslandPx(336,static_cast<int>(GetDpiForWindow(before)))&&rect.bottom>=IslandPx(176,static_cast<int>(GetDpiForWindow(before))),"calendar module uses its own workspace size");
    SaveFrame(window.Frame(),L"expanded-calendar.png");
    // Frame retains the canvas allocation, including transparent high-water
    // pixels from earlier four-card layouts. Compare every pixel actually
    // presented by this HWND, independently of that allocation's stride.
    const auto calendarFrame=[&]{
        RECT client{};Check(GetClientRect(before,&client)!=FALSE,"read calendar presentation size");
        const auto frame=window.Frame();WidgetBitmap visible;
        visible.width=static_cast<unsigned>(client.right-client.left);visible.height=static_cast<unsigned>(client.bottom-client.top);
        Check(visible.width&&visible.height&&visible.width<=frame.width&&visible.height<=frame.height&&
              frame.pixels.size()==static_cast<std::size_t>(frame.width)*frame.height,"calendar presentation fits its backing canvas");
        visible.pixels.resize(static_cast<std::size_t>(visible.width)*visible.height);
        for(unsigned y=0;y<visible.height;++y)
            std::copy_n(frame.pixels.begin()+static_cast<std::size_t>(y)*frame.width,visible.width,
                        visible.pixels.begin()+static_cast<std::size_t>(y)*visible.width);
        return visible;
    };
    const auto calendarStart=calendarFrame();
    Click(before,308,26);Pump(500);
    const auto nextMonth=calendarFrame().pixels;
    Check(nextMonth!=calendarStart.pixels,"next-month control turns the calendar");
    SaveFrame(window.Frame(),L"expanded-calendar-next.png");
    Click(before,227,108);Pump(500);
    Check(calendarFrame().pixels!=nextMonth&&window.Expanded(),"choosing a day updates the calendar without collapsing");
    SaveFrame(window.Frame(),L"expanded-calendar-selected.png");
    window.Expand(false);window.Settle();window.Expand(true);window.Settle();
    const auto reopenedCalendar=calendarFrame();
    Check(reopenedCalendar.width==calendarStart.width&&reopenedCalendar.height==calendarStart.height&&
          reopenedCalendar.pixels==calendarStart.pixels,"reopening returns the same visible calendar dimensions and every today pixel");
    display.expandedWidgets={W::Music,W::CalendarWeather};window.SetDisplay(display);window.Settle();
    SaveFrame(window.Frame(),L"expanded-music-calendar.png");
    tasks.push_back({"today-three",L"晚上健身",L"19:30",true,false,0x6bd89b});tasks.front().color=0xffa348;
    display.expandedWidgets={W::Music,W::CalendarWeather,W::Todos};window.SetDisplay(display);window.Settle();
    GetClientRect(before,&rect);
    Check(rect.right>=IslandPx(18*2+232+300+232+18*2,static_cast<int>(GetDpiForWindow(before))),"music, calendar and to-dos share one row");
    SaveFrame(window.Frame(),L"expanded-music-calendar-todos.png");
    display.expandedWidgets={W::Todos};window.SetDisplay(display);window.Settle();
    SaveFrame(window.Frame(),L"expanded-todos.png");
    tasks.clear();window.Expand(false);window.Settle();window.Expand(true);window.Settle();
    SaveFrame(window.Frame(),L"expanded-todos-empty.png");
    const auto paired=window.Preview(display,true);
    SaveFrame(renderer.Render(paired,display,{}),L"expanded-music-calendar-preview.png");
    window.ReleasePreview();
    display.expandedWidgets={W::Countdown};
    display.countdownTitle="New Year 还有";
    SYSTEMTIME countdownToday{};GetLocalTime(&countdownToday);
    display.countdownDate=calendar::DaysSinceUnixEpoch({countdownToday.wYear,countdownToday.wMonth,countdownToday.wDay})+189;
    window.SetDisplay(display);window.Settle();
    GetClientRect(before,&rect);
    Check(rect.right*20>=rect.bottom*23,"countdown card keeps the reference landscape aspect ratio");
    SaveFrame(window.Frame(),L"expanded-countdown.png");
    const auto countdownPreview=window.Preview(display,true);
    const auto countdownScene=renderer.Render(countdownPreview,display,{});
    SaveFrame(countdownScene,L"expanded-countdown-preview.png");
    const auto referenceBluePixels=std::count_if(countdownScene.pixels.begin(),countdownScene.pixels.end(),[](std::uint32_t p){
        return (p&0x00ffffff)==0x2183c0;
    });
    Check(referenceBluePixels>500,"countdown default header matches the supplied blue in the native preview");
    window.ReleasePreview();
    for(const auto style:{IslandCountdownStyle::Violet}){
        display.countdownStyle=style;
        window.SetDisplay(display);window.Settle();
        const auto frame=window.Frame();
        const std::uint32_t background=0xf0ecf4;
        std::uint32_t left=frame.width,top=frame.height,right=0,bottom=0;bool found=false;
        for(std::uint32_t y=0;y<frame.height;++y)for(std::uint32_t x=0;x<frame.width;++x){
            if((frame.pixels[static_cast<std::size_t>(y)*frame.width+x]&0xffffff)!=background)continue;
            found=true;left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
        }
        const auto cardWidth=right-left+1,cardHeight=bottom-top+1;
        Check(found&&cardWidth*100>=cardHeight*210&&cardWidth*100<=cardHeight*235,
              "horizontal countdown style renders at the compact 20:9 ratio");
        const auto colored=std::count_if(frame.pixels.begin(),frame.pixels.end(),[background](std::uint32_t pixel){return (pixel&0xffffff)==background;});
        Check(colored>500,"each horizontal style draws its own native background");
        SaveFrame(frame,L"expanded-countdown-violet.png");
    }
    display.countdownStyle=IslandCountdownStyle::Standard;display.countdownTextColor=0xc00089;display.countdownBackgroundColor=0x388561;
    window.SetDisplay(display);window.Settle();
    const auto recolored=window.Frame();
    for(const auto color:{display.countdownTextColor,display.countdownBackgroundColor})
        Check(std::count_if(recolored.pixels.begin(),recolored.pixels.end(),[color](std::uint32_t pixel){return (pixel&0xffffff)==color;})>100,
            "saved countdown font and header colors reach the native card");
    SaveFrame(recolored,L"expanded-countdown-colors.png");
    display.countdownStyle=IslandCountdownStyle::Violet;display.countdownSolidColor=0xc00089;window.SetDisplay(display);window.Settle();
    SaveFrame(window.Frame(),L"expanded-countdown-solid-pink.png");
    window.Stop();
}
void CheckClockRendering(){
    using namespace platform::windows;
    using W=dynamic_island::ExpandedWidget;
    {
        SYSTEMTIME sample{};sample.wYear=2026;sample.wMonth=11;sample.wDay=9;sample.wDayOfWeek=1;
        sample.wHour=16;sample.wMinute=28;sample.wSecond=36;
        const auto reading=ReadIslandClockProgress(sample);
        Check(reading.weekday==1&&std::abs(reading.day-59316./86400.)<1e-12&&
              std::abs(reading.week-59316./604800.)<1e-12,
              "clock progress uses the supplied local time and a Monday-start week");
        auto midnight=sample;midnight.wHour=midnight.wMinute=midnight.wSecond=0;
        const auto monday=ReadIslandClockProgress(midnight);
        Check(monday.day==0&&monday.week==0&&monday.weekday==1,"Monday midnight resets both progress bars");
        midnight.wDay=10;midnight.wDayOfWeek=2;
        const auto tuesday=ReadIslandClockProgress(midnight);
        Check(tuesday.day==0&&std::abs(tuesday.week-1./7.)<1e-12&&tuesday.weekday==2,
              "ordinary midnight resets daily progress while retaining the completed weekday");
        auto sunday=sample;sunday.wDay=15;sunday.wDayOfWeek=0;sunday.wHour=23;sunday.wMinute=59;sunday.wSecond=59;
        const auto weekEnd=ReadIslandClockProgress(sunday);
        Check(weekEnd.weekday==7&&weekEnd.day>.999&&weekEnd.day<1&&weekEnd.week>.999&&weekEnd.week<1,
              "Sunday remains the seventh weekday and both bars stay bounded before rollover");
        auto nextMonday=midnight;nextMonday.wDay=16;nextMonday.wDayOfWeek=1;
        const auto reset=ReadIslandClockProgress(nextMonday);
        Check(reset.weekday==1&&reset.day==0&&reset.week==0,"Sunday-to-Monday transition starts a new week");

        WidgetCanvas canvas;
        const auto draw=[&](const SYSTEMTIME& now,IslandClockStyle style,bool english=false){
            WidgetBitmap frame;Check(canvas.Begin(200,144,192),"clock style comparison canvas starts at 2x");
            canvas.Rounded({0,0,200,144},0,0x000000);
            DrawIslandClockCard(canvas,{0,0,200,144},now,english,style);
            Check(canvas.Finish(frame)&&frame.width==400&&frame.height==288,
                  "clock style renders into the complete 200 by 144 DIP card");
            return frame;
        };
        const auto simple=draw(sample,IslandClockStyle::Simple);
        const auto progress=draw(sample,IslandClockStyle::Progress);
        SaveFrame(simple,L"style-clock-simple.png");SaveFrame(progress,L"style-clock-progress.png");
        SaveFrame(draw(sample,IslandClockStyle::Simple,true),L"style-clock-simple-en.png");
        SaveFrame(draw(sample,IslandClockStyle::Progress,true),L"style-clock-progress-en.png");
        const auto digital=draw(sample,IslandClockStyle::Digital),date=draw(sample,IslandClockStyle::Date);
        SaveFrame(digital,L"style-clock-digital.png");SaveFrame(date,L"style-clock-date.png");
        SaveFrame(draw(sample,IslandClockStyle::Digital,true),L"style-clock-digital-en.png");
        SaveFrame(draw(sample,IslandClockStyle::Date,true),L"style-clock-date-en.png");
        Check(simple.pixels!=progress.pixels,"choosing progress changes the native clock card");
        Check(digital.pixels!=date.pixels&&digital.pixels!=simple.pixels&&date.pixels!=simple.pixels,
              "digital and date styles both have a distinct native time layout");
        const auto violetPixels=[](const WidgetBitmap& frame){return std::count_if(frame.pixels.begin(),frame.pixels.end(),[](std::uint32_t p){
            const auto red=(p>>16)&255,green=(p>>8)&255,blue=p&255;
            return red>24&&blue>80&&blue>red+50&&blue>green+50;
        });};
        Check(violetPixels(simple)==0&&violetPixels(progress)>300,
              "simple remains monochrome while progress draws its violet hours and weekday");
        Check(violetPixels(digital)==0&&violetPixels(date)==0,
              "digital and date preserve the reference white foreground without adding colored wallpaper");
        Check(draw(sample,IslandClockStyle::Progress).pixels==progress.pixels,
              "identical supplied time produces a deterministic style card without reading the clock");
        auto nextSecond=sample;++nextSecond.wSecond;
        Check(draw(nextSecond,IslandClockStyle::Simple).pixels==simple.pixels&&
              draw(nextSecond,IslandClockStyle::Progress).pixels!=progress.pixels,
              "seconds update the progress footer without changing the original minute-only clock");
        Check(draw(nextSecond,IslandClockStyle::Digital).pixels!=digital.pixels&&
              draw(nextSecond,IslandClockStyle::Date).pixels==date.pixels,
              "digital advances seconds while date keeps its minute-only time stable");
        Check(draw(sample,IslandClockStyle::Digital,true).pixels==digital.pixels&&
              draw(sample,IslandClockStyle::Date,true).pixels==date.pixels,
              "numeric digital and date styles use the same universal formatting in both languages");
        auto nextDate=sample;++nextDate.wDay;
        Check(draw(nextDate,IslandClockStyle::Digital).pixels==digital.pixels&&
              draw(nextDate,IslandClockStyle::Date).pixels!=date.pixels,
              "date updates YYYY.MM.DD while digital remains time-only");
        std::vector<std::uint32_t> previousDigit;
        for(unsigned digit=0;digit<10;++digit){
            auto tick=sample;tick.wSecond=static_cast<WORD>(digit);
            const auto frame=draw(tick,IslandClockStyle::Digital);
            Check(frame.pixels!=previousDigit,"each consecutive digital seconds digit has a distinct seven-segment silhouette");
            previousDigit=frame.pixels;
        }
        for(const auto style:{IslandClockStyle::Digital,IslandClockStyle::Date}){
            const auto frame=draw(sample,style);
            unsigned minX=frame.width,minY=frame.height,maxX=0,maxY=0,bright=0;
            for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){
                const auto pixel=frame.pixels[static_cast<std::size_t>(y)*frame.width+x];
                if((pixel&255)<96)continue;
                minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);++bright;
            }
            Check(bright>300&&minX>=12&&minY>=12&&maxX+12<frame.width&&maxY+12<frame.height,
                  "new clock styles remain readable with unclipped margins inside the shared card");
        }
        const auto flip=draw(sample,IslandClockStyle::Flip),pixel=draw(sample,IslandClockStyle::Pixel);
        Check(flip.pixels!=pixel.pixels&&pixel.pixels!=digital.pixels&&flip.pixels!=simple.pixels,
              "flip and pixel clocks are distinct native styles");
        auto flipExample=sample;flipExample.wHour=12;flipExample.wMinute=54;
        auto pixelExample=sample;pixelExample.wHour=15;pixelExample.wMinute=6;pixelExample.wSecond=0;
        SaveFrame(draw(flipExample,IslandClockStyle::Flip),L"style-clock-flip.png");
        SaveFrame(draw(flipExample,IslandClockStyle::Flip,true),L"style-clock-flip-en.png");
        SaveFrame(draw(pixelExample,IslandClockStyle::Pixel),L"style-clock-pixel.png");
        SaveFrame(draw(pixelExample,IslandClockStyle::Pixel,true),L"style-clock-pixel-en.png");
        Check(draw(nextSecond,IslandClockStyle::Flip).pixels==flip.pixels&&
              draw(nextSecond,IslandClockStyle::Pixel).pixels!=pixel.pixels,
              "pixel seconds advance while flip remains stable within one minute");
        auto midnightFace=sample;midnightFace.wHour=0;auto noonFace=midnightFace;noonFace.wHour=12;
        Check(draw(midnightFace,IslandClockStyle::Flip).pixels!=draw(noonFace,IslandClockStyle::Flip).pixels,
              "12 AM and 12 PM retain distinct period indicators");
        auto before=sample;before.wHour=23;before.wMinute=59;before.wSecond=59;
        auto after=before;++after.wDay;after.wHour=after.wMinute=after.wSecond=0;
        IslandClockFlipState flipState;
        Check(!flipState.Observe(before,1000,true)&&!flipState.Active(1000),
              "opening a flip clock never animates a stale time");
        Check(flipState.Observe(after,2000,true)&&flipState.Active(2000)&&flipState.Progress(2000)==0,
              "a visible consecutive minute starts one bounded flip across midnight");
        Check(flipState.Progress(2240)==.5f&&!flipState.Active(2480)&&flipState.Progress(2480)==1,
              "flip animation ends after 480 milliseconds");
        auto late=after;++late.wMinute;
        Check(!flipState.Observe(late,12000,true),
              "a stalled or resumed clock snaps to current time without replaying flips");
        auto hidden=late;++hidden.wMinute;
        Check(!flipState.Observe(hidden,13000,false)&&!flipState.Active(13000),
              "hidden or collapsed clocks request no animation");
        auto visible=hidden;++visible.wMinute;
        Check(flipState.Observe(visible,14000,true),
              "ordinary visible minute changes resume after hidden snapshots");
        flipState.Stop();Check(!flipState.Active(14001),
              "switching or closing a flip clock cancels its animation");
        const auto fold=[&](const SYSTEMTIME& old,const SYSTEMTIME& time,float phase){
            WidgetBitmap frame;Check(canvas.Begin(200,144,192),"flip transition canvas starts");
            canvas.Rounded({0,0,200,144},0,0x000000);
            DrawIslandClockCard(canvas,{0,0,200,144},time,false,IslandClockStyle::Flip,1,&old,phase);
            Check(canvas.Finish(frame),"flip transition finishes");return frame;
        };
        const auto folding=fold(before,after,.25f),unfolding=fold(before,after,.75f);
        SaveFrame(folding,L"clock-flip-folding.png");SaveFrame(unfolding,L"clock-flip-unfolding.png");
        Check(folding.pixels!=unfolding.pixels&&fold(before,after,1).pixels==draw(after,IslandClockStyle::Flip).pixels,
              "both flip phases draw distinct faces and land exactly on the new time");
        auto nextMinute=sample;++nextMinute.wMinute;
        const auto minuteFold=fold(sample,nextMinute,.25f),still=draw(nextMinute,IslandClockStyle::Flip);
        for(unsigned y=0;y<still.height;++y)for(unsigned x=0;x<200;++x)
            Check(minuteFold.pixels[y*still.width+x]==still.pixels[y*still.width+x],
                  "only changed flip tiles move; the hour tile stays still");
        const auto generation=canvas.SurfaceGeneration();
        for(unsigned frame=0;frame<30;++frame)fold(before,after,frame/29.f);
        Check(canvas.SurfaceGeneration()==generation&&canvas.CachedBitmaps()==0,
              "flips reuse their surface without per-frame bitmap allocation");
        std::vector<std::uint32_t> previousPixelDigit;
        for(unsigned digit=0;digit<10;++digit){
            auto time=sample;time.wSecond=static_cast<WORD>(digit);const auto frame=draw(time,IslandClockStyle::Pixel);
            Check(frame.pixels!=previousPixelDigit,"all ten pixel digits have distinct block silhouettes");
            previousPixelDigit=frame.pixels;
        }
        for(const auto style:{IslandClockStyle::Flip,IslandClockStyle::Pixel}){
            WidgetBitmap clipped;Check(canvas.Begin(120,92,96),"new clock scales into a narrow slot");
            DrawIslandClockCard(canvas,{10,10,100,72},sample,false,style);
            Check(canvas.Finish(clipped),"scaled clock completes");
            for(unsigned y=0;y<92;++y)for(unsigned x=0;x<120;++x)
                if(x<10||x>=110||y<10||y>=82)Check(clipped.pixels[y*120+x]==0,
                    "new styles stay inside the same scaled component bounds");
        }
        Check(draw(sample,IslandClockStyle::Progress,true).pixels!=progress.pixels,
              "progress date and progress labels follow the selected language");
        const auto sundayFrame=draw(sunday,IslandClockStyle::Progress);
        const auto mondayFrame=draw(nextMonday,IslandClockStyle::Progress);
        Check(sundayFrame.pixels!=mondayFrame.pixels&&violetPixels(mondayFrame)>300,
              "week rollover refreshes the visible date, time and progress while retaining the clock styling");
    }
    widget::Preferences prefs;prefs.enabled=true;
    IslandDisplay display;display.autoHide=false;display.expandedWidgets={W::Clock};
    IslandWindow window(GetModuleHandleW(nullptr),{});
    Check(window.Start(prefs,display),"clock-only native island starts");
    window.Expand(true);window.Settle();
    const auto hwnd=window.Handle();
    IslandPreviewRenderer renderer;
    for(const bool english:{false,true}){
        IslandPresentation presentation;presentation.english=english;window.Update(presentation);
        for(const auto shape:{IslandShape::Standard,IslandShape::Capsule})
          for(const auto size:{IslandSize::Small,IslandSize::Standard,IslandSize::Large}){
            display.shape=shape;display.size=size;
            Check(window.SetDisplay(display),"clock shape and size apply without replacing the window");window.Settle();
            const auto live=window.Frame();
            Check(!live.pixels.empty()&&window.Handle()==hwnd&&window.Expanded(),"clock stays expanded on the same native window");
            window.ReleasePreview();const auto frame=window.Preview(display,true);
            Check(frame.width&&frame.height&&frame.pixels.pixels.size()==static_cast<std::size_t>(frame.width)*frame.height,
                  "clock preview retains the complete native bitmap at every shape and size");
            unsigned minX=frame.width,minY=frame.height,maxX=0,maxY=0,bright=0;
            for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){
                const auto p=frame.pixels.pixels[static_cast<std::size_t>(y)*frame.width+x];
                if(((p>>24)&255)<100||((p>>16)&255)<96||((p>>8)&255)<96||(p&255)<96)continue;
                minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);++bright;
            }
            Check(bright>200&&minX>8&&maxX+8<frame.width&&minY>8&&maxY+8<frame.height,
                  "date and large time remain visible with clear margins rather than clipping against the island");
            Check(std::abs(static_cast<int>(minX+maxX)-static_cast<int>(frame.width))<12,
                  "the clock text group remains horizontally centered");
            const auto file=std::wstring(L"expanded-clock-")+(english?L"en-":L"zh-")+
                (shape==IslandShape::Standard?L"standard-":L"capsule-")+
                (size==IslandSize::Small?L"small":size==IslandSize::Large?L"large":L"normal")+L".png";
            SaveFrame(frame.pixels,file.c_str());
            if(shape==IslandShape::Capsule&&size==IslandSize::Standard)
                SaveFrame(renderer.Render(frame,display,{}),english?L"expanded-clock-preview-en.png":L"expanded-clock-preview.png");
        }
    }
    display.shape=IslandShape::Capsule;display.size=IslandSize::Standard;
    IslandPresentation chinese;window.Update(chinese);
    display.expandedWidgets={W::CalendarWeather,W::Clock,W::Todos,W::Music};
    window.SetMonitor({L"clock-wide",{0,0,1920,1080},{0,0,1920,1080},true});
    Check(window.SetDisplay(display),"clock combines with all released widgets");window.Settle();
    RECT rect{};GetClientRect(hwnd,&rect);
    Check(rect.right>IslandPx(880,static_cast<int>(GetDpiForWindow(hwnd)))&&rect.right<=1920&&rect.bottom<=IslandPx(177,static_cast<int>(GetDpiForWindow(hwnd))),
          "all four released widgets stay on one row on a wide monitor");
    SaveFrame(window.Frame(),L"expanded-clock-all-wide.png");
    window.ReleasePreview();const auto all=window.Preview(display,true);
    SaveFrame(renderer.Render(all,display,{}),L"expanded-clock-all-preview.png");
    IslandPresentation english;english.english=true;window.Update(english);
    window.ReleasePreview();const auto allEnglish=window.Preview(display,true);
    SaveFrame(renderer.Render(allEnglish,display,{}),L"expanded-clock-all-preview-en.png");
    window.Update(chinese);
    window.SetMonitor({L"clock-narrow",{0,0,640,900},{0,0,640,900},true});window.Settle();
    GetClientRect(hwnd,&rect);
    Check(rect.right<=640&&rect.bottom<=IslandPx(177,static_cast<int>(GetDpiForWindow(hwnd))),
          "the clock and other widgets fit one row on a narrow monitor");
    SaveFrame(window.Frame(),L"expanded-clock-all-narrow.png");
    window.Expand(false);window.Settle();window.Expand(true);window.Settle();
    SendMessageW(hwnd,WM_TIMER,1,0);window.Settle();
    Check(window.Expanded()&&!window.Frame().pixels.empty(),"existing time tick and repeated expansion retain clock content");
    display.expandedWidgets={W::Music};Check(window.SetDisplay(display),"clock can be removed without affecting the music widget");window.Settle();
    window.ReleasePreview();window.Stop();
}
void CheckPerformanceRendering(){
    using namespace platform::windows;
    using W=dynamic_island::ExpandedWidget;
    WidgetCanvas canvas;WidgetBitmap laptop,desktop,unknown;
    performance::Snapshot readings;readings.cpu=13;readings.gpu=77;readings.memory=57;
    CompactData battery;battery.hasBattery=true;battery.battery=81;
    const auto draw=[&](const std::optional<performance::Snapshot>& sample,const CompactData& power,WidgetBitmap& frame){
        Check(canvas.Begin(276,144,192),"performance comparison canvas starts");
        canvas.Rounded({0,0,276,144},16,0x161a22);
        DrawIslandPerformanceCard(canvas,{0,0,276,144},sample,power,false,1);
        Check(canvas.Finish(frame),"performance card rasterizes real metric projection");
    };
    draw(readings,battery,laptop);SaveFrame(laptop,L"expanded-performance-laptop-sample.png");
    SaveFrame(laptop,L"expanded-performance-battery-unplugged.png");
    CompactData charging=battery;charging.plugged=true;charging.charging=true;
    WidgetBitmap chargingFrame;draw(readings,charging,chargingFrame);
    SaveFrame(chargingFrame,L"expanded-performance-battery-charging.png");
    Check(chargingFrame.pixels!=laptop.pixels,"charging has a distinct visible status and color from battery power");
    CompactData plugged=battery;plugged.plugged=true;
    WidgetBitmap pluggedFrame;draw(readings,plugged,pluggedFrame);
    SaveFrame(pluggedFrame,L"expanded-performance-battery-plugged.png");
    Check(pluggedFrame.pixels!=chargingFrame.pixels&&pluggedFrame.pixels!=laptop.pixels,
          "connected power without actual charging is not labelled charging or battery-powered");
    CompactData full=plugged;full.battery=100;
    WidgetBitmap fullFrame;draw(readings,full,fullFrame);SaveFrame(fullFrame,L"expanded-performance-battery-full.png");
    Check(fullFrame.pixels!=pluggedFrame.pixels,"fully charged state is distinguished");
    CompactData noBattery;draw(readings,noBattery,desktop);SaveFrame(desktop,L"expanded-performance-desktop-sample.png");
    draw({},battery,unknown);SaveFrame(unknown,L"expanded-performance-unknown-sample.png");
    Check(laptop.pixels!=desktop.pixels&&laptop.pixels!=unknown.pixels,"battery presence and unavailable telemetry have distinct renderings");
    CompactData ignoredBattery;ignoredBattery.battery=81;ignoredBattery.plugged=true;
    WidgetBitmap noBatteryWithValue;draw(readings,ignoredBattery,noBatteryWithValue);
    Check(noBatteryWithValue.pixels==desktop.pixels,"desktops never draw a battery row from a stale percentage without a physical battery");
    performance::Snapshot zero;zero.cpu=0;zero.gpu=0;zero.memory=0;
    WidgetBitmap zeroFrame;draw(zero,battery,zeroFrame);
    Check(zeroFrame.pixels!=unknown.pixels,"missing metrics display unavailable rather than a fabricated zero");
    widget::Preferences prefs;prefs.enabled=true;
    IslandDisplay display;display.autoHide=false;display.expandedWidgets={W::System};
    IslandWindow window(GetModuleHandleW(nullptr),{});
    Check(window.Start(prefs,display),"performance island starts");window.Settle();
    Pump(1200);
    Check(!window.PerformanceReading(),"collapsed expanded-only performance does not acquire invisible readings");
    const auto awaitCpu=[&](std::uint64_t after){
        const auto deadline=GetTickCount64()+6000;
        std::optional<performance::Snapshot> reading;
        do{
            Pump(20);reading=window.PerformanceReading();
            if(reading&&reading->cpu&&reading->monotonicMillis>after)break;
        }while(GetTickCount64()<deadline);
        Check(reading&&reading->cpu&&std::isfinite(*reading->cpu)&&*reading->cpu>=0&&*reading->cpu<=100&&
              reading->monotonicMillis>after,"visible performance resumes a new real CPU delta within its deadline");
        return *reading;
    };
    window.Expand(true);window.Settle();
    auto current=awaitCpu(0);
    for(unsigned round=0;round<3;++round){
        window.Expand(false);window.Settle();
        const auto hidden=window.PerformanceReading();
        Check(hidden&&hidden->cpu&&hidden->monotonicMillis>=current.monotonicMillis,"collapse retains the last real CPU snapshot");
        Pump(1200);const auto paused=window.PerformanceReading();
        Check(paused&&paused->cpu==hidden->cpu&&paused->gpu==hidden->gpu&&paused->memory==hidden->memory&&
              paused->monotonicMillis==hidden->monotonicMillis,"invisible expanded card retains its exact cache without sampling or advancing timestamp");
        window.Expand(true);window.Settle();
        const auto opened=window.PerformanceReading();
        Check(opened&&opened->cpu==paused->cpu&&opened->gpu==paused->gpu&&opened->memory==paused->memory&&
              opened->monotonicMillis==paused->monotonicMillis,"reopening immediately shows the real cached CPU/GPU snapshot while its baseline resumes");
        current=awaitCpu(paused->monotonicMillis);
    }
    const auto refreshed=awaitCpu(current.monotonicMillis);
    Check(refreshed.monotonicMillis>current.monotonicMillis,"visible expanded performance continues updating after resume");
    IslandPreviewRenderer renderer;
    for(const bool english:{false,true}){
        IslandPresentation view;view.english=english;window.Update(view);
        window.ReleasePreview();const auto single=window.Preview(display,true);
        Check(single.width&&single.height&&!single.pixels.pixels.empty(),"performance-only preview renders");
        SaveFrame(renderer.Render(single,display,{}),english?L"expanded-performance-preview-en.png":L"expanded-performance-preview.png");
        display.expandedWidgets={W::CalendarWeather,W::Clock,W::Todos,W::Music,W::System};
        window.ReleasePreview();const auto five=window.Preview(display,true);
        Check(five.width<=960&&five.height<=300&&five.width>five.height*5,"five-component preview remains one complete horizontal row");
        SaveFrame(renderer.Render(five,display,{}),english?L"expanded-performance-five-preview-en.png":L"expanded-performance-five-preview.png");
        display.expandedWidgets={W::System};
    }
    IslandPresentation chinese;window.Update(chinese);
    display.expandedWidgets={W::CalendarWeather,W::Clock,W::Todos,W::Music,W::System};
    const HWND hwnd=window.Handle();
    for(const int monitorWidth:{1920,1366,640})for(const auto size:{IslandSize::Small,IslandSize::Standard,IslandSize::Large}){
        display.size=size;
        window.SetMonitor({L"performance-fit",{0,0,monitorWidth,1080},{0,0,monitorWidth,1080},true});
        Check(window.SetDisplay(display),"five-component row applies for monitor and size");window.Settle();
        RECT rect{};GetClientRect(hwnd,&rect);
        Check(rect.right>0&&rect.right<=monitorWidth&&rect.bottom>0&&rect.bottom<=IslandPx(177,static_cast<int>(GetDpiForWindow(hwnd)),1.4f)&&rect.right>rect.bottom*5,
              "all five widgets stay in one row inside every display at each size");
        const auto file=std::wstring(L"expanded-performance-five-")+std::to_wstring(monitorWidth)+L"-"+
            (size==IslandSize::Small?L"small":size==IslandSize::Large?L"large":L"normal")+L".png";
        SaveFrame(window.Frame(),file.c_str());
    }
    // The complete row is scaled, so the first calendar's input region must
    // follow the exact same scale as its pixels on a narrow monitor.
    RECT fitted{};GetClientRect(hwnd,&fitted);
    const auto before=window.Frame().pixels;
    const float rowScale=static_cast<float>(fitted.right)/1348.f;
    const auto point=MAKELPARAM(static_cast<int>(std::lround(308*rowScale)),static_cast<int>(std::lround(26*rowScale)));
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,point);SendMessageW(hwnd,WM_LBUTTONUP,0,point);Pump(500);
    Check(window.Frame().pixels!=before&&window.Expanded(),"scaled calendar remains clickable inside the five-component row");
    window.ReleasePreview();display.expandedWidgets={W::Clock};window.SetDisplay(display);window.Settle();
    Check(!window.PerformanceReading(),"removing performance releases an unused sampler");
    window.Stop();
}
void SaveWidgetStyleCards(){
    using namespace platform::windows;
    using W=dynamic_island::ExpandedWidget;
    IslandWindow window(GetModuleHandleW(nullptr),{});
    for(const bool english:{false,true}){
    IslandPresentation presentation;presentation.english=english;window.Update(presentation);
    IslandTodoSource todos;
    todos.forDay=[english](calendar::Date){return std::vector<IslandTodo>{
        {"sample-one",english?L"Plan the day":L"整理今日计划",L"09:00",true,false,0x6bd89b},
        {"sample-two",english?L"Reading":L"阅读与学习",L"14:00",false,false,0x769aff},
        {"sample-three",english?L"Evening exercise":L"晚上运动",L"19:30",false,false,0xffa348}};};
    window.SetTodoSource(todos);
    dynamic_island::Weather weather;weather.available=true;weather.enabled=true;
    weather.temperature=18;weather.condition=english?L"Cloudy":L"多云";weather.code=2;
    window.SetWeather(weather);
    IslandDisplay display;display.autoHide=false;
    for(const auto kind:{W::Music,W::CalendarWeather,W::Todos}){
        // A second card selects the shared expanded workspace for music too.
        display.expandedWidgets={kind,W::Clock};window.ReleasePreview();
        const auto frame=window.Preview(display,true);
        const unsigned width=PreviewPx(kind==W::CalendarWeather?300:232),height=PreviewPx(144);
        const unsigned left=PreviewPx(18),top=PreviewPx(16);
        Check(frame.pixels.width>=left+width&&frame.pixels.height>=top+height,"style card crop stays within production drawing");
        WidgetBitmap card;card.width=width;card.height=height;card.pixels.resize(static_cast<std::size_t>(width)*height);
        for(unsigned y=0;y<height;++y)
            std::copy_n(frame.pixels.pixels.data()+static_cast<std::size_t>(top+y)*frame.pixels.width+left,width,
                        card.pixels.data()+static_cast<std::size_t>(y)*width);
        const std::wstring name=kind==W::Music?L"style-music":kind==W::CalendarWeather?L"style-calendar-weather":L"style-todos";
        SaveFrame(card,(name+(english?L"-en.png":L".png")).c_str());
    }
    window.ReleasePreview();
    WidgetCanvas canvas;WidgetBitmap performanceCard;
    performance::Snapshot sample;sample.cpu=13;sample.gpu=77;sample.memory=57;
    Check(canvas.Begin(276,144,192),"performance style card canvas starts");
    canvas.Rounded({0,0,276,144},0,0x000000);
    DrawIslandPerformanceCard(canvas,{0,0,276,144},sample,{},english,1);
    Check(canvas.Finish(performanceCard),"performance style card uses the production renderer");
    SaveFrame(performanceCard,english?L"style-system-en.png":L"style-system.png");
    }
}
void CheckNotificationSurface(){
    using namespace platform::windows;
    namespace layer = window_layer;
    for(const auto state:{QUNS_ACCEPTS_NOTIFICATIONS,QUNS_BUSY,QUNS_APP}){
        Check(AllowIslandNotificationState(state,false),"ordinary application activity allows notifications on an unprotected target display");
        Check(!AllowIslandNotificationState(state,true),"target-display protection overrides every otherwise allowed notification state");
    }
    for(const auto state:{QUNS_NOT_PRESENT,QUNS_RUNNING_D3D_FULL_SCREEN,QUNS_PRESENTATION_MODE,QUNS_QUIET_TIME,
                          static_cast<QUERY_USER_NOTIFICATION_STATE>(0),static_cast<QUERY_USER_NOTIFICATION_STATE>(8)})
        Check(!AllowIslandNotificationState(state,false)&&!AllowIslandNotificationState(state,true),
              "absent user, exclusive fullscreen, presentations, quiet time and unknown states always suppress notifications");
    bool allowed = true;
    QUERY_USER_NOTIFICATION_STATE systemState=QUNS_ACCEPTS_NOTIFICATIONS;
    unsigned activated = 0;
    IslandWindow window(GetModuleHandleW(nullptr), {}, [&]{return allowed&&AllowIslandNotificationState(systemState,false);});
    NotificationIconCache iconCache;
    for(const auto* key:{"qq","wechat","water","stand","sleep","severe","rain","temperature","preview","off-work","off-work-squidward"}){
        const auto image=iconCache.Load(key);
        Check(image&&image->width<=256&&image->height<=256&&!image->pixels.empty(),"every bundled notification icon decodes within the resource bound");
        Check(iconCache.Load(key)==image,"repeated notices reuse the bounded decoded icon");
    }
    Check(!iconCache.Load("../qq")&&!iconCache.Load("QQ")&&!iconCache.Load("unknown"),"notification icons accept only fixed bundled names");
    iconCache.Clear();
    IslandNotification first{"message:first", L"QQ", L"有一条新消息", [&]{++activated;},"qq"};
    Check(!window.ShowNotification(first), "a stopped island cannot receive a notification");
    widget::Preferences prefs; prefs.enabled = true;
    IslandDisplay display; display.autoHide = false;
    layer::Context policy; policy.foreignForeground = true;
    layer::contextReader = [&]{return policy;}; layer::Start();
    Check(window.Start(prefs, display), "notification surface starts in the desktop layer"); window.Settle();
    const HWND before = GetForegroundWindow();
    Check(window.ShowNotification(first), "notification is accepted over a normal foreground application"); window.Settle();
    Check((GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0 &&
          layer::Overlay(layer::windows.at(window.Handle())), "notification temporarily uses the protected overlay band");
    Check(GetForegroundWindow() == before, "displaying a notification does not activate the island");
    SaveFrame(window.Frame(), L"notification-message.png");
    RECT targetArea{};GetWindowRect(window.Handle(),&targetArea);
    policy.fullscreen.push_back({targetArea.right+4096,targetArea.top,targetArea.right+8192,targetArea.bottom+2048});
    layer::Update(policy);
    for(const auto state:{QUNS_BUSY,QUNS_APP}){
        systemState=state;SendMessageW(window.Handle(),WM_TIMER,1,0);window.Settle();
        Check((GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)!=0&&
              layer::Overlay(layer::windows.at(window.Handle()))&&GetForegroundWindow()==before,
              "global busy/app activity on another display does not dismiss or retuck the current notification");
    }
    policy.fullscreen.clear();layer::Update(policy);systemState=QUNS_ACCEPTS_NOTIFICATIONS;
    auto update = first; update.body = L"更新后的提醒"; update.activate = [&]{activated += 2;};
    Check(window.ShowNotification(update), "the current notification can be updated without a duplicate");
    for (int i = 0; i < 10; ++i) {
        auto next = first; next.id = "queued:" + std::to_string(i);
        Check(window.ShowNotification(next), "notification queue accepts its bounded capacity");
    }
    auto excess = first; excess.id = "queued:overflow";
    Check(!window.ShowNotification(excess), "notification queue rejects overflow");
    Click(window.Handle(), 170, 30); window.Settle();
    Check(activated == 2 && !window.Expanded(), "a click activates only the current notification without opening a todo");
    window.ClearNotifications(); window.Settle();
    Check(!(GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) &&
          !layer::Overlay(layer::windows.at(window.Handle())), "clearing notifications restores the configured desktop band");
    Click(window.Handle(), 60, 15); window.Settle();
    Check(window.Expanded() && window.Page() == IslandPage::Music, "notifications never add entries to todo reminder history");
    window.Expand(false); window.Settle();
    Check(window.ShowNotification(first), "notification can resume after clearing");
    auto duplicate = first; duplicate.id = "message:deduplicated";
    Check(window.ShowNotification(duplicate), "a second distinct notification queues");
    duplicate.activate = [&]{activated += 4;};
    Check(window.ShowNotification(duplicate), "queued duplicate refreshes the existing entry");
    SendMessageW(window.Handle(), WM_TIMER, 7, 0); window.Settle();
    Click(window.Handle(), 170, 30); window.Settle();
    Check(activated == 6 && !(GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST),
          "timer advances the queue once and a queued duplicate activates exactly once");
    for(const auto state:{QUNS_PRESENTATION_MODE,QUNS_QUIET_TIME,QUNS_RUNNING_D3D_FULL_SCREEN,QUNS_NOT_PRESENT}){
        systemState=QUNS_ACCEPTS_NOTIFICATIONS;
        Check(window.ShowNotification(first),"notification can display before a protected global state");
        systemState=state;SendMessageW(window.Handle(),WM_TIMER,1,0);window.Settle();
        Check(!(GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&!window.ShowNotification(first),
              "presentation, quiet time, exclusive fullscreen and absent user clear visible notifications and block new content");
    }
    systemState=QUNS_BUSY;
    Check(window.ShowNotification(first), "notification is visible before quiet mode");
    allowed = false; SendMessageW(window.Handle(), WM_TIMER, 1, 0); window.Settle();
    Check(!(GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) && !window.ShowNotification(first),
          "a locked input desktop overrides permissive BUSY and removes visible content");
    allowed = true; policy.foreignForeground = false;
    Check(window.ShowNotice("todo:kept", L"既有待办", L"19:30") && window.ShowNotification(first),
          "notification can queue behind a todo reminder");
    allowed = false; SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    allowed = true; SendMessageW(window.Handle(), WM_TIMER, 7, 0); window.Settle();
    Check(!(GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) && activated == 6,
          "quiet mode also clears notification content queued behind a todo");
    Click(window.Handle(), 60, 15); window.Settle();
    Check(window.Page() == IslandPage::Reminder, "clearing notifications preserves the independent todo reminder history");
    Click(window.Handle(), 370, 20); window.Settle();
    systemState=QUNS_APP;
    policy.foreignForeground = true; display.hideFullscreen = false;
    Check(window.SetDisplay(display) && window.ShowNotification(first), "notification starts with idle fullscreen visibility allowed"); window.Settle();
    RECT area{}; GetWindowRect(window.Handle(), &area); policy.fullscreen.push_back(area);
    layer::Update(policy); Pump(30); window.Settle();
    Check(!window.ShowNotification(first) && !(GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST),
          "same-display fullscreen overrides APP even when idle fullscreen visibility is allowed");
    policy.fullscreen.clear(); layer::Update(policy); SendMessageW(window.Handle(), WM_TIMER, 1, 0); window.Settle();
    Check(window.ShowNotification(first), "new notifications work after protection clears");
    window.ClearNotifications(); display.topmost = true;
    Check(window.SetDisplay(display) && window.ShowNotification(first), "notifications support an explicitly selected overlay island");
    window.ClearNotifications(); window.Settle();
    Check((GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
          "clearing notifications preserves the user's explicit above-applications preference");
    display.topmost = false;
    Check(window.SetDisplay(display) && window.ShowNotification(first), "desktop display policy can change after notifications");
    window.Stop(); Check(window.Start(prefs, display), "notification surface restarts"); window.Settle();
    Check(!(GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) && activated == 6,
          "stopping drops pending notification content and callbacks");
    {
        unsigned firstCalls=0,latestCalls=0,otherCalls=0;
        IslandNotification older{"qq:older",L"QQ",L"上一条消息",[&]{++firstCalls;},"qq","qq"};
        IslandNotification latest{"qq:latest",L"QQ",L"最新消息",[&]{++latestCalls;},"qq","qq"};
        IslandNotification other{"wechat:latest",L"微信",L"另一应用提醒",[&]{++otherCalls;},"wechat","wechat"};
        Check(window.ShowNotification(older)&&window.ShowNotification(latest),"a new notification from the same app refreshes its current capsule");
        window.Settle();Click(window.Handle(),170,30);window.Settle();
        Check(firstCalls==0&&latestCalls==1&&!(GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
              "same-app refresh activates only the latest callback with no stale capsule left in the queue");
        Check(window.ShowNotification(older)&&window.ShowNotification(other)&&window.ShowNotification(latest),
              "refreshing one application's capsule preserves another application's queued notification");
        window.Settle();Click(window.Handle(),170,30);window.Settle();
        Check(firstCalls==0&&latestCalls==2&&otherCalls==0&&(GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
              "clicking the refreshed QQ capsule advances to the distinct WeChat group");
        Click(window.Handle(),170,30);window.Settle();
        Check(otherCalls==1&&!(GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
              "another app remains independently actionable without duplicate group backlog");
        Check(window.ShowNotification(other)&&window.ShowNotification(older)&&window.ShowNotification(latest),
              "same-app refresh also replaces a queued capsule behind another app");
        window.Settle();Click(window.Handle(),170,30);window.Settle();Click(window.Handle(),170,30);window.Settle();
        Check(firstCalls==0&&latestCalls==3&&otherCalls==2&&!(GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
              "queued coalescing retains only the newest callback for that application");
        latest.id="qq:same-id";latest.activate=[&]{latestCalls+=10;};
        Check(window.ShowNotification(latest),"a grouped notification can start after the queue drains");
        latest.activate=[&]{latestCalls+=100;};Check(window.ShowNotification(latest),"a same-id grouped update refreshes instead of duplicating");
        window.Settle();Click(window.Handle(),170,30);window.Settle();
        Check(latestCalls==103&&!(GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
              "same-id updates invoke only the latest callback and leave no queued duplicate");

        IslandNotification reading{"read:hover",L"QQ",L"把鼠标停留在通知上阅读",{},"qq"};
        display.hoverExpand=true;Check(window.SetDisplay(display),"hover expansion can remain enabled while reading a notification");
        Check(window.ShowNotification(reading),"a new notification is available for hover reading");window.Settle();
        const auto point=MAKELPARAM(IslandPx(170,static_cast<int>(GetDpiForWindow(window.Handle()))),
                                   IslandPx(30,static_cast<int>(GetDpiForWindow(window.Handle()))));
        SendMessageW(window.Handle(),WM_MOUSEMOVE,0,point);
        SendMessageW(window.Handle(),WM_TIMER,3,0);window.Settle();
        Check((GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&!window.Expanded(),
              "even an already queued hover timer cannot expand away an active transient notification");
        SendMessageW(window.Handle(),WM_TIMER,7,0);window.Settle();
        Check((GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&!window.Expanded(),
              "the expiry timer keeps a notification visible while the pointer is reading its capsule");
        SendMessageW(window.Handle(),WM_MOUSELEAVE,0,0);SendMessageW(window.Handle(),WM_TIMER,7,0);window.Settle();
        Check(!(GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
              "after the pointer leaves, the next expiry timer dismisses the notification");
        policy.foreignForeground=false;
        Check(window.ShowNotice("todo:hover-preserved",L"普通待办仍可悬停展开",L"19:30"),
              "ordinary todo notices keep their existing hover behavior");window.Settle();
        SendMessageW(window.Handle(),WM_MOUSEMOVE,0,point);SendMessageW(window.Handle(),WM_TIMER,3,0);window.Settle();
        Check(window.Expanded(),"the transient-only hover guard preserves ordinary todo hover expansion");
    }
    window.Stop();display.hoverExpand=false;policy.foreignForeground=true;
    IslandWindow detailWindow(GetModuleHandleW(nullptr),{},[&]{return allowed&&AllowIslandNotificationState(systemState,false);});
    Check(detailWindow.Start(prefs,display),"a clean island starts for care and weather detail checks");detailWindow.Settle();
    for(const auto* key:{"water","stand","sleep","severe","rain","temperature"}){
        IslandNotification reminder{std::string("detail:")+key,L"完整提醒",L"这条提醒应在灵动岛内显示完整内容，不打开设置页面。",{},key};
        Check(detailWindow.ShowNotification(reminder),"every care and weather reminder can show its native capsule");detailWindow.Settle();
        Click(detailWindow.Handle(),170,30);detailWindow.Settle();
        RECT detail{};GetClientRect(detailWindow.Handle(),&detail);
        const int dpi=static_cast<int>(GetDpiForWindow(detailWindow.Handle()));
        std::cerr<<"DETAIL_DIAGNOSTIC type="<<key<<" expanded="<<detailWindow.Expanded()
                 <<" rect="<<detail.right<<'x'<<detail.bottom<<" dpi="<<dpi
                 <<" expected="<<IslandPx(404,dpi)<<'x'<<IslandPx(208,dpi)
                 <<" topmost="<<((GetWindowLongPtrW(detailWindow.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)!=0)
                 <<" foreground="<<GetForegroundWindow()<<" before="<<before<<" activated="<<activated
                 <<" page="<<static_cast<int>(detailWindow.Page())<<'\n';
        Check(detailWindow.Expanded()&&detail.right==IslandPx(404,dpi)&&detail.bottom==IslandPx(208,dpi)&&
              (GetWindowLongPtrW(detailWindow.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&GetForegroundWindow()==before&&activated==6,
              "care and weather clicks fill the island with message details without activation or an application callback");
        const auto imageName=std::wstring(L"notification-")+std::wstring(key,key+std::char_traits<char>::length(key))+L"-expanded.png";
        SaveFrame(detailWindow.Frame(),imageName.c_str());
        const auto reminderPixels=detailWindow.Frame().pixels;
        Click(detailWindow.Handle(),202,100);detailWindow.Settle();
        Check(detailWindow.Expanded(),"clicking message text does not dismiss the detail page");
        Click(detailWindow.Handle(),374,27);detailWindow.Settle();
        Check(!detailWindow.Expanded()&&!(GetWindowLongPtrW(detailWindow.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&activated==6,
              "the close control dismisses reminder details and restores the configured desktop layer");
        detailWindow.Expand(true);detailWindow.Settle();
        Check(detailWindow.Expanded()&&detailWindow.Page()==IslandPage::Music&&detailWindow.Frame().pixels!=reminderPixels,
              "reopening the island after closing a reminder returns to its normal content");
        detailWindow.Expand(false);detailWindow.Settle();
    }
    IslandNotification longReminder{"detail:long",L"完整长消息",{},{},"rain"};
    for(int line=0;longReminder.body.size()<512;++line)
        longReminder.body+=std::to_wstring(line)+L". 这是一条需要滚动阅读的提醒，保留换行和末尾内容。\n";
    longReminder.body.resize(512);
    Check(detailWindow.ShowNotification(longReminder),"a bounded multiline reminder can be displayed");detailWindow.Settle();
    Click(detailWindow.Handle(),170,30);detailWindow.Settle();
    const auto longTop=detailWindow.Frame().pixels;SaveFrame(detailWindow.Frame(),L"notification-long-expanded-top.png");
    SendMessageW(detailWindow.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);detailWindow.Settle();
    Check(detailWindow.Expanded()&&detailWindow.Page()==IslandPage::Music&&detailWindow.Frame().pixels!=longTop,
          "the wheel scrolls long reminder text without changing the island page");
    for(int step=0;step<30;++step)SendMessageW(detailWindow.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
    detailWindow.Settle();const auto longEnd=detailWindow.Frame().pixels;SaveFrame(detailWindow.Frame(),L"notification-long-expanded-end.png");
    SendMessageW(detailWindow.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);detailWindow.Settle();
    Check(detailWindow.Frame().pixels==longEnd,"long reminder scrolling clamps at the bottom");
    for(int step=0;step<30;++step)SendMessageW(detailWindow.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(WHEEL_DELTA)),0);
    detailWindow.Settle();Check(detailWindow.Frame().pixels==longTop,"scrolling back restores the first line and complete title");
    SendMessageW(detailWindow.Handle(),WM_MOUSELEAVE,0,0);SendMessageW(detailWindow.Handle(),WM_TIMER,7,0);detailWindow.Settle();
    Check(!detailWindow.Expanded()&&!(GetWindowLongPtrW(detailWindow.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
          "the original transient display timer expires an opened reminder detail page");
    detailWindow.Expand(true);detailWindow.Settle();
    Check(detailWindow.Expanded()&&detailWindow.Page()==IslandPage::Music&&detailWindow.Frame().pixels!=longTop,
          "normal expansion after reminder expiry shows the usual island without replaying the message");
    detailWindow.Expand(false);detailWindow.Settle();
    Check(detailWindow.ShowNotification(longReminder),"a newly delivered reminder can open after an earlier reminder expired");detailWindow.Settle();
    Click(detailWindow.Handle(),170,30);detailWindow.Settle();
    Check(detailWindow.ShowNotification(first),"a QQ message can queue while a care or weather detail page is being read");detailWindow.Settle();
    Check(detailWindow.Expanded()&&detailWindow.Frame().pixels==longTop&&activated==6,
          "an incoming application message does not replace an opened reminder or launch an application");
    SendMessageW(detailWindow.Handle(),WM_APP+0x6e3,0,0);detailWindow.Settle();
    Check(!detailWindow.Expanded()&&(GetWindowLongPtrW(detailWindow.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
          "an outside click closes details and advances the queued application capsule");
    Click(detailWindow.Handle(),170,30);detailWindow.Settle();
    Check(activated==7&&!detailWindow.Expanded()&&!(GetWindowLongPtrW(detailWindow.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),
          "QQ retains its direct-open click behavior after a reminder detail page");
    for(const auto mode:{0,1,2,3}){
        Check(detailWindow.ShowNotification(longReminder),"a reminder can reopen before lifecycle cleanup");detailWindow.Settle();
        Click(detailWindow.Handle(),170,30);detailWindow.Settle();
        if(mode==0)detailWindow.ClearNotifications();
        else if(mode==1){allowed=false;SendMessageW(detailWindow.Handle(),WM_TIMER,1,0);}
        else if(mode==2){systemState=QUNS_PRESENTATION_MODE;SendMessageW(detailWindow.Handle(),WM_TIMER,1,0);}
        else{detailWindow.Stop();Check(detailWindow.Start(prefs,display),"an island can restart after stopping open reminder details");}
        detailWindow.Settle();
        Check(!detailWindow.Expanded()&&!(GetWindowLongPtrW(detailWindow.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&activated==7,
              "notification clearing, lock, protected state and stop remove reminder details without callbacks");
        allowed=true;systemState=QUNS_ACCEPTS_NOTIFICATIONS;
    }
    detailWindow.Stop(); layer::Stop(); layer::contextReader = {};
}
void CheckVoiceNotificationSurface(){
    using namespace platform::windows;
    namespace layer = window_layer;
    bool allowed=true;
    std::vector<bool> presentations;
    float amplitude=0;
    unsigned levelReads=0;
    layer::Context policy;policy.foreignForeground=true;
    layer::contextReader=[&]{return policy;};layer::Start();
    IslandWindow window(GetModuleHandleW(nullptr),{},[&]{return allowed;});
    widget::Preferences preferences;preferences.enabled=true;
    IslandDisplay display;display.autoHide=false;
    Check(window.Start(preferences,display),"voice surface starts without changing user widgets");
    window.Settle();
    IslandNotification ordinary{"voice:previous",L"QQ",L"Queued normal notification",{},"qq"};
    Check(window.ShowNotification(ordinary),"an ordinary notification can precede a timed voice");
    IslandNotification voice{"voice:timed",L"章鱼哥",L"我下班了，蟹老板",{},"off-work-squidward"};
    voice.immediate=true;voice.durationMs=18000;
    unsigned positionMs=0;
    bool devicePlaying=true;
    voice.presented=[&](bool visible){presentations.push_back(visible);};
    voice.audioLevel=[&]{++levelReads;return amplitude;};
    voice.audioPlaying=[&]{return devicePlaying;};
    voice.voiceCaption=[&]{return std::wstring(OffWorkVoiceCaption(positionMs));};
    Check(window.ShowNotification(voice),"timed voice preempts an ordinary capsule immediately");window.Settle();
    Check(presentations==std::vector<bool>{true},"voice starts only when its actual capsule is presented");
    const auto silent=window.Frame().pixels;
    SaveFrame(window.Frame(),L"notification-squidward-silent.png");
    amplitude=.9f;SendMessageW(window.Handle(),WM_TIMER,5,0);window.Settle();
    Check(window.Frame().pixels!=silent&&levelReads>1,"actual voice amplitude changes the right-side native bars");
    SaveFrame(window.Frame(),L"notification-squidward-speaking.png");
    SaveFrame(window.Frame(),L"notification-squidward-cue-0.png");
    auto previous=window.Frame().pixels;
    for(const auto next:{480u,1367u,2220u,2824u,3720u}) {
        positionMs=next;SendMessageW(window.Handle(),WM_TIMER,5,0);window.Settle();
        Check(window.Frame().pixels!=previous,"actual device progress changes every recorded count and first dialogue");
        previous=window.Frame().pixels;
        const auto name=L"notification-squidward-cue-"+std::to_wstring(next)+L".png";
        SaveFrame(window.Frame(),name.c_str());
    }
    window.Expand(true);window.Settle();
    Check(!window.Expanded(),"the voice capsule cannot reveal the retired generic notification detail");
    Check(window.ShowNotification(voice),"same voice notification may refresh its text");window.Settle();
    Check(presentations==std::vector<bool>{true},"a visible voice refresh does not restart its playback lease");
    for(const auto& cue:OffWorkVoiceCues) if(cue.startMs>3720) {
        positionMs=cue.startMs;SendMessageW(window.Handle(),WM_TIMER,5,0);window.Settle();
        Check(window.Frame().pixels!=previous,"later spoken phrases update from the device timeline");
        previous=window.Frame().pixels;
        const auto name=L"notification-squidward-cue-"+std::to_wstring(cue.startMs)+L".png";
        SaveFrame(window.Frame(),name.c_str());
    }
    amplitude=0;
    positionMs=17601;devicePlaying=false;SendMessageW(window.Handle(),WM_TIMER,5,0);window.Settle();
    Check(window.Frame().pixels!=previous,"completed voice commits its final caption before stopping the repaint timer");
    SaveFrame(window.Frame(),L"notification-squidward-final.png");
    window.ClearNotifications();window.Settle();
    Check(presentations==std::vector<bool>({true,false}),"clearing the capsule releases its voice exactly once");
    const auto readsAfterClear=levelReads;
    SendMessageW(window.Handle(),WM_TIMER,5,0);window.Settle();
    Check(levelReads==readsAfterClear,"dismissed voice no longer polls PCM for a running bar timer");
    voice.id="voice:blocked";allowed=false;
    Check(!window.ShowNotification(voice)&&presentations.size()==2,"blocked timed voices cannot start their audio");
    allowed=true;voice.id="voice:stopped";
    devicePlaying=true;
    Check(window.ShowNotification(voice),"voice can be presented before window shutdown");window.Settle();
    window.Stop();
    Check(presentations==std::vector<bool>({true,false,true,false}),"window shutdown balances every audible presentation");
    layer::Stop();layer::contextReader={};
}
void BrowserOverlayScenario(){
    using namespace platform::windows;
    namespace layer=window_layer;
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"browser overlay COM initialization");
    unsigned checks=0;
    const auto expect=[&](bool ok,const char* reason){Check(ok,reason);++checks;};
    for(const auto shape:{IslandShape::Capsule,IslandShape::Standard}){
        layer::Context policy;policy.foreignForeground=true;
        layer::contextReader=[&]{return policy;};layer::Start();
        IslandWindow window(GetModuleHandleW(nullptr),{});
        widget::Preferences prefs;prefs.enabled=true;
        IslandDisplay display;display.autoHide=false;display.topmost=false;display.hideFullscreen=true;display.shape=shape;
        expect(window.Start(prefs,display),"browser fixture starts actual island in desktop layer");window.Settle();
        MONITORINFO screen{sizeof(screen)};
        expect(GetMonitorInfoW(MonitorFromWindow(window.Handle(),MONITOR_DEFAULTTONEAREST),&screen)!=FALSE,
               "read actual island monitor for browser geometry");
        const auto monitor=screen.rcMonitor;
        const RECT normal{monitor.left+50,monitor.top+70,monitor.right-50,monitor.bottom-70};
        const HWND browser=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Browser overlay fixture",WS_OVERLAPPEDWINDOW,
            normal.left,normal.top,normal.right-normal.left,normal.bottom-normal.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        expect(browser!=nullptr,"create ordinary application window on private desktop");
        ShowWindow(browser,SW_SHOWNOACTIVATE);
        const HWND foreground=GetForegroundWindow();
        const auto aboveBrowser=[&]{
            for(HWND at=GetTopWindow(nullptr);at;at=GetWindow(at,GW_HWNDNEXT)){
                if(at==window.Handle())return true;
                if(at==browser)return false;
            }
            return false;
        };
        const auto visible=[&](bool wanted,const char* reason){
            const auto until=GetTickCount64()+1800;bool ok=false;
            do{
                Pump(10);
                ok=wanted?(IsWindowVisible(window.Handle())&&!layer::Suppressed(window.Handle())&&
                    (GetWindowLongPtrW(window.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&aboveBrowser()):
                    !IsWindowVisible(window.Handle());
            }while(!ok&&GetTickCount64()<until);
            expect(ok,reason);
            if(wanted){
                Pump(250);
                expect(IsWindowVisible(window.Handle())&&!layer::Suppressed(window.Handle())&&aboveBrowser(),
                    "overlay remains visible after ordinary animation and timer processing");
                const auto frame=window.Frame();
                expect(!frame.pixels.empty()&&std::any_of(frame.pixels.begin(),frame.pixels.end(),
                    [](auto pixel){return (pixel>>24)!=0;}),"visible island retains actual rendered foreground pixels");
            }
        };
        const auto apply=[&](const RECT& bounds,bool maximized,DWORD style,QUERY_USER_NOTIFICATION_STATE state){
            policy={};policy.foreground=browser;policy.foreignForeground=true;
            if(layer::ForegroundCoversMonitor(bounds,monitor,maximized,style,state))policy.fullscreen.push_back(monitor);
            layer::ApplyForegroundProtection(policy,false,state);layer::Update(policy);
            SetWindowPos(browser,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            // Use the production tick and animation messages, never force Settle
            // after a visibility transition: that could hide a restoration bug.
            SendMessageW(window.Handle(),WM_TIMER,1,0);
        };
        display.topmost=true;
        expect(window.SetDisplay(display),"enabling above-applications applies with a browser already open");
        apply(normal,false,WS_OVERLAPPEDWINDOW,QUNS_ACCEPTS_NOTIFICATIONS);
        visible(true,"actual island appears above ordinary browser window");
        for(const auto state:{QUNS_BUSY,QUNS_ACCEPTS_NOTIFICATIONS}){
            apply(monitor,true,0x15070000,state);
            visible(true,"maximized Chromium frame covering monitor does not hide actual overlay island");
            apply(screen.rcWork,true,0x15070000,state);
            visible(true,"maximized browser work-area frame preserves overlay");
        }
        apply(monitor,false,WS_POPUP|WS_VISIBLE,QUNS_BUSY);
        visible(false,"F11-style fullscreen still honors hide-fullscreen option");
        apply(normal,false,WS_OVERLAPPEDWINDOW,QUNS_ACCEPTS_NOTIFICATIONS);
        visible(true,"exiting browser fullscreen restores actual island without forced settling");
        apply(monitor,false,WS_POPUP|WS_VISIBLE,QUNS_BUSY);
        visible(false,"browser fullscreen hides the island again");
        display.hideFullscreen=false;
        expect(window.SetDisplay(display),"hide-fullscreen can be disabled while browser stays fullscreen");
        visible(true,"disabling hide-fullscreen restores overlay without a browser resize");
        apply(monitor,false,WS_POPUP|WS_VISIBLE,QUNS_RUNNING_D3D_FULL_SCREEN);
        visible(false,"exclusive-game protection remains stronger than overlay preference");
        apply(normal,false,WS_OVERLAPPEDWINDOW,QUNS_ACCEPTS_NOTIFICATIONS);
        visible(true,"ordinary browser restores after exclusive-game protection clears");
        display.hideFullscreen=true;expect(window.SetDisplay(display),"restore normal fullscreen preference");
        // Negative control: HEAD's previous geometry-only classification treats
        // an ordinary maximized monitor-sized browser exactly like fullscreen.
        policy={};policy.foreground=browser;policy.foreignForeground=true;policy.fullscreen.push_back(monitor);
        layer::Update(policy);SendMessageW(window.Handle(),WM_TIMER,1,0);
        visible(false,"legacy geometry-only classification reproduces invisible above-applications island");
        apply(monitor,true,0x15070000,QUNS_BUSY);
        visible(true,"current maximized-window classification restores the same native island");
        expect(GetForegroundWindow()==foreground,"overlay checks never activate the island");
        window.Stop();layer::Stop();layer::contextReader={};DestroyWindow(browser);
    }
    CoUninitialize();std::cout<<"PASS browser overlay scenarios: "<<checks<<" checks; actual island HWNDs, injected browser facts, private non-input desktop\n";
}
void Scenario(){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization");
    CheckExpansionModes();
    CheckNotificationSurface();
    CheckVoiceNotificationSurface();
    CheckAnimationSettings();
    CheckExpandedWidgets();
    CheckClockRendering();
    CheckPerformanceRendering();
    SaveWidgetStyleCards();
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
        const auto notificationState=nlohmann::json::parse(session.NotificationsState());
        Check(notificationState["preferences"]["waterEnabled"]==false&&notificationState["preferences"]["weatherRain"]==false&&
            notificationState["preferences"]["messagesQQ"]==false&&!weatherActive,
            "notification configuration starts opt-out without activating island or weather");
        const auto optedIn=session.HandleNotifications(LR"({"version":1,"scope":"island-notifications","action":"set","key":"messagesQQ","value":true})");
        Check(optedIn&&nlohmann::json::parse(*optedIn)["saved"]==true&&
            nlohmann::json::parse(*optedIn)["receiving"]==false&&!session.Running()&&!session.Snapshot().enabled&&!weatherActive,
            "saving a notification preference does not enable an island, weather consumer or notification receiver");
        Check(session.HandleNotifications(LR"({"version":1,"scope":"island-notifications","action":"reset"})").has_value(),
            "notification configuration can be reset through the owning session");
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
        Check(r.right==IslandPx(236,dpi)&&r.bottom==IslandPx(34,dpi),"compact capsule settles at the enlarged standard size");
        {
            const auto left=static_cast<PageCommand>(static_cast<int>(PageCommand::IslandLeftWidgetFirst)+20);
            const auto right=static_cast<PageCommand>(static_cast<int>(PageCommand::IslandRightWidgetFirst)+1);
            const auto before=files.values;
            const auto leftState=nlohmann::json::parse(*session.HandlePage(left));
            const auto rightState=nlohmann::json::parse(*session.HandlePage(right));
            Check(leftState["saved"]&&rightState["saved"],"compact widget choices are saved by the native session");
            Check(!weatherActive&&State(session)["leftWidget"]==20&&State(session)["rightWidget"]==1&&files.values!=before,
                  "retired compact preferences remain compatible without activating the shared weather source");
            const auto preview=nlohmann::json::parse(*session.HandlePage(PageCommand::IslandPreviewCompact));
            const auto previewImage=preview["image"].get<std::string>();
            Check(preview["leftWidget"]==20&&preview["rightWidget"]==1&&
                  preview["error"]==""&&previewImage.rfind("data:image/jpeg;base64,",0)==0,
                  "internal compact preview retains legacy preference metadata without exposing settings controls");
            Check(preview["readings"].size()==30,"preview publishes all compact readings for a page snapshot");
            SYSTEMTIME captured{};GetLocalTime(&captured);
            const auto expectedDate=std::to_string(captured.wMonth)+"/"+std::to_string(captured.wDay);
            for(const auto& reading:preview["readings"]){
                const int id=reading["id"].get<int>();
                if(id==3)Check(reading["available"]&&reading["text"]==expectedDate,"preview date is the real local calendar date");
                if(id>=6&&id<=10)Check(reading["available"]&&reading["progress"].is_number()&&
                    reading["progress"].get<double>()>=0&&reading["progress"].get<double>()<=1,
                    "preview progress is a measured fraction");
            }
            session.ReleasePreview();Pump(900);Check(!weatherActive,"hidden compact choices do not retain the shared weather source");
            GetClientRect(session.Handle(),&r);Check(r.right==IslandPx(236,dpi)&&r.bottom==IslandPx(34,dpi),
                "retired compact choices preserve the normal playback capsule instead of widening it for readouts");
        }
        Click(hwnd,100,15);Pump(900);
        Check(State(session)["expanded"]==true,"real capsule click expands");
        PostMessageW(hwnd,WM_APP+0x6e3,0,0);Pump(60);Check(State(session)["expanded"]==false,"outside click collapses");
        Click(hwnd,100,15);Pump(900);Check(State(session)["expanded"]==true,"capsule expands again");
        GetClientRect(hwnd,&r);Check(r.right==IslandPx(372,dpi)&&r.bottom==IslandPx(156,dpi),"spring settles at the expanded standard size");
        Click(hwnd,186,128);Check(controls==1&&last==dynamic_island::Action::Toggle,"real play pause button routes narrow control");
        Click(hwnd,114,128);Click(hwnd,258,128);Check(controls==3&&last==dynamic_island::Action::Next,"previous next route controls");
        Check(State(session)["hideFullscreen"]==true&&State(session)["size"]=="standard","default display options");
        Check(State(session)["shape"]=="capsule","legacy preferences retain floating capsule by default");
        Check(session.SaveCountdownSettings({"距离新年还有",0,false}),"countdown title settings save through the native session");
        Check(State(session)["countdownTitle"]=="距离新年还有","dynamic island state preserves UTF-8 countdown titles");
        Check(!session.CountdownSettings().countUp,"countdown remains the default for older and new preferences");
        auto countdownColors=session.CountdownSettings();countdownColors.textColor=0xc00089;countdownColors.backgroundColor=0x388561;countdownColors.solidColor=0x5655ae;
        countdownColors.textAlpha=190;countdownColors.backgroundAlpha=128;countdownColors.solidAlpha=80;countdownColors.countUp=true;
        Check(session.SaveCountdownSettings(countdownColors),"countdown color-only changes save atomically");
        const auto colorJson=nlohmann::json::parse(files.values["island-display.json"]);
        Check(colorJson["countdownTextColor"]==0xc00089&&colorJson["countdownBackgroundColor"]==0x388561&&colorJson["countdownSolidColor"]==0x5655ae,
            "all countdown colors persist in the existing display preferences");
        Check(colorJson["countdownTextAlpha"]==190&&colorJson["countdownBackgroundAlpha"]==128&&colorJson["countdownSolidAlpha"]==80,
            "custom color opacity persists with RGB values");
        Check(colorJson["countdownCountUp"]==true,"positive-day mode persists with the countdown settings");
        files.fail=true;countdownColors.solidColor=0xffffff;
        Check(!session.SaveCountdownSettings(countdownColors)&&session.CountdownSettings().solidColor==0x5655ae,"failed color save restores the previous appearance");files.fail=false;
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
        {
            RECT island{};GetWindowRect(session.Handle(),&island);
            const auto overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"STATIC",L"Screenshot overlay",WS_POPUP,
                island.left,island.top,island.right-island.left,island.bottom-island.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            Check(overlay!=nullptr,"create a topmost screenshot overlay probe");
            if(overlay){
                SetWindowPos(overlay,HWND_TOPMOST,island.left,island.top,island.right-island.left,island.bottom-island.top,
                    SWP_NOACTIVATE|SWP_SHOWWINDOW);
                Pump(1400);
                bool overlayAboveIsland=false;
                for(HWND item=GetWindow(session.Handle(),GW_HWNDPREV);item;item=GetWindow(item,GW_HWNDPREV))
                    if(item==overlay){overlayAboveIsland=true;break;}
                Check(overlayAboveIsland,"the island does not reclaim the topmost position over a screenshot overlay");
                DestroyWindow(overlay);
            }
        }
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
        const auto colors=restored.CountdownSettings();
        Check(colors.textColor==0xc00089&&colors.backgroundColor==0x388561&&colors.solidColor==0x5655ae&&
              colors.textAlpha==190&&colors.backgroundAlpha==128&&colors.solidAlpha==80&&colors.countUp,
              "countdown custom colors and opacity survive restart");
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
            {"size","large"},{"material","liquid-glass"},{"countdownStyle","anniversary"},
            {"customization",{{"customIcons",true},{"overview",true},{"left",{{"widget","clock"},{"style","capsule"}}}}}}.dump()+std::string(4500,' ');
        legacyFiles.values["legacy-display.json"]=display;
        shell::DynamicIslandSession legacy(GetModuleHandleW(nullptr),legacyOptions,&legacyFiles);
        Check(legacy.Restore()&&!legacy.Running(),"legacy side/workspace settings never activate a disabled island");
        const auto state=State(legacy);
        Check(!state.contains("customization")&&state["size"]=="large"&&state["material"]=="liquid-glass"&&state["countdownStyle"]=="standard"&&
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
        unsigned retiredSettingsOpened=0;bool screenWeatherActive=false;
        screenOptions.openWeatherSettings=[&]{++retiredSettingsOpened;};
        screenOptions.openPerformanceSettings=[&]{++retiredSettingsOpened;};
        screenOptions.weatherActive=[&](bool active){screenWeatherActive=active;};
        const auto legacySides=nlohmann::json{{"version",1},{"expandedLayout",0},{"screen",0},
            {"leftWidget","memory"},{"rightWidget","weather"},{"autoHide",false}}.dump();
        screenFiles.values["screen-policy-display.json"]=legacySides;
        const auto select=[](int id){return static_cast<PageCommand>(static_cast<int>(PageCommand::IslandScreenFirst)+id);};
        {
            shell::DynamicIslandSession session(GetModuleHandleW(nullptr),screenOptions,&screenFiles);
            Check(session.Restore()&&!session.Running()&&State(session)["screens"]==nlohmann::json({1,2}),"disabled settings list the current screens without creating windows");
            Check(State(session)["expandedLayout"]==0&&State(session)["leftWidget"]==15&&State(session)["rightWidget"]==19&&
                screenFiles.values["screen-policy-display.json"]==legacySides&&!screenWeatherActive,
                "legacy memory and weather choices load unchanged while the empty expanded layout stays empty");
            session.HandlePage(select(1));Check(!session.Running(),"display policy can be saved while the island is disabled");
            session.HandlePage(PageCommand::IslandEnable);session.HandlePage(PageCommand::IslandAutoHideOff);Pump(160);
            Check(IslandWindows().size()==1&&OnScreen(session.Handle(),first),"screen 1 creates one island centered on that screen");
            RECT retiredRect{};GetClientRect(session.Handle(),&retiredRect);
            Check(retiredRect.right<IslandPx(236,static_cast<int>(GetDpiForWindow(session.Handle())))&&!screenWeatherActive,
                "even an empty expanded layout projects old compact readouts out of the real idle island");
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
            for(const auto hwnd:all){
                GetClientRect(hwnd,&retiredRect);
                Check(retiredRect.right<IslandPx(236,static_cast<int>(GetDpiForWindow(hwnd))),
                    "neither monitor restores the old wide compact component capsule");
            }
            Check(!screenWeatherActive,"all-screen projection does not revive the old weather consumer");
            const auto cpu=static_cast<PageCommand>(static_cast<int>(PageCommand::IslandLeftWidgetFirst)+14);
            session.HandlePage(cpu);
            using W=dynamic_island::ExpandedWidget;
            const auto five=dynamic_island::EncodeExpandedLayout({W::Clock,W::System,W::Music,W::CalendarWeather,W::Todos});
            session.HandlePage(ui::ParsePageCommand(L"v1|dynamicIsland|expandedLayout|"+std::to_wstring(five)));
            Check(State(session)["expandedLayout"]==five&&State(session)["leftWidget"]==14&&State(session)["rightWidget"]==19&&screenWeatherActive,
                "five expanded widgets retain their order and request weather for the calendar rather than retired sides");
            const auto reads=inventoryReads;
            for(const HWND hwnd:all)SendMessageW(hwnd,WM_TIMER,1,0);
            Check(inventoryReads==reads+1,"multiple islands poll the display inventory only once per tick");
            const auto other=*std::find_if(all.begin(),all.end(),[&](HWND hwnd){return hwnd!=session.Handle();});
            Click(other,32,10);
            Check(State(session)["expanded"]==true&&retiredSettingsOpened==0&&State(session)["expandedLayout"]==five,
                "clicking the former compact readout opens the unchanged expanded workspace without opening settings");
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
        Check(compactRect.right==IslandPx(260,static_cast<int>(GetDpiForWindow(window.Handle()))),
              "real compact island leaves separate readable left and right lanes");
        const auto nativePreview=window.Preview(display,false);
        Check(nativePreview.width>0&&nativePreview.pixels.pixels.size()==
              static_cast<std::size_t>(nativePreview.width)*nativePreview.height,
              "the production preview uses the same native compact rendering path");
        SaveFrame(nativePreview.pixels,L"compact-widgets-preview.png");
        IslandPreviewRenderer compactRenderer;
        SaveFrame(compactRenderer.Render(nativePreview,display,{}),L"compact-widgets-scene.png");
        for(const auto shape:{IslandShape::Capsule,IslandShape::Standard})
          for(const auto sides:{0,1,2}){
            auto choice=display;choice.shape=shape;
            if(sides==1)choice.rightWidget=dynamic_island::CompactWidget::None;
            if(sides==2)choice.leftWidget=dynamic_island::CompactWidget::None;
            window.SetDisplay(choice);window.Update({playing,L"",L"",0,false,false,false});window.Expand(false);window.Settle();
            const auto withMusic=window.Frame();
            window.Update({{},L"09:41",L"",0,false,false,false});window.Settle();
            Check(window.Frame().pixels==withMusic.pixels,
                  "collapsed side widgets have identical pixels and geometry with or without music");
            const auto preview=window.Preview(choice,false);
            Check(preview.width==PreviewPx(260)&&preview.height==PreviewPx(36),
                  "compact side-widget preview reserves no center music lane");
            window.Update({playing,L"",L"",0,false,false,false});window.Expand(true);window.Settle();
            const auto expandedMusic=window.Frame();
            window.Update({{},L"09:41",L"",0,false,false,false});window.Settle();
            Check(window.Frame().pixels!=expandedMusic.pixels,"expanded side widgets still show available music");
            if(sides==0){
                SaveFrame(withMusic,shape==IslandShape::Capsule?L"compact-side-only-capsule.png":L"compact-side-only-attached.png");
                SaveFrame(expandedMusic,shape==IslandShape::Capsule?L"expanded-side-music-capsule.png":L"expanded-side-music-attached.png");
            }
            window.Update({playing,L"",L"",0,false,false,false});window.Expand(false);window.Settle();
          }
        window.SetDisplay(display);window.Settle();
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
            for(const int x:{20,240}){
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
        {const auto frame=window.Frame();const auto alpha=[&](int x,int y){return frame.pixels[static_cast<std::size_t>(IslandPx(y,static_cast<int>(GetDpiForWindow(window.Handle()))))*frame.width+IslandPx(x,static_cast<int>(GetDpiForWindow(window.Handle())))]>>24;};
         Check(alpha(12,1)>200&&alpha(1,18)==0&&alpha(186,70)>200,"attached contour has top shoulder, transparent side, and solid body");SaveFrame(frame,L"shape-standard-expanded.png");}
        window.Expand(false);window.Settle();SaveFrame(window.Frame(),L"shape-standard-compact.png");
        {
            playing.artwork="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAAEnQAABJ0Ad5mH3gAAAAiSURBVChThcihEQAACAAh919aOx88GjP7SCihhBJKKKEEDoMXf4GjQpJVAAAAAElFTkSuQmCC";
            display.autoHide=false;window.SetDisplay(display);
            window.Update({playing,L"",L"",0,false,false,false});window.Settle();
            const int dpi=static_cast<int>(GetDpiForWindow(window.Handle()));
            const auto px=[&](int dip){return IslandPx(dip,dpi);};
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
                const float scale=size==IslandSize::Small?.86f:size==IslandSize::Large?1.4f:1.2f;
                RECT bounds{};GetClientRect(window.Handle(),&bounds);
                Check(bounds.right==IslandPx(236,dpi,scale)&&bounds.bottom==IslandPx(34,dpi,scale),
                      "both shapes scale artwork, text and native bounds together at every size");
                const auto preview=window.Preview(choice,false);
                Check(preview.width==PreviewPx(236,scale)&&preview.height==PreviewPx(34,scale),
                      "native preview uses the same enlarged size as the desktop island");
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
        {const auto frame=window.Frame();const auto index=static_cast<std::size_t>(IslandPx(1,static_cast<int>(GetDpiForWindow(window.Handle()))))*frame.width+IslandPx(12,static_cast<int>(GetDpiForWindow(window.Handle())));
         Check((frame.pixels[index]>>24)==0,"capsule keeps a convex round top corner");SaveFrame(frame,L"shape-capsule-expanded.png");}
        window.Expand(false);window.Settle();SaveFrame(window.Frame(),L"shape-capsule-compact.png");
        display.size=IslandSize::Small;window.SetDisplay(display);window.Expand(false);window.Settle();
        SaveFrame(window.Frame(),L"compact-small.png");
        display.size=IslandSize::Standard;Check(window.SetDisplay(display),"idle entry uses standard geometry");window.Settle();
        // Idle entry remains available even without a playback session.
        window.Update({{},L"09:41",L"",0,false,false,false});window.Settle();
        const auto clickCentre=[&]{
            RECT bounds{};GetClientRect(window.Handle(),&bounds);
            const int canvasDpi=static_cast<int>(std::lround(static_cast<float>(GetDpiForWindow(window.Handle()))*1.2f));
            Click(window.Handle(),MulDiv(bounds.right/2,96,canvasDpi),MulDiv(bounds.bottom/2,96,canvasDpi));window.Settle();
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
        window.Expand(false);window.Settle();
        const auto warmDeadline=GetTickCount64()+6000;
        while ((!window.PerformanceReading() || !window.PerformanceReading()->cpu) && GetTickCount64()<warmDeadline) Pump(20);
        Check(window.PerformanceReading() && window.PerformanceReading()->cpu,"visible compact performance acquires real CPU readings");
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
int main(int argc,char** argv){
    const bool browserOnly=argc==2&&std::strcmp(argv[1],"--browser-overlay-only")==0;
    if(argc!=1&&!browserOnly)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto name=L"LumeIslandTest-"+std::to_wstring(GetCurrentProcessId());
    HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);Check(desktop!=nullptr,"create isolated desktop");
    std::thread runner([&]{Check(SetThreadDesktop(desktop)!=FALSE,"bind isolated desktop");
        Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;Check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"PNG encoder");
        if(browserOnly)BrowserOverlayScenario();else Scenario();Gdiplus::GdiplusShutdown(token);});runner.join();
    const auto closed=CloseDesktop(desktop);
    if(!closed){
        std::cerr<<"desktop close error="<<GetLastError()<<'\n';
        EnumDesktopWindows(desktop,[](HWND window,LPARAM)->BOOL{
            DWORD pid=0;const DWORD thread=GetWindowThreadProcessId(window,&pid);
            wchar_t type[128]{};GetClassNameW(window,type,128);
            std::wcerr<<L"remaining desktop window pid="<<pid<<L" thread="<<thread<<L" class="<<type<<L'\n';
            return TRUE;
        },0);
    }
    Check(closed!=FALSE,"close isolated desktop");return 0;
}
