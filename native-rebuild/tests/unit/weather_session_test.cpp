#include "apps/shell/weather_session.h"
#include "apps/shell/performance_session.h"
#include "apps/shell/almanac_session.h"
#include "platform/windows/weather/weather_tooltip.h"
#include "platform/windows/weather/weather_view.h"
#include "platform/windows/material/material_api.h"
#include "weather_fixtures.h"
#include <objbase.h>
#include <commctrl.h>
#include <atomic>
#include <map>
#include <thread>
#include <iostream>
#include <cstring>
using namespace lume;
using namespace lume::platform::windows;
namespace {
constexpr const char* LongPlaceName=u8"黄区 (堪萨斯州里诺县)";
void Expect(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
struct Files:settings::IFileStore {
    std::map<std::string,std::string> data;bool fail=false;
    bool Read(const std::string& p,std::string& out)const override{auto it=data.find(p);if(it==data.end())return false;out=it->second;return true;}
    bool Exists(const std::string& p)const override{return data.count(p)!=0;}
    bool Write(const std::string& p,const std::string& v)override{if(fail)return false;data[p]=v;return true;}
    bool Rename(const std::string& a,const std::string& b)override{if(fail||!Exists(a))return false;data[b]=data[a];data.erase(a);return true;}
    bool Remove(const std::string& p)override{data.erase(p);return true;}
};
void Pump(unsigned milliseconds){
    const auto end=GetTickCount64()+milliseconds;
    do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
        MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
    }while(GetTickCount64()<end);
}
HWND Tooltip(HWND owner){
    HWND found=nullptr;
    for(auto hwnd=GetWindow(owner,GW_HWNDFIRST);hwnd;hwnd=GetWindow(hwnd,GW_HWNDNEXT)){
        wchar_t name[80]{};GetClassNameW(hwnd,name,80);
        if(GetWindow(hwnd,GW_OWNER)==owner&&std::wstring(name)==TOOLTIPS_CLASSW){found=hwnd;break;}
    }
    return found;
}
struct Backend:IWeatherBackend {
    Backend(std::atomic<int>& live,std::atomic<int>& requests,std::atomic<bool>& block):live(live),requests(requests),block(block){++live;}
    ~Backend(){--live;}
    std::optional<weather::Snapshot> Load(std::string_view)override{return {};}
    WeatherFetch Fetch(std::string_view city,const weather::Cancellation& cancel)override{
        ++requests;
        while(block&&!cancel.Cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if(cancel.Cancelled())return {{},false};
        auto snap=weather::ParseMet(weather_fixture::MetBody().dump(),weather_fixture::Shanghai,city,weather_fixture::Now,weather_fixture::Now,weather_fixture::Local);
        if(snap)snap->cityName=LongPlaceName;
        return {{std::move(snap),weather::ProviderError::None,0},false};
    }
    std::atomic<int>& live;std::atomic<int>& requests;std::atomic<bool>& block;
};
}
int main(){
    using ui::PageCommand;
    Expect(ui::ParsePageCommand(L"v1|weather|city|shanghai")==PageCommand::WeatherCityShanghai,"city protocol");
    Expect(ui::ParsePageCommand(L"v1|weather|city|other")==PageCommand::Invalid,"unknown city rejected");
    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    HMODULE glass=LoadLibraryW(L"LumeGlass.dll");Expect(glass!=nullptr,"shared material library");
    auto address=GetProcAddress(glass,"LumeMaterialStatistics");LumeMaterialStatisticsFn statistics=nullptr;
    std::memcpy(&statistics,&address,sizeof(statistics));Expect(statistics!=nullptr,"material diagnostics");
    const auto stats=[&]{LumeMaterialStatisticsData value{};value.size=sizeof(value);Expect(statistics(&value)!=0,"read material diagnostics");return value;};
    std::atomic<int> live=0,requests=0;std::atomic<bool> block=false;Files files;
    weather::Preferences initial;initial.window.locked=true;
    Expect(weather::EncodePreferences(initial,files.data["weather.json"]),"locked input fixture");
    const auto factory=[&]{return std::make_unique<Backend>(live,requests,block);};
    const auto clock=[] {return weather_fixture::Now;};
    {
        shell::WeatherSession session(GetModuleHandleW(nullptr),"weather.json",{},[]{},&files,factory,clock);
        Expect(session.Restore()&&!session.Running()&&live==0,"disabled restore does not allocate backend");
        auto reply=session.HandlePage(PageCommand::WeatherEnable);
        Expect(reply&&reply->find(L"\"saved\":true")!=std::wstring::npos&&session.Running(),"production native window enabled and saved");
        for(int i=0;i<100&&!session.Reading().snapshot;++i)Pump(10);
        Expect(session.Reading().snapshot&&session.Reading().snapshot->cityId=="auto","worker result reaches native UI");
        const auto tip=Tooltip(session.Handle());Expect(tip!=nullptr,"native tooltip attached");
        auto hints=WeatherTips(session.Reading(),weather_fixture::Local);
        Expect(hints[0].find(L"预报")!=std::wstring::npos&&hints[0].find(L"数据发布")!=std::wstring::npos&&
            hints[0].find(L"Asia/Shanghai")!=std::wstring::npos,"overview preserves forecast provenance and location timezone");
        auto reading=session.Reading();reading.snapshot->source=weather::Source::Qweather;
        reading.snapshot->cityName=u8"拉薩市";reading.snapshot->condition=u8"多雲";reading.snapshot->remainingDay=true;
        hints=WeatherTips(reading,weather_fixture::Local);
        Expect(hints[0].find(L"拉萨市")!=std::wstring::npos&&hints[0].find(L"多云")!=std::wstring::npos&&
            hints[0].find(L"不含此前实测温度")!=std::wstring::npos,"tooltips simplify full external text and distinguish remaining-day range");
        Expect(hints[0].find(L"观测")!=std::wstring::npos&&hints[0].find(L"数据发布")==std::wstring::npos,"observation provenance not mislabeled");
        // Tooltip rectangles are in DIP, mouse messages in pixels.
        const auto hwnd=session.Handle();const unsigned dpi=GetDpiForWindow(hwnd);
        RECT rect{};GetClientRect(hwnd,&rect);
        Expect(rect.right==MulDiv(320,static_cast<int>(dpi),96)&&rect.bottom==MulDiv(152,static_cast<int>(dpi),96),
            "original weather default is 320x152 DIP at the window DPI");
        TTTOOLINFOW overview{};overview.cbSize=TTTOOLINFOW_V2_SIZE;overview.hwnd=hwnd;overview.uId=1;
        Expect(SendMessageW(tip,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&overview))!=FALSE,
            "weather overview hover region remains registered");
        wchar_t overviewText[4096]{};overview.lpszText=overviewText;
        SendMessageW(tip,TTM_GETTEXTW,std::size(overviewText),reinterpret_cast<LPARAM>(&overview));
        Expect(std::wstring(overviewText).find(WeatherText(LongPlaceName))!=std::wstring::npos&&
            session.Reading().snapshot->cityName==LongPlaceName,
            "actual native hover retains the complete long location after visible ellipsis");
        TTHITTESTINFOW locationHit{};locationHit.hwnd=hwnd;locationHit.ti.cbSize=TTTOOLINFOW_V2_SIZE;
        locationHit.pt={rect.right-MulDiv(22,static_cast<int>(dpi),96),MulDiv(30,static_cast<int>(dpi),96)};
        Expect(SendMessageW(tip,TTM_HITTESTW,0,reinterpret_cast<LPARAM>(&locationHit))!=FALSE&&locationHit.ti.uId==1,
            "long city label is inside the native overview hover region");
        Expect(overview.rect.top>=MulDiv(24,static_cast<int>(dpi),96),
            "full-location hover region starts below the close control");
        const auto point=MAKELPARAM(rect.right-MulDiv(39,static_cast<int>(dpi),96),MulDiv(12,static_cast<int>(dpi),96));
        const int beforeClick=requests;
        SendMessageW(hwnd,WM_MOUSEMOVE,0,point);
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(10,100));Pump(25);
        Expect(requests==beforeClick,"release outside refresh cancels request");
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(hwnd,WM_LBUTTONUP,0,point);
        Pump(50);
        Expect(requests==beforeClick,"removed header refresh area never dispatches a request");
        TTTOOLINFOW removed{};removed.cbSize=TTTOOLINFOW_V2_SIZE;removed.hwnd=hwnd;removed.uId=2;
        const auto hasRemoved=SendMessageW(tip,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&removed));
        Expect(!hasRemoved||IsRectEmpty(&removed.rect),"removed refresh tooltip has no hit region");
        SendMessageW(hwnd,WM_KEYDOWN,VK_F5,0);
        for(int i=0;i<100&&requests==beforeClick;++i)Pump(5);
        Expect(requests>beforeClick,"F5 refresh remains functional");
        Pump(20);
        Expect(!session.HandlePage(PageCommand::PerformanceEnable),"weather does not consume unrelated commands");
        const auto old=files.data["weather.json"];files.fail=true;
        reply=session.HandlePage(PageCommand::WeatherCityShanghai);
        Expect(reply&&reply->find(L"\"error\":\"save\"")!=std::wstring::npos&&session.Snapshot().city=="auto"&&session.Running(),"city persistence failure restores previous runtime");
        Expect(files.data["weather.json"]==old,"failed city save preserves settings");
        files.fail=false;session.HandlePage(PageCommand::WeatherCityShanghai);
        Expect(!session.Reading().snapshot||session.Reading().snapshot->cityId=="shanghai","switch never displays old city");
        for(int i=0;i<100&&(!session.Reading().snapshot||session.Reading().snapshot->cityId!="shanghai");++i)Pump(10);
        Expect(session.Reading().snapshot&&session.Reading().snapshot->cityId=="shanghai","new city delivered");
        const int calls=requests;session.HandlePage(PageCommand::WeatherFrostedGlass);Pump(30);
        Expect(session.Snapshot().window.frosted&&requests==calls,"material change retains weather backend and avoids fetch");
        block=true;session.HandlePage(PageCommand::WeatherRefresh);Pump(25);
        const auto closePoint=MAKELPARAM(rect.right-MulDiv(16,static_cast<int>(dpi),96),MulDiv(14,static_cast<int>(dpi),96));
        const auto beforeClose=requests.load();const auto start=GetTickCount64();
        SendMessageW(hwnd,WM_MOUSEMOVE,0,closePoint);
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,closePoint);SendMessageW(hwnd,WM_LBUTTONUP,0,closePoint);
        Expect(GetTickCount64()-start<500&&!session.Running()&&!session.Handle()&&live==0&&!session.Reading().snapshot,"close cancels and joins in-flight work");
        Expect(requests==beforeClose,"header close hitbox is separate from refresh");
        Expect(!IsWindow(tip),"close releases tooltip and subclass");
        Expect(stats().users==0&&stats().devices==0&&stats().captures==0,"close releases weather material lease and GPU resources");
        Expect(!session.Snapshot().window.enabled,"native close persisted");
        block=false;
        for(int i=0;i<15;++i){
            session.HandlePage(PageCommand::WeatherEnable);
            Expect(stats().users==1&&stats().devices==1,"one weather session has one material lease");
            session.HandlePage(PageCommand::WeatherDisable);
            Expect(live==0&&!session.Handle(),"repeated lifecycle releases backend and window");
            Expect(stats().users==0&&stats().devices==0&&stats().captures==0,"repeated lifecycle releases shared material resources");
        }
        session.HandlePage(PageCommand::WeatherEnable);session.Shutdown();
        Expect(live==0&&!session.Handle()&&!session.HandlePage(PageCommand::WeatherEnable),"shutdown cannot resurrect");
    }
    {
        shell::WeatherSession restored(GetModuleHandleW(nullptr),"weather.json",{},[]{},&files,factory,clock);
        Expect(restored.Restore()&&restored.Running()&&restored.Snapshot().city=="shanghai"&&restored.Snapshot().window.frosted,"saved city, appearance and enabled restored");
        shell::PerformanceSession performance(GetModuleHandleW(nullptr),"performance.json",{},[]{},&files);
        shell::AlmanacSession almanac(GetModuleHandleW(nullptr),"almanac.json",{},[]{},&files);
        performance.HandlePage(PageCommand::PerformanceEnable);almanac.HandlePage(PageCommand::AlmanacEnable);
        Expect(performance.Running()&&almanac.Running()&&stats().users==3&&stats().devices==1,
            "weather, performance and almanac share one GPU device with independent business lifetimes");
        const auto calendarRevision=almanac.DataRevision();
        restored.HandlePage(PageCommand::WeatherCityBeijing);
        Expect(performance.Running()&&almanac.DataRevision()==calendarRevision&&stats().users==3,"weather city change does not restart other components");
        restored.Shutdown();
        Expect(live==0&&performance.Running()&&almanac.Running()&&stats().users==2&&stats().devices==1,
            "closing weather releases network backend without closing shared device used by other widgets");
        almanac.Shutdown();performance.Shutdown();
        Expect(stats().users==0&&stats().devices==0&&stats().captures==0,"last of three sessions releases all shared material resources");
    }
    {
        weather::Preferences sharedPrefs;sharedPrefs.city="auto";
        Expect(weather::EncodePreferences(sharedPrefs,files.data["shared-weather.json"]),"shared weather fixture");
        shell::WeatherSession shared(GetModuleHandleW(nullptr),"shared-weather.json",{},[]{},&files,factory,clock);
        Expect(shared.Restore()&&!shared.Running()&&!shared.Handle()&&live==0,"no consumer starts no worker");
        const auto saved=files.data["shared-weather.json"];
        Expect(shared.SetSharedConsumer(true),"island starts shared weather data");
        for(int i=0;i<100&&!shared.Reading().snapshot;++i)Pump(10);
        Expect(shared.Reading().snapshot&&live==1&&!shared.Handle()&&!shared.Running(),"island receives weather without a desktop card");
        Expect(files.data["shared-weather.json"]==saved&&!shared.Snapshot().window.enabled&&stats().users==0,"data consumer never enables or saves desktop widget");
        const int calls=requests;shared.HandlePage(PageCommand::WeatherEnable);Pump(30);
        Expect(shared.Running()&&live==1&&requests==calls,"both views reuse one worker and snapshot");
        Expect(shared.SetSharedConsumer(false)&&shared.Running()&&live==1&&shared.Reading().snapshot,"closing island retains weather for the desktop widget");
        Expect(shared.SetSharedConsumer(true)&&live==1&&requests==calls,"reopening island reuses existing weather worker");
        shared.HandlePage(PageCommand::WeatherDisable);Pump(20);
        Expect(!shared.Handle()&&live==1&&shared.Reading().snapshot,"closing desktop weather keeps island data alive");
        shared.HandlePage(PageCommand::WeatherCityBeijing);
        Expect(!shared.Reading().snapshot||shared.Reading().snapshot->cityId=="beijing","shared consumer clears old city immediately");
        for(int i=0;i<100&&(!shared.Reading().snapshot||shared.Reading().snapshot->cityId!="beijing");++i)Pump(10);
        Expect(shared.Reading().snapshot&&shared.Reading().snapshot->cityId=="beijing"&&live==1,"island data follows shared city");
        Expect(shared.SetSharedConsumer(false)&&live==0&&!shared.Reading().snapshot,"last consumer releases shared worker");
        shared.SetSharedConsumer(true);shared.Shutdown();
        Expect(live==0&&!shared.SetSharedConsumer(true),"shutdown prevents shared weather resurrection");
    }
    FreeLibrary(glass);CoUninitialize();std::cout<<"weather session persistence, cancellation and rollback passed\n";
}
