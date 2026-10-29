#include "apps/shell/organizer_session.h"
#include "platform/windows/organizer/organizer_text.h"
#include "platform/windows/organizer/organizer_view.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/material/material_api.h"
#include "composed_corners_test.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace {
using namespace lume;
void Expect(bool ok,const char* why){if(std::getenv("LUME_ORGANIZER_TRACE"))std::cout<<why<<std::endl;if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
struct Files:settings::IFileStore{
    std::map<std::string,std::string> data;bool fail=false;unsigned writes=0,commits=0;
    bool Read(const std::string& path,std::string& out)const override{const auto found=data.find(path);if(found==data.end())return false;out=found->second;return true;}
    bool Exists(const std::string& path)const override{return data.count(path)!=0;}
    bool Write(const std::string& path,const std::string& value)override{if(fail)return false;++writes;data[path]=value;return true;}
    bool Rename(const std::string& from,const std::string& to)override{
        if(fail||!Exists(from))return false;
        data[to]=data[from];data.erase(from);
        if(to.size()>=5&&to.compare(to.size()-5,5,".json")==0)++commits;
        return true;
    }
    bool Remove(const std::string& path)override{data.erase(path);return true;}
};
void Pump(){
    MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
}
void DropFiles(HWND window,const std::vector<std::wstring>& paths){
    std::vector<wchar_t> names;
    for(const auto& path:paths){names.insert(names.end(),path.begin(),path.end());names.push_back(L'\0');}
    names.push_back(L'\0');
    const auto memory=GlobalAlloc(GHND,sizeof(DROPFILES)+names.size()*sizeof(wchar_t));
    Expect(memory!=nullptr,"allocate owned file-drop payload");
    const auto drop=static_cast<DROPFILES*>(GlobalLock(memory));
    Expect(drop!=nullptr,"lock owned file-drop payload");
    drop->pFiles=sizeof(DROPFILES);drop->fWide=TRUE;
    std::memcpy(reinterpret_cast<unsigned char*>(drop)+sizeof(DROPFILES),names.data(),names.size()*sizeof(wchar_t));
    GlobalUnlock(memory);
    // The real receiver owns DragFinish, just as it does for an Explorer drop.
    SendMessageW(window,WM_DROPFILES,reinterpret_cast<WPARAM>(memory),0);
}
void WaitForIconBatch(){
    const auto deadline=GetTickCount64()+8000;
    while(platform::windows::OrganizerIconCache::ActiveWorkers()!=0&&GetTickCount64()<deadline){
        Pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);
    }
    Pump();
    Expect(platform::windows::OrganizerIconCache::ActiveWorkers()==0,
        "file drop finishes its bounded extraction batch while organizer stays open");
}
void WriteShortcut(const std::filesystem::path& destination,const std::wstring& target){
    IShellLinkW* link=nullptr;IPersistFile* file=nullptr;
    Expect(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))),
        "create isolated real shortcut for file-drop test");
    const bool saved=SUCCEEDED(link->SetPath(target.c_str()))&&
        SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))&&SUCCEEDED(file->Save(destination.c_str(),TRUE));
    if(file)file->Release();
    link->Release();
    Expect(saved,"save real shortcut without opening its target");
}
HWND Editor(HWND owner){
    struct Search{HWND owner,result;};Search search{owner,nullptr};
    EnumWindows([](HWND hwnd,LPARAM data)->BOOL{
        auto& search=*reinterpret_cast<Search*>(data);
        if(GetWindow(hwnd,GW_OWNER)==search.owner&&GetPropW(hwnd,L"LumeOrganizerEditor")){
            search.result=hwnd;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
LPARAM Point(HWND hwnd,platform::windows::WidgetRect rect){
    const auto dpi=static_cast<int>(GetDpiForWindow(hwnd));
    return MAKELPARAM(MulDiv(static_cast<int>(rect.x+rect.width/2),dpi?dpi:96,96),
        MulDiv(static_cast<int>(rect.y+rect.height/2),dpi?dpi:96,96));
}
void Click(HWND hwnd,platform::windows::WidgetRect rect){
    const auto point=Point(hwnd,rect);
    SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(hwnd,WM_LBUTTONUP,0,point);
}
void ExpectMaterial(const std::wstring& snapshot,const char* material,bool running,const char* why){
    const auto state=nlohmann::json::parse(platform::windows::OrganizerUtf8(snapshot));
    if(state.at("materialStatus")!=material||state.at("running")!=running){
        std::cerr<<"snapshot: "<<state.dump()<<'\n';Expect(false,why);
    }
}
template<class T> T Resolve(HMODULE module,const char* name){
    const auto address=GetProcAddress(module,name);T result=nullptr;
    static_assert(sizeof(result)==sizeof(address));std::memcpy(&result,&address,sizeof(result));
    Expect(result!=nullptr,name);return result;
}
int MaterialUnavailable(){
    using ui::PageCommand;using namespace platform::windows;
    Files files;std::wstring notification;unsigned notices=0;
    shell::OrganizerSession session(GetModuleHandleW(nullptr),"material-unavailable.json",
        [&](const auto& state,bool failed){Expect(!failed,"material fallback is not a settings failure");notification=state;++notices;},
        []{},&files);
    Expect(session.Restore(),"disabled material-unavailable restore");
    ExpectMaterial(session.State(),"inactive",false,"disabled organizer material is inactive");
    const auto reply=session.HandlePage(PageCommand::OrganizerEnable);
    Expect(reply.has_value()&&session.Running(),"organizer still starts when material DLL is unavailable");
    ExpectMaterial(*reply,"transparent-fallback",true,"missing material DLL reports fallback with live window");
    Pump();Expect(notices>0,"material load failure publishes a notification");
    ExpectMaterial(notification,"transparent-fallback",true,"material failure notification preserves fallback");
    const auto disabled=session.HandlePage(PageCommand::OrganizerDisable);
    Expect(disabled.has_value(),"material-unavailable organizer disables");
    ExpectMaterial(*disabled,"inactive",false,"material fallback becomes inactive only after window stops");
    session.Shutdown();Pump();
    Expect(OrganizerIconCache::ActiveWorkers()==0,"unavailable-material session releases workers");
    std::cout<<"PASS organizer missing material DLL feedback\n";return 0;
}
int MaterialFeedback(){
    using ui::PageCommand;using namespace platform::windows;
    const auto module=LoadLibraryW(L"LumeGlass.dll");Expect(module!=nullptr,"isolated material fixture loads");
    using SetStatus=int (__cdecl*)(HWND,const LumeMaterialStatus*);
    const auto setStatus=Resolve<SetStatus>(module,"LumeOrganizerMaterialFixtureSetStatus");
    const auto statistics=Resolve<LumeMaterialStatisticsFn>(module,"LumeMaterialStatistics");
    const auto users=[&]{LumeMaterialStatisticsData stats{};stats.size=sizeof(stats);
        Expect(statistics(&stats)!=0,"fixture material statistics");return stats.users;};
    {
        Files files;std::wstring notification;unsigned notices=0;
        shell::OrganizerSession session(GetModuleHandleW(nullptr),"material-feedback.json",
            [&](const auto& state,bool failed){Expect(!failed,"material transitions do not fake settings failures");notification=state;++notices;},
            []{},&files);
        Expect(session.Restore(),"material feedback fixture restores disabled");
        ExpectMaterial(session.State(),"inactive",false,"no material windows means inactive");
        const auto enabled=session.HandlePage(PageCommand::OrganizerEnable);
        Expect(enabled.has_value(),"fixture organizer enable handled");
        ExpectMaterial(*enabled,"native-starting",true,"live starting material is not inactive");
        Pump();Expect(notices>0,"startup material notification is delivered");
        ExpectMaterial(notification,"native-starting",true,"startup notification preserves starting");
        const auto root=session.Handle();Expect(root!=nullptr&&users()==1,"one material lease for default collection");
        const auto transition=[&](HWND owner,bool initialized,bool frameReady,bool failed,const char* expected,const char* why){
            LumeMaterialStatus status{};status.size=sizeof(status);status.initialized=initialized;
            status.frameReady=frameReady;status.failed=failed;status.frames=frameReady?1u:0u;
            status.lastError=failed?static_cast<int32_t>(E_FAIL):0;
            const auto before=notices;Expect(setStatus(owner,&status)!=0,"fixture status transition accepted");
            Pump();Expect(notices>before,"material transition reaches the session notify callback");
            ExpectMaterial(session.State(),expected,true,why);
            ExpectMaterial(notification,expected,true,why);
            Expect(session.Running(),"material state changes keep organizer window running");
        };
        transition(root,true,true,false,"native-ready","first material frame reports ready");
        transition(root,true,false,true,"transparent-fallback","live material failure reports transparent fallback");
        const auto previous=files.data.at("material-feedback.json");files.fail=true;
        const auto failedSave=session.HandlePage(PageCommand::OrganizerFrostedGlass);
        Expect(failedSave.has_value(),"failed material save returns an applied reply");
        const auto failedState=nlohmann::json::parse(OrganizerUtf8(*failedSave));
        Expect(failedState.at("error")=="save"&&failedState.at("saved")==false&&
            failedState.at("material")=="liquid-glass"&&files.data.at("material-feedback.json")==previous,
            "save error and rolled-back preferences survive material failure");
        ExpectMaterial(*failedSave,"transparent-fallback",true,"save failure does not hide material fallback");
        transition(root,false,false,false,"native-starting","recovering material reports starting");
        Expect(nlohmann::json::parse(OrganizerUtf8(notification)).at("error")=="save",
            "material notification does not erase a real settings save error");
        transition(root,true,true,false,"native-ready","material recovers on the same running window");
        Expect(session.Handle()==root,"material recovery preserves collection HWND");
        files.fail=false;session.HandlePage(PageCommand::OrganizerLiquidGlass);
        Expect(nlohmann::json::parse(OrganizerUtf8(session.State())).at("error")=="",
            "successful settings operation clears prior save error");

        SendMessageW(root,WM_COMMAND,3,0);
        Expect(session.Snapshot().collections.size()==2,"material fixture creates independent second collection");
        const auto secondId=session.Snapshot().collections.back().id;
        const auto second=session.Handle(secondId);Expect(second&&second!=root&&users()==2,"second collection owns its material");
        Pump();ExpectMaterial(session.State(),"native-starting",true,"starting extra collection is visible in shared material status");
        transition(second,true,true,false,"native-ready","all live collection materials ready");
        transition(second,true,false,true,"transparent-fallback","extra collection failure is not hidden by ready default collection");
        transition(root,false,false,false,"transparent-fallback","fallback takes priority over another collection starting");
        transition(root,true,true,false,"transparent-fallback","default recovery does not hide remaining extra collection fallback");
        transition(second,true,false,false,"transparent-fallback","initialized material without frames is transparent fallback");
        transition(second,false,false,false,"native-starting","extra collection retry reports starting");
        transition(second,true,true,false,"native-ready","extra collection recovery restores ready summary");
        SendMessageW(second,WM_CLOSE,0,0);Pump();
        Expect(session.Running()&&session.Handle()==root&&!session.Handle(secondId)&&users()==1,
            "closing extra collection preserves default collection and releases only its material");
        ExpectMaterial(session.State(),"native-ready",true,"stopped extra collection does not mask live ready material");

        SendMessageW(root,WM_COMMAND,3,0);
        const auto survivingId=session.Snapshot().collections.back().id;
        const auto surviving=session.Handle(survivingId);Expect(surviving&&users()==2,"new surviving extra collection");
        transition(surviving,true,true,false,"native-ready","replacement extra collection ready");
        Expect(DestroyWindow(root)!=FALSE,"simulate unexpected default window loss");
        Expect(!session.Handle()&&session.Handle(survivingId)&&session.Running(),"extra collection remains running without default HWND");
        ExpectMaterial(session.State(),"native-ready",true,"remaining extra window keeps actual live material status");
        transition(surviving,true,false,true,"transparent-fallback","remaining extra window can still report material failure");
        const auto disabled=session.HandlePage(PageCommand::OrganizerDisable);
        Expect(disabled.has_value(),"disable after partial window loss returns state");
        ExpectMaterial(*disabled,"inactive",false,"all collections stopped reports inactive");
        Expect(users()==0,"module disable releases every material lease");
        session.Shutdown();Pump();
        Expect(OrganizerIconCache::ActiveWorkers()==0,"material feedback session releases workers");
    }
    Expect(users()==0,"fixture session destruction leaves no material leases");
    FreeLibrary(module);std::cout<<"PASS organizer four-state material notifications and multiple collection recovery\n";return 0;
}
void ExpectEditorGlass(HWND editor,double minimumAlpha,double maximumAlpha,const char* label){
    const bool placeholder=GetWindowTextLengthW(editor)==0;
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumeOrganizerEditorBackdrop";
    wc.lpfnWndProc=[](HWND window,UINT message,WPARAM wp,LPARAM lp)->LRESULT{
        if(message==WM_ERASEBKGND){
            RECT rect{};GetClientRect(window,&rect);
            FillRect(reinterpret_cast<HDC>(wp),&rect,static_cast<HBRUSH>(
                GetStockObject(GetWindowLongPtrW(window,GWLP_USERDATA)?WHITE_BRUSH:BLACK_BRUSH)));
            return 1;
        }
        return DefWindowProcW(window,message,wp,lp);
    };
    Expect(RegisterClassW(&wc)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"register own editor backdrop");
    RECT bounds{};Expect(GetWindowRect(editor,&bounds)!=FALSE,"editor has screen bounds");
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    Expect(width>20&&height>8,"editor has measurable input area");
    const bool wasTopmost=(GetWindowLongPtrW(editor,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    constexpr int referenceBorder=4;
    const auto backdrop=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
        wc.lpszClassName,L"",WS_POPUP,bounds.left-referenceBorder,bounds.top-referenceBorder,
        width+referenceBorder*2,height+referenceBorder*2,
        GetWindow(editor,GW_OWNER),nullptr,wc.hInstance,nullptr);
    Expect(backdrop!=nullptr,"create owned editor backdrop without changing the input owner");
    const auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    Expect(screen&&dc&&bitmap&&pixels,"allocate editor compositor capture");
    const auto previous=SelectObject(dc,bitmap);
    std::vector<std::uint32_t> black,white;
    bool hit=true;
    for(int pass=0;pass<2;++pass){
        SetWindowLongPtrW(backdrop,GWLP_USERDATA,pass);
        ShowWindow(backdrop,SW_SHOWNOACTIVATE);
        SetWindowPos(editor,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(editor,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(backdrop,editor,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        RedrawWindow(backdrop,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
        Pump();DwmFlush();Sleep(40);DwmFlush();
        Expect(corner_test::Unoccluded(editor,bounds),
            "editor compositor fixture is occluded; no transparency or glyph verdict is valid");
        const auto expectedBackdrop=pass?RGB(255,255,255):RGB(0,0,0);
        Expect(GetPixel(screen,bounds.left-2,bounds.top+height/2)==expectedBackdrop,
            "editor compositor fixture must present its known black or white reference border");
        const auto actualHit=WindowFromPoint({bounds.left+width*3/4,bounds.top+height/2});
        DWORD targetProcess=0;GetWindowThreadProcessId(actualHit,&targetProcess);
        Expect(targetProcess==GetCurrentProcessId(),
            "editor input fixture is intercepted by another process; no transparency verdict is valid");
        hit=hit&&actualHit==editor;
        Expect(BitBlt(dc,0,0,width,height,screen,bounds.left,bounds.top,SRCCOPY)!=FALSE,"capture real composited editor");
        GdiFlush();
        const auto first=static_cast<const std::uint32_t*>(pixels);
        (pass?white:black).assign(first,first+static_cast<std::size_t>(width)*height);
        if(const auto directory=std::getenv("LUME_ORGANIZER_EDITOR_DIAGNOSTICS")){
            const auto output=std::filesystem::path(directory);
            std::filesystem::create_directories(output);
            BITMAPFILEHEADER header{};header.bfType=0x4d42;
            header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);
            header.bfSize=header.bfOffBits+static_cast<DWORD>(width*height*4);
            std::ofstream file(output/(std::string(label)+(pass?"-white.bmp":"-black.bmp")),std::ios::binary);
            file.write(reinterpret_cast<const char*>(&header),sizeof(header));
            file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
            file.write(static_cast<const char*>(pixels),static_cast<std::streamsize>(width)*height*4);
            Expect(file.good(),"save editor composition evidence");
        }
    }
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
    DestroyWindow(backdrop);
    if(!wasTopmost)SetWindowPos(editor,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    double alpha=0;unsigned count=0,brightGlyphPixels=0,coloredPixels=0;
    for(int y=height/4;y<height*3/4;++y)for(int x=width*2/3;x<width*9/10;++x){
        const auto index=static_cast<std::size_t>(y)*width+x;
        double difference=0;
        for(const int shift:{0,8,16})
            difference+=static_cast<int>((white[index]>>shift)&255)-static_cast<int>((black[index]>>shift)&255);
        alpha+=std::clamp(1.-difference/765.,0.,1.);++count;
    }
    int inkTop=height,inkBottom=-1;
    for(std::size_t i=0;i<black.size();++i){
        const auto pixel=black[i];
        const auto low=std::min({pixel&255,(pixel>>8)&255,(pixel>>16)&255});
        const auto high=std::max({pixel&255,(pixel>>8)&255,(pixel>>16)&255});
        if(low>(placeholder?110u:225u))++brightGlyphPixels;
        if(high-low>3)++coloredPixels;
        if(low>80){inkTop=std::min(inkTop,static_cast<int>(i/width));inkBottom=std::max(inkBottom,static_cast<int>(i/width));}
    }
    const auto meanAlpha=count?alpha/count:1.;
    std::cout<<"editor composition "<<label<<" blankAlpha="<<meanAlpha
        <<" brightGlyphPixels="<<brightGlyphPixels<<" coloredPixels="<<coloredPixels<<" inputHit="<<hit<<'\n';
    Expect(hit,"transparent blank input pixels retain native mouse hit testing");
    Expect(meanAlpha>=minimumAlpha&&meanAlpha<=maximumAlpha,
        "native EDIT background matches original transparent glass input instead of an opaque rectangle");
    Expect(brightGlyphPixels>=4,"input glyphs remain opaque instead of applying whole-window transparency");
    Expect(coloredPixels==0,"transparent white EDIT glyphs use grayscale antialiasing without colored ClearType fringes");
    if(std::string(label).find("search")==0)
        Expect(inkBottom>=inkTop&&std::abs(inkTop+inkBottom-(height-1))<=4,
            "actual layered search text is vertically centered");
}
int EditorMaterial(){
    using ui::PageCommand;using namespace platform::windows;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Files files;
    shell::OrganizerSession session(GetModuleHandleW(nullptr),"editor-material.json",{},[]{},&files);
    Expect(session.HandlePage(PageCommand::OrganizerEnable).has_value(),"isolated editor organizer enables");
    const auto root=session.Handle();Expect(root!=nullptr,"isolated organizer exists");
    const auto collection=[&]()->const organizer::Collection& {return *organizer::Find(session.Snapshot(),"default");};
    const auto read=[](HWND window){wchar_t text[256]{};GetWindowTextW(window,text,256);return std::wstring(text);};
    auto layout=LayoutOrganizer(420,392,collection());
    Click(root,layout.search);
    auto editor=Editor(root);Expect(editor!=nullptr,"search remains a real native editor");
    wchar_t kind[16]{};GetClassNameW(editor,kind,16);
    Expect(lstrcmpiW(kind,L"EDIT")==0,"IME and clipboard keep the Windows EDIT input implementation");
    SetWindowTextW(editor,L"");Pump();ExpectEditorGlass(editor,0,.025,"search-placeholder");
    SetWindowTextW(editor,L"W");SendMessageW(editor,EM_SETSEL,0,0);Pump();
    ExpectEditorGlass(editor,0,.025,"search");
    SetWindowTextW(editor,L"中文搜索 \U0001f680");
    SendMessageW(editor,WM_IME_STARTCOMPOSITION,0,0);
    SendMessageW(editor,WM_KEYDOWN,VK_RETURN,0);SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);
    Expect(Editor(root)==editor,"IME composition Enter and Escape do not close search");
    SendMessageW(editor,WM_IME_ENDCOMPOSITION,0,0);
    SendMessageW(editor,EM_SETSEL,0,2);
    DWORD first=0,last=0;SendMessageW(editor,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
    Expect(first==0&&last==2,"native Unicode text selection remains available");
    SendMessageW(editor,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"替换"));
    Expect(read(editor)==L"替换搜索 \U0001f680","native selected-text replacement remains available");
    SendMessageW(editor,WM_UNDO,0,0);
    Expect(read(editor)==L"中文搜索 \U0001f680","native undo retains Unicode and surrogate pairs");
    SetWindowTextW(editor,L"W");SendMessageW(editor,EM_SETSEL,0,0);Pump();
    RECT before{},after{},rootBounds{};GetWindowRect(editor,&before);GetWindowRect(root,&rootBounds);
    const auto dpi=GetDpiForWindow(root);
    SetWindowPos(root,nullptr,0,0,MulDiv(520,static_cast<int>(dpi),96),
        rootBounds.bottom-rootBounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    Pump();GetWindowRect(editor,&after);
    Expect(after.right-after.left>before.right-before.left,"active search follows the resized organizer");
    ExpectEditorGlass(editor,0,.025,"search-resized");
    SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);
    Expect(!Editor(root),"Escape closes search and releases its native editor");
    layout=LayoutOrganizer(520,392,collection());
    SendMessageW(root,WM_LBUTTONDBLCLK,MK_LBUTTON,Point(root,layout.title));
    editor=Editor(root);Expect(editor!=nullptr,"title editing remains available");
    SetWindowTextW(editor,L"W");SendMessageW(editor,EM_SETSEL,0,0);Pump();
    ExpectEditorGlass(editor,.035,.10,"title");
    SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);
    Expect(!Editor(root),"cancelled title edit releases the composition window");
    session.Shutdown();Pump();
    Expect(!IsWindow(root)&&OrganizerIconCache::ActiveWorkers()==0,"editor fixture shuts down without a window or worker");
    std::cout<<"PASS organizer transparent native editing, selection, IME routing, resize and cleanup\n";
    return 0;
}
}
int GroupedClose(){
    using ui::PageCommand;using namespace platform::windows;
    {
        Files groupedFiles;const std::string host="00000000-0000-4000-8000-000000000501";
        auto state=organizer::Defaults();organizer::CreateCollection(state,host,u8"分组");
        organizer::MergeGroup(state,"default",host);std::string encoded;
        Expect(organizer::Encode(state,encoded),"grouped default state encodes");groupedFiles.data["grouped-close.json"]=encoded;
        shell::OrganizerSession session(GetModuleHandleW(nullptr),"grouped-close.json",{},[]{},&groupedFiles);
        Expect(session.Restore()&&session.Running()&&session.Handle(host),"default child tab starts under its host");
        auto reply=session.HandlePage(PageCommand::OrganizerDisable);
        Expect(reply.has_value(),"grouped disable returns an acknowledgement");
        const auto disabled=nlohmann::json::parse(OrganizerUtf8(*reply));
        Expect(disabled.value("saved",false)&&!disabled.value("enabled",true)&&!disabled.value("running",true)&&
            !session.Handle(host)&&!session.Running(),"saved disable must actually close the host containing default");
        session.HandlePage(PageCommand::OrganizerEnable);
        Expect(session.Running()&&session.Handle(host),"grouped organizer can reenable without losing its tabs");
        SendMessageW(session.Handle(host),WM_COMMAND,1,0);Pump();
        Expect(!session.Running()&&!session.Handle(host)&&!organizer::Find(session.Snapshot(),"default")->enabled,
            "right-click close command on default child tab stops every organizer window");
        organizer::Store store(groupedFiles,OrganizerPathKey);
        Expect(!organizer::Find(store.Load("grouped-close.json").value,"default")->enabled,
            "right-click close remains disabled when reloaded");
        session.Shutdown();
    }
    return 0;
}
int BatchDrop(){
    using ui::PageCommand;using namespace platform::windows;
    const auto directory=std::filesystem::temp_directory_path()/("lume-batch-import-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    struct Cleanup{std::filesystem::path directory;~Cleanup(){std::error_code error;std::filesystem::remove_all(directory,error);}}cleanup{directory};
    Files batchFiles;unsigned failures=0;
    shell::OrganizerSession session(GetModuleHandleW(nullptr),"batch-import.json",[&](const auto&,bool failed){failures+=failed?1:0;},[]{},&batchFiles);
    Expect(session.Restore(),"bulk fixture restores");session.HandlePage(PageCommand::OrganizerEnable);
    std::vector<std::wstring> paths;
    for(unsigned i=0;i<48;++i){const auto item=directory/("batch-"+std::to_string(i)+".txt");std::ofstream(item)<<i;paths.push_back(item.wstring());}
    DropFiles(session.Handle(),paths);
    Expect(organizer::Find(session.Snapshot(),"default")->items.size()==48&&!failures,
        "one desktop drop imports every item in a 48-file batch");
    organizer::Store reader(batchFiles,OrganizerPathKey);const auto reloaded=reader.Load("batch-import.json");
    Expect(organizer::Find(reloaded.value,"default")->items.size()==48,"entire bulk drop is persisted together");
    SendMessageW(session.Handle(),WM_COMMAND,4,0);Pump();
    DropFiles(session.Handle(),paths);
    Expect(organizer::Find(session.Snapshot(),"default")->items.size()==48,"collapsed preview does not truncate imported items or duplicate paths");
    session.HandlePage(PageCommand::OrganizerDisable);
    Expect(!session.Running()&&OrganizerIconCache::ActiveWorkers()==0,"bulk organizer stops its window and extraction worker");
    session.Shutdown();return 0;
}
int main(int argc,char** argv){
    using ui::PageCommand;using namespace platform::windows;
    Expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    if(argc==2&&std::string(argv[1])=="--material-unavailable"){
        const auto result=MaterialUnavailable();CoUninitialize();return result;
    }
    if(argc==2&&std::string(argv[1])=="--material-feedback"){
        const auto result=MaterialFeedback();CoUninitialize();return result;
    }
    if(argc==2&&std::string(argv[1])=="--editor-material"){
        const auto result=EditorMaterial();CoUninitialize();return result;
    }
    if(argc==2&&std::string(argv[1])=="--grouped-close"){const auto result=GroupedClose();CoUninitialize();return result;}
    if(argc==2&&std::string(argv[1])=="--batch-drop"){const auto result=BatchDrop();CoUninitialize();return result;}
    Expect(argc==1,"Usage: lume-organizer-session-test [--material-unavailable|--material-feedback|--editor-material|--grouped-close|--batch-drop]");
    Expect(OrganizerPathKey(u8"C:\\ÉCOLE.TXT")==OrganizerPathKey(u8"c:\\école.txt"),"Unicode path identity");
    Expect(OrganizerSearchKey(u8"ＡＢＣ　报告")=="abc "+std::string(u8"报告"),"NFKC search matching");
    {
    auto searchCollection=organizer::Defaults().collections.front();
    OrganizerViewState searchView;
    auto searchLayout=LayoutOrganizer(420,392,searchCollection,searchView);
    auto glassPanels=OrganizerSearchPanels(searchLayout,searchView);
    Expect(glassPanels.size()==1&&glassPanels[0].x==searchLayout.search.x&&
        glassPanels[0].radius<0,"closed organizer search uses the launchpad's round glass button");
    searchView.searchOpen=true;
    searchLayout=LayoutOrganizer(420,392,searchCollection,searchView);
    glassPanels=OrganizerSearchPanels(searchLayout,searchView);
    Expect(glassPanels.size()==2&&glassPanels[1].x==searchLayout.searchInput.x-6&&
        glassPanels[1].height==searchLayout.searchInput.height+4&&
        glassPanels[1].radius==glassPanels[1].height/2,
        "expanded organizer search uses the launchpad's pill glass panel");
    searchCollection.mode=organizer::Mode::Collapsed;
    searchLayout=LayoutOrganizer(180,216,searchCollection,searchView);
    Expect(OrganizerSearchPanels(searchLayout,searchView).size()==1,
        "collapsed organizer never adds an offscreen search input panel");
    }
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto directory=std::filesystem::path(executable).parent_path()/"organizer-fixtures"/std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(directory/"folder");
    const auto file=directory/L"资料.txt",shortcut=directory/L"链接.lnk";
    std::ofstream(file)<<"keep this file";
    WriteShortcut(shortcut,executable);
    Files files;unsigned notifications=0;
    {
        shell::OrganizerSession session(GetModuleHandleW(nullptr),"organizer.json",[&](const auto&,bool){++notifications;},[]{},&files);
        Expect(session.Restore()&&!session.Running(),"fresh collection data does not auto-enable module");
        auto reply=session.HandlePage(PageCommand::OrganizerEnable);
        Expect(reply&&reply->find(L"\"saved\":true")!=std::wstring::npos&&session.Running(),"native organizer enable saved");
        Expect((GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_ACCEPTFILES)!=0,"only opted-in organizer accepts shell drops");
        MINMAXINFO expandedLimits{};
        SendMessageW(session.Handle(),WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&expandedLimits));
        const auto expandedDpi=static_cast<int>(GetDpiForWindow(session.Handle()));
        Expect(expandedLimits.ptMinTrackSize.x==MulDiv(120,expandedDpi?expandedDpi:96,96),
            "expanded organizer can be resized down to one icon column");
        Expect(expandedLimits.ptMaxTrackSize.x==MulDiv(1200,expandedDpi?expandedDpi:96,96),
            "expanded organizer supports a wide layout on large screens");
        DropFiles(session.Handle(),{file.wstring(),shortcut.wstring(),(directory/"folder").wstring()});
        const auto* collection=organizer::Find(session.Snapshot(),"default");
        Expect(collection&&collection->items.size()==3&&collection->items[1].kind==organizer::ItemKind::Shortcut&&collection->items[1].name==u8"链接","file-drop message accepts real local targets with reference kinds and names");
        Expect(collection->iconScale==70,"new organizer defaults to 70 percent icons");
        const auto oneColumn=LayoutOrganizer(120,392,*collection);
        Expect(oneColumn.cells.size()==3&&oneColumn.cells[0].bounds.x==oneColumn.cells[1].bounds.x&&
            oneColumn.cells[0].bounds.x==oneColumn.cells[2].bounds.x,
            "one-column organizer layout keeps all icons inside a compact width");
        const auto twoColumn=LayoutOrganizer(180,392,*collection);
        Expect(twoColumn.cells.size()==3&&twoColumn.cells[0].bounds.x!=twoColumn.cells[1].bounds.x&&
            twoColumn.cells[0].bounds.x==twoColumn.cells[2].bounds.x,
            "two-column organizer layout keeps both icon columns inside the narrow width");
        WaitForIconBatch();
        for(const int scale:{50,100,120}){
            const auto parsed=ui::ParsePageCommand(L"v1|organizer|iconScale|"+std::to_wstring(scale));
            Expect(parsed!=PageCommand::Invalid,"icon scale command parses");
            const auto sizeReply=session.HandlePage(parsed);
            Expect(sizeReply&&nlohmann::json::parse(OrganizerUtf8(*sizeReply)).at("iconScale")==scale,"icon size command acknowledged");
            const auto narrow=LayoutOrganizer(120,392,*organizer::Find(session.Snapshot(),"default"));
            for(const auto& cell:narrow.cells)Expect(cell.bounds.x>=narrow.viewport.x&&
                cell.bounds.x+cell.bounds.width<=narrow.viewport.x+narrow.viewport.width+.01f,"all icon sizes fit the narrow column");
        }
        files.fail=true;session.HandlePage(ui::ParsePageCommand(L"v1|organizer|iconScale|60"));
        Expect(organizer::Find(session.Snapshot(),"default")->iconScale==120,"failed icon size save restores previous size");
        files.fail=false;session.HandlePage(ui::ParsePageCommand(L"v1|organizer|iconScale|100"));
        Expect(session.Running()&&IsWindow(session.Handle()),"completed file-drop extraction keeps organizer usable");
        Expect(!session.AddPaths({(directory/"missing.txt").u8string()})&&collection->items.size()==3,"missing target rejected without partial changes");
        Expect(session.AddPaths({file.u8string()})&&collection->items.size()==3,"duplicate path does not consume a slot");
        const auto previous=files.data["organizer.json"];files.fail=true;
        reply=session.HandlePage(PageCommand::OrganizerLiquidGlass);
        Expect(reply&&reply->find(L"\"error\":\"save\"")!=std::wstring::npos&&session.Running()&&
            organizer::Find(session.Snapshot(),"default")->frosted&&files.data["organizer.json"]==previous,"material save failure rolls back runtime");
        files.fail=false;session.HandlePage(PageCommand::OrganizerFrostedGlass);
        const auto bounds=organizer::Find(session.Snapshot(),"default")->bounds;
        Expect(bounds&&bounds->width==420&&bounds->height==392,"source default dimensions persisted");
        // Icons carry no remove button: clicks only select, and 删除图标 in the
        // icon menu (command 13) takes the selected reference out.
        const auto dpi=GetDpiForWindow(session.Handle());
        const auto layout=LayoutOrganizer(420,392,*organizer::Find(session.Snapshot(),"default"));
        const auto cell=layout.cells.front().bounds;
        const auto cellPoint=MAKELPARAM(MulDiv(static_cast<int>(cell.x+cell.width-12),static_cast<int>(dpi),96),
            MulDiv(static_cast<int>(cell.y+12),static_cast<int>(dpi),96));
        SendMessageW(session.Handle(),WM_LBUTTONUP,0,cellPoint);
        SendMessageW(session.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,cellPoint);
        SendMessageW(session.Handle(),WM_LBUTTONUP,0,cellPoint);
        Expect(organizer::Find(session.Snapshot(),"default")->items.size()==3,"clicking where the remove button was never removes a reference");
        SendMessageW(session.Handle(),WM_COMMAND,13,0);
        Expect(organizer::Find(session.Snapshot(),"default")->items.size()==2,"删除图标 removes the selected reference");
        Expect(std::filesystem::exists(file)&&std::filesystem::exists(shortcut),"reference removal never deletes source files");
        SendMessageW(session.Handle(),WM_CLOSE,0,0);
        Expect(!session.Handle()&&!session.Running()&&notifications>0&&OrganizerIconCache::ActiveWorkers()==0,
            "native close releases window, icon worker and notifies host");
        Expect(!organizer::Find(session.Snapshot(),"default")->enabled,"native close persisted disabled");
        session.Shutdown();Expect(!session.HandlePage(PageCommand::OrganizerEnable),"shutdown cannot revive");
    }
    {
        shell::OrganizerSession session(GetModuleHandleW(nullptr),"organizer.json",{},[]{},&files);
        Expect(session.Restore()&&!session.Running()&&organizer::Find(session.Snapshot(),"default")->items.size()==2,"references reload while disabled");
    }
    {
        auto state=organizer::Defaults();
        Expect(organizer::CreateCollection(state,"00000000-0000-4000-8000-000000000001",u8"独立收纳",
            organizer::Bounds{80,80,420,392})==organizer::Change::Changed,"second collection fixture");
        std::string encoded;Expect(organizer::Encode(state,encoded),"multiple collection fixture encoded");
        Files multiple;multiple.data["multiple.json"]=encoded;
        shell::OrganizerSession session(GetModuleHandleW(nullptr),"multiple.json",{},[]{},&multiple);
        Expect(session.Restore(),"multiple collection restore");
        unsigned count=0;
        EnumWindows([](HWND hwnd,LPARAM data)->BOOL{
            DWORD process=0;GetWindowThreadProcessId(hwnd,&process);
            if(process==GetCurrentProcessId()&&GetPropW(hwnd,L"LumeDesktopWidget"))
                ++*reinterpret_cast<unsigned*>(data);
            return TRUE;
        },reinterpret_cast<LPARAM>(&count));
        Expect(count==2,"every enabled collection restores an independent native window");
        const auto second=session.Handle("00000000-0000-4000-8000-000000000001");
        Expect(second&&second!=session.Handle(),"collection IDs map to distinct windows");
        SendMessageW(second,WM_CLOSE,0,0);
        Expect(session.Running()&&session.Handle()&&!session.Handle("00000000-0000-4000-8000-000000000001"),
            "closing one extra collection leaves the default window alive");
    }
    {
        Files boundaryFiles;
        shell::OrganizerSession session(GetModuleHandleW(nullptr),"boundary.json",{},[]{},&boundaryFiles);
        session.HandlePage(PageCommand::OrganizerEnable);
        std::vector<std::string> paths;
        for(unsigned i=0;i<4;++i){const auto path=directory/("boundary-"+std::to_string(i)+".txt");std::ofstream(path)<<i;paths.push_back(path.u8string());}
        Expect(session.AddPaths(paths),"boundary fixtures added");
        const auto hwnd=session.Handle();
        const auto order=[&]{std::vector<std::string> ids;for(const auto& item:session.Snapshot().collections.front().items)ids.push_back(item.id);return ids;};
        const auto layout=LayoutOrganizer(420,392,session.Snapshot().collections.front());
        const auto initial=order();
        const auto groupPoint=Point(hwnd,layout.cells[2].bounds);
        Click(hwnd,layout.cells[0].bounds);
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON|MK_CONTROL,groupPoint);
        SendMessageW(hwnd,WM_LBUTTONUP,MK_CONTROL,groupPoint);
        const auto groupNearby=Point(hwnd,{layout.cells[2].bounds.x+30,layout.cells[2].bounds.y+20,0,0});
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,groupPoint);
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,groupNearby);
        SendMessageW(hwnd,WM_LBUTTONUP,0,groupNearby);
        Expect(order()==initial,"moving within a selected group's own slot must not compact or reorder it");
        const auto p=Point(hwnd,layout.cells[1].bounds);
        const auto nearby=Point(hwnd,{layout.cells[1].bounds.x+30,layout.cells[1].bounds.y+20,0,0});
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,p);
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,nearby);
        SendMessageW(hwnd,WM_LBUTTONUP,0,nearby);
        Expect(order()==initial,"moving inside the grabbed item's slot must not reorder it");

        Click(hwnd,layout.cells[0].bounds);
        const auto second=Point(hwnd,layout.cells[1].bounds);
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON|MK_CONTROL,second);
        SendMessageW(hwnd,WM_LBUTTONUP,MK_CONTROL,second);
        const auto searchLayout=LayoutOrganizer(420,392,session.Snapshot().collections.front());
        Click(hwnd,searchLayout.search);
        const auto editor=Editor(hwnd);Expect(editor!=nullptr,"boundary search editor opens");
        SendMessageW(editor,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"boundary-0"));
        SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);
        SendMessageW(hwnd,WM_KEYDOWN,VK_DELETE,0);
        const auto afterFilterDelete=order();
        Expect(afterFilterDelete.size()==3&&std::find(afterFilterDelete.begin(),afterFilterDelete.end(),initial[0])==afterFilterDelete.end(),
            "remaining filtered multi-selection stays actionable after focused item disappears");

        auto remaining=LayoutOrganizer(420,392,session.Snapshot().collections.front());
        Click(hwnd,remaining.cells[0].bounds);
        const auto add=Point(hwnd,remaining.cells[1].bounds);
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON|MK_CONTROL,add);
        SendMessageW(hwnd,WM_LBUTTONUP,MK_CONTROL,add);
        const auto beforeFailedDelete=order();boundaryFiles.fail=true;
        SendMessageW(hwnd,WM_KEYDOWN,VK_DELETE,0);
        Expect(order()==beforeFailedDelete,"failed multi-delete restores references");
        boundaryFiles.fail=false;
        SendMessageW(hwnd,WM_KEYDOWN,VK_DELETE,0);
        Expect(order().size()==1,"failed multi-delete keeps selection so retry deletes the same group");

        remaining=LayoutOrganizer(420,392,session.Snapshot().collections.front());
        const auto last=Point(hwnd,remaining.cells[0].bounds);
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,last);
        SendMessageW(hwnd,WM_KEYDOWN,VK_DELETE,0);
        Expect(GetCapture()!=hwnd,"deleting a pressed item releases stale pointer capture");
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(350,300));
        SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(350,300));
        Expect(order().empty(),"moving after pressed-item deletion is safe and does not revive it");
    }
    {
        Files dragFiles;
        std::vector<std::wstring> exported;bool releasedOnDesktop=false;
        shell::OrganizerSession session(GetModuleHandleW(nullptr),"drag.json",{},[]{},&dragFiles,{},
            [&](HWND,const auto& paths){exported=paths;return releasedOnDesktop;});
        bool overDesktop=false;session.SetDesktopProbeForTest([&](POINT){return overDesktop;});
        session.HandlePage(PageCommand::OrganizerEnable);
        std::vector<std::string> paths;
        for(unsigned i=0;i<6;++i){const auto path=directory/("drag-"+std::to_string(i)+".txt");std::ofstream(path)<<i;paths.push_back(path.u8string());}
        Expect(session.AddPaths(paths),"batch drag fixtures added");
        const auto hwnd=session.Handle();
        const auto order=[&]{std::vector<std::string> ids;for(const auto& item:session.Snapshot().collections.front().items)ids.push_back(item.id);return ids;};
        const auto initial=order();
        auto layout=LayoutOrganizer(420,392,session.Snapshot().collections.front());
        const auto click=[&](std::size_t index,WPARAM modifiers){
            const auto p=Point(hwnd,layout.cells[index].bounds);
            SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON|modifiers,p);SendMessageW(hwnd,WM_LBUTTONUP,modifiers,p);
        };
        click(0,0);click(2,MK_CONTROL);
        const auto writes=dragFiles.writes;
        const auto commits=dragFiles.commits;
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,Point(hwnd,layout.cells[0].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,Point(hwnd,layout.cells[4].bounds));
        Expect(order()==initial&&dragFiles.writes==writes,"live group preview does not mutate model or write settings");
        SendMessageW(hwnd,WM_LBUTTONUP,0,Point(hwnd,layout.cells[4].bounds));
        Expect(order()==std::vector<std::string>({initial[1],initial[3],initial[0],initial[2],initial[4],initial[5]}),
            "Ctrl-selected discontiguous items move together in original order");
        Expect(dragFiles.commits==commits+1,"group drop commits exactly once");
        const auto committed=order();
        layout=LayoutOrganizer(420,392,session.Snapshot().collections.front());
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,Point(hwnd,layout.cells[4].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,Point(hwnd,layout.cells[0].bounds));
        SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);
        SendMessageW(hwnd,WM_LBUTTONUP,0,Point(hwnd,layout.cells[0].bounds));
        Expect(order()==committed&&dragFiles.commits==commits+1,"Escape restores preview without saving or opening a target");
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,Point(hwnd,layout.cells[4].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,Point(hwnd,layout.cells[0].bounds));
        ReleaseCapture();
        SendMessageW(hwnd,WM_LBUTTONUP,0,Point(hwnd,layout.cells[0].bounds));
        Expect(order()==committed,"lost capture cancels group drag");
        dragFiles.fail=true;
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,Point(hwnd,layout.cells[4].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,Point(hwnd,layout.cells[0].bounds));
        SendMessageW(hwnd,WM_LBUTTONUP,0,Point(hwnd,layout.cells[0].bounds));
        Expect(order()==committed,"failed group save restores original order");
        dragFiles.fail=false;
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,Point(hwnd,layout.cells[4].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-30,-30));
        SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(-30,-30));
        Expect(order()==committed&&dragFiles.commits==commits+1,"dropping outside the collection restores original order without saving");
        Expect(!exported.empty(),"external drag exports selected paths through the Shell port");
        // Released over the desktop, the dragged icons leave the collection; the files stay.
        releasedOnDesktop=true;exported.clear();
        click(1,0);
        const auto dropped=order()[1];
        const auto droppedPath=session.Snapshot().collections.front().items[1].path;
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,Point(hwnd,layout.cells[1].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-30,-30));
        const auto left=order();
        Expect(exported.size()==1&&left.size()==5&&std::find(left.begin(),left.end(),dropped)==left.end(),
            "dragging an icon onto the desktop takes it out of the collection");
        Expect(std::filesystem::exists(directory/"drag-0.txt"),"taking an icon out never deletes its file");
        releasedOnDesktop=false;
        Expect(session.AddPaths({droppedPath}),"the icon can be put back");
        // Dragged straight onto the desktop: the icon follows the pointer with
        // "移除" (as on the Dock) and the release takes it out.
        overDesktop=true;exported.clear();
        layout=LayoutOrganizer(420,392,session.Snapshot().collections.front());
        click(2,0);
        const auto ghostId=order()[2];const auto ghostPath=session.Snapshot().collections.front().items[2].path;
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,Point(hwnd,layout.cells[2].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,Point(hwnd,layout.cells[3].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-40,-40));
        const auto ghost=FindWindowW(L"LumeOrganizerDragIcon",nullptr);
        Expect(ghost&&IsWindowVisible(ghost)&&exported.empty()&&order().size()==6,"over the desktop the icon follows the pointer without a shell drag");
        SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(-40,-40));
        const auto afterGhost=order();
        Expect(afterGhost.size()==5&&std::find(afterGhost.begin(),afterGhost.end(),ghostId)==afterGhost.end()&&
            !FindWindowW(L"LumeOrganizerDragIcon",nullptr),"releasing over the desktop removes the icon and its ghost");
        overDesktop=false;
        Expect(session.AddPaths({ghostPath}),"the second icon can be put back");
        layout=LayoutOrganizer(420,392,session.Snapshot().collections.front());

        click(0,0);click(2,MK_SHIFT);
        const auto beforeRange=order();
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,Point(hwnd,layout.cells[0].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,Point(hwnd,layout.cells[5].bounds));
        SendMessageW(hwnd,WM_LBUTTONUP,0,Point(hwnd,layout.cells[5].bounds));
        Expect(order()==std::vector<std::string>({beforeRange[3],beforeRange[4],beforeRange[0],beforeRange[1],beforeRange[2],beforeRange[5]}),
            "Shift-selected range moves as a stable ordered group");

        layout=LayoutOrganizer(420,392,session.Snapshot().collections.front());
        const auto beforeMarquee=order();
        const auto blank=Point(hwnd,{layout.viewport.x+1,layout.viewport.y+1,0,0});
        const auto end=Point(hwnd,{layout.cells[1].bounds.x+layout.cells[1].bounds.width-1,
            layout.cells[1].bounds.y+layout.cells[1].bounds.height-1,0,0});
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,blank);
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,end);
        SendMessageW(hwnd,WM_LBUTTONUP,0,end);
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,Point(hwnd,layout.cells[0].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,Point(hwnd,layout.cells[5].bounds));
        SendMessageW(hwnd,WM_LBUTTONUP,0,Point(hwnd,layout.cells[5].bounds));
        Expect(order()==std::vector<std::string>({beforeMarquee[2],beforeMarquee[3],beforeMarquee[4],beforeMarquee[0],beforeMarquee[1],beforeMarquee[5]}),
            "blank-area marquee selection can be dragged as a group");

        layout=LayoutOrganizer(420,392,session.Snapshot().collections.front());
        SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,Point(hwnd,layout.cells[0].bounds));
        SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,Point(hwnd,layout.cells[4].bounds));
        const auto beforeReset=order();
        session.HandlePage(PageCommand::OrganizerReset);
        SendMessageW(hwnd,WM_LBUTTONUP,0,Point(hwnd,layout.cells[4].bounds));
        Expect(order()==beforeReset&&GetCapture()!=hwnd,"reset while dragging cancels preview and retains existing file references");
        Expect(std::all_of(paths.begin(),paths.end(),[](const auto& path){return std::filesystem::exists(std::filesystem::u8path(path));}),
            "dragging and cancellation never move source files");
    }
    {
        const auto normalizedName=directory/L"ＡＢＣ　报告.txt";
        std::ofstream(normalizedName)<<"normalization fixture";
        Files interactions;std::vector<std::wstring> opened;
        shell::OrganizerSession session(GetModuleHandleW(nullptr),"interactions.json",{},[]{},&interactions,
            [&](HWND,const std::wstring& target){opened.push_back(target);return true;});
        session.HandlePage(PageCommand::OrganizerEnable);
        Expect(session.AddPaths({file.u8string(),normalizedName.u8string(),shortcut.u8string(),(directory/"folder").u8string()}),
            "interaction references added");
        const auto root=session.Handle();
        const auto collection=[&]()->const organizer::Collection& {return *organizer::Find(session.Snapshot(),"default");};
        auto layout=LayoutOrganizer(420,392,collection());
        Expect(std::abs(layout.cells[0].bounds.width-57.4f)<.01f&&layout.cells[0].bounds.height==60&&
               layout.viewport.y==65&&layout.cells[0].bounds.y==67,
            "70 percent icon-grid track size keeps header spacing");
        Click(root,layout.cells[0].bounds);
        Expect(opened.empty(),"single click selects without opening the target");
        SendMessageW(root,WM_KEYDOWN,VK_RETURN,0);
        Expect(opened.size()==1&&opened.back()==file.wstring(),"Enter opens the selected target");
        SendMessageW(root,WM_LBUTTONDBLCLK,MK_LBUTTON,Point(root,layout.cells[1].bounds));
        Expect(opened.size()==2&&opened.back()==normalizedName.wstring(),"double click opens its target");
        SendMessageW(root,WM_SYSKEYDOWN,VK_LEFT,static_cast<LPARAM>(1)<<29);
        Expect(collection().items.front().path==normalizedName.u8string(),
            "Alt plus Left is routed as a system key and reorders the focused item");
        SendMessageW(root,WM_SYSKEYDOWN,VK_RIGHT,static_cast<LPARAM>(1)<<29);
        Expect(collection().items[1].path==normalizedName.u8string(),"Alt plus Right restores the original item order");

        Click(root,layout.search);
        auto editor=Editor(root);Expect(editor!=nullptr,"search button opens a real IME-capable editor");
        const auto editorDc=GetDC(editor);
        const auto editorBrush=SendMessageW(root,WM_CTLCOLOREDIT,reinterpret_cast<WPARAM>(editorDc),reinterpret_cast<LPARAM>(editor));
        Expect(editorBrush&&GetTextColor(editorDc)==RGB(255,255,255)&&
            (GetWindowLongPtrW(editor,GWL_EXSTYLE)&WS_EX_LAYERED)&&!(GetWindowLongPtrW(editor,GWL_STYLE)&WS_BORDER),
            "native editor keeps white text over the existing glass without an opaque native border");
        ReleaseDC(editor,editorDc);
        SetWindowTextW(editor,L"ａｂｃ 报告");
        OrganizerViewState filtered;filtered.searchOpen=true;filtered.filtered=true;filtered.query=u8"ａｂｃ 报告";filtered.visible={1};
        layout=LayoutOrganizer(420,392,collection(),filtered);
        SendMessageW(root,WM_LBUTTONDBLCLK,MK_LBUTTON,Point(root,layout.cells[0].bounds));
        Expect(opened.size()==3&&opened.back()==normalizedName.wstring()&&!Editor(root),
            "NFKC lowercase search hits map back to the original item and opening closes search");

        layout=LayoutOrganizer(420,392,collection());
        SendMessageW(root,WM_LBUTTONDBLCLK,MK_LBUTTON,Point(root,layout.title));
        editor=Editor(root);Expect(editor!=nullptr,"heading double click begins collection rename");
        SetWindowTextW(editor,L"  学习资料  ");SendMessageW(editor,WM_KEYDOWN,VK_RETURN,0);
        Expect(collection().title==u8"学习资料"&&!Editor(root),"Enter commits sanitized collection title");
        Expect(std::filesystem::exists(file)&&std::filesystem::exists(normalizedName)&&
            collection().items[1].name==u8"ＡＢＣ　报告.txt","renaming collection never renames source files or item names");
        SendMessageW(root,WM_LBUTTONDBLCLK,MK_LBUTTON,Point(root,layout.title));
        editor=Editor(root);SetWindowTextW(editor,L"不要保存");SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);
        Expect(collection().title==u8"学习资料","Escape cancels title editing");
        interactions.fail=true;
        SendMessageW(root,WM_LBUTTONDBLCLK,MK_LBUTTON,Point(root,layout.title));
        editor=Editor(root);SetWindowTextW(editor,L"保存失败");SendMessageW(editor,WM_KEYDOWN,VK_RETURN,0);
        Expect(collection().title==u8"学习资料"&&session.Running(),"failed rename save rolls back data and runtime");
        interactions.fail=false;

        SendMessageW(root,WM_COMMAND,6,0);
        Expect(collection().layout==organizer::Layout::List,"native menu list command persists");
        layout=LayoutOrganizer(420,392,collection());
        Expect(layout.cells[0].bounds.height==42&&layout.cells[1].bounds.y-layout.cells[0].bounds.y==46,
            "source list rows use 42 pixels and a 4-pixel gap");
        SendMessageW(root,WM_COMMAND,4,0);
        RECT rectangle{};GetWindowRect(root,&rectangle);
        const auto dpi=static_cast<int>(GetDpiForWindow(root));
        Expect(collection().mode==organizer::Mode::Expanded&&
            MulDiv(rectangle.right-rectangle.left,96,dpi)==420,"retired collapse command leaves the organizer expanded");
        MINMAXINFO limits{};SendMessageW(root,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
        Expect(limits.ptMinTrackSize.x==MulDiv(120,dpi,96)&&limits.ptMaxTrackSize.x>limits.ptMinTrackSize.x,"organizer remains freely resizable to 120 DIP");
        SendMessageW(root,WM_COMMAND,160,0);
        Expect(collection().iconScale==60,"right click icon size command is scoped to this organizer");
        SendMessageW(root,WM_COMMAND,200,0);
        layout=LayoutOrganizer(420,392,collection());
        Click(root,layout.search);
        Expect(collection().mode==organizer::Mode::Expanded&&Editor(root),"search works in the expanded organizer");
        GetWindowRect(root,&rectangle);
        Expect(MulDiv(rectangle.right-rectangle.left,96,dpi)==420,"temporary search uses expanded default width");

        SendMessageW(root,WM_COMMAND,3,0);
        Expect(session.Snapshot().collections.size()==2,"native create command adds a collection");
        const auto secondId=session.Snapshot().collections.back().id;
        const auto second=session.Handle(secondId);
        Expect(second&&second!=root,"new collection creates its own window");
        Expect(session.AddPaths(secondId,{(directory/"folder").u8string()}),"extra collection accepts its own references");
        Expect(collection().items.size()==4&&organizer::Find(session.Snapshot(),secondId)->items.size()==1,
            "collection additions remain scoped");
        SetFocus(second);Pump();
        GetWindowRect(root,&rectangle);
        Expect(!Editor(root)&&MulDiv(rectangle.right-rectangle.left,96,dpi)==420&&collection().mode==organizer::Mode::Expanded,
            "focus loss closes search without changing organizer geometry");
        session.HandlePage(PageCommand::OrganizerDisable);
        Expect(!session.Running()&&!session.Handle(secondId)&&OrganizerIconCache::ActiveWorkers()==0,
            "module disable releases every collection window and icon worker");
        session.HandlePage(PageCommand::OrganizerEnable);
        Expect(session.Handle()&&session.Handle(secondId),"module enable restores still-enabled extra collections");
        SendMessageW(session.Handle(secondId),WM_COMMAND,3,0);Pump();
        const auto emptyId=session.Snapshot().collections.back().id;
        Expect(emptyId!=secondId&&session.Handle(emptyId),"a further empty box is created");
        SendMessageW(session.Handle(emptyId),WM_CLOSE,0,0);Pump();
        Expect(session.Handle()&&session.Handle(secondId)&&!organizer::Find(session.Snapshot(),emptyId),
            "closing an empty extra box deletes it instead of hiding it forever");
        session.Shutdown();Pump();
        Expect(OrganizerIconCache::ActiveWorkers()==0,"interaction session shutdown releases all icon workers");
    }
    {
        // Tab groups through real windows: merge on header release, click
        // to switch, per-tab drops, long-press detach and persistence.
        Files groupFiles;
        shell::OrganizerSession session(GetModuleHandleW(nullptr),"groups.json",{},[]{},&groupFiles,
            [](HWND,const std::wstring&){return true;});
        session.HandlePage(PageCommand::OrganizerEnable);
        const auto root=session.Handle();Expect(root!=nullptr,"group fixture root window");
        SendMessageW(root,WM_COMMAND,3,0);Pump();
        const auto otherId=session.Snapshot().collections.back().id;
        const auto other=session.Handle(otherId);Expect(other&&other!=root,"second box for grouping");
        Expect(session.AddPaths(otherId,{file.u8string()}),"second box has its own icon");
        const auto dpi=static_cast<int>(GetDpiForWindow(root));
        // Resize the root first: collapse/expand must keep this size.
        RECT rootRect{};GetWindowRect(root,&rootRect);
        SetWindowPos(root,nullptr,0,0,MulDiv(500,dpi,96),MulDiv(480,dpi,96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(root,WM_EXITSIZEMOVE,0,0);Pump();
        SendMessageW(root,WM_COMMAND,4,0);Pump();SendMessageW(root,WM_COMMAND,5,0);Pump();
        GetWindowRect(root,&rootRect);
        Expect(MulDiv(rootRect.right-rootRect.left,96,dpi)==500&&MulDiv(rootRect.bottom-rootRect.top,96,dpi)==480,
            "collapse then expand restores the user's expanded size");
        // Place the second box elsewhere and drop it on the root's header.
        SetWindowPos(other,nullptr,rootRect.right+MulDiv(40,dpi,96),rootRect.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        Pump();
        const auto rootDefault=organizer::Find(session.Snapshot(),"default")->title;
        SetCursorPos(rootRect.left+MulDiv(150,dpi,96),rootRect.top+MulDiv(30,dpi,96));
        {
            POINT cursor{};GetCursorPos(&cursor);RECT r{};GetWindowRect(root,&r);
            Expect(PtInRect(&r,cursor)!=FALSE,"test cursor is over the root header");
        }
        // The OS move loop cannot run synchronously in a test; this drives the
        // same header hit-test and deferred merge that ends the loop.
        Expect(session.MergeAtCursorForTest(otherId),"release over the root header merges");Pump();
        Expect(!session.Handle(otherId)&&session.Handle()==root,"merged box window closes, target survives");
        const auto& merged=session.Snapshot();
        Expect(organizer::Tabs(merged,"default").size()==2&&organizer::ActiveTab(merged,"default")->id==otherId,
            "merged box becomes the active second tab");
        auto layout=LayoutOrganizer(500,480,*organizer::ActiveTab(merged,"default"),[&]{
            OrganizerViewState v;v.tabs={OrganizerWide(rootDefault),L"新建收纳"};v.activeTab=1;return v;}());
        Expect(layout.tabs.size()==2&&layout.tabStrip.width>0,"expanded group lays out a tab strip");
        // Click the first tab to switch back; the root's own items show.
        Click(root,layout.tabs[0]);Pump();
        Expect(organizer::ActiveTab(session.Snapshot(),"default")->id=="default","clicking a tab switches the visible collection");
        Expect(session.AddPaths("default",{(directory/"folder").u8string()}),"drop into the visible tab");
        Expect(organizer::Find(session.Snapshot(),otherId)->items.size()==1,"the hidden tab keeps its own icons");
        // Partial drops add the valid paths and report the first problem.
        const auto before=organizer::Find(session.Snapshot(),"default")->items.size();
        Expect(!session.AddPaths("default",{(directory/"missing.txt").u8string(),shortcut.u8string(),"\\\\nas\\share\\x"})&&
            organizer::Find(session.Snapshot(),"default")->items.size()==before+1,"one bad path no longer rejects the batch");
        // Long-press the second tab and release far outside: it detaches.
        layout=LayoutOrganizer(500,480,*organizer::ActiveTab(session.Snapshot(),"default"),[&]{
            OrganizerViewState v;v.tabs={OrganizerWide(rootDefault),L"新建收纳"};v.activeTab=0;return v;}());
        const auto tabPoint=Point(root,layout.tabs[1]);
        SendMessageW(root,WM_LBUTTONDOWN,MK_LBUTTON,tabPoint);
        // Pump through the 400 ms long-press timer instead of one fixed sleep.
        for(const auto until=GetTickCount64()+700;GetTickCount64()<until;){Pump();Sleep(10);}
        SetCursorPos(rootRect.right+MulDiv(300,dpi,96),rootRect.top+MulDiv(200,dpi,96));
        SendMessageW(root,WM_MOUSEMOVE,MK_LBUTTON,tabPoint);
        SendMessageW(root,WM_LBUTTONUP,0,tabPoint);Pump();
        Expect(organizer::Tabs(session.Snapshot(),"default").size()==1&&session.Handle(otherId)&&
            organizer::Find(session.Snapshot(),otherId)->items.size()==1,"long-press and release outside detaches the tab with its icons");
        // A short press never detaches.
        SetCursorPos(rootRect.left+MulDiv(150,dpi,96),rootRect.top+MulDiv(30,dpi,96));
        Expect(session.MergeAtCursorForTest(otherId),"merge again");Pump();
        SendMessageW(root,WM_LBUTTONDOWN,MK_LBUTTON,tabPoint);SendMessageW(root,WM_LBUTTONUP,0,tabPoint);Pump();
        Expect(organizer::Tabs(session.Snapshot(),"default").size()==2,"a short tab click does not detach");
        // Persistence round trip of the group.
        organizer::Store reader(groupFiles,OrganizerPathKey);
        const auto loaded=reader.Load("groups.json");
        Expect(loaded.state==organizer::LoadState::Loaded&&organizer::Tabs(loaded.value,"default").size()==2,"tab group persisted");
        session.Shutdown();Pump();
    }
    GroupedClose();
    BatchDrop();
    CoUninitialize();std::cout<<"organizer native lifecycle and local references passed\n";
}
