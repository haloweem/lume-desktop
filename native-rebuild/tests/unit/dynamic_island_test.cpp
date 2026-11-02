#include "apps/shell/dynamic_island_session.h"
#include "platform/windows/dynamic_island/island_window.h"
#include "platform/windows/dynamic_island/island_preview.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <gdiplus.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <thread>

using namespace lume;
using ui::PageCommand;
namespace {
void Check(bool ok,const char* reason){if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}}
struct Files final:settings::IFileStore {
    std::map<std::string,std::string> values;
    bool fail=false;
    bool Read(const std::string& p,std::string& out)const override{const auto i=values.find(p);if(i==values.end())return false;out=i->second;return true;}
    bool Exists(const std::string& p)const override{return values.count(p)!=0;}
    bool Write(const std::string& p,const std::string& value)override{if(fail)return false;values[p]=value;return true;}
    bool Rename(const std::string& a,const std::string& b)override{if(fail||!Exists(a))return false;values[b]=values[a];values.erase(a);return true;}
    bool Remove(const std::string& p)override{values.erase(p);return true;}
};
void Pump(unsigned ms){const auto end=GetTickCount64()+ms;do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<end);}
void Click(HWND hwnd,int x,int y){const auto dpi=static_cast<int>(GetDpiForWindow(hwnd));const auto at=MAKELPARAM(MulDiv(x,dpi,96),MulDiv(y,dpi,96));SendMessageW(hwnd,WM_LBUTTONDOWN,0,at);SendMessageW(hwnd,WM_LBUTTONUP,0,at);}
nlohmann::json State(const shell::DynamicIslandSession& s){return nlohmann::json::parse(s.State());}
void SaveFrame(const platform::windows::WidgetBitmap& frame,const wchar_t* name){
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto root=std::filesystem::path(exe).parent_path()/L"verification"/L"dynamic-island";
    std::filesystem::create_directories(root);
    Gdiplus::Bitmap image(frame.width,frame.height,static_cast<INT>(frame.width*4),PixelFormat32bppPARGB,
                         reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(frame.pixels.data())));
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    Check(image.Save((root/name).c_str(),&png)==Gdiplus::Ok,"save actual native foreground");
}
bool HasLightning(const platform::windows::WidgetBitmap& frame){
    return std::any_of(frame.pixels.begin(),frame.pixels.end(),[](std::uint32_t pixel){
        const auto red=(pixel>>16)&0xff,green=(pixel>>8)&0xff,blue=pixel&0xff;
        return red>green+35&&green>blue+35&&red>100;
    });
}
void Scenario(){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization");
    std::uint64_t now=1000;
    dynamic_island::Model model;model.StartFocus(now);Check(model.RemainingSeconds()==1500,"25 minute timer");
    model.Tick(now+999);Check(model.RemainingSeconds()==1500,"round up remaining seconds");
    model.Tick(now+1500000);Check(!model.Focusing()&&model.Finished(),"sleep or delayed tick completes once");
    Check(!model.Tick(now+1600000),"completion not repeated");model.CancelFocus();Check(!model.Finished(),"dismiss completion");
    dynamic_island::Media media{7,"fixture",{},L"A long song title for native layout verification",L"Native fixture",true,true,35,180};
    auto invalid=media;invalid.position=std::numeric_limits<double>::infinity();invalid.duration=-1;model.Update(invalid);
    Check(model.Playback().position==0&&model.Playback().duration==0,"nonfinite timeline normalized");
    Files files;int notifications=0,controls=0;bool weatherActive=false;dynamic_island::Action last=dynamic_island::Action::Tick;
    shell::DynamicIslandSessionOptions options;
    options.preferencesPath="island.json";options.monotonicMillis=[&]{return now;};
        options.notify=[&](const auto&){++notifications;};
    options.control=[&](auto action,const auto& projection){Check(projection.session==7&&projection.id=="fixture","control retains native identity");last=action;++controls;};
    options.weatherActive=[&](bool active){weatherActive=active;};
    const auto foreground=GetForegroundWindow();
    {
        shell::DynamicIslandSession session(GetModuleHandleW(nullptr),options,&files);
        Check(session.Restore()&&!session.Running()&&files.values.empty(),"disabled restore has no window or file writes");
        Check(!session.HandlePage(PageCommand::DockEnable),"unrelated commands rejected");
        Check(session.HandlePage(PageCommand::IslandEnable).has_value()&&session.Running(),"enable creates window");
        Check(weatherActive,"island enables its shared weather consumer");
        Check((GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,"island shows above windows by default");
        Pump(900);Check(IsWindowVisible(session.Handle()),"idle capsule is visible by default");
        Check(GetForegroundWindow()==foreground,"enable never takes focus");
        Check(SendMessageW(session.Handle(),WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,"click never activates");
        const auto original=files.values["island.json"];files.fail=true;
        session.HandlePage(PageCommand::IslandDisable);
        Check(session.Running()&&session.Snapshot().enabled&&files.values["island.json"]==original,"failed disable restores live window and original preferences");
        Check(weatherActive,"failed disable also restores the shared weather consumer");
        Check(State(session)["error"]=="save","save failure reported");
        files.fail=false;session.HandlePage(PageCommand::IslandLock);Check(session.Snapshot().locked,"lock persisted");
        session.Update(media);const HWND hwnd=session.Handle();
        Pump(900);Check(IsWindowVisible(hwnd),"playback keeps the island visible");
        RECT r{};GetClientRect(hwnd,&r);const int dpi=static_cast<int>(GetDpiForWindow(hwnd));
        Check(r.right==MulDiv(236,dpi,96)&&r.bottom==MulDiv(34,dpi,96),"compact capsule settles at its playing size");
        Click(hwnd,100,15);Pump(900);
        Check(State(session)["expanded"]==true,"real capsule click expands");
        PostMessageW(hwnd,WM_APP+0x6e3,0,0);Pump(60);Check(State(session)["expanded"]==false,"outside click collapses");
        Click(hwnd,100,15);Pump(900);Check(State(session)["expanded"]==true,"capsule expands again");
        GetClientRect(hwnd,&r);Check(r.right==MulDiv(372,dpi,96)&&r.bottom==MulDiv(156,dpi,96),"spring settles at the expanded size");
        Click(hwnd,186,128);Check(controls==1&&last==dynamic_island::Action::Toggle,"real play pause button routes narrow control");
        Click(hwnd,114,128);Click(hwnd,258,128);Check(controls==3&&last==dynamic_island::Action::Next,"previous next route controls");
        Check(State(session)["hideFullscreen"]==true&&State(session)["size"]=="standard","default display options");
        Check(State(session)["shape"]=="capsule","legacy preferences retain floating capsule by default");
        session.HandlePage(PageCommand::IslandShapeStandard);Pump(900);
        {RECT at{};GetWindowRect(session.Handle(),&at);MONITORINFO monitor{sizeof(monitor)};
         GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
         Check(State(session)["shape"]=="standard"&&at.top==monitor.rcMonitor.top,"attached shape applies at the actual screen top");}
        {files.fail=true;const auto saved=files.values["island-display.json"];
         session.HandlePage(PageCommand::IslandShapeCapsule);Pump(900);
         Check(State(session)["shape"]=="standard"&&State(session)["error"]=="save"&&files.values["island-display.json"]==saved,"failed shape save rolls back native shape and file");files.fail=false;}
        session.HandlePage(PageCommand::IslandSizeLarge);session.HandlePage(PageCommand::IslandHoverOn);
        Check(State(session)["size"]=="large"&&State(session)["hover"]==true&&files.values.count("island-display.json"),"display options persisted beside preferences");
        {   files.fail=true;const auto saved=files.values["island-display.json"];
            session.HandlePage(PageCommand::IslandTopmostOff);
            Check(State(session)["error"]=="save"&&State(session)["topmost"]==true&&session.Running()&&
                  (GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST)&&files.values["island-display.json"]==saved,
                  "failed display save keeps the previous window and file");
            files.fail=false;}
        session.HandlePage(PageCommand::IslandTopmostOff);
        Check(session.Running()&&!(GetWindowLongPtrW(session.Handle(),GWL_EXSTYLE)&WS_EX_TOPMOST),"desktop layer option recreates a non-topmost window");
        session.HandlePage(PageCommand::IslandTopmostOn);session.HandlePage(PageCommand::IslandSizeStandard);session.HandlePage(PageCommand::IslandHoverOff);
        session.HandlePage(PageCommand::IslandMaterialFrosted);Check(State(session)["material"]=="frosted-glass","material persisted");
        session.HandlePage(PageCommand::IslandMaterialNone);
        session.HandlePage(PageCommand::IslandCollapse);session.HandlePage(PageCommand::IslandAutoHideOn);
        Check(State(session)["autoHide"]==true,"auto-hide persisted");
        Pump(5200);{RECT rect{};GetWindowRect(session.Handle(),&rect);MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&info);
            Check(rect.bottom>info.rcMonitor.top&&rect.bottom<=info.rcMonitor.top+MulDiv(8,dpi,96),"idle island tucks to a sliver at the screen top");}
        session.HandlePage(PageCommand::IslandAutoHideOff);
        SendMessageW(hwnd,WM_CONTEXTMENU,0,0);
        session.HandlePage(PageCommand::IslandDisable);
        Check(!session.Running()&&!weatherActive,"disable releases window and shared weather consumer");
        Check(GetForegroundWindow()==foreground,"native controls preserve foreground");
        for(int i=0;i<12;++i){session.HandlePage(PageCommand::IslandEnable);const auto old=session.Handle();session.HandlePage(PageCommand::IslandDisable);Check(!IsWindow(old),"repeat lifecycle releases HWND");}
        session.HandlePage(PageCommand::IslandEnable);session.Shutdown();
        Check(!session.Running()&&!weatherActive&&!session.HandlePage(PageCommand::IslandEnable),"shutdown releases shared weather and cannot resurrect");
    }
    {
        shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),options,&files);
        Check(restored.Restore()&&restored.Running()&&restored.Snapshot().locked,"enabled and locked survive restart");
        Check(State(restored)["topmost"]==true&&State(restored)["hover"]==false&&
              State(restored)["autoHide"]==false&&State(restored)["material"]=="none"&&State(restored)["shape"]=="standard",
              "display options including unchecked auto-hide survive restart");restored.Shutdown();
        files.values["broken.json"]="bad data";options.preferencesPath="broken.json";
        shell::DynamicIslandSession broken(GetModuleHandleW(nullptr),options,&files);
        Check(broken.Restore()&&!broken.Running()&&files.values["broken.json"]=="bad data","corrupt data not overwritten on read");
        broken.HandlePage(PageCommand::IslandEnable);Check(files.values["broken.json.corrupt"]=="bad data"&&broken.Running(),"explicit save isolates corrupt file");
    }
    {
        Files legacyFiles;
        auto legacyOptions=options;legacyOptions.preferencesPath="legacy.json";
        const auto display=nlohmann::json{{"version",1},{"topmost",false},{"autoHide",false},
            {"size","large"},{"material","liquid-glass"},
            {"customization",{{"customIcons",true},{"overview",true},{"left",{{"widget","clock"},{"style","capsule"}}}}}}.dump()+std::string(4500,' ');
        legacyFiles.values["legacy-display.json"]=display;
        shell::DynamicIslandSession legacy(GetModuleHandleW(nullptr),legacyOptions,&legacyFiles);
        Check(legacy.Restore()&&!legacy.Running(),"legacy side/workspace settings never activate a disabled island");
        const auto state=State(legacy);
        Check(!state.contains("customization")&&state["size"]=="large"&&state["material"]=="liquid-glass"&&
              state["topmost"]==false&&state["autoHide"]==false,"legacy customization is ignored while ordinary display settings survive");
        Check(legacyFiles.values["legacy-display.json"]==display,"loading legacy settings does not rewrite user data");
        Check(legacy.HandlePage(PageCommand::IslandSizeSmall).has_value(),"ordinary settings remain editable");
        const auto saved=nlohmann::json::parse(legacyFiles.values["legacy-display.json"]);
        Check(!saved.contains("customization")&&saved["size"]=="small"&&saved["topmost"]==false&&saved["autoHide"]==false,
              "next explicit save retires customization and preserves ordinary settings");
        Check(legacy.HandlePage(PageCommand::IslandResetAppearance).has_value(),"appearance reset is supported while disabled");
        const auto reset=nlohmann::json::parse(legacyFiles.values["legacy-display.json"]);
        Check(reset["shape"]=="capsule"&&reset["material"]=="none"&&reset["size"]=="standard"&&
              reset["topmost"]==false&&reset["autoHide"]==false,
              "appearance reset restores only shape material and size and preserves visibility preferences");
    }
    {
        using namespace platform::windows;
        IslandWindow window(GetModuleHandleW(nullptr),{});
        widget::Preferences prefs;prefs.enabled=true;
        IslandDisplay display;
        Check(window.Start(prefs,display),"preview window starts");
        window.Update({{},L"09:41",L"2026年10月19日 星期一",0,false,false,false});window.Settle();
        SaveFrame(window.Frame(),L"idle.png");
        auto playing=media;playing.spectrum.fill(.5f);playing.spectrum[2]=.9f;playing.energy=.6f;
        window.Update({playing,L"",L"",0,false,false,false});window.Settle();SaveFrame(window.Frame(),L"compact.png");
        window.Expand(true);window.Settle();window.Update({playing,L"",L"",0,false,false,false});
        SaveFrame(window.Frame(),L"music.png");
        {BOOL animation=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0);
         const HWND same=window.Handle();RECT before{},during{},final{};GetWindowRect(same,&before);
         display.shape=IslandShape::Standard;window.SetDisplay(display);Pump(85);GetWindowRect(same,&during);
         SaveFrame(window.Frame(),L"shape-transition.png");
         window.Settle();GetWindowRect(same,&final);
         Check(window.Handle()==same,"shape morph retains one native HWND");
         if(animation)Check(during.top<before.top&&during.top>final.top,"shape morph moves through an intermediate position");
         display.shape=IslandShape::Capsule;window.SetDisplay(display);Pump(45);
         display.shape=IslandShape::Standard;window.SetDisplay(display);window.Settle();}
        {const auto frame=window.Frame();const auto alpha=[&](int x,int y){return frame.pixels[static_cast<std::size_t>(MulDiv(y,GetDpiForWindow(window.Handle()),96))*frame.width+MulDiv(x,GetDpiForWindow(window.Handle()),96)]>>24;};
         Check(alpha(12,1)>200&&alpha(1,18)==0&&alpha(186,70)>200,"attached contour has top shoulder, transparent side, and solid body");SaveFrame(frame,L"shape-standard-expanded.png");}
        window.Expand(false);window.Settle();SaveFrame(window.Frame(),L"shape-standard-compact.png");
        {
            playing.artwork="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAAEnQAABJ0Ad5mH3gAAAAiSURBVChThcihEQAACAAh919aOx88GjP7SCihhBJKKKEEDoMXf4GjQpJVAAAAAElFTkSuQmCC";
            display.autoHide=false;window.SetDisplay(display);
            window.Update({playing,L"",L"",0,false,false,false});window.Settle();
            const int dpi=static_cast<int>(GetDpiForWindow(window.Handle()));
            const auto px=[&](int dip){return MulDiv(dip,dpi,96);};
            const auto frame=window.Frame();
            int minX=frame.width,minY=frame.height,maxX=0,maxY=0,count=0;
            for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){
                const auto p=frame.pixels[static_cast<std::size_t>(y)*frame.width+x];
                if(x<static_cast<unsigned>(px(100))&&((p>>8)&255)>180&&((p>>16)&255)<80&&(p&255)<80){
                    minX=std::min(minX,static_cast<int>(x));maxX=std::max(maxX,static_cast<int>(x));
                    minY=std::min(minY,static_cast<int>(y));maxY=std::max(maxY,static_cast<int>(y));++count;
                }
            }
            Check(minX>=px(16)&&count>px(20)*px(20)&&count>=(maxX-minX+1)*(maxY-minY+1)*.96,
                  "standard compact artwork remains a complete rectangle inside the shoulders");
            SaveFrame(frame,L"shape-standard-artwork.png");
            playing.playing=false;window.Update({playing,L"",L"",0,false,false,false});window.Settle();
            const auto paused=window.Frame();int barPixels=0;
            for(int y=px(6);y<px(28);++y){
                const auto p=paused.pixels[static_cast<std::size_t>(y)*paused.width+px(199)];
                if(((p>>8)&255)>90&&((p>>8)&255)>((p>>16)&255)+40)++barPixels;
            }
            Check(barPixels>=px(6),"standard paused spectrum retains upright bars instead of corner dots");
            SaveFrame(paused,L"shape-standard-paused.png");
            IslandPreviewRenderer renderer;
            wchar_t samplePath[32768]{};
            const DWORD sampleLength=GetEnvironmentVariableW(L"LUME_ISLAND_PREVIEW_SAMPLES",samplePath,32768);
            const auto sampleDirectory=sampleLength&&sampleLength<32768?std::filesystem::path(samplePath):std::filesystem::path{};
            if(!sampleDirectory.empty())std::filesystem::create_directories(sampleDirectory);
            wchar_t wallpaperPath[32768]{};
            const DWORD wallpaperLength=GetEnvironmentVariableW(L"LUME_ISLAND_PREVIEW_WALLPAPER",wallpaperPath,32768);
            const auto sampleWallpaper=wallpaperLength&&wallpaperLength<32768?std::filesystem::path(wallpaperPath):std::filesystem::path{};
            const auto same=window.Handle();
            for(const auto shape:{IslandShape::Standard,IslandShape::Capsule})
              for(const auto size:{IslandSize::Small,IslandSize::Standard,IslandSize::Large})
              for(const auto material:{IslandMaterial::None,IslandMaterial::Liquid,IslandMaterial::Frosted}) {
                auto choice=display;choice.shape=shape;choice.size=size;choice.material=material;
                window.SetDisplay(choice);window.Settle();
                const auto preview=window.Preview(choice,false);
                Check(window.Handle()==same&&!window.Expanded(),"preview never changes live HWND or expansion");
                Check(preview.width>0&&preview.height>0,"offscreen native foreground exists");
                auto changed=playing;changed.title=L"Different desktop song";changed.artist=L"Different artist";changed.artwork.clear();changed.position=90;
                window.Update({changed,L"",L"",0,false,false,false});
                const auto afterMedia=window.Preview(choice,false);
                Check(preview.pixels.pixels==afterMedia.pixels.pixels,"sample preview is independent of desktop media and lyrics");
                window.Update({playing,L"",L"",0,false,false,false});
                const auto scene=renderer.Render(preview,choice,{});
                Check(scene.width==960&&scene.height==300,"shared material shader produces a complete native preview scene");
                if(size==IslandSize::Standard&&shape==IslandShape::Standard)SaveFrame(scene,
                    material==IslandMaterial::None?L"preview-native-black.png":material==IslandMaterial::Liquid?L"preview-native-liquid.png":L"preview-native-frosted.png");
                const auto url=IslandPreviewRenderer::PngDataUrl(scene);
                Check(url.rfind("data:image/png;base64,",0)==0&&url.size()<2800000,"native preview encodes bounded PNG data");
                const auto jpeg=IslandPreviewRenderer::JpegDataUrl(scene);
                Check(jpeg.rfind("data:image/jpeg;base64,",0)==0&&jpeg.size()<200000,"animation frame uses bounded JPEG data");
                const char* shapeName=shape==IslandShape::Standard?"standard":"capsule";
                const char* sizeName=size==IslandSize::Small?"small":size==IslandSize::Large?"large":"standard";
                const char* materialName=material==IslandMaterial::Liquid?"liquid-glass":material==IslandMaterial::Frosted?"frosted-glass":"none";
                if(!sampleDirectory.empty())for(bool expanded:{false,true}) {
                    const auto sample=window.Preview(choice,expanded);
                    const auto wallpaper=!sampleWallpaper.empty()&&std::filesystem::exists(sampleWallpaper)?sampleWallpaper.wstring():std::wstring{};
                    const auto rendered=renderer.Render(sample,choice,wallpaper);
                    const auto filename="island-native-"+std::string(shapeName)+"-"+sizeName+"-"+materialName+"-"+(expanded?"expanded":"compact")+".png";
                    Check(IslandPreviewRenderer::WritePng(rendered,(sampleDirectory/filename).wstring()),"write native reference preview");
                }
              }
            window.ReleasePreview();display.shape=IslandShape::Standard;display.size=IslandSize::Standard;display.material=IslandMaterial::None;
            {
                IslandDisplay from;from.shape=IslandShape::Capsule;
                const auto start=window.Preview(from,false);
                auto to=from;to.shape=IslandShape::Standard;
                auto mid=window.Preview(to,false,1.f/30);
                Check(mid.animating&&mid.shapeMix>0&&mid.shapeMix<1&&mid.topOffset>0&&mid.topOffset<start.topOffset,
                      "preview shape morph has continuous shoulders and top position");
                const auto reversed=window.Preview(from,false,1.f/30);
                Check(reversed.animating&&reversed.shapeMix>0&&reversed.shapeMix<1,"reversal retains intermediate shape");
                to.size=IslandSize::Large;
                unsigned frames=0;
                do {mid=window.Preview(to,true,1.f/30);++frames;}while(mid.animating&&frames<90);
                Check(frames>3&&frames<90&&mid.shapeMix==1&&mid.width>start.width&&mid.height>start.height,
                      "preview geometry transition settles and stops");
                const auto stable=window.Preview(to,true,1.f/30);
                Check(!stable.animating&&stable.pixels.pixels==mid.pixels.pixels,"settled sample needs no recurring frames");
                window.ReleasePreview();
            }
            window.SetDisplay(display);window.Settle();
            display.autoHide=true;window.SetDisplay(display);SendMessageW(window.Handle(),WM_MOUSELEAVE,0,0);Pump(4300);window.Settle();
            RECT tucked{};GetWindowRect(window.Handle(),&tucked);MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
            Check(tucked.bottom>monitor.rcMonitor.top&&tucked.bottom<=monitor.rcMonitor.top+px(8),"standard auto-hide leaves a reachable edge");
            const auto hidden=window.Frame();
            Check(std::none_of(hidden.pixels.begin(),hidden.pixels.end(),[](std::uint32_t p){return (p&0x00ffffff)!=0;}),
                  "tucked edge contains no artwork lyrics or spectrum pixels");
            SaveFrame(hidden,L"shape-standard-tucked.png");
            display.autoHide=false;playing.artwork.clear();playing.playing=true;
            window.SetDisplay(display);window.Update({playing,L"",L"",0,false,false,false});
        }
        display.shape=IslandShape::Capsule;window.SetDisplay(display);window.Expand(true);window.Settle();
        {const auto frame=window.Frame();const auto index=static_cast<std::size_t>(MulDiv(1,GetDpiForWindow(window.Handle()),96))*frame.width+MulDiv(12,GetDpiForWindow(window.Handle()),96);
         Check((frame.pixels[index]>>24)==0,"capsule keeps a convex round top corner");SaveFrame(frame,L"shape-capsule-expanded.png");}
        window.Expand(false);window.Settle();SaveFrame(window.Frame(),L"shape-capsule-compact.png");
        display.size=IslandSize::Small;window.SetDisplay(display);window.Expand(false);window.Settle();
        SaveFrame(window.Frame(),L"compact-small.png");
        // Media drops out for a moment between tracks while collapsed; once it is
        // back the island must open on the music page, not the calendar fallback.
        window.Update({{},L"09:41",L"",0,false,false,false});window.Settle();
        Check(window.Page()==IslandPage::Calendar,"no media falls back to the calendar");
        window.Update({playing,L"",L"",0,false,false,false});window.Settle();
        window.Expand(true);window.Settle();
        Check(window.Page()==IslandPage::Music,"music page again after a brief media gap");
        window.Expand(false);window.Settle();
        // Opened during the gap: the calendar fallback gives way when the song is back.
        window.Update({{},L"09:41",L"",0,false,false,false});window.Settle();
        window.Expand(true);window.Settle();
        window.Update({playing,L"",L"",0,false,false,false});window.Settle();
        Check(window.Page()==IslandPage::Music,"an open island returns to music once media is back");
        window.Expand(false);window.Settle();
        // Calendar and todo pages read the shared todo store through the source.
        int toggles=0;
        IslandTodoSource source;
        source.forDay=[](calendar::Date){return std::vector<IslandTodo>{
            {"a",L"提交周报",L"09:30",true,false,0x6bd89b},{"b",L"和设计评审灵动岛",L"14:00",false,false,0xff6961},{"c",L"买牛奶",L"",false,false,0}};};
        source.hasTodos=[](calendar::Date d){return d.day%5==0;};
        source.toggle=[&](const IslandTodo& t){Check(t.id=="b","toggle routes the clicked todo");++toggles;};
        window.SetTodoSource(source);
        display.size=IslandSize::Standard;window.SetDisplay(display);
        window.Expand(true);window.Settle();
        Check(window.Page()==IslandPage::Music,"music page first while playing");
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Calendar,"wheel moves to the calendar page");
        SaveFrame(window.Frame(),L"calendar.png");
        // The weather page only projects the host's reading; it never fetches.
        dynamic_island::Weather weather;
        weather.enabled=true;weather.available=true;weather.city=L"广州 · 天河区";weather.condition=L"多云";
        weather.temperature=26;weather.hasRange=true;weather.high=29;weather.low=21;weather.code=3;weather.isDay=true;
        weather.hours=6;
        for(std::size_t i=0;i<weather.hourly.size();++i){
            auto& hour=weather.hourly[i];hour.label=std::to_wstring(10+i);hour.temperature=26+static_cast<int>(i%3);
            hour.code=i<2?3:i<4?61:0;
        }
        window.SetWeather(weather);
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Weather,"wheel moves to the weather page");
        SaveFrame(window.Frame(),L"weather.png");
        auto rain=weather;rain.city=L"上海 · 浦东新区";rain.condition=L"中雨";rain.code=61;rain.temperature=19;rain.high=22;rain.low=16;
        window.SetWeather(rain);window.Settle();
        SaveFrame(window.Frame(),L"weather-rain.png");
        auto clear=weather;clear.city=L"北京 · 朝阳区";clear.condition=L"晴";clear.code=0;clear.temperature=31;clear.high=34;clear.low=23;
        window.SetWeather(clear);window.Settle();
        SaveFrame(window.Frame(),L"weather-clear.png");
        clear.isDay=false;clear.condition=L"晴";clear.temperature=18;clear.high=34;clear.low=23;
        window.SetWeather(clear);window.Settle();
        SaveFrame(window.Frame(),L"weather-night.png");
        auto unknown=weather;unknown.code=999;unknown.condition=L"霾";
        window.SetWeather(unknown);window.Settle();
        Check(!HasLightning(window.Frame()),"unknown weather code does not render as thunderstorm");
        auto storm=weather;storm.code=95;storm.condition=L"雷暴";
        window.SetWeather(storm);window.Settle();
        Check(HasLightning(window.Frame()),"WMO thunderstorm code renders lightning");
        window.SetWeather({});window.Settle();
        SaveFrame(window.Frame(),L"weather-off.png");
        window.SetWeather(weather);window.Settle();
        const auto warmDeadline=GetTickCount64()+6000;
        while ((!window.PerformanceReading() || !window.PerformanceReading()->cpu) &&
               GetTickCount64()<warmDeadline) Pump(20);
        Check(window.PerformanceReading() && window.PerformanceReading()->cpu,
              "performance rates are ready before opening their page");
        const auto warmStamp=window.PerformanceReading()->monotonicMillis;
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Performance,"wheel moves to the performance page");
        Check(window.PerformanceReading() && window.PerformanceReading()->cpu &&
              window.PerformanceReading()->monotonicMillis>=warmStamp,"first frame retains warm rates");
        window.Expand(false);
        Pump(1200);
        Check(window.PerformanceReading() && window.PerformanceReading()->cpu &&
              window.PerformanceReading()->monotonicMillis>warmStamp,"collapsed island keeps rates current");
        window.Expand(true);window.Settle();
        Check(window.PerformanceReading() && window.PerformanceReading()->cpu,"reopening keeps rates");
        for(int i=0;i<6&&window.Page()!=IslandPage::Performance;++i){
            SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        }
        SaveFrame(window.Frame(),L"performance.png");
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Todo,"wheel moves to the todo page");
        SaveFrame(window.Frame(),L"todo.png");
        Click(window.Handle(),30,48+29+13);Check(toggles==1,"todo checkbox toggles through the shared store");
        SendMessageW(window.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Music,"wheel wraps back to music");
        std::string opened;source.openTask=[&](const std::string& id){opened=id;};
        source.find=[](const std::string& id)->std::optional<IslandTodo>{IslandTodo t;t.id=id;t.title=L"给妈妈打电话";t.time=L"10月20日 19:30";t.notes=L"记得问周末安排";return t;};
        window.SetTodoSource(source);
        window.Expand(false);window.Settle();
        Check(window.ShowNotice("task-7",L"给妈妈打电话",L"19:30"),"island shows a reminder notice");window.Settle();
        SaveFrame(window.Frame(),L"notice.png");
        Check(window.ShowNotice("task-8",L"第二条提醒",L"19:30"),"a second reminder due at the same time is accepted");
        Click(window.Handle(),170,30);window.Settle();
        Check(window.Expanded()&&window.Page()==IslandPage::Reminder&&opened.empty(),"clicking the notice opens the reminder page");
        SaveFrame(window.Frame(),L"reminders.png");
        Click(window.Handle(),200,64);Check(opened=="task-8","the newest reminder is listed first and opens its task");
        Click(window.Handle(),200,108);Check(opened=="task-7","a queued reminder is listed too");
        SendMessageW(window.Handle(),WM_MOUSEHWHEEL,MAKEWPARAM(0,static_cast<WORD>(WHEEL_DELTA)),0);window.Settle();
        Check(window.Page()==IslandPage::Music,"sideways scrolling wraps to the first page");
        window.Expand(false);window.Settle();
        display.topmost=false;Check(window.SetDisplay(display),"desktop layer option applies");
        Check(!window.ShowNotice("task-8",L"x",L""),"a desktop-layer island leaves reminders to the popup");
        window.SetTodoSource({});
        window.Stop();Check(window.Frame().pixels.empty(),"stop frees rendered bitmap");
    }
    CoUninitialize();std::cout<<"PASS island model, native input, persistence, rollback, lifecycle and rendering\n";
}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto name=L"LumeIslandTest-"+std::to_wstring(GetCurrentProcessId());
    HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);Check(desktop!=nullptr,"create isolated desktop");
    std::thread runner([&]{Check(SetThreadDesktop(desktop)!=FALSE,"bind isolated desktop");
        Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;Check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"PNG encoder");
        Scenario();Gdiplus::GdiplusShutdown(token);});runner.join();
    Check(CloseDesktop(desktop)!=FALSE,"close isolated desktop");return 0;
}
