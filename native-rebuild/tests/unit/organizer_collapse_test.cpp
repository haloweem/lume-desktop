#include "apps/shell/organizer_session.h"
#include "platform/windows/organizer/organizer_text.h"
#include "platform/windows/organizer/organizer_view.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "unique_test_directory.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <windowsx.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace lume;
using namespace platform::windows;
using ui::PageCommand;
constexpr int StripWidth=260,StripHeight=44;

void Check(bool value,const char* description){
    if(!value)throw std::runtime_error(description);
}
struct Files final:settings::IFileStore {
    std::map<std::string,std::string> values;
    bool failWrites=false;
    bool Read(const std::string& path,std::string& out)const override{
        const auto found=values.find(path);if(found==values.end())return false;out=found->second;return true;
    }
    bool Exists(const std::string& path)const override{return values.count(path)!=0;}
    bool Write(const std::string& path,const std::string& value)override{
        if(failWrites)return false;values[path]=value;return true;
    }
    bool Rename(const std::string& from,const std::string& to)override{
        if(failWrites||!Exists(from))return false;values[to]=values[from];values.erase(from);return true;
    }
    bool Remove(const std::string& path)override{values.erase(path);return true;}
};
void Pump(){
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
}
void PumpFor(DWORD milliseconds){
    const auto deadline=GetTickCount64()+milliseconds;
    while(GetTickCount64()<deadline){Pump();Sleep(20);}
    Pump();
}
void WaitForIcons(){
    const auto deadline=GetTickCount64()+8000;
    while(OrganizerIconCache::ActiveWorkers()!=0&&GetTickCount64()<deadline){
        Pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);
    }
    Pump();Check(OrganizerIconCache::ActiveWorkers()==0,"isolated organizer icon workers shut down");
}
void Size(HWND window,int width,int height,const char* description){
    RECT bounds{};Check(window&&GetWindowRect(window,&bounds),"organizer window exists");
    const auto dpi=static_cast<int>(GetDpiForWindow(window));
    const int actualWidth=MulDiv(bounds.right-bounds.left,96,dpi?dpi:96);
    const int actualHeight=MulDiv(bounds.bottom-bounds.top,96,dpi?dpi:96);
    if(actualWidth!=width||actualHeight!=height){
        std::cerr<<description<<": expected "<<width<<'x'<<height<<", got "<<actualWidth<<'x'<<actualHeight<<'\n';
        Check(false,description);
    }
}
const organizer::Collection& Host(const shell::OrganizerSession& session){
    const auto* host=organizer::Find(session.Snapshot(),organizer::DefaultCollectionId);
    Check(host!=nullptr,"default collection remains present");return *host;
}
void WaitForStrip(HWND window){
    const auto deadline=GetTickCount64()+1500;
    const auto height=MulDiv(StripHeight,GetDpiForWindow(window),96);
    RECT rect{};
    while(GetClientRect(window,&rect)&&rect.bottom!=height&&GetTickCount64()<deadline)PumpFor(16);
    Size(window,StripWidth,StripHeight,"the closing transition settles at the compact HWND size");
}
nlohmann::json Snapshot(const shell::OrganizerSession& session){return nlohmann::json::parse(OrganizerUtf8(session.State()));}
void Command(HWND window,UINT command){SendMessageW(window,WM_COMMAND,command,0);Pump();}
LPARAM Point(HWND window,float x,float y){
    const auto dpi=static_cast<int>(GetDpiForWindow(window));
    return MAKELPARAM(MulDiv(static_cast<int>(x),dpi?dpi:96,96),MulDiv(static_cast<int>(y),dpi?dpi:96,96));
}
void Click(HWND window,float x,float y){
    SetFocus(window);const auto point=Point(window,x,y);
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,point);
    SendMessageW(window,WM_LBUTTONUP,0,point);Pump();
}
void Wheel(HWND window,float x,float y,int detents){
    const auto client=Point(window,x,y);POINT screen{GET_X_LPARAM(client),GET_Y_LPARAM(client)};
    ClientToScreen(window,&screen);
    SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(WHEEL_DELTA*detents)),MAKELPARAM(screen.x,screen.y));Pump();
}
void LoseFocus(HWND window){
    // Exercise the actual focus-loss handler without moving the system cursor
    // or activating any existing user application.
    SetFocus(nullptr);SendMessageW(window,WM_KILLFOCUS,0,0);Pump();WaitForStrip(window);
}
void ClickStripPart(shell::OrganizerSession& session,OrganizerHit::Kind kind){
    const auto& collection=Host(session);
    for(int y=2;y<StripHeight-2;y+=3)for(int x=2;x<StripWidth-2;x+=3){
        if(HitOrganizer(StripWidth,StripHeight,collection,static_cast<float>(x),static_cast<float>(y)).kind==kind){
            Click(session.Handle(),static_cast<float>(x),static_cast<float>(y));return;
        }
    }
    Check(false,"collapsed strip exposes the expected title/header hit area");
}
organizer::State Seed(const std::filesystem::path& item,int width,int height){
    auto state=organizer::Defaults();auto& collection=state.collections.front();collection.enabled=true;
    collection.bounds=organizer::Bounds{80,80,width,height};
    collection.items.push_back({"12345678-1234-1234-1234-123456789001",item.u8string(),"collapse fixture.txt",organizer::ItemKind::File});
    return state;
}
void SaveSeed(Files& files,const std::string& path,const organizer::State& state){
    Check(organizer::Encode(state,files.values[path]),"isolated organizer seed encodes");
}
void CheckStored(Files& files,const std::string& path,organizer::Mode mode,int width,int height){
    organizer::Store reader(files,OrganizerPathKey);const auto loaded=reader.Load(path);
    const auto* collection=organizer::Find(loaded.value,organizer::DefaultCollectionId);
    Check(loaded.state==organizer::LoadState::Loaded&&collection&&collection->mode==mode&&collection->bounds&&
        collection->bounds->width==width&&collection->bounds->height==height,
        "stored mode and original expanded dimensions survive the strip transition");
}
// Read the submitted layered foreground, including its one-alpha hit pane.
// The HWND already occupies its final bounds throughout the reveal.
int PaintedHeight(HWND window){
    RECT r{};Check(GetClientRect(window,&r)!=FALSE,"read organizer client");
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;const auto dc=CreateCompatibleDC(nullptr);
    const auto dib=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    Check(dc&&dib&&bits,"allocate organizer frame readback");
    const auto old=SelectObject(dc,dib);
    SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();
    const auto* pixels=static_cast<const std::uint32_t*>(bits);
    int height=r.bottom;
    while(height>0&&(pixels[(height-1)*r.right+r.right/3]>>24)==0)--height;
    SelectObject(dc,old);DeleteObject(dib);DeleteDC(dc);
    return height;
}
void UnfoldFrames(const std::filesystem::path& item){
    Files files;const std::string path="unfold-frames.json";auto state=Seed(item,420,392);
    state.collections.front().mode=organizer::Mode::Collapsed;SaveSeed(files,path,state);
    shell::OrganizerSession session(GetModuleHandleW(nullptr),path,{},[]{},&files);
    Check(session.Restore(),"unfold frame fixture restores");const auto window=session.Handle();
    const int compact=PaintedHeight(window);
    const auto saved=files.values.at(path);
    BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    SendMessageW(window,WM_KEYDOWN,VK_SPACE,0);
    RECT expanded{};GetClientRect(window,&expanded);
    const int first=PaintedHeight(window);
    if(animations){
        Check(first==compact,"opening starts with only the folded strip painted and interactive");
        bool intermediate=false;const auto deadline=GetTickCount64()+1500;
        while(GetTickCount64()<deadline){
            Pump();const int current=PaintedHeight(window);
            if(current>compact&&current<expanded.bottom)intermediate=true;
            if(current==expanded.bottom)break;
            Sleep(8);
        }
        Check(intermediate,"opening presents an intermediate body size");
    }else Check(first==expanded.bottom,"reduced motion immediately presents the final body");
    Check(PaintedHeight(window)==expanded.bottom&&files.values.at(path)==saved,
        "temporary animation reaches full height without a preference write");
    SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
    if(animations){
        Check(PaintedHeight(window)==expanded.bottom,"closing starts from the presented full panel");
        int middle=expanded.bottom;const auto deadline=GetTickCount64()+1500;
        do{PumpFor(16);middle=PaintedHeight(window);}while(middle==expanded.bottom&&GetTickCount64()<deadline);
        Check(middle>compact&&middle<expanded.bottom,"closing renders an intermediate frame");
        SendMessageW(window,WM_KEYDOWN,VK_RETURN,0);
        Check(PaintedHeight(window)==middle,"reopening mid-close retains the presented frame");
        SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
        Check(PaintedHeight(window)==middle,"reversing an opening starts at its current frame");
    }else Check(PaintedHeight(window)==compact,"reduced motion immediately closes the panel");
    WaitForStrip(window);
    Check(PaintedHeight(window)==compact,"an interrupted opening stays folded after queued ticks");
    SendMessageW(window,WM_COMMAND,5,0);
    Check(PaintedHeight(window)==(animations?compact:expanded.bottom),"persistent unfold uses the same reveal");
    CheckStored(files,path,organizer::Mode::Expanded,420,392);
    PumpFor(350);
    SendMessageW(window,WM_COMMAND,4,0);
    CheckStored(files,path,organizer::Mode::Collapsed,420,392);
    Check(PaintedHeight(window)==(animations?expanded.bottom:compact),"menu fold follows the same return transition");
    WaitForStrip(window);
    session.Shutdown();WaitForIcons();
}
void SingleCollection(const std::filesystem::path& item,int width,int height){
    Files files;const std::string path="single-"+std::to_string(width)+".json";
    SaveSeed(files,path,Seed(item,width,height));unsigned opened=0;
    const auto open=[&](HWND,const std::wstring&){++opened;return true;};
    {
        shell::OrganizerSession session(GetModuleHandleW(nullptr),path,{},[]{},&files,open);
        Check(session.Restore()&&session.Running(),"custom-size organizer restores");
        const HWND window=session.Handle();Size(window,width,height,"existing custom dimensions stay unchanged");
        Command(window,4);WaitForStrip(window);
        Check(Host(session).mode==organizer::Mode::Collapsed,"command 4 enables folding");
        Size(window,StripWidth,StripHeight,"folding uses a 260 by 44 title strip");
        session.DropHoverForTest(organizer::DefaultCollectionId,POINT{24,20},true);
        session.DropHoverForTest(organizer::DefaultCollectionId,POINT{24,20},false);
        PumpFor(550);
        Size(window,StripWidth,StripHeight,"quick drop enter/leave cancels pending expansion");
        SetFocus(nullptr);const auto priorFocus=GetFocus();
        session.DropHoverForTest(organizer::DefaultCollectionId,POINT{24,20},true);
        PumpFor(200);
        Size(window,StripWidth,StripHeight,"short drop hover does not expand the compact strip");
        PumpFor(400);
        Size(window,width,height,"sustained drop hover temporarily expands the compact strip");
        Check(GetFocus()==priorFocus,"drop hover expansion does not take keyboard focus");
        session.DropHoverForTest(organizer::DefaultCollectionId,POINT{24,20},false);
        Pump();WaitForStrip(window);
        Size(window,StripWidth,StripHeight,"leaving a drop hover refolds the temporary strip");
        const auto state=Snapshot(session);
        Check(state.at("mode")=="collapsed"&&state.at("width")==StripWidth&&state.at("height")==StripHeight,
            "host snapshot reports the exact collapsed geometry");
        CheckStored(files,path,organizer::Mode::Collapsed,width,height);
        const auto saved=files.values.at(path);
        for(const auto part:{OrganizerHit::Kind::Title,OrganizerHit::Kind::Header}){
            ClickStripPart(session,part);
            Size(window,width,height,"clicking the strip temporarily restores saved expanded dimensions");
            Check(Host(session).mode==organizer::Mode::Collapsed&&files.values.at(path)==saved,
                "temporary opening does not persist an expanded mode or overwrite geometry");
            LoseFocus(window);Size(window,StripWidth,StripHeight,"focus loss refolds the temporary window");
        }
        files.failWrites=true;Command(window,5);
        Check(Host(session).mode==organizer::Mode::Collapsed&&Snapshot(session).at("error")=="save"&&files.values.at(path)==saved,
            "failed unfolding save rolls back stored mode and file");
        Size(window,StripWidth,StripHeight,"failed unfolding also restores actual collapsed size");
        files.failWrites=false;Command(window,5);
        Check(Host(session).mode==organizer::Mode::Expanded,"command 5 disables folding");
        Size(window,width,height,"unfolding restores the custom dimensions");
        const auto expandedSaved=files.values.at(path);
        files.failWrites=true;Command(window,4);
        Check(Host(session).mode==organizer::Mode::Expanded&&files.values.at(path)==expandedSaved&&Snapshot(session).at("error")=="save",
            "failed folding save rolls back to expanded state");
        Size(window,width,height,"failed folding also restores actual expanded dimensions");
        files.failWrites=false;
        const auto oldScale=Host(session).iconScale;
        for(const auto command:{150u,160u,200u,220u})Command(window,command);
        Check(Host(session).iconScale==oldScale&&files.values.at(path)==expandedSaved,"removed context-menu icon-size commands are inert");
        const auto scale=static_cast<PageCommand>(static_cast<int>(PageCommand::OrganizerIconScaleFirst)+23);
        const auto reply=session.HandlePage(scale);
        Check(reply&&nlohmann::json::parse(OrganizerUtf8(*reply)).value("saved",false)&&Host(session).iconScale==73,
            "the settings-page icon-size command remains functional");
        const auto savedWithoutMarker=files.values.at(path);
        Command(window,234);
        Check(files.values.at(path)==savedWithoutMarker,"retired color commands are inert");
        Command(window,6);Command(window,4);
        Check(Host(session).layout==organizer::Layout::List&&Host(session).mode==organizer::Mode::Collapsed,"folding preserves list mode");
        Command(window,7);
        Check(Host(session).layout==organizer::Layout::Icons&&Host(session).mode==organizer::Mode::Collapsed,"choosing icons preserves folding");
        Command(window,5);
        Check(Host(session).layout==organizer::Layout::Icons&&Host(session).mode==organizer::Mode::Expanded,"unfolding preserves icon layout");
        Command(window,4);CheckStored(files,path,organizer::Mode::Collapsed,width,height);
        session.Shutdown();WaitForIcons();Check(!IsWindow(window),"single organizer releases its window");
    }
    {
        shell::OrganizerSession reopened(GetModuleHandleW(nullptr),path,{},[]{},&files,open);
        Check(Host(reopened).mode==organizer::Mode::Collapsed&&reopened.Restore(),"folding remains enabled after session recreation");
        Size(reopened.Handle(),StripWidth,StripHeight,"recreated session restores the title strip");
        Command(reopened.Handle(),5);Size(reopened.Handle(),width,height,"recreated session restores the saved expanded size");
        Check(Host(reopened).items.size()==1&&Host(reopened).iconScale==73&&Host(reopened).accent==organizer::Accent::None,
            "folding and restart retain contents, settings-page icon scale and marker color");
        reopened.Shutdown();WaitForIcons();
    }
    Check(opened==0,"title-strip interactions never launch the referenced file");
}
OrganizerViewState GroupView(const organizer::State& state){
    OrganizerViewState view;const auto tabs=organizer::Tabs(state,organizer::DefaultCollectionId);
    const auto* active=organizer::ActiveTab(state,organizer::DefaultCollectionId);
    for(std::size_t index=0;index<tabs.size();++index){view.tabs.push_back(OrganizerWide(tabs[index]->title));if(tabs[index]==active)view.activeTab=index;}
    return view;
}
void GroupAndNewCollection(const std::filesystem::path& item){
    Files files;const std::string path="groups.json";auto state=Seed(item,520,400);
    state.collections.front().title="Tab 1";
    for(unsigned index=2;index<=8;++index){
        const auto id="12345678-1234-1234-1234-12345678900"+std::to_string(index);
        Check(organizer::CreateCollection(state,id,"Tab "+std::to_string(index))==organizer::Change::Changed,"group fixture creates tabs");
        Check(organizer::MergeGroup(state,id,organizer::DefaultCollectionId)==organizer::Change::Changed,"group fixture joins tabs without cursor input");
    }
    organizer::ActivateTab(state,organizer::DefaultCollectionId,organizer::DefaultCollectionId);
    SaveSeed(files,path,state);unsigned opened=0;
    shell::OrganizerSession session(GetModuleHandleW(nullptr),path,{},[]{},&files,[&](HWND,const std::wstring&){++opened;return true;});
    Check(session.Restore()&&session.Running(),"tab group restores");
    const auto window=session.Handle();Command(window,4);WaitForStrip(window);
    Check(std::all_of(session.Snapshot().collections.begin(),session.Snapshot().collections.end(),[](const auto& collection){
        return collection.mode==organizer::Mode::Collapsed;
    }),"folding mode propagates to grouped tabs");
    auto view=GroupView(session.Snapshot());
    auto layout=LayoutOrganizer(StripWidth,StripHeight,*organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId),view);
    Check(layout.tabs.size()==8&&layout.maxTabScroll>0&&layout.cells.empty(),"folded groups render scrollable tabs without item cells");
    const auto second=layout.tabs[1];
    const float secondX=std::min(second.x+second.width/2,layout.tabStrip.x+layout.tabStrip.width-4);
    const float tabY=layout.tabStrip.y+layout.tabStrip.height/2;
    Check(HitOrganizer(StripWidth,StripHeight,*organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId),secondX,tabY,view).kind==OrganizerHit::Kind::Tab,
        "second compact tab has a visible hit region");
    const auto secondId=organizer::Tabs(session.Snapshot(),organizer::DefaultCollectionId)[1]->id;
    Click(window,secondX,tabY);
    Check(organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id==secondId,"single compact-tab click activates the selected tab");
    Size(window,520,400,"compact-tab click also temporarily expands the group");
    LoseFocus(window);Size(window,StripWidth,StripHeight,"group folds again after focus leaves");
    const auto hoverPosition=Point(window,layout.tabs[0].x+30,tabY);
    session.DropHoverForTest(organizer::DefaultCollectionId,POINT{GET_X_LPARAM(hoverPosition),GET_Y_LPARAM(hoverPosition)},true);
    PumpFor(600);
    Check(organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id==organizer::DefaultCollectionId,
        "drop hover over another compact tab selects that target");
    Size(window,520,400,"drop hover over a compact tab opens its saved expanded dimensions");
    session.DropHoverForTest(organizer::DefaultCollectionId,{},false);Pump();WaitForStrip(window);
    Size(window,StripWidth,StripHeight,"leaving the tab drop target refolds the group");
    const auto selectedBefore=organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id;
    Wheel(window,layout.tabStrip.x+30,tabY,-8);
    Check(organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id==selectedBefore,
        "scrolling compact tabs alone preserves the active tab");
    Size(window,StripWidth,StripHeight,"tab-wheel scrolling does not expand the strip");
    Click(window,layout.tabStrip.x+30,tabY);
    Check(organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id!=organizer::DefaultCollectionId,
        "wheel scrolling exposes a later tab at the left edge");
    Size(window,520,400,"a scrolled compact tab still expands to the saved group size");
    LoseFocus(window);Command(window,5);
    const auto originalCount=session.Snapshot().collections.size();Command(window,3);
    Check(session.Snapshot().collections.size()==originalCount+1,"new collection command creates one box");
    const auto& created=session.Snapshot().collections.back();
    Check(created.parent.empty()&&created.mode==organizer::Mode::Expanded&&created.bounds&&created.bounds->width==260&&created.bounds->height==680,
        "new boxes default to 260 by 680 expanded geometry");
    Size(session.Handle(created.id),260,680,"new collection window uses the new defaults");
    Size(window,520,400,"creating a box does not resize the existing customized group");
    Check(Host(session).items.size()==1&&opened==0,"group folding, scrolling and creation retain original items without opening them");
    session.Shutdown();WaitForIcons();
}
void ScrollPreservation(const std::filesystem::path& root){
    Files files;const std::string path="scroll.json";auto state=organizer::Defaults();
    auto& collection=state.collections.front();collection.enabled=true;collection.layout=organizer::Layout::List;
    collection.bounds=organizer::Bounds{90,100,420,392};
    for(unsigned index=0;index<48;++index){
        const auto name="scroll-"+std::to_string(index)+".txt";const auto item=root/OrganizerWide(name);
        {std::ofstream file(item,std::ios::binary);file<<"isolated scroll fixture\n";Check(file.good(),"write harmless scroll fixture");}
        const auto suffix=std::to_string(index);
        collection.items.push_back({"12345678-1234-1234-1234-"+std::string(12-suffix.size(),'0')+suffix,
            item.u8string(),name,organizer::ItemKind::File});
    }
    const std::string other="12345678-1234-1234-1234-123456789003";
    Check(organizer::CreateCollection(state,other,"Other")==organizer::Change::Changed&&
        organizer::MergeGroup(state,other,organizer::DefaultCollectionId)==organizer::Change::Changed,
        "scroll fixture creates another tab without duplicating its references");
    organizer::ActivateTab(state,organizer::DefaultCollectionId,organizer::DefaultCollectionId);
    SaveSeed(files,path,state);std::vector<std::wstring> opened;
    shell::OrganizerSession session(GetModuleHandleW(nullptr),path,{},[]{},&files,[&](HWND,const std::wstring& item){opened.push_back(item);return true;});
    Check(session.Restore(),"scroll fixture restores");const auto window=session.Handle();
    RECT original{};Check(GetWindowRect(window,&original)!=FALSE,"capture original scroll fixture position");
    const auto layout=LayoutOrganizer(420,392,Host(session),{});
    Check(layout.maxScroll>0,"scroll fixture has content below its viewport");
    const auto cell=std::find_if(layout.cells.begin(),layout.cells.end(),[&](const auto& value){
        return value.bounds.y>=layout.viewport.y&&value.bounds.y+value.bounds.height<=layout.viewport.y+layout.viewport.height;
    });
    Check(cell!=layout.cells.end(),"scrolled list exposes a whole item row");
    const float x=cell->bounds.x+cell->bounds.width/2,y=cell->bounds.y+cell->bounds.height/2;
    const auto openAt=[&]{
        const auto before=opened.size();SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,Point(window,x,y));Pump();
        Check(opened.size()==before+1,"visible row reaches the injected target opener once");return opened.back();
    };
    const auto beforeScroll=openAt();Wheel(window,x,y,-3);const auto afterScroll=openAt();
    Check(afterScroll!=beforeScroll,"wheel input actually changes the visible item");
    Click(window,layout.search.x+layout.search.width/2,layout.search.y+layout.search.height/2);
    const auto editor=GetFocus();
    Check(editor&&GetWindow(editor,GW_OWNER)==window&&GetPropW(editor,L"LumeOrganizerEditor"),
        "scroll regression opens the real search editor");
    SetWindowTextW(editor,L"scroll-");Pump();
    SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);Pump();
    Check(!IsWindow(editor)&&openAt()==afterScroll,"closing search restores its original scroll position once");
    Wheel(window,x,y,-2);const auto afterSearchScroll=openAt();
    Check(afterSearchScroll!=afterScroll,"scrolling after closing search establishes a different visible row");
    Command(window,4);WaitForStrip(window);
    for(int attempt=0;attempt<2;++attempt){
        ClickStripPart(session,OrganizerHit::Kind::Title);Size(window,420,392,"temporary opening retains expanded dimensions while scrolled");
        RECT bounds{};Check(GetWindowRect(window,&bounds)&&bounds.left==original.left&&bounds.top==original.top,
            "temporary opening retains the original position");
        Check(openAt()==afterSearchScroll,"temporary opening keeps newer scrolling after the search editor was destroyed");
        LoseFocus(window);Size(window,StripWidth,StripHeight,"scrolled temporary view refolds on focus loss");
    }
    Command(window,5);Check(openAt()==afterSearchScroll,"disabling folding retains the same content scroll position");
    const auto grouped=LayoutOrganizer(420,392,Host(session),GroupView(session.Snapshot()));
    Check(grouped.tabs.size()==2,"search scroll regression retains both group tabs");
    Click(window,grouped.tabs[1].x+grouped.tabs[1].width/2,grouped.tabs[1].y+grouped.tabs[1].height/2);
    Check(organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id==other,
        "scroll regression switches to the other tab");
    Click(window,grouped.tabs[0].x+grouped.tabs[0].width/2,grouped.tabs[0].y+grouped.tabs[0].height/2);
    Check(organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id==organizer::DefaultCollectionId&&
        openAt()==afterSearchScroll,"switching tabs preserves newer scrolling instead of restoring an already-closed search");
    session.Shutdown();WaitForIcons();
}
void HoverLifecycleAndSelectedReset(const std::filesystem::path& item){
    constexpr UINT_PTR HoverTimer=43107;
    {
        Files files;const std::string path="hover-restart.json";auto state=Seed(item,420,392);
        state.collections.front().mode=organizer::Mode::Collapsed;SaveSeed(files,path,state);
        shell::OrganizerSession session(GetModuleHandleW(nullptr),path,{},[]{},&files,[](HWND,const std::wstring&){return true;});
        Check(session.Restore(),"hover lifecycle fixture restores");
        session.DropHoverForTest(organizer::DefaultCollectionId,POINT{24,20},true);
        const auto stopped=session.HandlePage(PageCommand::OrganizerDisable);Pump();
        Check(stopped&&nlohmann::json::parse(OrganizerUtf8(*stopped)).value("saved",false)&&!session.Running(),
            "module disables while compact hover expansion is pending");
        const auto restored=session.HandlePage(PageCommand::OrganizerEnable);Pump();
        Check(restored&&nlohmann::json::parse(OrganizerUtf8(*restored)).value("saved",false)&&session.Running(),
            "same session enables again after pending hover");
        Size(session.Handle(),StripWidth,StripHeight,"reenabled collection starts folded");
        session.DropHoverForTest(organizer::DefaultCollectionId,POINT{24,20},true);
        PumpFor(600);
        Size(session.Handle(),420,392,"same target hover arms a new timer after disable and enable");
        session.DropHoverForTest(organizer::DefaultCollectionId,{},false);Pump();WaitForStrip(session.Handle());
        Size(session.Handle(),StripWidth,StripHeight,"reenabled hover still refolds on leave");
        session.Shutdown();WaitForIcons();
    }
    {
        Files files;const std::string path="hover-cancel-reset.json";auto state=Seed(item,520,400);
        state.collections.front().title="A";
        const std::string other="12345678-1234-1234-1234-123456789002";
        Check(organizer::CreateCollection(state,other,"B")==organizer::Change::Changed&&
            organizer::MergeGroup(state,other,organizer::DefaultCollectionId)==organizer::Change::Changed,
            "hover cancellation fixture creates a tab group");
        organizer::ActivateTab(state,organizer::DefaultCollectionId,organizer::DefaultCollectionId);
        state.collections.front().accent=organizer::Accent::Blue;
        organizer::Find(state,other)->accent=organizer::Accent::Rose;SaveSeed(files,path,state);
        shell::OrganizerSession session(GetModuleHandleW(nullptr),path,{},[]{},&files,[](HWND,const std::wstring&){return true;});
        Check(session.Restore(),"expanded hover cancellation fixture restores");const auto window=session.Handle();
        const auto view=GroupView(session.Snapshot());const auto layout=LayoutOrganizer(520,400,Host(session),view);
        const auto pointFor=[&](std::size_t index){
            const auto& rect=layout.tabs[index];const auto point=Point(window,rect.x+rect.width/2,rect.y+rect.height/2);
            return POINT{GET_X_LPARAM(point),GET_Y_LPARAM(point)};
        };
        const auto a=pointFor(0),b=pointFor(1);
        session.DropHoverForTest(organizer::DefaultCollectionId,b,true);
        SendMessageW(window,WM_TIMER,HoverTimer,0);
        Check(organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id==organizer::DefaultCollectionId,
            "an early queued hover timer cannot switch tabs before its deadline");
        session.DropHoverForTest(organizer::DefaultCollectionId,a,true);
        PumpFor(600);SendMessageW(window,WM_TIMER,HoverTimer,0);Pump();
        Check(organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id==organizer::DefaultCollectionId,
            "returning to the active tab cancels hover and ignores a stale timer after its old deadline");
        session.DropHoverForTest(organizer::DefaultCollectionId,{},false);
        Click(window,layout.tabs[1].x+layout.tabs[1].width/2,layout.tabs[1].y+layout.tabs[1].height/2);
        Check(organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id==other&&
            organizer::Find(session.Snapshot(),other)->accent==organizer::Accent::Rose,
            "selected child tab has its own marker before reset");
        Command(window,10);
        Check(organizer::Find(session.Snapshot(),other)->accent==organizer::Accent::None&&Host(session).accent==organizer::Accent::Blue&&
            organizer::ActiveTab(session.Snapshot(),organizer::DefaultCollectionId)->id==other,
            "native reset clears the selected child tab marker while retaining the active tab");
        Check(Host(session).mode==organizer::Mode::Expanded&&Host(session).layout==organizer::Layout::Icons&&Host(session).iconScale==70&&
            Host(session).items.size()==1&&organizer::Tabs(session.Snapshot(),organizer::DefaultCollectionId).size()==2,
            "selected-tab reset preserves the existing host reset and group contents");
        organizer::Store reader(files,OrganizerPathKey);const auto stored=reader.Load(path);
        const auto* savedTab=organizer::Find(stored.value,other);
        const auto* savedHost=organizer::Find(stored.value,organizer::DefaultCollectionId);
        Check(stored.state==organizer::LoadState::Loaded&&savedTab&&savedTab->accent==organizer::Accent::None&&
            savedHost&&savedHost->accent==organizer::Accent::Blue,
            "selected child marker reset is persisted");
        session.Shutdown();WaitForIcons();
    }
}
}

int main(){
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com)){std::cerr<<"FAIL COM initialization\n";return 1;}
    int result=0;
    try{
        const auto root=UniqueTestDirectory(std::filesystem::temp_directory_path()/L"LumeOrganizerCollapseTest");
        const auto item=root/L"collapse fixture.txt";
        {std::ofstream file(item,std::ios::binary);file<<"isolated organizer collapse fixture\n";Check(file.good(),"write harmless local fixture");}
        UnfoldFrames(item);SingleCollection(item,420,392);SingleCollection(item,520,400);GroupAndNewCollection(item);ScrollPreservation(root);
        HoverLifecycleAndSelectedReset(item);
        std::cout<<"PASS organizer folding, temporary opening, focus, position, scroll, persistence, rollback, tabs, color and new-box defaults\n";
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';result=1;}
    CoUninitialize();return result;
}
