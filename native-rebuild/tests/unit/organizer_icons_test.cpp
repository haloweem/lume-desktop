#include "platform/windows/organizer/organizer_icons.h"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include "organizer_viewport_test.h"
using namespace lume::platform::windows;
namespace {
void Check(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
template<class F>bool Until(F&& condition){
    const auto deadline=GetTickCount64()+5000;
    while(!condition()&&GetTickCount64()<deadline)Sleep(2);
    return condition();
}
}
int main(){
    Check(OrganizerIconPixelsForDpi(96)==64&&OrganizerIconPixelsForDpi(120)==64&&
        OrganizerIconPixelsForDpi(192)==88&&OrganizerIconPixelsForDpi(288)==132&&
        OrganizerIconPixelsForDpi(UINT_MAX)==256,"DPI request follows physical 44 DIP size with a bounded allocation");
    {
        std::atomic_uint loads{0};
        const auto pixels=std::make_shared<const WidgetBitmap>(WidgetBitmap{1,1,{0xff4477aa}});
        OrganizerIconCache tabs([&](const std::wstring&,const std::atomic_bool&,unsigned){++loads;return pixels;},true);
        std::vector<std::wstring> desktop,files,third;
        for(unsigned i=0;i<120;++i){
            desktop.push_back(L"C:\\Desktop\\"+std::to_wstring(i)+L".lnk");
            files.push_back(L"C:\\Files\\"+std::to_wstring(i)+L".txt");
            third.push_back(L"C:\\Third\\"+std::to_wstring(i)+L".txt");
        }
        Check(tabs.SetPaths(desktop,{}),"warm a full desktop tab");
        Check(Until([&]{return tabs.Find(desktop.back())&&OrganizerIconCache::ActiveWorkers()==0;}),"120 desktop icons load in a finite background batch");
        Check(tabs.SetPaths(files,{}),"load a second full tab");
        Check(Until([&]{return tabs.Find(files.back())&&OrganizerIconCache::ActiveWorkers()==0;}),"second tab completes");
        Check(loads==240&&tabs.CachedPaths()==240&&tabs.Find(desktop.front())==pixels,"switch retains first tab pixels");
        for(unsigned i=0;i<50;++i){
            Check(tabs.SetPaths(i%2?files:desktop,{}),"repeated tab switch accepted");
            Check(tabs.Find(desktop.front())==pixels&&tabs.Find(files.back())==pixels,"warm tab icons available synchronously");
        }
        Check(loads==240&&OrganizerIconCache::ActiveWorkers()==0,"50 warm switches perform zero icon extractions");
        Check(tabs.SetPaths(third,{}),"third tab starts bounded cache eviction");
        Check(Until([&]{return tabs.Find(third.back())&&OrganizerIconCache::ActiveWorkers()==0;}),"third full tab completes");
        Check(tabs.CachedPaths()==MaxIconPaths&&tabs.Find(files.front())&&tabs.Find(files.back()),"LRU eviction preserves recently used complete tab within 256 paths");
        for(const auto& path:third)Check(tabs.Find(path)==pixels,"active tab is never evicted");
        Check(tabs.SetPaths({},{}),"empty active tab accepted");
        Check(tabs.Find(files.front())==pixels&&tabs.CachedPaths()==MaxIconPaths,"empty tab does not flush recent tab icons");
        tabs.Stop();Check(tabs.CachedPaths()==0&&OrganizerIconCache::ActiveWorkers()==0,"closing organizer releases recent icons and workers");
        std::cout<<"PASS 120+120 icons: 50 warm tab switches, zero extractions; 3-tab LRU <=256\n";
    }
    {
        std::atomic_uint loads{0};std::atomic_bool release{false};
        const auto pixels=std::make_shared<const WidgetBitmap>(WidgetBitmap{1,1,{0xff4477aa}});
        std::vector<std::wstring> loaded;
        OrganizerIconCache priority([&](const std::wstring& path,const std::atomic_bool& cancelled,unsigned){
            ++loads;while(!release&&!cancelled)Sleep(2);loaded.push_back(path);return pixels;
        },true);
        Check(priority.SetPaths({L"C:\\old.txt"},{}),"start extraction before rapid switch");
        Check(Until([&]{return loads==1;}),"old tab extraction is in progress");
        Check(priority.SetPaths({L"C:\\visible.txt",L"C:\\offscreen.txt"},{}),"new visible paths replace queued old work");
        release=true;
        Check(Until([&]{return priority.Find(L"C:\\offscreen.txt")&&OrganizerIconCache::ActiveWorkers()==0;}),"priority queue finishes");
        Check(loaded==std::vector<std::wstring>{L"C:\\old.txt",L"C:\\visible.txt",L"C:\\offscreen.txt"},"visible icons load before offscreen icons without parallel Shell flooding");
        Check(priority.Find(L"C:\\old.txt")==pixels,"completed in-flight previous tab result remains reusable");
    }
    {
        std::atomic_uint attempts{0},notices{0};
        const auto recovered=std::make_shared<const WidgetBitmap>(WidgetBitmap{1,1,{0xff55aa77}});
        OrganizerIconCache transient([&](const std::wstring&,const std::atomic_bool&,unsigned){
            return ++attempts==1?std::shared_ptr<const WidgetBitmap>{}:recovered;
        });
        Check(transient.SetPaths({L"C:\\new-shortcut.lnk"},[&]{++notices;}),"queue newly dropped shortcut");
        Check(Until([&]{return transient.Find(L"C:\\new-shortcut.lnk")==recovered&&
            OrganizerIconCache::ActiveWorkers()==0;}),"a transient first extraction recovers within the finite batch");
        Check(attempts==2&&notices==1,"recovery publishes only the usable result and retries once");
        Check(transient.SetPaths({L"C:\\new-shortcut.lnk"},{}),"reuse recovered shortcut");
        Sleep(40);
        Check(attempts==2,"recovered shortcut does not keep a retry thread or decode on repaint");
    }
    {
        std::atomic_uint calls{0};std::atomic_bool unavailable{true};
        const auto recovered=std::make_shared<const WidgetBitmap>(WidgetBitmap{1,1,{0xff55aa77}});
        OrganizerIconCache delayed([&](const std::wstring&,const std::atomic_bool&,unsigned){
            ++calls;return unavailable?std::shared_ptr<const WidgetBitmap>{}:recovered;
        });
        Check(delayed.SetPaths({L"C:\\cold.exe"},{}),"start cold Shell failure fixture");
        Check(Until([&]{return calls==2&&delayed.CachedPaths()==1&&OrganizerIconCache::ActiveWorkers()==0;}),
              "two failed attempts leave a negative cache entry with no idle worker");
        Check(delayed.RetryMissing(),"poll negative result during cooldown");Sleep(20);
        Check(calls==2,"recovery is not retried on every poll or paint");
        unavailable=false;const auto deadline=GetTickCount64()+6500;
        while(!delayed.Find(L"C:\\cold.exe")&&GetTickCount64()<deadline){Check(delayed.RetryMissing(),"retry missing on owner poll");Sleep(20);}
        Check(Until([&]{return delayed.Find(L"C:\\cold.exe")==recovered&&OrganizerIconCache::ActiveWorkers()==0;}),
              "unchanged Dock recovers a temporary cold extraction failure after cooldown");
        Check(calls==3&&delayed.RetryMissing(),"successful recovery does not reload existing icons");
    }
    {
        std::atomic_uint calls{0},requested{0};std::atomic_bool hold{false};
        OrganizerIconCache sized([&](const std::wstring&,const std::atomic_bool& cancelled,unsigned side){
            ++calls;requested=side;
            while(hold&&!cancelled)Sleep(2);
            return std::make_shared<const WidgetBitmap>(WidgetBitmap{side,side,
                std::vector<std::uint32_t>(static_cast<std::size_t>(side)*side,0xff44aa88)});
        });
        Check(sized.SetPaths({L"C:\\dpi.lnk"},{},64),"queue normal-DPI icon");
        Check(Until([&]{return sized.Find(L"C:\\dpi.lnk")&&OrganizerIconCache::ActiveWorkers()==0;}),
            "normal-DPI icon is ready");
        const auto original=sized.Find(L"C:\\dpi.lnk");hold=true;
        Check(sized.SetPaths({L"C:\\dpi.lnk"},{},88),"request 44 DIP at 192 DPI");
        Check(Until([&]{return requested==88;}),"DPI change requests enough physical icon pixels");
        Check(sized.Find(L"C:\\dpi.lnk")==original,"DPI refresh keeps the old visible icon while loading");
        hold=false;
        Check(Until([&]{const auto pixels=sized.Find(L"C:\\dpi.lnk");
            return pixels&&pixels->width==88&&OrganizerIconCache::ActiveWorkers()==0;}),
            "larger icon atomically replaces the cached bitmap");
        Check(sized.CachedPaths()==1&&calls==2,"one cached bitmap per path across DPI changes");
        Check(sized.SetPaths({L"C:\\dpi.lnk"},{},88),"repeat current-DPI paths");
        Sleep(40);Check(calls==2,"current-DPI cache does not extract again");
    }
    {
        std::atomic_uint calls{0},notices{0};std::atomic_bool hold{true};
        OrganizerIconCache crossing([&](const std::wstring&,const std::atomic_bool& cancelled,unsigned side){
            ++calls;
            while(hold&&!cancelled)Sleep(2);
            return std::make_shared<const WidgetBitmap>(WidgetBitmap{side,side,
                std::vector<std::uint32_t>(static_cast<std::size_t>(side)*side,0xff4488aa)});
        });
        const auto notify=[&]{++notices;};
        Check(crossing.SetPaths({L"C:\\crossing.lnk"},notify,64),"start cold icon before DPI change");
        Check(Until([&]{return calls==1;}),"old-DPI extraction entered");
        Check(crossing.SetPaths({L"C:\\crossing.lnk"},notify,88),"DPI change while an old extraction is running");
        hold=false;
        Check(Until([&]{const auto pixels=crossing.Find(L"C:\\crossing.lnk");
            return pixels&&pixels->width==88&&OrganizerIconCache::ActiveWorkers()==0;}),
            "in-flight old-DPI extraction cannot overwrite current-DPI pixels");
        Check(calls==2&&notices==1,"discarded old-DPI result is not published or retained");
    }
    std::atomic_uint calls{0},notices{0};
    std::atomic_bool hold{true};
    const auto owner=GetCurrentThreadId();
    const auto pixel=std::make_shared<const WidgetBitmap>(WidgetBitmap{1,1,{0xff4488aa}});
    OrganizerIconCache cache([&](const std::wstring& path,const std::atomic_bool& cancelled,unsigned){
        Check(GetCurrentThreadId()!=owner,"shell work never executes on paint/UI thread");
        ++calls;
        while(hold&&!cancelled)Sleep(2);
        return path==L"C:\\missing.txt"?std::shared_ptr<const WidgetBitmap>{}:pixel;
    });
    Check(OrganizerIconCache::ActiveWorkers()==0,"construction has no worker");
    Check(cache.SetPaths({L"C:\\one.txt",L"C:\\missing.txt"},[&]{++notices;}),"queue initial icons");
    Check(Until([&]{return calls>0;}),"loader started");
    Check(!cache.Find(L"C:\\one.txt"),"paint lookup does not wait for extraction");
    Check(cache.SetPaths({L"C:\\two.txt",L"C:\\missing.txt"},[&]{++notices;}),"replace queue during extraction");
    hold=false;
    Check(Until([&]{return cache.CachedPaths()==2;}),"replacement paths resolved");
    Check(!cache.Find(L"C:\\one.txt")&&cache.Find(L"C:\\two.txt")==pixel,"obsolete result is not retained");
    Check(notices>0&&calls==4,"serial bounded work retries a negative result only once");
    Check(Until([&]{return OrganizerIconCache::ActiveWorkers()==0;}),
        "a resolved collection retains pixels without retaining an idle thread");
    Check(cache.SetPaths({L"C:\\two.txt",L"C:\\missing.txt"},[&]{++notices;}),"same visible paths");
    Sleep(40);
    Check(calls==4,"idle and failed paths do not retry on every frame");
    Check(cache.SetPaths({L"C:\\two.txt",L"C:\\three.txt"},[&]{++notices;}),"new path restarts a completed extraction batch");
    Check(Until([&]{return cache.Find(L"C:\\three.txt")&&OrganizerIconCache::ActiveWorkers()==0;})&&calls==5,
        "a later batch starts only missing work and stops again");
    cache.Stop();
    Check(cache.CachedPaths()==0&&OrganizerIconCache::ActiveWorkers()==0,"close clears worker and bitmap cache");
    hold=true;
    Check(cache.SetPaths({L"C:\\blocked.txt"},{}),"restart with cancellable load");
    Check(Until([&]{return calls==6;}),"blocked load entered");
    const auto at=GetTickCount64();
    cache.Stop();
    Check(GetTickCount64()-at<1000&&OrganizerIconCache::ActiveWorkers()==0,"stop signals loader before joining");
    {
        OrganizerIconCache limitCache([&](const std::wstring&,const std::atomic_bool&,unsigned){
            return pixel;
        });
        std::vector<std::wstring> duplicates(121,L"C:\\file.txt");
        Check(limitCache.SetPaths(duplicates,{}),"duplicate paths count once");
        Check(Until([&]{return limitCache.Find(L"C:\\file.txt")==pixel&&
            OrganizerIconCache::ActiveWorkers()==0;}),"duplicate paths resolve");
        std::vector<std::wstring> tooMany;
        tooMany.reserve(MaxIconPaths+1);
        for(std::size_t i=0;i<=MaxIconPaths;++i)
            tooMany.push_back(L"C:\\limit\\"+std::to_wstring(i)+L".txt");
        Check(!limitCache.SetPaths(tooMany,{})&&OrganizerIconCache::ActiveWorkers()==0,
            "oversized unique queue rejected before allocation");
        Check(limitCache.Find(L"C:\\file.txt")==pixel,
            "rejected oversized update retains the previous visible cache");
        limitCache.Stop();
    }
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM for shell extraction test");
    Check(VerifyOrganizerViewport(),"partially visible organizer rows render and hit inside viewport");
    wchar_t directory[32768]{};
    Check(GetWindowsDirectoryW(directory,32768)!=0,"Windows directory");
    std::atomic_bool cancelled{false};
    const auto icon=ReadOrganizerIcon(std::wstring(directory)+L"\\explorer.exe",cancelled);
    Check(icon&&icon->width>0&&icon->height>0&&icon->width<=256&&icon->height<=256,
        "actual executable shell icon decoded");
    unsigned ink=0;
    for(const auto color:icon->pixels){
        const auto alpha=color>>24;
        Check(((color>>16)&255)<=alpha&&((color>>8)&255)<=alpha&&(color&255)<=alpha,
              "shell icon uses premultiplied transparent pixels");
        if(alpha)++ink;
    }
    Check(ink>0,"shell icon contains visible pixels");
    cancelled=true;
    Check(!ReadOrganizerIcon(std::wstring(directory)+L"\\explorer.exe",cancelled),"pre-cancelled extraction is skipped");
    CoUninitialize();
    std::cout<<"organizer asynchronous icon queue, stale-result discard and real shell pixels passed\n";
}
