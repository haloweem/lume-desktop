#include "apps/shell/performance_session.h"
#include <map>
#include <iostream>
#include <cstdlib>
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
void Pump(unsigned ms){
    const auto end=GetTickCount64()+ms;
    while(GetTickCount64()<end){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);}
}
}
int main(){
    using lume::ui::PageCommand;
    Expect(lume::ui::ParsePageCommand(L"v1|performance|material|frosted-glass")==PageCommand::PerformanceFrostedGlass,"bridge accepts exact new material command");
    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    Files files;const auto instance=GetModuleHandleW(nullptr);
    {
        lume::shell::PerformanceSession session(instance,"performance.json",{},[]{},&files);
        Expect(session.Restore()&&!session.Running()&&!session.Sample(),"disabled restore is resource free");
        Expect(session.State().find(L"\"result\":\"snapshot\"")!=std::wstring::npos,"state notifications are not command acknowledgements");
        Expect(session.Snapshot().width==340&&session.Snapshot().height==216,"source default size");
        Expect(!session.HandlePage(PageCommand::CalendarEnable),"module does not consume unrelated commands");
        auto reply=session.HandlePage(PageCommand::PerformanceEnable);
        Expect(reply&&session.Running()&&session.Handle(),"enable opens real sampler window");
        Expect(reply->find(L"\"saved\":true")!=std::wstring::npos&&reply->find(L"\"result\":\"applied\"")!=std::wstring::npos,"success acknowledges completed persistence");
        Pump(1500);
        Expect(session.Sample()&&session.Sample()->memory&&session.Sample()->cpu,"native production data delivered");
        Expect(files.values.count("performance.json")==1,"enabled persisted");
        auto before=files.values["performance.json"];files.fail=true;
        reply=session.HandlePage(PageCommand::PerformanceLiquidGlass);
        Expect(reply&&session.Snapshot().frosted&&session.Running(),"save failure rolls back appearance and keeps sampler");
        Expect(reply->find(L"\"saved\":false")!=std::wstring::npos&&reply->find(L"\"error\":\"save\"")!=std::wstring::npos,"failed command is not reported saved");
        Expect(files.values["performance.json"]==before,"save failure preserves previous file");
        files.fail=false;
        session.HandlePage(PageCommand::PerformanceLiquidGlass);
        Expect(!session.Snapshot().frosted,"material applies after save recovers");
        session.HandlePage(PageCommand::PerformanceReset);
        Expect(session.Running()&&session.Snapshot().frosted,"reset retains explicit enabled state and restores frosted glass");
        SendMessageW(session.Handle(),WM_CLOSE,0,0);
        Expect(!session.Running()&&!session.Handle()&&!session.Sample(),"native close stops sampling and window");
        Expect(!session.Snapshot().enabled,"native close persists disabled state");
        session.HandlePage(PageCommand::PerformanceEnable);
        session.Shutdown();Expect(!session.Sample()&&!session.Handle(),"shutdown releases resources");
        Expect(!session.HandlePage(PageCommand::PerformanceEnable),"stopped session cannot resurrect");
    }
    {
        lume::shell::PerformanceSession restored(instance,"performance.json",{},[]{},&files);
        Expect(restored.Restore()&&restored.Running(),"saved enabled restores");
        restored.Shutdown();
    }
    const auto valid=files.values["performance.json"];
    files.values["performance.json.bak"]=valid;files.values["performance.json"]="{broken";
    {
        lume::shell::PerformanceSession recovered(instance,"performance.json",{},[]{},&files);
        Expect(recovered.Restore()&&recovered.Running(),"valid backup recovers");
        Expect(files.values["performance.json"]=="{broken","read-only restore preserves corrupt evidence");
        recovered.HandlePage(PageCommand::PerformanceDisable);
        Expect(files.values["performance.json.corrupt"]=="{broken","explicit save preserves corrupt original");
    }
    CoUninitialize();std::cout<<"performance session production lifecycle and rollback passed\n";
}
