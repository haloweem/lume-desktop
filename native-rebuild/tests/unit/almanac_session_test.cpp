#include "apps/shell/almanac_session.h"
#include "apps/shell/performance_session.h"
#include "platform/windows/material/material_api.h"
#include <map>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <objbase.h>
namespace {
void Expect(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
class Files:public lume::settings::IFileStore{
public:
    bool Read(const std::string& p,std::string& out)const override{auto i=values.find(p);if(i==values.end())return false;out=i->second;return true;}
    bool Exists(const std::string& p)const override{return values.count(p)!=0;}
    bool Write(const std::string& p,const std::string& text)override{if(fail)return false;values[p]=text;return true;}
    bool Rename(const std::string& a,const std::string& b)override{if(fail||!values.count(a))return false;values[b]=values[a];values.erase(a);return true;}
    bool Remove(const std::string& p)override{values.erase(p);return true;}
    std::map<std::string,std::string> values;bool fail=false;
};
}
int main(){
    using lume::ui::PageCommand;
    Expect(lume::ui::ParsePageCommand(L"v1|almanac|enabled|true")==PageCommand::AlmanacEnable,"strict almanac command");
    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    HMODULE glass=LoadLibraryW(L"LumeGlass.dll");Expect(glass!=nullptr,"shared material library");
    auto address=GetProcAddress(glass,"LumeMaterialStatistics");LumeMaterialStatisticsFn statistics=nullptr;
    std::memcpy(&statistics,&address,sizeof(statistics));Expect(statistics!=nullptr,"material diagnostics");
    const auto stats=[&]{LumeMaterialStatisticsData value{};value.size=sizeof(value);Expect(statistics(&value)!=0,"read material diagnostics");return value;};
    Files files;SYSTEMTIME now{};now.wYear=2026;now.wMonth=9;now.wDay=26;now.wHour=23;now.wMinute=59;now.wSecond=59;
    {
        lume::shell::PerformanceSession performance(GetModuleHandleW(nullptr),"performance.json",{},[]{},&files);
        Expect(performance.Restore(),"performance disabled restore");
        performance.HandlePage(PageCommand::PerformanceEnable);
        Expect(performance.Running()&&stats().users==1,"independent real performance session");
        lume::shell::AlmanacSession session(GetModuleHandleW(nullptr),"almanac.json",{},[]{},&files,[&]{return now;});
        Expect(session.Restore()&&!session.Running()&&session.DataRevision()==0,"disabled creates no date calculation or window");
        Expect(!session.HandlePage(PageCommand::PerformanceEnable),"almanac does not control other widgets");
        auto reply=session.HandlePage(PageCommand::AlmanacEnable);
        Expect(reply&&session.Running()&&session.Handle(),"real native almanac starts");
        Expect(stats().users==2&&stats().devices==1,"different business sessions share one material GPU device");
        Expect(reply->find(L"\"saved\":true")!=std::wstring::npos,"successful enable persisted");
        Expect(session.Snapshot().width==320&&session.Snapshot().height==152,"source default size");
        Expect(session.DisplayedDate()==lume::calendar::Date{2026,9,26}&&session.DataRevision()==1,"source data for local date");
        SendMessageW(session.Handle(),WM_TIMECHANGE,0,0);
        Expect(session.DataRevision()==1,"same-date event does not rebuild or reset content");
        now.wDay=27;now.wHour=0;now.wMinute=0;now.wSecond=0;
        const auto deadline=GetTickCount64()+2500;
        while(session.DataRevision()==1&&GetTickCount64()<deadline){
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            Sleep(10);
        }
        Expect(session.DisplayedDate()==lume::calendar::Date{2026,9,27}&&session.DataRevision()==2,"clock/date change refreshes real data");
        now.wDay=26;
        SendMessageW(session.Handle(),WM_TIMECHANGE,0,0);
        Expect(session.DisplayedDate()==lume::calendar::Date{2026,9,26}&&session.DataRevision()==3,"timezone or clock moving backward changes local date");
        SendMessageW(session.Handle(),WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
        Expect(session.DataRevision()==3,"same-date resume does not recalculate");
        const auto before=files.values["almanac.json"];files.fail=true;
        reply=session.HandlePage(PageCommand::AlmanacLiquidGlass);
        Expect(reply&&reply->find(L"\"error\":\"save\"")!=std::wstring::npos&&session.Snapshot().frosted&&session.Running(),"save failure rolls back shared material");
        Expect(files.values["almanac.json"]==before,"failed save preserves prior settings");
        files.fail=false;session.HandlePage(PageCommand::AlmanacFrostedGlass);
        Expect(session.Snapshot().frosted,"frosted material applied");
        SendMessageW(session.Handle(),WM_CLOSE,0,0);
        Expect(!session.Handle()&&!session.Running()&&!session.Snapshot().enabled,"close persists disable and releases timer/window");
        Expect(session.DataRevision()==0,"closed almanac releases date view content");
        Expect(performance.Running()&&stats().users==1&&stats().devices==1,"closing almanac preserves independent sampler and material lease");
        session.HandlePage(PageCommand::AlmanacEnable);
        session.Shutdown();Expect(!session.Handle()&&!session.Running(),"shutdown releases enabled resources");
        Expect(!session.HandlePage(PageCommand::AlmanacEnable),"stopped session cannot resurrect");
        performance.Shutdown();
        Expect(stats().users==0&&stats().devices==0&&stats().captures==0,"last business session releases shared resources");
    }
    {
        lume::shell::AlmanacSession restored(GetModuleHandleW(nullptr),"almanac.json",{},[]{},&files);
        Expect(restored.Restore()&&restored.Running()&&restored.Snapshot().frosted,"restart restores persisted enabled and material");
    }
    FreeLibrary(glass);CoUninitialize();std::cout<<"almanac session lifecycle date update and rollback passed\n";
}
