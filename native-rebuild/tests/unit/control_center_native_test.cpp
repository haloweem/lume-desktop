#include "platform/windows/control_center/control_center_window.h"
#include "platform/windows/control_center/panel_worker.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/material/material_api.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <tlhelp32.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
using namespace lume::platform::windows;
namespace cc=lume::platform::windows::control_center;
using Json=nlohmann::json;
namespace {
void Check(bool value,const char* text){if(!value){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
struct Apartment {HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Apartment(){Check(SUCCEEDED(result),"match the production shell's UI COM apartment");}void Close(){if(SUCCEEDED(result))CoUninitialize();result=E_FAIL;}~Apartment(){Close();}};
void Pump(unsigned duration=10){const auto end=GetTickCount64()+duration;do{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(2);}while(GetTickCount64()<end);}
unsigned prewarmShows=0;
void CALLBACK PrewarmShow(HWINEVENTHOOK,DWORD,HWND window,LONG object,LONG,DWORD,DWORD){
    wchar_t name[64]{};
    if(object==OBJID_WINDOW&&GetClassNameW(window,name,64)&&wcscmp(name,L"LumeControlCenter")==0)++prewarmShows;
}
template<class F>void Wait(F test,const char* text){const auto end=GetTickCount64()+3000;while(!test()&&GetTickCount64()<end)Pump();Check(test(),text);Pump();}
void PrewarmVisibilityRegression(HWND owner){
    std::atomic<unsigned> reads{0};
    auto worker=std::make_shared<cc::PanelWorker>([&](auto,const auto&){++reads;Sleep(35);return std::string("{}");});
    const auto hook=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_SHOW,nullptr,PrewarmShow,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
    Check(hook!=nullptr,"observe transient prewarm show events");prewarmShows=0;
    unsigned samples=0;
    {
        ControlCenterWindow center(worker);
        const ControlCenterWindow::Material modes[]{ControlCenterWindow::Material::Liquid,ControlCenterWindow::Material::Frosted,ControlCenterWindow::Material::None};
        for(unsigned cycle=0;cycle<6;++cycle){
            Check(center.Prewarm(GetModuleHandleW(nullptr),owner,modes[cycle%3]),"Dock startup and changed materials prewarm successfully");
            const auto window=center.Handle();const auto dpi=GetDpiForWindow(window);RECT suggested{8,8,380,428};
            SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
            const auto until=GetTickCount64()+2200;
            do{Pump(2);++samples;Check(!IsWindowVisible(window)&&!center.IsOpen(),"prewarm stays hidden through asynchronous state and DPI callbacks");}while(GetTickCount64()<until);
            Check(!worker->Running(),"prewarm completes without hidden polling");
        }
    }
    UnhookWinEvent(hook);
    Check(prewarmShows==0&&reads==6,"six prewarm generations have no transient show event");
    Check(!GetModuleHandleW(L"WebView2Loader.dll")&&!GetModuleHandleW(L"LumeGlass.dll"),"prewarm does not create a browser or material surface");
    std::cout<<"PREWARM_VISIBILITY_PASS cycles=6 materials=3 samples="<<samples<<" shows=0 reads=6\n";
}
LPARAM Point(HWND window,int x,int y){const auto dpi=GetDpiForWindow(window);return MAKELPARAM(MulDiv(x,dpi,96),MulDiv(y,dpi,96));}
void Click(HWND window,int x,int y){const auto point=Point(window,x,y);SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(window,WM_LBUTTONUP,0,point);Pump();}
HWND Password(){HWND result=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL{wchar_t name[64]{};GetClassNameW(window,name,64);if(std::wstring(name)==L"LumeControlCenterPassword"){*reinterpret_cast<HWND*>(data)=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&result));return result;}
unsigned Children(){unsigned count=0;const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);Check(snapshot!=INVALID_HANDLE_VALUE,"process snapshot");PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);if(Process32FirstW(snapshot,&entry))do{if(entry.th32ParentProcessID==GetCurrentProcessId())++count;}while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return count;}
void Shot(HWND window,const std::filesystem::path& path,bool composed=false){RECT rect{};GetClientRect(window,&rect);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=rect.right;info.bmiHeader.biHeight=-rect.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;
    const auto dc=CreateCompatibleDC(nullptr);const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);Check(bitmap&&pixels,"capture buffer");const auto old=SelectObject(dc,bitmap);
    if(composed){RECT bounds{};GetWindowRect(window,&bounds);const auto screen=GetDC(nullptr);DwmFlush();BitBlt(dc,0,0,rect.right,rect.bottom,screen,bounds.left,bounds.top,SRCCOPY|CAPTUREBLT);ReleaseDC(nullptr,screen);}
    else SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();
    const auto bytes=static_cast<DWORD>(rect.right*rect.bottom*4);BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+bytes;
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char*>(&header),sizeof(header));file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));file.write(static_cast<char*>(pixels),bytes);file.close();SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
COLORREF ForegroundPixel(HWND window,int x,int y){const auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);RECT rect{};GetClientRect(window,&rect);info.bmiHeader.biWidth=rect.right;info.bmiHeader.biHeight=-rect.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;
    const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);Check(bitmap&&pixels,"foreground buffer");const auto old=SelectObject(dc,bitmap);SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);const auto dpi=GetDpiForWindow(window);const auto color=GetPixel(dc,MulDiv(x,dpi,96),MulDiv(y,dpi,96));SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return color;}
void MouseClick(POINT point){
    const auto x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    INPUT input[3]{};for(auto& item:input)item.type=INPUT_MOUSE;
    input[0].mi.dx=MulDiv(point.x-x,65535,width-1);input[0].mi.dy=MulDiv(point.y-y,65535,height-1);input[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    input[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;input[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;Check(SendInput(3,input,sizeof(INPUT))==3,"native mouse injection reaches the test surface");Pump(80);
}
LRESULT CALLBACK OutsideProc(HWND window,UINT message,WPARAM wp,LPARAM lp){if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;return DefWindowProcW(window,message,wp,lp);}
LRESULT CALLBACK BackdropProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);RECT area{};GetClientRect(window,&area);
        const COLORREF colors[]{RGB(225,80,180),RGB(30,210,155),RGB(65,145,235)};
        for(int y=0;y<area.bottom;y+=72){const RECT band{0,y,area.right,std::min(y+72,static_cast<int>(area.bottom))};const auto brush=CreateSolidBrush(colors[(y/72)%3]);FillRect(dc,&band,brush);DeleteObject(brush);}
        EndPaint(window,&paint);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
HWND GlassSurface(HWND owner){struct Search{HWND owner,surface=nullptr;}search{owner};EnumWindows([](HWND window,LPARAM context)->BOOL{auto& value=*reinterpret_cast<Search*>(context);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid==GetCurrentProcessId()&&GetPropW(window,lume::window_ids::GlassSurfaceOwnerProp)==value.owner){value.surface=window;return FALSE;}return TRUE;
},reinterpret_cast<LPARAM>(&search));return search.surface;}
void CheckFirstFrames(ControlCenterWindow& center,ULONGLONG opening,unsigned& frames,unsigned& placeholders,ULONGLONG& firstMs){
    RECT bounds{};GetWindowRect(center.Handle(),&bounds);const auto dpi=GetDpiForWindow(center.Handle());
    bool glassSeen=false;const auto screen=GetDC(nullptr);const auto deadline=GetTickCount64()+1800;
    do{
        DwmFlush();++frames;
        const auto pixel=[&](int x,int y){return GetPixel(screen,bounds.left+MulDiv(x,dpi,96),bounds.top+MulDiv(y,dpi,96));};
        if(pixel(5,120)==RGB(30,30,34)||pixel(367,120)==RGB(30,30,34)||pixel(186,422)==RGB(30,30,34))++placeholders;
        const auto glass=GlassSurface(center.Handle());if(glass&&IsWindowVisible(glass)){
            const auto tile=pixel(108,69);const bool controls=std::abs(static_cast<int>(GetRValue(tile))-91)<6&&std::abs(static_cast<int>(GetGValue(tile))-123)<6&&std::abs(static_cast<int>(GetBValue(tile))-234)<6;
            Check(controls,"the first visible glass surface includes the panel controls");
            if(!glassSeen){glassSeen=true;firstMs=GetTickCount64()-opening;}
        }
        Pump(8);
    }while(GetTickCount64()<deadline&&(!glassSeen||GetTickCount64()-opening<500));
    ReleaseDC(nullptr,screen);Check(glassSeen,"real glass becomes visible within the bounded first-frame check");
    Check(placeholders==0,"no opaque placeholder frame before the requested glass material");
}
}
int main(int argc,char** argv){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::string argument=argc>1?argv[1]:"";
    const bool interaction=argument=="--interaction"||argument=="--interaction-liquid"||argument=="--interaction-frosted";
    const bool firstFrame=argument=="--first-frame-liquid"||argument=="--first-frame-frosted";
    const bool fallback=argument=="--material-fallback";
    const auto look=argument=="--interaction-liquid"||argument=="--first-frame-liquid"||fallback?ControlCenterWindow::Material::Liquid:argument=="--interaction-frosted"||argument=="--first-frame-frosted"?ControlCenterWindow::Material::Frosted:ControlCenterWindow::Material::None;
    const auto previous=GetThreadDesktop(GetCurrentThreadId());const auto desktop=interaction||firstFrame?nullptr:CreateDesktopW((L"LumeControlCenterNative-"+std::to_wstring(GetCurrentProcessId())).c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    Check(interaction||firstFrame||(desktop&&SetThreadDesktop(desktop)),"private desktop without changing the user's screen");
    Apartment apartment;
    if(firstFrame){WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=BackdropProc;wc.lpszClassName=L"LumeControlCenterFirstFrameBackdrop";RegisterClassW(&wc);}
    const auto owner=CreateWindowExW(firstFrame?WS_EX_TOPMOST|WS_EX_TOOLWINDOW:0,firstFrame?L"LumeControlCenterFirstFrameBackdrop":L"STATIC",L"Control center test owner",WS_POPUP|WS_VISIBLE,20,20,firstFrame?800:400,firstFrame?1040:100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);Check(owner!=nullptr,"owner window");
    std::atomic<int> refreshes{0},wifiChanges{0},volume{-1},connections{0},outputs{0},trayCalls{0},pairings{0};std::atomic<bool> wifiOn{true},muted{false},slow{false};
    auto queue=std::make_shared<cc::PanelWorker>([&](auto,const cc::PanelWorker::Job& job){
        if(job.key=="refresh"){++refreshes;if(slow)std::this_thread::sleep_for(std::chrono::milliseconds(700));}
        else if(job.key=="wifi"){wifiOn=job.value.get<bool>();++wifiChanges;}
        else if(job.key=="volume")volume=job.value.get<int>();else if(job.key=="mute")muted=job.value.get<bool>();
        else if(job.key=="connect"){Check(job.value.value("ssid",std::string{})=="Native test network","connect targets the chosen SSID");Check(job.value.value("password",std::string{})=="test-only-password","native password passed only to the fake backend");++connections;}
        else if(job.key=="output"){Check(job.value=="speaker-2","selected endpoint id passed to worker");++outputs;}
        else if(job.key=="tray"){++trayCalls;}
        else if(job.key=="pair"){Check(job.value=="test-device-id","Bluetooth pairs the selected device");++pairings;}
        Json state={{"wifi",{{"available",true},{"on",wifiOn.load()},{"ssid","Native test network"},{"signal",80}}},
            {"bluetooth",{{"available",true},{"on",true}}},{"hotspot",{{"available",false},{"on",false}}},
            {"airplane",{{"available",true},{"on",false}}},{"audio",{{"available",true},{"volume",volume.load()<0?35:volume.load()},{"muted",muted.load()},{"output",outputs.load()?"speaker-2":"speaker-1"},
                {"outputs",Json::array({{{"id","speaker-1"},{"name","Speakers"}},{{"id","speaker-2"},{"name","Headphones"}}})}}},
            {"brightness",{{"available",true},{"level",65},{"display","display-1"},{"displays",Json::array({{{"id","display-1"},{"name","Display 1"}}})}}},
            {"battery",{{"available",false}}},{"tray",true},{"accent","#5b7bea"},
            {"networks",Json::array({{{"ssid","Native test network"},{"signal",80},{"secured",true},{"known",false},{"connected",false}}})}};
        if(job.key=="tray"){state["trayResult"]=trayCalls.load()>=2;state["trayRequest"]=job.value.value("request",0u);}
        state["devices"]=Json::array({{{"id","test-device-id"},{"name","Test headset"},{"canPair",pairings.load()==0},{"paired",pairings.load()>0}}});
        return state.dump();
    });
    const auto children=Children();ULONGLONG firstMs=0;
    if(!interaction&&!firstFrame&&!fallback)PrewarmVisibilityRegression(owner);
    {
        std::vector<std::wstring> opened;bool allowLaunch=false;
        ControlCenterWindow center(queue,[&](HWND,const wchar_t* uri){opened.emplace_back(uri);return allowLaunch;});const auto warming=GetTickCount64();Check(center.Prewarm(GetModuleHandleW(nullptr),owner,look),"snapshot prewarm starts asynchronously");
        Check(GetTickCount64()-warming<200&&center.Handle()&&!IsWindowVisible(center.Handle()),"prewarm keeps only a hidden lightweight host");
        Wait([&]{return refreshes.load()>0&&!queue->Running();},"one-shot snapshot completes and its worker retires");
        const auto prewarmed=refreshes.load();Pump(80);Check(refreshes.load()==prewarmed,"prewarming does not leave hidden polling");
        const auto started=GetTickCount64();Check(center.Toggle(GetModuleHandleW(nullptr),owner,RECT{500,800,540,840},look),"native center opens");firstMs=GetTickCount64()-started;
        Check(center.IsOpen()&&IsWindowVisible(center.Handle()),"native first frame ready when Toggle returns");Check(GetModuleHandleW(L"WebView2Loader.dll")==nullptr,"no WebView loader");
        if(fallback){
            Check(GetModuleHandleW(L"LumeGlass.dll")==nullptr,"fallback fixture runs beside no material DLL");
            Check(ForegroundPixel(center.Handle(),5,120)==RGB(30,30,34),"unavailable material immediately gives a usable solid panel");
            Pump(1300);SendMessageW(center.Handle(),LumeMaterialStatusChangedMessage,0,0);Pump();
            Check(center.IsOpen()&&ForegroundPixel(center.Handle(),5,120)==RGB(30,30,34)&&!GlassSurface(center.Handle()),"fallback stays solid without a late material switch");
            Click(center.Handle(),60,95);Wait([&]{return wifiChanges.load()==1;},"fallback controls remain usable");
            center.Close();Wait([&]{return !queue->Running();},"fallback close retires the worker");
            std::cout<<"NATIVE_CONTROL_CENTER_FALLBACK_PASS missingMaterial=solid lateSwitch=0 controls=usable\n";
        }
        if(firstFrame){
            unsigned frames=0,placeholders=0;ULONGLONG firstVisible=0;CheckFirstFrames(center,started,frames,placeholders,firstVisible);
            const auto firstOpen=firstVisible;std::vector<ULONGLONG> reopenTimes;
            for(unsigned repeat=0;repeat<2;++repeat){center.Close();Wait([&]{return !queue->Running();},"closed first-frame worker retires");Pump(100);const auto reopened=GetTickCount64();Check(center.Toggle(GetModuleHandleW(nullptr),owner,RECT{500,800,540,840},look),"reopen for material first-frame sampling");CheckFirstFrames(center,reopened,frames,placeholders,firstVisible);reopenTimes.push_back(firstVisible);}
            if(argc>2){std::filesystem::create_directories(argv[2]);Shot(center.Handle(),std::filesystem::path(argv[2])/(look==ControlCenterWindow::Material::Liquid?"control-center-liquid-composed.bmp":"control-center-frosted-composed.bmp"),true);}
            center.Close();Wait([&]{return !queue->Running();},"final first-frame worker retires");
            Check(center.Toggle(GetModuleHandleW(nullptr),owner,RECT{500,800,540,840},look),"open before immediate startup cancellation");center.Close();Pump(250);
            Check(!center.IsOpen()&&!IsWindowVisible(center.Handle())&&!GlassSurface(center.Handle()),"closing during material startup cannot expose a late frame");
            Wait([&]{return !queue->Running();},"canceled startup retires its worker");DestroyWindow(owner);UnregisterClassW(L"LumeControlCenterFirstFrameBackdrop",GetModuleHandleW(nullptr));
            std::cout<<"NATIVE_CONTROL_CENTER_FIRST_FRAME_PASS material="<<static_cast<int>(look)<<" opens=3 sampledFrames="<<frames<<" opaquePlaceholderFrames="<<placeholders<<" firstVisibleMs="<<firstOpen<<" reopenMs="<<reopenTimes[0]<<','<<reopenTimes[1]<<" controlsWithGlass=verified cancelStartup=verified\n";return 0;
        }
        if(fallback){DestroyWindow(owner);Pump();apartment.Close();SetThreadDesktop(previous);CloseDesktop(desktop);return 0;}
        Wait([&]{return refreshes.load()>0;},"background state arrives");Pump(100);const auto window=center.Handle();
        const auto detailsAt=[&](int x){
            RECT before{},after{};GetWindowRect(window,&before);
            Click(window,x,95);GetWindowRect(window,&after);
            Check(center.IsOpen()&&center.Handle()==window&&EqualRect(&before,&after)&&!FindWindowW(L"LumeControlCenterDetails",nullptr),"list stays in the existing center without a second window or layout jump");
        };
        const auto settingsClick=[&]{RECT bounds{};GetClientRect(window,&bounds);Click(window,100,MulDiv(bounds.bottom,96,GetDpiForWindow(window))-27);};
        if(interaction){
            WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=OutsideProc;wc.lpszClassName=L"LumeControlCenterNoActivateTest";wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));RegisterClassW(&wc);
            RECT bounds{};GetWindowRect(window,&bounds);const auto outside=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Outside click test",WS_POPUP,bounds.left-96,bounds.top+60,80,80,nullptr,nullptr,wc.hInstance,nullptr);Check(outside!=nullptr,"nonactivating outside test window");ShowWindow(outside,SW_SHOWNOACTIVATE);
            POINT original{};GetCursorPos(&original);const POINT inside{bounds.left+30,bounds.top+20},away{bounds.left-56,bounds.top+100};
            MouseClick(inside);Check(center.IsOpen(),"a real inside click keeps the panel open");
            detailsAt(160);settingsClick();Pump(80);Check(!opened.empty()&&center.IsOpen(),"failed settings launch retains the inline list");
            MouseClick(away);Wait([&]{return !center.IsOpen();},"a real outside click dismisses the panel even when the clicked window does not activate");
            POINT current{};if(GetCursorPos(&current)&&std::abs(current.x-away.x)<=2&&std::abs(current.y-away.y)<=2)SetCursorPos(original.x,original.y);
            Wait([&]{return !queue->Running();},"outside dismissal removes the worker binding");DestroyWindow(outside);DestroyWindow(owner);UnregisterClassW(wc.lpszClassName,wc.hInstance);
            std::cout<<"NATIVE_CONTROL_CENTER_INTERACTION_PASS material="<<static_cast<int>(look)<<" insideClick=retained settingsFailure=retained outsideNoActivateClick=dismissed browserChildren="<<Children()-children<<'\n';return 0;
        }
        if(argc>1){std::filesystem::create_directories(argv[1]);Shot(window,std::filesystem::path(argv[1])/"native-control-center.bmp");}
        Click(window,60,95);Wait([&]{return wifiChanges.load()==1&&!wifiOn.load();},"native Wi-Fi toggle uses fake worker");
        Click(window,60,95);Wait([&]{return wifiChanges.load()==2&&wifiOn.load();},"Wi-Fi can be enabled again");
        Click(window,233,246);Wait([&]{return volume.load()==75;},"native volume slider keeps correct scaling");
        SendMessageW(window,WM_KEYDOWN,VK_RIGHT,0);Pump();Wait([&]{return volume.load()==76;},"focused slider supports keyboard adjustment");
        Click(window,36,246);Wait([&]{return muted.load();},"mute button uses worker state");
        detailsAt(160);Check(opened.empty(),"Wi-Fi arrow stays in Lume");
        Click(window,308,73);const auto password=Password();Check(password&&GetWindow(password,GW_OWNER)==window,"an unfamiliar secured network requests its password");
        SetWindowTextW(GetDlgItem(password,1),L"test-only-password");
        SendMessageW(password,WM_COMMAND,MAKEWPARAM(2,BN_CLICKED),0);
        Wait([&]{return connections.load()==1;},"Wi-Fi list connects through the worker");
        Check(!IsWindow(password)&&center.IsOpen(),"password submission returns to the inline Wi-Fi list");
        settingsClick();Pump();Check(opened==std::vector<std::wstring>{L"ms-settings:network-wifisettings",L"ms-settings:network-status",L"ms-settings:"}&&center.IsOpen(),"Wi-Fi settings falls back to Windows Settings and retains the center if no page opens");
        Click(window,32,32);
        detailsAt(337);Click(window,308,73);Wait([&]{return pairings.load()==1;},"back returns to the main controls and inline Bluetooth can pair");
        SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);Check(center.IsOpen(),"escape returns to the main controls without closing the center");
        detailsAt(337);allowLaunch=true;settingsClick();Pump();Check(opened.back()==L"ms-settings:bluetooth"&&!center.IsOpen(),"Bluetooth settings remains available for more management");
        Check(center.Toggle(GetModuleHandleW(nullptr),owner,RECT{500,800,540,840},look),"reopen after settings");Pump();
        Click(window,90,346);Click(window,90,306);Wait([&]{return outputs.load()==1;},"native output picker selects second endpoint");
        Click(window,160,395);Wait([&]{return trayCalls.load()==1;},"tray uses the bound worker");Pump(60);
        Check(center.IsOpen(),"failed tray open retains a visible retry action");
        Click(window,160,395);Wait([&]{return trayCalls.load()==2&&!center.IsOpen();},"verified tray open dismisses the center");
        Check(center.Toggle(GetModuleHandleW(nullptr),owner,RECT{500,800,540,840},look),"reopen after tray action");
        center.Close();Check(!center.IsOpen()&&!IsWindowVisible(window),"close hides the native host");Wait([&]{return !queue->Running();},"close unbinds and releases the worker thread");
        const auto idle=refreshes.load();Pump(80);Check(refreshes.load()==idle&&queue->Queued()==0,"closed panel stops all system polling");
        slow=true;Check(center.Toggle(GetModuleHandleW(nullptr),owner,RECT{500,800,540,840},ControlCenterWindow::Material::None),"reopen starts a new worker generation");
        Wait([&]{return refreshes.load()>idle;},"slow fake read started");Click(window,60,95);Check(queue->Queued()>0,"cached controls accept input before a slow reopen refresh completes");
        const auto closing=GetTickCount64();center.Close();Check(GetTickCount64()-closing<200,"closing does not join a blocked system API");
        Wait([&]{return !queue->Running();},"old generation retires after its read returns");
        slow=false;Check(center.Toggle(GetModuleHandleW(nullptr),owner,RECT{500,800,540,840},ControlCenterWindow::Material::None),"reopen for null activation regression");
        SendMessageW(center.Handle(),WM_ACTIVATE,WA_INACTIVE,0);Pump();Check(!center.IsOpen(),"desktop activation with no destination HWND dismisses the panel");
        Pump(360);Check(center.Toggle(GetModuleHandleW(nullptr),owner,RECT{500,800,540,840},ControlCenterWindow::Material::None),"reopen after desktop dismissal");
        SendMessageW(center.Handle(),WM_ACTIVATEAPP,FALSE,0);Pump();Check(!center.IsOpen(),"switching applications dismisses the panel");
        Wait([&]{return !queue->Running();},"dismissal releases worker binding");
        Check(Children()==children,"native center creates no browser child processes");
    }
    DestroyWindow(owner);Pump();apartment.Close();SetThreadDesktop(previous);CloseDesktop(desktop);
    std::cout<<"NATIVE_CONTROL_CENTER_PASS firstMs="<<firstMs<<" browserChildren=0 fakeSystemActions=verified cachedReopen=verified nullActivation=verified closedPolling=0\n";
}
