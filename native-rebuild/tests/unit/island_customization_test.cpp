#include "apps/shell/dynamic_island_session.h"
#include "core/dynamic_island/island_customization.h"
#include "platform/windows/dynamic_island/island_mini_widgets.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <gdiplus.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <thread>

namespace {
using namespace lume;
using namespace platform::windows;
using ui::PageCommand;
using dynamic_island::ExpandedWidget;
using dynamic_island::MiniWidget;
using dynamic_island::MiniStyle;
void Check(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
struct Files final:settings::IFileStore {
    std::map<std::string,std::string> values;bool fail=false;
    bool Read(const std::string& key,std::string& result)const override{const auto it=values.find(key);if(it==values.end())return false;result=it->second;return true;}
    bool Exists(const std::string& key)const override{return values.count(key)!=0;}
    bool Write(const std::string& key,const std::string& value)override{if(fail)return false;values[key]=value;return true;}
    bool Rename(const std::string& from,const std::string& to)override{if(fail||!Exists(from))return false;values[to]=values[from];values.erase(from);return true;}
    bool Remove(const std::string& key)override{values.erase(key);return true;}
};
PageCommand Command(PageCommand first,std::size_t offset){return static_cast<PageCommand>(static_cast<int>(first)+static_cast<int>(offset));}
nlohmann::json State(const shell::DynamicIslandSession& session){return nlohmann::json::parse(session.State());}
void Click(HWND hwnd,int x,int y){const auto dpi=static_cast<int>(GetDpiForWindow(hwnd));const auto at=MAKELPARAM(MulDiv(x,dpi,96),MulDiv(y,dpi,96));SendMessageW(hwnd,WM_LBUTTONDOWN,0,at);SendMessageW(hwnd,WM_LBUTTONUP,0,at);}
void Save(const WidgetBitmap& frame,const wchar_t* name){
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto dir=std::filesystem::path(exe).parent_path()/L"verification"/L"island-customization";std::filesystem::create_directories(dir);
    Gdiplus::Bitmap image(frame.width,frame.height,static_cast<INT>(frame.width*4),PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(frame.pixels.data())));
    const CLSID encoder={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    Check(image.Save((dir/name).c_str(),&encoder)==Gdiplus::Ok,"write native screenshot");
}
bool Bright(const WidgetBitmap& b){return std::any_of(b.pixels.begin(),b.pixels.end(),[](std::uint32_t p){return ((p>>16)&255)>75||((p>>8)&255)>75||(p&255)>75;});}
void Codec(){
    using namespace dynamic_island;
    Customization defaults;Check(!defaults.customIcons&&!defaults.overview,"legacy behavior remains default");
    Check(defaults.left.widget==MiniWidget::Date&&defaults.right.widget==MiniWidget::Battery,"independent slot defaults");
    const auto parsed=ParseCustomization(SerializeCustomization(defaults));Check(parsed&&*parsed==defaults,"default codec round trip");
    auto changed=defaults;changed.customIcons=true;changed.overview=true;changed.left={MiniWidget::Cat,MiniStyle::Capsule};
    changed.right={MiniWidget::Clock,MiniStyle::Circle};changed.enabled.fill(false);changed.enabled[6]=true;
    Check(changed.MoveTo(ExpandedWidget::Focus,0),"move a widget atomically to first position");
    Check(changed.order.front()==ExpandedWidget::Focus&&changed.order[1]==ExpandedWidget::Music,"atomic move shifts intervening cards");
    Check(changed.MoveTo(ExpandedWidget::Focus,8)&&changed.order.back()==ExpandedWidget::Focus,"atomic move to last position");
    Check(!changed.MoveTo(ExpandedWidget::Focus,9)&&!changed.Move(ExpandedWidget::Focus,1),"out of range moves preserve order");
    const auto roundTrip=ParseCustomization(SerializeCustomization(changed));Check(roundTrip&&*roundTrip==changed,"custom values round trip");
    const auto bad=nlohmann::json::parse(SerializeCustomization(defaults));
    for(int i=0;i<8;++i){auto item=bad;switch(i){
        case 0:item["version"]="1";break;case 1:item["customIcons"]=1;break;
        case 2:item["left"]["widget"]="unknown";break;case 3:item["right"]["style"]="square";break;
        case 4:item["order"][1]=item["order"][0];break;case 5:item["enabled"].erase(item["enabled"].begin());break;
        case 6:item["extra"]=true;break;case 7:item["overview"]=nullptr;break;}
        Check(!ParseCustomization(item.dump()),"reject malformed customization schema");
    }
    Check(!ParseCustomization(std::string(4097,'x'))&&!ParseCustomization("[]"),"bounded object-only parser");
    for(std::size_t i=0;i<MiniWidgetIds.size();++i)Check(ParseMiniWidget(MiniWidgetIds[i])==static_cast<MiniWidget>(i),"stable mini ids");
    Check(!Valid(ParseMiniWidget("date|extra")),"unknown mini id rejected");
    const std::pair<std::wstring,PageCommand> valid[]{
        {L"v1|dynamicIsland|leftWidget|0",PageCommand::IslandLeftWidgetFirst},
        {L"v1|dynamicIsland|rightWidget|30",PageCommand::IslandRightWidgetLast},
        {L"v1|dynamicIsland|leftStyle|capsule",PageCommand::IslandLeftCapsule},
        {L"v1|dynamicIsland|rightStyle|circle",PageCommand::IslandRightCircle},
        {L"v1|dynamicIsland|customIcons|true",PageCommand::IslandCustomIconsOn},
        {L"v1|dynamicIsland|overview|false",PageCommand::IslandOverviewOff},
        {L"v1|dynamicIsland|card|35",PageCommand::IslandCardLast},
        {L"v1|dynamicIsland|cardMove|80",PageCommand::IslandCardMoveLast}};
    for(const auto& entry:valid)Check(ui::ParsePageCommand(entry.first)==entry.second,"new canonical protocol accepted");
    for(const auto* text:{L"v1|dynamicIsland|leftWidget|-1",L"v1|dynamicIsland|leftWidget|31",L"v1|dynamicIsland|leftWidget|01",L"v1|dynamicIsland|rightWidget|0|extra",L"v1|dynamicIsland|card|36",L"v1|dynamicIsland|cardMove|81",L"v1|dynamicIsland|cardMove|00",L"v1|dynamicIsland|leftStyle|Circle",L"v1|dynamicIsland|overview|1"})
        Check(ui::ParsePageCommand(text)==PageCommand::Invalid,"new protocol rejects malformed payloads");
}
void Session(){
    Files files;std::uint64_t now=1000;
    shell::DynamicIslandSessionOptions options;options.preferencesPath="customization.json";options.monotonicMillis=[&]{return now;};
    nlohmann::json saved;
    {
        shell::DynamicIslandSession s(GetModuleHandleW(nullptr),options,&files);Check(s.Restore(),"restore initially disabled");
        s.HandlePage(Command(PageCommand::IslandLeftWidgetFirst,static_cast<std::size_t>(MiniWidget::Cat)));s.HandlePage(PageCommand::IslandLeftCapsule);
        s.HandlePage(Command(PageCommand::IslandRightWidgetFirst,static_cast<std::size_t>(MiniWidget::Cpu)));
        s.HandlePage(Command(PageCommand::IslandCardFirst,6*4));s.HandlePage(Command(PageCommand::IslandCardMoveFirst,6*9));
        saved=State(s)["customization"];
        Check(saved["left"]["widget"]=="cat"&&saved["left"]["style"]=="capsule"&&saved["right"]["widget"]=="cpu","slots update independently");
        Check(saved["customIcons"]==true&&saved["enabled"][6]==true&&saved["order"][0]=="focus","card enable and atomic order persist");
        Check(files.values.count("customization-display.json")==1,"customization saved beside shared preferences");
        s.HandlePage(PageCommand::IslandEnable);s.HandlePage(PageCommand::IslandAutoHideOff);Check(s.Running(),"create real native window from stored settings");
        const auto diskBefore=files.values["customization-display.json"];files.fail=true;
        Check(s.HandlePage(PageCommand::IslandRightCapsule).has_value(),"failure acknowledged");
        Check(State(s)["error"]=="save"&&State(s)["customization"]==saved,"failed save rolls back confirmed settings");
        Check(files.values["customization-display.json"]==diskBefore&&s.Running(),"failed save preserves disk and window");
        files.fail=false;s.HandlePage(PageCommand::IslandLeftWidgetFirst);
        Check(State(s)["customization"]["left"]["widget"]=="none"&&State(s)["customization"]["right"]["widget"]=="cpu","clearing one side preserves the other");
        s.HandlePage(PageCommand::IslandFocus);Check(State(s)["focusing"]==true&&State(s)["remaining"]==1500,"session starts real focus timer");
        now+=61000;SendMessageW(s.Handle(),WM_TIMER,1,0);Check(State(s)["remaining"]==1439,"session timer follows monotonic elapsed time");
        now+=1500000;SendMessageW(s.Handle(),WM_TIMER,1,0);
        Check(State(s)["finished"]==true&&State(s)["focusing"]==false,"late timer completes focus after sleep");
        s.HandlePage(PageCommand::IslandStopFocus);Check(State(s)["remaining"]==0&&State(s)["finished"]==false,"cancel clears completed focus");
        saved=State(s)["customization"];s.Shutdown();
    }
    {
        shell::DynamicIslandSession s(GetModuleHandleW(nullptr),options,&files);
        Check(State(s)["customization"]==saved,"new session restores all customization");
        Check(s.Restore()&&s.Running(),"restored enabled session creates native window");
        s.HandlePage(PageCommand::IslandReset);Check(State(s)["customization"]==nlohmann::json::parse(dynamic_island::SerializeCustomization({})),"reset restores customization defaults");s.Shutdown();
    }
}
void Rendering(){
    IslandMiniData data;data.time.wYear=2026;data.time.wMonth=11;data.time.wDay=1;data.time.wDayOfWeek=0;
    data.time.wHour=10;data.time.wMinute=28;data.time.wSecond=30;data.battery=73.;data.disk=62.;
    IslandPresentation view;view.time=L"10:28";view.date=L"2026年11月1日";view.remaining=1423;view.focusing=true;
    view.media.available=true;view.media.playing=true;view.media.session=1;view.media.id="fixture";view.media.title=L"Native customization";view.media.artist=L"Lume fixture";view.media.position=84;view.media.duration=228;
    dynamic_island::Weather weather;weather.available=true;weather.enabled=true;weather.temperature=23;weather.condition=L"晴";weather.city=L"广州";weather.code=0;weather.humidity=65.;weather.windSpeed=3.2;
    performance::Snapshot metrics;metrics.cpu=14.;metrics.memory=52.;metrics.download=2500000.;const std::optional<performance::Snapshot> sample=metrics;
    WidgetCanvas atlas;Check(atlas.Begin(960,560,96),"create bounded native style atlas");
    for(std::size_t i=1;i<dynamic_island::MiniWidgetIds.size();++i)for(std::size_t style=0;style<2;++style){
        dynamic_island::MiniSelection selection{static_cast<MiniWidget>(i),style?MiniStyle::Capsule:MiniStyle::Circle};
        WidgetCanvas cell;Check(cell.Begin(90,40,96),"create mini canvas");
        DrawIslandMiniWidget(cell,selection,{3,4,IslandMiniWidth(selection),28},data,view,weather,sample,1);
        WidgetBitmap frame;Check(cell.Finish(frame)&&Bright(frame),"each mini type and style draws real foreground");
        const auto index=(i-1)*2+style,x=index%8,y=index/8;
        DrawIslandMiniWidget(atlas,selection,{static_cast<float>(x*120+8),static_cast<float>(y*70+7),IslandMiniWidth(selection),28},data,view,weather,sample,1);
        atlas.Text(std::to_wstring(i)+(style?L" pill":L" circle"),{static_cast<float>(x*120+4),static_cast<float>(y*70+39),110,16},10,0xffffff);
    }
    WidgetBitmap atlasFrame;Check(atlas.Finish(atlasFrame),"finish native atlas");Save(atlasFrame,L"native-mini-styles.png");
    auto prefs=widget::Defaults({260,48,220,44,560,220});prefs.enabled=true;
    IslandDisplay display;display.autoHide=false;display.hideFullscreen=false;display.customization.customIcons=true;
    display.customization.left={MiniWidget::Clock,MiniStyle::Circle};display.customization.right={MiniWidget::Memory,MiniStyle::Capsule};
    dynamic_island::Action last=dynamic_island::Action::Tick;unsigned calendarOpened=0;
    IslandWindow window(GetModuleHandleW(nullptr),[&](auto action){last=action;});
    IslandTodoSource todos;todos.forDay=[](calendar::Date){IslandTodo row;row.id="one";row.title=L"Verify native card";return std::vector<IslandTodo>{row};};todos.openCalendar=[&]{++calendarOpened;};window.SetTodoSource(todos);
    Check(window.Start(prefs,display),"start native custom display");window.Update(view);window.SetWeather(weather);window.Settle();Save(window.Frame(),L"native-mini-capsule.png");
    display.customization.overview=true;display.customization.enabled.fill(true);Check(display.customization.MoveTo(ExpandedWidget::Focus,0),"bring focus card first");
    Check(window.SetDisplay(display),"apply workspace display");window.Expand(true);window.Settle();Check(window.Page()==IslandPage::Overview,"workspace opens as overview");Save(window.Frame(),L"native-workspace.png");
    RECT rect{};GetClientRect(window.Handle(),&rect);const auto dpi=GetDpiForWindow(window.Handle());const int width=MulDiv(rect.right,96,static_cast<int>(dpi));
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window.Handle(),MONITOR_DEFAULTTOPRIMARY),&monitor);Check(rect.right<=monitor.rcWork.right-monitor.rcWork.left,"workspace stays within monitor work width");
    Click(window.Handle(),60,100);window.Settle();Check(window.Page()==IslandPage::Focus,"click workspace focus card opens detail");
    Click(window.Handle(),202,165);Check(last==dynamic_island::Action::CancelFocus,"focus detail routes cancel action");
    window.Expand(false);window.Settle();window.Expand(true);window.Settle();
    SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();Check(window.Page()==IslandPage::Focus,"workspace wheel follows selected order");
    display.customization.overview=false;display.customization.enabled.fill(false);display.customization.enabled[7]=true;
    Check(window.SetDisplay(display),"select only quick launch");window.Settle();Check(window.Page()==IslandPage::Launchpad,"disabled current card safely falls back");
    Click(window.Handle(),202,162);Check(last==dynamic_island::Action::Launchpad,"launchpad button routes launch action");
    display.customization.enabled[7]=false;display.customization.enabled[8]=true;Check(window.SetDisplay(display),"select only quick actions");window.Expand(true);window.Settle();Check(window.Page()==IslandPage::Actions,"quick action page selected");
    Click(window.Handle(),30,75);Check(calendarOpened==1,"calendar quick action uses shared source port");
    display.customization.enabled.fill(false);Check(window.SetDisplay(display),"disable every card");window.Settle();Check(window.Page()==IslandPage::Empty,"all disabled yields safe empty view");
    Save(window.Frame(),L"native-empty-workspace.png");Click(window.Handle(),202,150);Check(last==dynamic_island::Action::Settings,"empty state opens settings");
    Check(width>=220,"workspace has usable logical width");window.Stop();Check(window.Frame().pixels.empty(),"shutdown releases renderer");
}
void Scenario(){Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization");Codec();Session();Rendering();CoUninitialize();std::cout<<"PASS island customization codec, protocol, persistence, rollback, rendering and input\n";}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto name=L"LumeIslandCustomization-"+std::to_wstring(GetCurrentProcessId());const auto desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);Check(desktop!=nullptr,"create isolated desktop");
    std::thread runner([&]{Check(SetThreadDesktop(desktop)!=FALSE,"bind isolated desktop");Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;Check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"PNG encoder");Scenario();Gdiplus::GdiplusShutdown(token);});runner.join();Check(CloseDesktop(desktop)!=FALSE,"close isolated desktop");return 0;
}
