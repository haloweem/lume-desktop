#include "apps/shell/organizer_session.h"
#include "platform/windows/organizer/organizer_text.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/organizer/organizer_view.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
using namespace lume;
using namespace lume::platform::windows;
namespace {
void Check(bool ok,const char* reason){if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}}
void Pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
template<class F> bool Until(F condition){const auto deadline=GetTickCount64()+7000;while(!condition()&&GetTickCount64()<deadline){Pump();Sleep(10);}return condition();}
struct Files:settings::IFileStore {
    std::map<std::string,std::string> data;bool fail=false;unsigned writes=0;
    bool Read(const std::string& p,std::string& value)const override{auto found=data.find(p);if(found==data.end())return false;value=found->second;return true;}
    bool Exists(const std::string& p)const override{return data.count(p)!=0;}
    bool Write(const std::string& p,const std::string& value)override{if(fail)return false;++writes;data[p]=value;return true;}
    bool Rename(const std::string& a,const std::string& b)override{if(fail||!Exists(a))return false;data[b]=data[a];data.erase(a);return true;}
    bool Remove(const std::string& p)override{data.erase(p);return true;}
};
const organizer::Collection* Role(const shell::OrganizerSession& session,organizer::DesktopRole role){
    for(const auto& c:session.Snapshot().collections)if(c.desktopRole==role)return &c;
    return nullptr;
}
void Link(const std::filesystem::path& file,const std::filesystem::path& target){
    IShellLinkW* link=nullptr;Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))),"create local shortcut fixture");
    Check(SUCCEEDED(link->SetPath(target.c_str())),"set fixture shortcut target");IPersistFile* persist=nullptr;
    Check(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persist)))&&SUCCEEDED(persist->Save(file.c_str(),TRUE)),"save fixture shortcut");persist->Release();link->Release();
}
void Tab(shell::OrganizerSession& session,std::size_t index){
    const auto hwnd=session.Handle();RECT r{};GetClientRect(hwnd,&r);const auto dpi=GetDpiForWindow(hwnd);
    OrganizerViewState view;view.tabs={L"桌面",L"文件"};view.activeTab=index?0:1;
    const auto* active=organizer::ActiveTab(session.Snapshot(),"default");
    const auto layout=LayoutOrganizer(static_cast<float>(r.right)*96/dpi,static_cast<float>(r.bottom)*96/dpi,*active,view);
    Check(index<layout.tabs.size(),"default grouped header has two real tab hit regions");
    const auto& tab=layout.tabs[index];const auto point=MAKELPARAM(
        static_cast<int>((tab.x+tab.width/2)*dpi/96),static_cast<int>((tab.y+tab.height/2)*dpi/96));
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,point);SendMessageW(hwnd,WM_LBUTTONUP,0,point);Pump();
}
}
int main(){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto fixture=std::filesystem::path(executable).parent_path()/L"organizer-desktop-fixtures"/std::to_wstring(GetCurrentProcessId());
    const auto user=fixture/L"Desktop",common=fixture/L"Public Desktop";
    std::filesystem::create_directories(user/L"Folder");std::filesystem::create_directories(common);
    for(const auto* name:{L"report.docx",L"photo.png",L"movie.mp4",L"tool.exe",L"hidden.txt"})std::ofstream(user/name)<<"fixture";
    SetFileAttributesW((user/L"hidden.txt").c_str(),FILE_ATTRIBUTE_HIDDEN);
    Link(common/L"Lume.lnk",executable);Link(user/L"Folder link.lnk",user/L"Folder");Link(user/L"Document link.lnk",user/L"report.docx");
    std::ofstream(user/L"Folder"/L"nested.txt")<<"not a desktop entry";
    std::ofstream(fixture/L"outside.mp4")<<"not a desktop entry";
    Files files;unsigned attention=0;
    {
        shell::OrganizerSession session(GetModuleHandleW(nullptr),"organizer.json",[&](const auto&,bool value){attention+=value?1:0;},[]{},&files,{},{},true,{user.wstring(),common.wstring()});
        Check(session.Restore()&&!session.Running()&&files.writes==0,"fresh defaults remain disabled and do not scan or write");
        Check(session.Snapshot().collections.size()==2&&Role(session,organizer::DesktopRole::Applications)->title==u8"桌面"&&
            Role(session,organizer::DesktopRole::Files)->title==u8"文件","first use creates exactly Desktop and Files");
        Check(session.HandlePage(ui::PageCommand::OrganizerEnable).has_value(),"enable grouped organizer");
        const auto filesId=Role(session,organizer::DesktopRole::Files)->id;
        Check(session.Handle()&&!session.Handle(filesId)&&organizer::Host(session.Snapshot(),filesId)->id=="default","one native window contains Desktop and Files tabs");
        Check(organizer::Tabs(session.Snapshot(),"default").size()==2&&organizer::ActiveTab(session.Snapshot(),"default")->id=="default","Desktop tab is shown first");
        Check(Until([&]{return Role(session,organizer::DesktopRole::Applications)->items.size()==2&&Role(session,organizer::DesktopRole::Files)->items.size()==6;}),
            "actual user/public desktop roots classify apps folders documents images videos and shortcut targets");
        Check(Role(session,organizer::DesktopRole::Applications)->iconScale==70&&Role(session,organizer::DesktopRole::Files)->iconScale==70,"both tabs use default 70 percent icons");
        const auto groupedWindow=session.Handle();
        Tab(session,1);Check(organizer::ActiveTab(session.Snapshot(),"default")->id==filesId&&session.Handle()==groupedWindow&&!session.Handle(filesId),"click Files changes content inside the same window");
        Tab(session,0);Check(organizer::ActiveTab(session.Snapshot(),"default")->id=="default"&&session.Handle()==groupedWindow,"click Desktop restores applications without another window");
        for(const auto& c:session.Snapshot().collections)for(const auto& item:c.items)
            Check(item.desktop&&std::filesystem::path(OrganizerWide(item.path)).parent_path()!=fixture,"only direct desktop entries are included");
        const auto appId=Role(session,organizer::DesktopRole::Applications)->items.front().id;
        const auto writes=files.writes;
        organizer::DesktopSnapshot incomplete;session.SynchronizeDesktop(incomplete);
        Check(files.writes==writes&&Role(session,organizer::DesktopRole::Applications)->items.front().id==appId,"incomplete source does not erase or save");
        std::filesystem::rename(user/L"report.docx",user/L"renamed.docx");
        std::filesystem::remove(user/L"photo.png");std::ofstream(user/L"new.pdf")<<"new desktop file";
        SHChangeNotify(SHCNE_UPDATEDIR,SHCNF_PATHW|SHCNF_FLUSH,user.c_str(),nullptr);
        Check(Until([&]{const auto* c=Role(session,organizer::DesktopRole::Files);bool renamed=false,newFile=false,old=false;
            for(const auto& item:c->items){renamed|=item.name=="renamed.docx";newFile|=item.name=="new.pdf";old|=item.name=="photo.png";}
            return renamed&&newFile&&!old;}),"desktop watcher synchronizes rename create and delete");
        Check(std::filesystem::exists(user/L"movie.mp4")&&std::filesystem::exists(common/L"Lume.lnk")&&std::filesystem::exists(user/L"Folder"/L"nested.txt"),"sync leaves original files untouched");
        Check(attention==0,"background synchronization never requests a settings window");
        session.HandlePage(ui::ParsePageCommand(L"v1|organizer|iconScale|83"));
        session.HandlePage(ui::PageCommand::OrganizerLiquidGlass);
        const auto dpi=GetDpiForWindow(session.Handle());RECT before{};GetWindowRect(session.Handle(),&before);
        SetWindowPos(session.Handle(),nullptr,before.left+11,before.top+13,MulDiv(180,dpi,96),MulDiv(500,dpi,96),SWP_NOACTIVATE|SWP_NOZORDER);
        SendMessageW(session.Handle(),WM_EXITSIZEMOVE,0,0);
        Tab(session,1);
        Check(Role(session,organizer::DesktopRole::Applications)->bounds->width==180,"user narrow width saved");
        session.HandlePage(ui::PageCommand::OrganizerDisable);
        Check(!session.Running()&&OrganizerIconCache::ActiveWorkers()==0,"disable releases grouped window and icon workers");
        session.Shutdown();
    }
    {
        shell::OrganizerSession restored(GetModuleHandleW(nullptr),"organizer.json",{},[]{},&files,{},{},true,{user.wstring(),common.wstring()});
        Check(restored.Restore()&&!restored.Running()&&restored.Snapshot().collections.size()==2&&Role(restored,organizer::DesktopRole::Files)->items.size()==6&&
            organizer::Host(restored.Snapshot(),Role(restored,organizer::DesktopRole::Files)->id)->id=="default",
            "classified references and two grouped roles persist after restart");
        const auto* desktop=Role(restored,organizer::DesktopRole::Applications);
        Check(desktop->iconScale==83&&!desktop->frosted&&desktop->bounds&&desktop->bounds->width==180&&
              desktop->bounds->height==500&&Role(restored,organizer::DesktopRole::Files)->iconScale==83&&
              organizer::ActiveTab(restored.Snapshot(),"default")->desktopRole==organizer::DesktopRole::Files,
              "custom icon size material narrow bounds active tab and disabled module survive restart");
    }
    CoUninitialize();std::cout<<"PASS one 70% organizer with Desktop/Files tabs, actual-root classification, file notifications and reference-only persistence\n";
}
