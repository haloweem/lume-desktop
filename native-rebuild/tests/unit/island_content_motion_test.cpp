#include "platform/windows/dynamic_island/island_window.h"
#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace lume;
using namespace lume::platform::windows;
namespace {
constexpr UINT FrameMessage=WM_APP+0x6e4;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int Px(HWND hwnd,float dip){
    const auto canvasDpi=static_cast<unsigned>(std::lround(GetDpiForWindow(hwnd)*1.4f));
    return static_cast<int>(std::lround(dip*canvasDpi/96.f));
}
bool Different(std::uint32_t a,std::uint32_t b){
    for(unsigned shift:{0u,8u,16u,24u})
        if(std::abs(static_cast<int>((a>>shift)&255)-static_cast<int>((b>>shift)&255))>3)return true;
    return false;
}
struct Pixels {unsigned tested=0,different=0;};
struct RecordedFrame {ULONGLONG milliseconds=0;RECT bounds{};WidgetBitmap pixels;};
void SaveFrames(const std::vector<RecordedFrame>& frames,IslandShape shape,const wchar_t* scene=L""){
    wchar_t output[32768]{};
    if(!GetEnvironmentVariableW(L"LUME_ISLAND_CONTENT_FRAMES",output,32768))return;
    const auto folder=std::filesystem::path(output)/scene/(shape==IslandShape::Capsule?L"capsule":L"attached");
    std::filesystem::create_directories(folder);
    const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    std::ofstream metadata(folder/L"frames.tsv");metadata<<"file\tms\tleft\ttop\twidth\theight\n";
    for(std::size_t i=0;i<frames.size();++i){
        const auto& frame=frames[i];
        const int width=frame.bounds.right-frame.bounds.left,height=frame.bounds.bottom-frame.bounds.top;
        Check(width>0&&height>0&&static_cast<unsigned>(width)<=frame.pixels.width&&static_cast<unsigned>(height)<=frame.pixels.height,
            "recorded native frame covers its displayed window");
        wchar_t name[32]{};swprintf_s(name,L"frame-%03u.png",static_cast<unsigned>(i));
        Gdiplus::Bitmap image(width,height,static_cast<INT>(frame.pixels.width*4),PixelFormat32bppPARGB,
            reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(frame.pixels.pixels.data())));
        Check(image.Save((folder/name).c_str(),&png,nullptr)==Gdiplus::Ok,"native animation PNG is saved");
        metadata<<std::filesystem::path(name).string()<<'\t'<<frame.milliseconds<<'\t'<<frame.bounds.left<<'\t'<<frame.bounds.top
            <<'\t'<<width<<'\t'<<height<<'\n';
    }
}
Pixels CompareContent(const WidgetBitmap& actual,const RECT& position,const WidgetBitmap& reference,const RECT& settled,int margin){
    RECT region{};IntersectRect(&region,&position,&settled);
    // Exclude the changing rounded shell, its outline and bottom clipping. The
    // remaining pixels are actual native card graphics and text, in screen
    // coordinates rather than matching a helper's layout calculation.
    region.left+=margin;region.right-=margin;
    region.top+=margin;region.bottom-=margin;
    Pixels result;
    for(LONG y=region.top;y<region.bottom;++y)for(LONG x=region.left;x<region.right;++x){
        const auto a=actual.pixels[static_cast<std::size_t>(y-position.top)*actual.width+static_cast<unsigned>(x-position.left)];
        const auto b=reference.pixels[static_cast<std::size_t>(y-settled.top)*reference.width+static_cast<unsigned>(x-settled.left)];
        // Ignore the black shell: it changes size and is intentionally animated.
        if((b&0x00ffffffu)==0)continue;
        ++result.tested;if(Different(a,b))++result.different;
    }
    return result;
}
void Scenario(IslandShape shape){
    IslandWindow window(GetModuleHandleW(nullptr),{});
    IslandDisplay display;
    display.autoHide=false;display.hideFullscreen=false;display.topmost=true;
    display.material=IslandMaterial::None;display.size=IslandSize::Large;display.shape=shape;
    display.animationSpeed=IslandAnimationSpeed::Slow;display.bounce=IslandBounce::Bouncy;
    display.calendarStyle=IslandCalendarStyle::ChineseAlmanac;
    using W=dynamic_island::ExpandedWidget;
    display.expandedWidgets={W::Music,W::CalendarWeather,W::Focus,W::Todos};
    window.SetMonitor({L"ContentMotionFixture",{0,0,1280,900},{0,0,1280,860},true});
    widget::Preferences prefs;prefs.enabled=true;
    Check(window.Start(prefs,display),"native island fixture starts");
    KillTimer(window.Handle(),1); // No wall-clock tick may change the reference.
    IslandPresentation view;
    view.media.available=true;view.media.playing=false;view.media.id="stationary-content";
    view.media.title=L"A stable native title";view.media.artist=L"Fixed text and artwork";
    view.media.duration=180;view.media.position=45;view.english=true;
    window.Update(view);
    IslandTodoSource todos;
    todos.forDay=[](calendar::Date){return std::vector<IslandTodo>{{"fixture",L"Stationary task",L"10:08",false,false,0x30d158,L""}};};
    todos.hasTodos=[](calendar::Date){return false;};
    window.SetTodoSource(std::move(todos));
    window.Expand(true);window.Settle();
    const auto reference=window.Frame();RECT settled{};GetWindowRect(window.Handle(),&settled);
    Check(!reference.pixels.empty(),"settled native content is available");
    window.Expand(false);window.Settle();
    const bool record=GetEnvironmentVariableW(L"LUME_ISLAND_CONTENT_FRAMES",nullptr,0)!=0;
    std::vector<RecordedFrame> recorded;
    ULONGLONG recordedAt=0;
    const auto animationStarted=GetTickCount64();
    window.Expand(true);
    unsigned compared=0,changingWidth=0,worstDifferent=0;
    bool unstable=false;
    const auto deadline=GetTickCount64()+4500;
    while(GetTickCount64()<deadline){
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.hwnd==window.Handle()&&(message.message==WM_MOUSEMOVE||message.message==WM_MOUSELEAVE))continue;
            TranslateMessage(&message);DispatchMessageW(&message);
            if(message.hwnd!=window.Handle()||message.message!=FrameMessage)continue;
            RECT current{};GetWindowRect(window.Handle(),&current);
            const auto now=GetTickCount64();
            if(record&&now-animationStarted<=1200&&now-recordedAt>=16&&recorded.size()<80){
                recorded.push_back({now-animationStarted,current,window.Frame()});recordedAt=now;
            }
            // Height past the final target means expanded-content opacity is
            // exactly one. Width continues its independent spring/bounce.
            if(current.bottom-current.top<=settled.bottom-settled.top+1)continue;
            const auto frame=window.Frame();
            const auto pixels=CompareContent(frame,current,reference,settled,Px(window.Handle(),30));
            Check(pixels.tested>1000,"actual card text and graphics are sampled during the bounce");
            worstDifferent=std::max(worstDifferent,pixels.different);
            if(std::abs((current.right-current.left)-(settled.right-settled.left))>3)++changingWidth;
            ++compared;
            // Permit tiny raster rounding variation, never a resized/moved card.
            if(pixels.different>pixels.tested/100){
                if(!unstable)std::cerr<<"content sample: shape="<<static_cast<int>(shape)<<" width="<<current.right-current.left
                    <<" final="<<settled.right-settled.left<<" tested="<<pixels.tested<<" changed="<<pixels.different<<'\n';
                unstable=true;
            }
        }
        MsgWaitForMultipleObjects(0,nullptr,FALSE,2,QS_ALLINPUT);
    }
    SaveFrames(recorded,shape);
    Check(!unstable,"visible native content must stay fixed in screen pixels while the shell bounces");
    Check(compared>=3&&changingWidth>=2,"multiple real animation frames keep content stable while shell geometry changes");
    window.Stop();
    std::cout<<"content-motion shape="<<static_cast<int>(shape)<<" samples="<<compared<<" changing-width="<<changingWidth
        <<" max-different="<<worstDifferent<<'\n';
}
bool AlmanacOpenScenario(IslandBounce bounce){
    IslandWindow window(GetModuleHandleW(nullptr),{});
    IslandDisplay display;
    display.autoHide=false;display.hideFullscreen=false;display.topmost=true;
    display.material=IslandMaterial::None;display.size=IslandSize::Standard;display.shape=IslandShape::Capsule;
    display.animationSpeed=IslandAnimationSpeed::Normal;display.bounce=bounce;
    display.calendarStyle=IslandCalendarStyle::ChineseAlmanac;
    using W=dynamic_island::ExpandedWidget;
    display.expandedWidgets={W::Clock,W::Music,W::CalendarWeather,W::System};
    window.SetMonitor({L"AlmanacOpenFixture",{0,0,2560,1440},{0,0,2560,1380},true});
    widget::Preferences prefs;prefs.enabled=true;
    Check(window.Start(prefs,display),"almanac opening fixture starts");
    KillTimer(window.Handle(),1);
    window.Expand(true);window.Settle();
    const auto reference=window.Frame();RECT settled{};GetWindowRect(window.Handle(),&settled);
    const auto canvasDpi=static_cast<unsigned>(std::lround(GetDpiForWindow(window.Handle())*1.2f));
    const auto pixel=[&](float dip){return static_cast<int>(std::lround(dip*canvasDpi/96.f));};
    // The screenshot's clock/music/calendar/system row, sampled only inside
    // the stationary calendar so real clock/performance updates are excluded.
    const RECT area{settled.left+pixel(494),settled.top+pixel(46),
                    settled.left+pixel(686),settled.top+pixel(186)};
    window.Expand(false);window.Settle();
    std::vector<RecordedFrame> recorded;
    const bool record=GetEnvironmentVariableW(L"LUME_ISLAND_CONTENT_FRAMES",nullptr,0)!=0;
    unsigned compared=0,undershoot=0,worstDifferent=0;
    bool opaque=false;
    ULONGLONG recordedAt=0;
    const auto started=GetTickCount64();
    window.Expand(true);
    while(GetTickCount64()<started+1800){
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.hwnd==window.Handle()&&(message.message==WM_MOUSEMOVE||message.message==WM_MOUSELEAVE))continue;
            TranslateMessage(&message);DispatchMessageW(&message);
            if(message.hwnd!=window.Handle()||message.message!=FrameMessage)continue;
            RECT current{};GetWindowRect(window.Handle(),&current);
            const auto frame=window.Frame();const auto now=GetTickCount64();
            if(record&&now-recordedAt>=16&&recorded.size()<90){recorded.push_back({now-started,current,frame});recordedAt=now;}
            if(current.bottom-current.top>settled.bottom-settled.top+1)opaque=true;
            if(!opaque)continue;
            if(current.bottom-current.top<settled.bottom-settled.top-1)++undershoot;
            unsigned different=0;
            Check(current.left<=area.left&&current.right>=area.right&&current.bottom>=area.bottom,
                  "calendar sample remains fully visible through the rebound");
            for(LONG y=area.top;y<area.bottom;++y)for(LONG x=area.left;x<area.right;++x){
                const auto a=frame.pixels[static_cast<std::size_t>(y-current.top)*frame.width+static_cast<unsigned>(x-current.left)];
                const auto b=reference.pixels[static_cast<std::size_t>(y-settled.top)*reference.width+static_cast<unsigned>(x-settled.left)];
                if(Different(a,b))++different;
            }
            worstDifferent=std::max(worstDifferent,different);++compared;
        }
        MsgWaitForMultipleObjects(0,nullptr,FALSE,2,QS_ALLINPUT);
    }
    SaveFrames(recorded,display.shape,bounce==IslandBounce::Normal?L"almanac-normal":L"almanac-bouncy");
    window.Stop();
    std::cout<<"almanac-open bounce="<<static_cast<int>(bounce)<<" dpi="<<canvasDpi<<" samples="<<compared
             <<" undershoot="<<undershoot<<" max-different="<<worstDifferent<<'\n';
    Check(compared>=3,"calendar is sampled after its first fully visible frame");
    if(bounce==IslandBounce::Bouncy)Check(undershoot>=2,"calendar regression includes shell contraction after overshoot");
    return worstDifferent<=32;
}
void Run(bool almanacOnly){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initializes");
    Gdiplus::GdiplusStartupInput startup;ULONG_PTR token=0;
    Check(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok,"graphics initializes");
    BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    if(!animations)throw std::runtime_error("this animation regression requires the existing Windows animation preference enabled");
    try{
        if(!almanacOnly){Scenario(IslandShape::Capsule);Scenario(IslandShape::Standard);}
        const bool normal=AlmanacOpenScenario(IslandBounce::Normal);
        const bool bouncy=AlmanacOpenScenario(IslandBounce::Bouncy);
        Check(normal&&bouncy,"revealed calendar text must not change again when the opening shell rebounds");
    }
    catch(...){Gdiplus::GdiplusShutdown(token);CoUninitialize();throw;}
    Gdiplus::GdiplusShutdown(token);CoUninitialize();
}
}
int main(int argc,char** argv){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto name=L"LumeIslandContentMotion-"+std::to_wstring(GetCurrentProcessId());
    const auto desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop){std::cerr<<"FAIL create isolated desktop\n";return 1;}
    std::string failure;
    std::thread fixture([&]{try{Check(SetThreadDesktop(desktop)!=FALSE,"fixture binds isolated desktop");
        Run(argc==2&&std::string(argv[1])=="--almanac-open-only");}
        catch(const std::exception& error){failure=error.what();}});
    fixture.join();CloseDesktop(desktop);
    if(!failure.empty()){std::cerr<<"FAIL "<<failure<<'\n';return 1;}
    std::cout<<"island content motion passed\n";
}
