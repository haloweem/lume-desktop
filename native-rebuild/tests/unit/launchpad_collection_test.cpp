#include "platform/windows/dock/launchpad_window.h"
#include <objbase.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <chrono>

using namespace lume::platform::windows;
using lume::dock::Entry;
namespace {
void Check(bool ok,const char* reason){if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}}
void Pump(DWORD ms=0){
    const auto end=GetTickCount64()+ms;
    do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}if(ms)Sleep(2);}
    while(GetTickCount64()<end);
}
// Fixture-only: user activity must not dismiss synthetic pointer sequences.
WNDPROC original=nullptr;
bool sampleFrames=false;
std::vector<double> frameCosts;
LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_ACTIVATEAPP&&!w)return 0;
    const auto begin=std::chrono::steady_clock::now();
    const auto result=CallWindowProcW(original,window,message,w,l);
    if(sampleFrames&&message==WM_APP+0x7a5)
        frameCosts.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
    return result;
}
LaunchpadWindow::Layout Layout(LaunchpadWindow& pad,bool collection=false){
    RECT r{};GetClientRect(pad.Handle(),&r);const float scale=GetDpiForWindow(pad.Handle())/96.f;
    return LaunchpadWindow::LayoutFor(r.right/scale,r.bottom/scale,collection);
}
LPARAM At(LaunchpadWindow& pad,float x,float y){
    const float scale=GetDpiForWindow(pad.Handle())/96.f;
    return MAKELPARAM(static_cast<int>(std::lround(x*scale)),static_cast<int>(std::lround(y*scale)));
}
void Click(LaunchpadWindow& pad,WidgetRect r){
    const auto at=At(pad,r.x+r.width/2,r.y+r.height/2);
    SendMessageW(pad.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(pad.Handle(),WM_LBUTTONUP,0,at);
}
void Drag(LaunchpadWindow& pad,WidgetRect from,WidgetRect to,bool release=true){
    const float x=from.x+from.width/2,y=from.y+from.height/2;
    SendMessageW(pad.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,At(pad,x,y));
    for(int i=1;i<=24;++i){
        SendMessageW(pad.Handle(),WM_MOUSEMOVE,MK_LBUTTON,
            At(pad,x+(to.x+to.width/2-x)*i/24,y+(to.y+to.height/2-y)*i/24));Pump(4);
    }
    if(release)SendMessageW(pad.Handle(),WM_LBUTTONUP,0,At(pad,to.x+to.width/2,to.y+to.height/2));
}
void SaveFrame(LaunchpadWindow& pad,const wchar_t* name){
    wchar_t directory[32768]{};
    if(!GetEnvironmentVariableW(L"LUME_COLLECTION_PREVIEW",directory,32768))return;
    RECT r{};GetWindowRect(pad.Handle(),&r);const int width=r.right-r.left,height=r.bottom-r.top;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;const HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    const auto old=SelectObject(memory,bitmap);BitBlt(memory,0,0,width,height,screen,r.left,r.top,SRCCOPY|CAPTUREBLT);
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
    header.bfSize=header.bfOffBits+width*height*4;
    std::filesystem::create_directories(directory);
    std::ofstream file(std::filesystem::path(directory)/name,std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header),sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
    file.write(static_cast<const char*>(pixels),static_cast<std::streamsize>(width)*height*4);
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::vector<Entry> flat{{"collection:c1","收纳",true,true,"c1"}},members;
    for(int i=0;i<27;++i)flat.push_back({"C:\\Fixture\\app"+std::to_string(i)+".exe","App "+std::to_string(i),false});
    for(int i=0;i<12;++i)members.push_back({"C:\\Fixture\\member"+std::to_string(i)+".exe","Member "+std::to_string(i),false});
    bool inCollection=false,failMove=false,failRemove=false,collectionOnPageTwo=false;
    int visibility=0,moves=0,removed=0,opens=0,renames=0;std::string opened;
    LaunchpadWindow* window=nullptr;
    const auto arrange=[](std::vector<Entry>& entries,const std::vector<std::string>& order){
        const auto previous=entries;entries.clear();
        for(const auto& path:order)for(const auto& entry:previous)if(entry.path==path){entries.push_back(entry);break;}
    };
    LaunchpadPorts ports;
    ports.entries=[&]{
        flat[0].previewPaths.clear();
        for(std::size_t i=0;i<std::min<std::size_t>(4,members.size());++i)flat[0].previewPaths.push_back(members[i].path);
        if(inCollection)return members;
        auto entries=flat;
        if(collectionOnPageTwo)std::rotate(entries.begin(),entries.begin()+1,entries.begin()+22);
        return entries;
    };
    ports.filter=[](const auto& all,const auto& query){
        std::vector<Entry> result;for(const auto& entry:all)if(entry.name.find(query)!=std::string::npos)result.push_back(entry);
        return result;
    };
    std::set<std::string> requestedIcons;
    std::map<std::string,std::shared_ptr<const WidgetBitmap>> images;
    ports.icon=[&](const std::string& path){
        requestedIcons.insert(path);auto& image=images[path];
        if(!image){
            const std::uint32_t colors[]{0xff40a0ee,0xffed8750,0xff62cf87,0xffb481ed};
            const auto it=std::find_if(members.begin(),members.end(),[&](const auto& e){return e.path==path;});
            const auto index=it==members.end()?0:static_cast<std::size_t>(it-members.begin())%4;
            image=std::make_shared<const WidgetBitmap>(WidgetBitmap{32,32,std::vector<std::uint32_t>(32*32,colors[index])});
        }
        return image;
    };
    ports.rename=[&](const auto& id,const auto& title){
        Check(id=="c1","rename keeps collection identity");++renames;flat[0].name=title;
        if(inCollection)window->SetCollection(id,title);
        window->Refresh();
    };
    ports.visibility=[&]{++visibility;};
    ports.open=[&](const Entry& entry){if(entry.collection){inCollection=true;window->SetCollection("c1","收纳");}
        else{++opens;opened=entry.path;}};
    ports.back=[&]{inCollection=false;window->SetCollection("","");};
    ports.arrange=[&](const auto& order){arrange(inCollection?members:flat,order);};
    ports.moveIntoCollection=[&](const Entry& entry,const std::string& id){
        Check(id=="c1","drop keeps collection identity");if(failMove)return false;
        ++moves;members.push_back(entry);flat.erase(std::remove_if(flat.begin(),flat.end(),[&](const auto& e){return e.path==entry.path;}),flat.end());
        window->Refresh();return true;
    };
    ports.removeFromCollection=[&](const Entry& entry){
        if(failRemove)return false;++removed;flat.push_back(entry);
        members.erase(std::remove_if(members.begin(),members.end(),[&](const auto& e){return e.path==entry.path;}),members.end());
        inCollection=false;window->SetCollection("","");window->Refresh();return true;
    };
    LaunchpadWindow pad(GetModuleHandleW(nullptr),ports);window=&pad;
    Check(pad.Show({},true),"launchpad opens");
    original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(pad.Handle(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Procedure)));
    pad.Settle();auto layout=Layout(pad);
    HWND background=nullptr;
    if(GetEnvironmentVariableW(L"LUME_COLLECTION_PREVIEW",nullptr,0)){
        RECT r{};GetWindowRect(pad.Handle(),&r);
        background=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP|SS_BLACKRECT,
            r.left-64,r.top-64,r.right-r.left+128,r.bottom-r.top+128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        SetWindowPos(background,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        SetWindowPos(pad.Handle(),HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        Pump(80);SaveFrame(pad,L"flat.bmp");
    }
    for(std::size_t i=0;i<4;++i)Check(requestedIcons.count(members[i].path)!=0,"flat tile requests actual member thumbnail");
    pad.BeginRename("c1",flat[0].name);
    const HWND edit=pad.SearchHandle();RECT editRect{},padRect{};GetWindowRect(edit,&editRect);GetWindowRect(pad.Handle(),&padRect);
    const auto tile=LaunchpadWindow::Cell(layout,0);const float scale=GetDpiForWindow(pad.Handle())/96.f;
    if(std::abs((editRect.left+editRect.right)/2.f-padRect.left-(tile.x+tile.width/2)*scale)>=2)
        std::cerr<<"rename geometry: edit="<<editRect.left<<','<<editRect.right<<" pad="<<padRect.left<<','<<padRect.right
                 <<" expected DIP="<<tile.x+tile.width/2<<" dpi scale="<<scale<<'\n';
    Check((GetWindowLongPtrW(edit,GWL_STYLE)&ES_CENTER)!=0,"inline rename uses centered native text");
    Check(std::abs((editRect.left+editRect.right)/2.f-padRect.left-(tile.x+tile.width/2)*scale)<2,
          "inline rename centered over tile label");
    SetWindowTextW(edit,L"新的收纳名称");Pump(60);SaveFrame(pad,L"renaming.bmp");
    Check(pad.PageEntries().size()==21,"rename text never filters launchpad items");
    SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);SendMessageW(edit,WM_CHAR,L'\r',0);Pump(30);
    Check(renames==1&&flat[0].name=="新的收纳名称"&&pad.PageEntries().size()==21&&pad.Page()==0,
          "rename commits without turning into search results");
    Check(GetWindowTextLengthW(edit)==0&&(GetWindowLongPtrW(edit,GWL_STYLE)&(ES_CENTER|ES_RIGHT))==0,
          "search native text and alignment restored");
    SaveFrame(pad,L"renamed.bmp");
    SetWindowTextW(edit,L"收纳");Check(pad.PageEntries().size()==1,"fixture search really filters");
    pad.BeginRename("c1",flat[0].name);SetWindowTextW(edit,L"收纳重命名");SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);
    wchar_t restoredQuery[32]{};GetWindowTextW(edit,restoredQuery,32);
    Check(renames==2&&std::wstring(restoredQuery)==L"收纳"&&pad.PageEntries().size()==1,
          "rename preserves an existing search query");
    SetWindowTextW(edit,L"");collectionOnPageTwo=true;pad.Refresh();pad.Turn(1);pad.Settle();
    const auto pageTwo=pad.PageEntries().front().path;
    Check(pad.PageEntries().front().collection,"rename fixture collection is on the second page");
    pad.BeginRename("c1",flat[0].name);SetWindowTextW(edit,L"第二页收纳");SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);
    Check(renames==3&&pad.Page()==1&&pad.PageEntries().front().path==pageTwo&&GetWindowTextLengthW(edit)==0,
          "commit keeps the renamed tile on its original second page");
    pad.BeginRename("c1",flat[0].name);SetWindowTextW(edit,L"取消的名称");SendMessageW(edit,WM_KEYDOWN,VK_ESCAPE,0);
    Check(renames==3&&pad.Page()==1&&pad.PageEntries().front().path==pageTwo&&GetWindowTextLengthW(edit)==0,
          "cancel keeps the original page and search text");
    collectionOnPageTwo=false;pad.Refresh();pad.Turn(-1);pad.Settle();
    // Follow a continuous trajectory across insertion gaps; one-jump tests miss this regression.
    const auto firstPath=flat[6].path;
    Drag(pad,LaunchpadWindow::Cell(layout,6),LaunchpadWindow::Cell(layout,0));
    Check(moves==1&&members.back().path==firstPath&&pad.Visible(),"continuous drag enters stationary collection and persists");
    pad.Settle();failMove=true;const auto count=flat.size();
    Drag(pad,LaunchpadWindow::Cell(layout,5),LaunchpadWindow::Cell(layout,0));
    Check(moves==1&&flat.size()==count&&pad.Visible(),"failed save retains flat item and open launchpad");
    failMove=false;pad.Settle();
    // Carry an item from page two to the collection on page one.
    pad.Turn(1);pad.Settle();
    Drag(pad,LaunchpadWindow::Cell(layout,0),LaunchpadWindow::Cell(layout,2),false);
    pad.Turn(-1);Pump(30);
    const auto collection=LaunchpadWindow::Cell(layout,0);
    SendMessageW(pad.Handle(),WM_MOUSEMOVE,MK_LBUTTON,At(pad,collection.x+collection.width/2,collection.y+collection.height/2));
    SendMessageW(pad.Handle(),WM_LBUTTONUP,0,At(pad,collection.x+collection.width/2,collection.y+collection.height/2));
    Check(moves==2,"cross-page drag enters collection");pad.Settle();
    const auto native=pad.Handle();const auto presence=pad.Presence();
    const auto collectionBegin=std::chrono::steady_clock::now();
    Click(pad,LaunchpadWindow::Cell(layout,0));
    const auto collectionCost=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-collectionBegin).count();
    Check(inCollection&&pad.Visible()&&pad.Handle()==native&&pad.Presence()==presence&&visibility==1,
          "collection click stays in the same visible launchpad without a close cycle");
    BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    const auto initialBounds=pad.CollectionBounds();Pump(55);const auto middle=pad.CollectionBounds();
    Check(!animations||middle.width>initialBounds.width,"collection expansion advances in real frame messages");
    pad.Settle();Check(pad.PageEntries().size()==9&&pad.Pages()==2,"collection keeps three by three paging");
    Pump(100);SaveFrame(pad,L"expanded.bmp");
    Check(!IsWindowVisible(pad.SearchHandle()),"native search does not reappear above open collection");
    pad.Turn(1);pad.Settle();const auto secondPageMembers=pad.PageEntries().size();
    pad.BeginRename("c1",flat[0].name);SetWindowTextW(pad.SearchHandle(),L"收纳里的重命名");
    SendMessageW(pad.SearchHandle(),WM_KEYDOWN,VK_RETURN,0);SendMessageW(pad.SearchHandle(),WM_CHAR,L'\r',0);Pump(20);
    Check(renames==4&&inCollection&&pad.Page()==1&&pad.PageEntries().size()==secondPageMembers&&!IsWindowVisible(pad.SearchHandle()),
          "open collection rename survives callback resetting its view without searching members");
    pad.Turn(-1);pad.Settle();
    const auto folder=Layout(pad,true);const auto path=members[0].path;
    Drag(pad,LaunchpadWindow::Cell(folder,0),LaunchpadWindow::Cell(folder,2));pad.Settle();
    Check(members[2].path==path&&pad.PageEntries()[2].path==path,"members use native reorder motion and persisted order");
    failRemove=true;
    const WidgetRect outside{10,folder.panel.y+folder.panel.height/2,2,2};
    Drag(pad,LaunchpadWindow::Cell(folder,0),outside);
    Check(inCollection&&removed==0&&pad.Visible(),"failed drag-out preserves collection membership");pad.Settle();failRemove=false;
    Drag(pad,LaunchpadWindow::Cell(folder,0),outside,false);
    Check(!inCollection&&removed==1&&GetCapture()==pad.Handle()&&pad.Visible(),"drag out transfers to flat grid without releasing pointer capture");
    SendMessageW(pad.Handle(),WM_MOUSEMOVE,MK_LBUTTON,At(pad,layout.grid.x+layout.grid.width-20,layout.grid.y+50));
    SendMessageW(pad.Handle(),WM_LBUTTONUP,0,At(pad,layout.grid.x+layout.grid.width-20,layout.grid.y+50));
    pad.Settle();pad.Turn(-9);pad.Settle();
    Click(pad,LaunchpadWindow::Cell(layout,0));pad.Settle();
    Check(inCollection,"collection reopens with a single click");
    SendMessageW(pad.Handle(),WM_KEYDOWN,VK_ESCAPE,0);pad.Settle();
    Check(!inCollection&&pad.Visible()&&visibility==1&&pad.CollectionPresence()==0,"Escape reverses expansion and keeps the launchpad open");
    SaveFrame(pad,L"returned.bmp");Pump(100);SaveFrame(pad,L"returned-stable.bmp");
    Click(pad,LaunchpadWindow::Cell(layout,0));pad.Settle();
    const WidgetRect blank{folder.width-18,folder.height/2,2,2};
    Click(pad,blank);pad.Settle();
    Check(!inCollection&&pad.Visible()&&pad.CollectionPresence()==0,"blank click returns from collection to launchpad");
    Click(pad,LaunchpadWindow::Cell(layout,0));pad.Settle();
    const auto expected=members.front().path;Click(pad,LaunchpadWindow::Cell(folder,0));
    Check(opens==1&&opened==expected&&!pad.Visible()&&visibility==2,"member click uses ordinary application launch and close behavior");
    // Actual frame delivery, without Settle, verifies the EDIT handoff and
    // repeated open/close lifecycle rather than only the final layout.
    const HWND retainedEdit=pad.SearchHandle();
    Check(retainedEdit==edit&&IsWindow(retainedEdit)&&!IsWindowVisible(retainedEdit),
          "closing retains and hides the prepared native EDIT");
    pad.Settle();sampleFrames=true;
    Check(pad.Show({},true),"launchpad reopens with retained surfaces");
    Check(pad.SearchHandle()==retainedEdit&&GetWindowTextLengthW(retainedEdit)==0,
          "reopening reuses EDIT without stale collection rename text");
    Pump(80);SaveFrame(pad,L"opening-motion.bmp");
    Drag(pad,LaunchpadWindow::Cell(layout,1),LaunchpadWindow::Cell(layout,2),false);
    Pump(850);
    Check(IsWindowVisible(retainedEdit)&&GetCapture()==pad.Handle(),
          "search takes over when the panel settles even while an ongoing drag keeps the frame clock active");
    SendMessageW(pad.Handle(),WM_LBUTTONUP,0,At(pad,layout.grid.x+layout.grid.width/2,layout.grid.y+layout.grid.height/2));
    SetWindowTextW(retainedEdit,L"App");Pump(30);
    SaveFrame(pad,L"before-close.bmp");
    const auto closeFrom=pad.Presence();pad.Hide();
    Check(pad.SearchHandle()==retainedEdit&&!IsWindowVisible(retainedEdit)&&(!animations||IsWindowVisible(pad.Handle())),
          "live EDIT yields to retained text while the launchpad is still visibly closing");
    Pump(45);SaveFrame(pad,L"closing-motion.bmp");
    Check(!animations||pad.Presence()<closeFrom,"closing progresses with real display frame messages");
    // Reverse a close, then complete another close and a fresh open.
    Check(pad.Show({},true),"rapid close-open reversal succeeds");Pump(700);
    wchar_t reversedText[16]{};GetWindowTextW(retainedEdit,reversedText,16);
    Check(pad.Visible()&&IsWindowVisible(retainedEdit)&&std::wstring(reversedText)==L"App",
          "reversal preserves query and restores input without another EDIT");
    pad.Hide();Pump(600);
    Check(!IsWindowVisible(pad.Handle())&&IsWindow(retainedEdit),"completed close keeps input resources for reuse");
    Check(pad.Show({},true),"fresh open after animation completes");
    const auto input=IsWindowVisible(retainedEdit)?retainedEdit:pad.Handle();
    SendMessageW(input,WM_CHAR,L'A',0);SendMessageW(input,WM_CHAR,L'p',0);SendMessageW(input,WM_CHAR,L'p',0);
    Pump(700);GetWindowTextW(retainedEdit,reversedText,16);
    Check(std::wstring(reversedText)==L"App"&&pad.PageEntries().size()>1&&IsWindowVisible(retainedEdit),
          "typing during the opening animation transfers to the live search");
    sampleFrames=false;
    if(!frameCosts.empty()){
        std::sort(frameCosts.begin(),frameCosts.end());
        std::cout<<"animation UI frame work ms: p50="<<frameCosts[frameCosts.size()/2]
                 <<" p95="<<frameCosts[(frameCosts.size()-1)*95/100]<<" max="<<frameCosts.back()
                 <<" samples="<<frameCosts.size()<<" collection open ms="<<collectionCost<<'\n';
    }
    pad.Stop();if(background)DestroyWindow(background);CoUninitialize();std::cout<<"launchpad collection interactions passed\n";
}
