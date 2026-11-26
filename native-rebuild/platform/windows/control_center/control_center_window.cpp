#include "platform/windows/control_center/control_center_window.h"
#include "platform/windows/dock/dock_popup_bounds.h"
#include "platform/windows/control_center/system_controls.h"
#include "platform/windows/control_center/network_controls.h"
#include "platform/windows/control_center/panel_worker.h"
#include "platform/windows/material/material_api.h"
#include "platform/windows/material/widget_material_runtime.h"
#include "platform/windows/widget_ui/layered_surface.h"
#include "platform/windows/widget_ui/window_layer_policy.h"
#include "platform/windows/calendar/calendar_todo_ui.h"
#include "platform/windows/rounded_window_surface.h"
#include "platform/windows/text_util.h"
#include <shellapi.h>
#include <windowsx.h>
#include <winrt/base.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <thread>

namespace lume::platform::windows {
namespace cc = control_center;
namespace {
using Json = nlohmann::json;
constexpr wchar_t kClass[]=L"LumeControlCenter", kPasswordClass[]=L"LumeControlCenterPassword";
constexpr UINT kState=WM_APP+0x6f6, kAction=WM_APP+0x6f8,kDismiss=WM_APP+0x6f9;
constexpr UINT kTrayState=WM_APP+0x6fa;
constexpr UINT_PTR kRefreshTimer=1,kContactTimer=2,kMaterialStartTimer=3;
constexpr int kWidth=372,kGap=12;
constexpr float kCornerRadius=22.f;
std::atomic<bool> english{false};
std::atomic<ULONGLONG> lastDismiss{0};
std::string Utf8(std::wstring_view value){return lume::text::Utf8(value);}
std::wstring Wide(std::string_view value){return lume::text::Wide(value);}
nlohmann::json Choices(const std::vector<cc::Choice>& items) {
    auto out = nlohmann::json::array();
    for (const auto& item : items) out.push_back({{"id", Utf8(item.id)}, {"name", Utf8(item.name)}});
    return out;
}
nlohmann::json Encode(const cc::State& s) {
    return {{"wifi", {{"available", s.wifi.available}, {"on", s.wifi.on}, {"ssid", Utf8(s.ssid)}, {"signal", s.signal}}},
            {"ethernet", s.ethernet}, {"wifiAdapter",s.wifiAdapter.value_or(true)},
            {"bluetooth", {{"available", s.bluetooth.available}, {"on", s.bluetooth.on}}},
            {"hotspot", {{"available", s.hotspotAvailable}, {"on", s.hotspotOn}, {"reason", Utf8(s.hotspotReason)}}},
            {"airplane", {{"available", s.airplaneAvailable}, {"on", s.airplaneOn}}},
            {"audio", {{"available", s.audioAvailable}, {"volume", s.volume}, {"muted", s.muted}, {"output", Utf8(s.output)},
                       {"outputs", Choices(s.outputs)}}},
            {"brightness", {{"available", s.brightnessAvailable}, {"level", s.brightness}, {"display", Utf8(s.display)},
                            {"displays", Choices(s.displays)}}},
            {"battery", {{"available", s.batteryAvailable}, {"percent", s.battery}, {"charging", s.charging}}},
            {"tray", s.trayAvailable}};
}
// Windows settings pages the panel may open; anything else is refused.
bool LaunchSettings(HWND owner,const wchar_t* uri){
    SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_FLAG_NO_UI;info.hwnd=owner;
    info.lpVerb=L"open";info.lpFile=uri;info.nShow=SW_SHOWNORMAL;
    return ShellExecuteExW(&info)!=FALSE;
}
const wchar_t* SettingsUri(const std::string& page) {
    if (page == "wifi") return L"ms-settings:network-wifi";
    if (page == "bluetooth") return L"ms-settings:bluetooth";
    if (page == "hotspot") return L"ms-settings:network-mobilehotspot";
    if (page == "sound") return L"ms-settings:sound";
    if (page == "display") return L"ms-settings:display";
    if (page == "home") return L"ms-settings:";
    if (page == "wifi-more") return L"ms-settings:network-wifisettings";
    return nullptr;
}

// One worker thread runs every system call in order; consecutive slider
// values for the same control collapse to the newest.
struct Worker {
    std::weak_ptr<cc::PanelWorker> queue;
    cc::PanelWorker::Generation generation=0;
    std::wstring display;    // selected brightness display (worker only)
    std::string view;        // "", "wifi" or "bluetooth": the open list page (worker only)
    // The newest Wi-Fi connection attempt (worker only).
    std::wstring joining;
    std::string joinStatus;  // started | needs-password | unsupported | failed | connected
    Json pairResult=Json::object();
    ULONGLONG joinDeadline = 0;
    std::optional<bool> trayResult;
    unsigned trayRequest=0;
    std::string Execute(cc::PanelWorker::Generation current,const cc::PanelWorker::Job& job) {
            if(generation!=current){generation=current;display.clear();view.clear();joining.clear();joinStatus.clear();joinDeadline=0;trayResult.reset();trayRequest=0;pairResult.clear();}
            bool full = false;
            const auto& v = job.value;
            if (job.key == "refresh") full = v.is_boolean() && v.get<bool>();
            else if (job.key == "tray" && v.is_object()) {trayResult=cc::OpenTrayOverflow({v.value("x",0),v.value("y",0)});trayRequest=v.value("request",0u);}
            else if (job.key == "wifi" && v.is_boolean()) cc::SetRadio(true, v.get<bool>());
            else if (job.key == "bluetooth" && v.is_boolean()) cc::SetRadio(false, v.get<bool>());
            else if (job.key == "airplane" && v.is_boolean()) cc::SetAirplane(v.get<bool>());
            else if (job.key == "hotspot" && v.is_boolean()) cc::SetHotspot(v.get<bool>());
            else if (job.key == "volume" && v.is_number_integer()) cc::SetVolume(v.get<int>());
            else if (job.key == "mute" && v.is_boolean()) cc::SetMute(v.get<bool>());
            else if (job.key == "output" && v.is_string()) cc::SetOutput(Wide(v.get<std::string>()));
            else if (job.key == "display" && v.is_string()) display = Wide(v.get<std::string>());
            else if (job.key == "brightness" && v.is_number_integer()) cc::SetBrightness(display, v.get<int>());
            else if (job.key == "view" && v.is_string()) {
                view = v.get<std::string>();
                if (view != "wifi" && view != "bluetooth") view.clear();
                if (view == "wifi") cc::ScanNetworks();
            }
            else if (job.key == "scan") cc::ScanNetworks();
            else if (job.key == "pair" && v.is_string() && !v.get_ref<const std::string&>().empty()) {
                const auto id=v.get<std::string>();pairResult={{"id",id},{"success",cc::PairDevice(Wide(id))}};
            }
            else if (job.key == "disconnect") cc::Disconnect();
            else if (job.key == "connect" && v.is_object() && v.contains("ssid") && v["ssid"].is_string()) {
                if (!joining.empty() && joinStatus == "started" && joining != Wide(v["ssid"].get<std::string>())) cc::ForgetCreatedProfile(joining);
                joining = Wide(v["ssid"].get<std::string>());
                const auto password = v.contains("password") && v["password"].is_string() ? Wide(v["password"].get<std::string>()) : std::wstring{};
                const auto result = cc::Connect(joining, password);
                joinStatus = result == cc::ConnectResult::Started ? "started" : result == cc::ConnectResult::NeedsPassword ? "needs-password" :
                             result == cc::ConnectResult::Unsupported ? "unsupported" : "failed";
                joinDeadline = GetTickCount64() + 15000;
            }
            else return {};
            // Skip the read while more changes for the panel are queued.
            if(const auto active=queue.lock();active&&active->PendingActions(current))return {};
            const auto state = cc::Read(display, full);
            display = state.display;
            auto encoded = Encode(state);
            if(trayResult){encoded["trayResult"]=*trayResult;encoded["trayRequest"]=trayRequest;}
            char accent[8]{};
            std::snprintf(accent, sizeof(accent), "#%06x", static_cast<unsigned>(cc::AccentColor() & 0xffffff));
            encoded["accent"] = accent;
            if (joinStatus == "started") {
                if (cc::ConnectedTo(joining)) joinStatus = "connected";
                else if (GetTickCount64() > joinDeadline) { cc::ForgetCreatedProfile(joining);joinStatus = "failed"; }
            }
            if (!joining.empty()) encoded["join"] = {{"ssid", Utf8(joining)}, {"status", joinStatus}};
            if (view == "wifi") {
                auto rows = nlohmann::json::array();
                for (const auto& n : cc::Networks())
                    rows.push_back({{"ssid", Utf8(n.ssid)}, {"signal", n.signal}, {"secured", n.secured}, {"connected", n.connected}, {"known", n.known}});
                encoded["networks"] = rows;
            } else if (view == "bluetooth") {
                auto rows = nlohmann::json::array();
                if(state.bluetooth.on)for (const auto& d : cc::BluetoothDevices()) rows.push_back({{"name", Utf8(d.name)}, {"id",Utf8(d.id)},{"connected", d.connected},{"paired",d.paired},{"canPair",!d.paired&&!d.id.empty()}});
                encoded["devices"] = rows;
                if(!pairResult.empty())encoded["pair"]=pairResult;
            }
            return encoded.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
};
std::shared_ptr<cc::PanelWorker> SharedWorker() {
    static const auto worker=[] {
        auto backend=std::make_shared<Worker>();
        auto queue=std::make_shared<cc::PanelWorker>(
            [backend](auto generation,const auto& job){return backend->Execute(generation,job);},
            []{winrt::init_apartment(winrt::apartment_type::multi_threaded);},[] {winrt::uninit_apartment();});
        backend->queue=queue;return queue;
    }();
    return worker;
}
}  // namespace

struct ControlCenterWindow::Impl : std::enable_shared_from_this<Impl> {
    struct HitTarget { WidgetRect rect; std::string key; Json value; bool enabled=true; };
    struct Pending { Json value; ULONGLONG until=0; };
    HWND window=nullptr,owner=nullptr,passwordWindow=nullptr,passwordEdit=nullptr;
    HINSTANCE instance=nullptr;
    std::shared_ptr<cc::PanelWorker> worker;
    std::shared_ptr<cc::PanelWorker> trayWorker;
    std::function<bool(HWND,const wchar_t*)> openSettings;
    bool settingsFailed=false;
    bool trayBusy=false,trayFailed=false;
    unsigned trayRequest=0;
    cc::PanelWorker::Generation workerGeneration=0;
    cc::PanelWorker::Generation trayGeneration=0;
    std::unique_ptr<WidgetCanvas> canvas;
    LayeredSurface surface;
    WidgetMaterialRuntime material;
    std::unique_ptr<todo_ui::Surface> passwordSurface;
    Json state=Json::object();
    std::vector<HitTarget> hits;
    std::optional<HitTarget> pressedTarget;
    std::map<std::string,Pending> pending;
    RECT anchor{};
    dock::Edge edge=dock::Edge::Bottom;
    Material look=Material::Liquid;
    bool closing=false,shown=false,fresh=false,haveState=false,en=false,solidFallback=false;
    int heightDip=432,pressed=-1,focus=-1,scroll=0,pickerScroll=0;
    std::string page,picker,dragging,passwordSsid;
    ULONGLONG lastFullRead=0;
    float opacity=1;
    POINT cursor{};
    WidgetBitmap lastFrame;
    WidgetBitmap inputMask;
    std::uint32_t contentGeneration=0;
    struct OutsideState {
        std::atomic<HWND> window{nullptr},password{nullptr};
        std::atomic<cc::PanelWorker::Generation> generation{0};
        std::atomic<DWORD> thread{0};
        std::atomic<bool> stopped{false};
    };
    std::shared_ptr<OutsideState> outside;
    inline static thread_local OutsideState* hookState=nullptr;

    static bool Contains(HWND window,POINT point){RECT bounds{};return window&&IsWindowVisible(window)&&GetWindowRect(window,&bounds)&&PtInRect(&bounds,point);}
    static LRESULT CALLBACK OutsideMouse(int code,WPARAM message,LPARAM data){
        if(code>=0&&(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||message==WM_MBUTTONDOWN||message==WM_XBUTTONDOWN)){
            const auto point=reinterpret_cast<const MSLLHOOKSTRUCT*>(data)->pt;
            if(const auto state=hookState;state&&!state->stopped.load()){
                const auto window=state->window.load();
                if(window&&!Contains(window,point)&&!Contains(state->password.load(),point))
                    PostMessageW(window,kDismiss,static_cast<WPARAM>(state->generation.load()),0);
            }
        }
        return CallNextHookEx(nullptr,code,message,data);
    }
    void WatchOutside(){
        if(outside)return;
        auto watcher=std::make_shared<OutsideState>();watcher->window=window;watcher->password=passwordWindow;watcher->generation=workerGeneration;outside=watcher;
        // Low-level callbacks must keep pumping even while the UI initializes
        // a glass scene or paints an input popup. The thread owns no UI state.
        try{std::thread([watcher]{
            MSG message{};PeekMessageW(&message,nullptr,WM_USER,WM_USER,PM_NOREMOVE);watcher->thread=GetCurrentThreadId();
            if(watcher->stopped)return;hookState=watcher.get();
            const auto hook=SetWindowsHookExW(WH_MOUSE_LL,OutsideMouse,GetModuleHandleW(nullptr),0);
            if(hook){while(!watcher->stopped&&GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}UnhookWindowsHookEx(hook);}
            hookState=nullptr;
        }).detach();}catch(...){outside.reset();}
    }
    void StopWatchingOutside(){
        if(const auto watcher=std::exchange(outside,{})){watcher->stopped=true;watcher->window=nullptr;watcher->password=nullptr;
            if(const auto thread=watcher->thread.load())PostThreadMessageW(thread,WM_QUIT,0,0);}
    }
    void BindWorker(){
        if(!workerGeneration)workerGeneration=worker->Bind([weak=weak_from_this()](auto generation){const auto self=weak.lock();return self&&self->window&&!self->closing&&PostMessageW(self->window,kState,static_cast<WPARAM>(generation),0)!=FALSE;});
    }
    void OpenTray(){
        trayFailed=false;
        trayBusy=true;
        Json request={{"x",(anchor.left+anchor.right)/2},{"y",anchor.top-MulDiv(kGap,static_cast<int>(Dpi()),96)},{"request",++trayRequest}};
        if(!trayWorker){Push("tray",std::move(request));return;}
        if(!trayGeneration)trayGeneration=trayWorker->Bind([weak=weak_from_this()](auto generation){const auto self=weak.lock();return self&&self->window&&PostMessageW(self->window,kTrayState,static_cast<WPARAM>(generation),0)!=FALSE;});
        if(!trayWorker->Push(trayGeneration,{"tray",std::move(request)})){trayBusy=false;trayFailed=true;}
    }
    void TrayCompleted(const Json& value){
        if(!trayBusy||!value.contains("trayResult")||value.value("trayRequest",0u)!=trayRequest)return;
        trayBusy=false;trayFailed=!value.value("trayResult",false);
        if(!trayFailed)Close();
    }
    void ReadState(){
        BindWorker();const auto now=GetTickCount64();const bool full=!lastFullRead||now-lastFullRead>=30000;
        if(full)lastFullRead=now;worker->Push(workerGeneration,{"refresh",full});
    }
    bool Prewarm(){if(!window||closing)return false;if(!haveState&&!workerGeneration)ReadState();return true;}
    void Dismiss(){POINT point{};lastDismiss=GetCursorPos(&point)&&PtInRect(&anchor,point)?GetTickCount64():0;Close();}

    std::wstring T(const wchar_t* zh,const wchar_t* translated)const{return en?translated:zh;}
    unsigned Dpi()const{return window?GetDpiForWindow(window):GetDpiForSystem();}
    Json Group(const char* key)const{const auto found=state.find(key);return found!=state.end()&&found->is_object()?*found:Json::object();}
    bool Available(const char* key)const{return fresh&&Group(key).value("available",false);}
    Json Value(const std::string& key,Json actual){
        const auto found=pending.find(key);if(found==pending.end())return actual;
        if(found->second.value==actual||GetTickCount64()>found->second.until){pending.erase(found);return actual;}
        return found->second.value;
    }
    bool On(const std::string& key){const auto group=Group(key.c_str());return Value(key,group.value("on",false)).get<bool>();}
    std::uint32_t Accent()const{
        const auto text=state.value("accent",std::string{});
        if(text.size()==7&&text[0]=='#'){unsigned color=0;if(std::sscanf(text.c_str()+1,"%x",&color)==1)return color&0xffffff;}
        return 0x0067c0;
    }
    bool Glass()const{return look!=Material::None;}
    bool UseGlass()const{return Glass()&&!solidFallback;}
    void Push(std::string key,Json value){
        if(!shown||!workerGeneration)return;
        if(key=="volume"||key=="brightness"||key=="mute"||key=="wifi"||key=="bluetooth"||key=="hotspot"||key=="airplane")
            pending[key]={value,GetTickCount64()+2500};
        worker->Push(workerGeneration,{std::move(key),std::move(value)});
    }
    void Place(){
        if(!window)return;const int dpi=static_cast<int>(Dpi());
        MONITORINFO monitor{sizeof(monitor)};const POINT centre{(anchor.left+anchor.right)/2,anchor.top};
        if(!GetMonitorInfoW(MonitorFromPoint(centre,MONITOR_DEFAULTTONEAREST),&monitor))return;
        auto area=monitor.rcWork;const LONG margin=MulDiv(8,dpi,96);
        InflateRect(&area,-margin,-margin);
        const auto inside=SpaceInsideDock(area,anchor,edge,MulDiv(kGap,dpi,96));
        const LONG width=std::min<LONG>(MulDiv(kWidth,dpi,96),area.right-area.left);
        const LONG height=std::min<LONG>(MulDiv(heightDip,dpi,96),area.bottom-area.top);
        const bool fits=inside.right-inside.left>=width&&inside.bottom-inside.top>=height;
        const auto origin=DockPopupOrigin(fits?inside:area,anchor,edge,width,height);
        SetWindowPos(window,HWND_TOPMOST,origin.x,origin.y,width,height,SWP_NOACTIVATE);
    }
    bool UpdateMaterial(){
        if(!window||closing||!shown||!UseGlass()||lastFrame.pixels.empty())return false;
        LumeMaterialParameters parameters{};parameters.size=sizeof(parameters);parameters.owner=window;
        parameters.frosted=look==Material::Frosted;parameters.opticalProfile=look==Material::Frosted?1u:3u;
        parameters.opacityEnabled=1;parameters.opacity=opacity;parameters.lightForeground=1;parameters.cornerRadius=kCornerRadius;
        RECT rect{};GetClientRect(window,&rect);
        parameters.contentEnabled=1;parameters.contentWidth=rect.right*96.f/Dpi();parameters.contentHeight=rect.bottom*96.f/Dpi();
        parameters.contentPixels=lastFrame.pixels.data();parameters.contentPixelWidth=lastFrame.width;parameters.contentPixelHeight=lastFrame.height;parameters.contentGeneration=contentGeneration;
        MaterialContact(window,rect.right*96.f/Dpi(),rect.bottom*96.f/Dpi(),Dpi(),parameters.contactX,parameters.contactY);
        BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);parameters.reducedMotion=!animations;
        return material.State()==WidgetMaterialState::Detached?material.Attach(parameters):material.Update(parameters);
    }
    int HitAt(float x,float y)const{
        for(std::size_t i=hits.size();i>0;--i){const auto& h=hits[i-1];if(x>=h.rect.x&&y>=h.rect.y&&x<h.rect.x+h.rect.width&&y<h.rect.y+h.rect.height)return static_cast<int>(i-1);}
        return -1;
    }
    void Hit(WidgetRect rect,std::string key,Json value={},bool enabled=true){hits.push_back({rect,std::move(key),std::move(value),enabled});}
    void Text(const std::wstring& text,WidgetRect rect,float size=13,float alpha=1,bool bold=false,WidgetTextAlign align=WidgetTextAlign::Left){
        auto display=text;
        if(canvas->MeasureText(display,size,bold)>rect.width){
            const auto ellipsis=std::wstring(L"…");std::size_t low=0,high=display.size();
            while(low<high){const auto middle=(low+high+1)/2;if(canvas->MeasureText(display.substr(0,middle)+ellipsis,size,bold)<=rect.width)low=middle;else high=middle-1;}
            if(low&&display[low-1]>=0xd800&&display[low-1]<=0xdbff)--low;display=display.substr(0,low)+ellipsis;
        }
        if(UseGlass()&&look==Material::Liquid){auto shadow=rect;shadow.y+=1;canvas->Text(display,shadow,size,0x000000,alpha*.5f,bold,align);}
        canvas->Text(display,rect,size,0xffffff,alpha,bold,align);
    }
    void Panel(WidgetRect rect,float radius,float alpha=.075f){canvas->Rounded(rect,radius,UseGlass()&&look==Material::Liquid?0x121418:0xffffff,UseGlass()&&look==Material::Liquid?.34f:alpha);}
    void Glyph(const std::string& key,float x,float y,float alpha=1,int level=4){
        auto& c=*canvas;const auto line=[&](float a,float b,float d,float e,float thickness=1.6f){c.Line(x+a,y+b,x+d,y+e,0xffffff,alpha,thickness);};
        if(key=="wifi"){
            for(int i=0;i<3;++i)c.AnnularSector(x+10,y+16,static_cast<float>(3+i*4),static_cast<float>(4.6+i*4),-45,90,0xffffff,alpha*(level>=i+2?1.f:.28f));
            c.Ellipse({x+8.4f,y+14.4f,3.2f,3.2f},0xffffff,alpha);
        }else if(key=="bluetooth"){line(9,1,9,19);line(9,1,16,7);line(16,7,4,17);line(4,3,16,13);line(16,13,9,19);}
        else if(key=="hotspot"){c.Ellipse({x+8,y+8,4,4},0xffffff,alpha);c.AnnularSector(x+10,y+10,5,6.5f,20,140,0xffffff,alpha);c.AnnularSector(x+10,y+10,5,6.5f,200,140,0xffffff,alpha);c.AnnularSector(x+10,y+10,9,10.5f,20,140,0xffffff,alpha);c.AnnularSector(x+10,y+10,9,10.5f,200,140,0xffffff,alpha);}
        else if(key=="airplane"){c.Polygon({x+9,y+1,x+11,y+1,x+12,y+8,x+19,y+12,x+19,y+14,x+11,y+12,x+11,y+17,x+14,y+19,x+14,y+20,x+10,y+19,x+6,y+20,x+6,y+19,x+9,y+17,x+9,y+12,x+1,y+14,x+1,y+12,x+8,y+8},0xffffff,alpha);}
        else if(key=="sun"){c.AnnularSector(x+10,y+10,3,4.5f,0,360,0xffffff,alpha);for(int i=0;i<8;++i){const float angle=i*3.14159265f/4;c.Line(x+10+7*std::cos(angle),y+10+7*std::sin(angle),x+10+10*std::cos(angle),y+10+10*std::sin(angle),0xffffff,alpha,1.5f);}}
        else if(key=="volume"||key=="muted"){c.Polygon({x+2,y+7,x+6,y+7,x+11,y+3,x+11,y+17,x+6,y+13,x+2,y+13},0xffffff,alpha);if(key=="muted"){line(14,7,19,13);line(19,7,14,13);}else{c.AnnularSector(x+9,y+10,5,6.5f,45,90,0xffffff,alpha);c.AnnularSector(x+9,y+10,9,10.5f,45,90,0xffffff,alpha);}}
        else if(key=="back"){line(13,3,6,10);line(6,10,13,17);}
        else if(key=="right"){line(7,4,13,10);line(13,10,7,16);}
        else if(key=="settings"){c.AnnularSector(x+10,y+10,6,8,0,360,0xffffff,alpha);c.AnnularSector(x+10,y+10,2,3.5f,0,360,0xffffff,alpha);for(int i=0;i<8;++i){const float a=i*3.14159265f/4;c.Line(x+10+7*std::cos(a),y+10+7*std::sin(a),x+10+10*std::cos(a),y+10+10*std::sin(a),0xffffff,alpha,3);}}
        else if(key=="tray"){c.Rounded({x+1,y+3,18,14},4,0xffffff,alpha*.7f);for(int i=0;i<3;++i)c.Ellipse({x+4+i*5.f,y+10,2,2},0x202024,alpha);}
    }
    void Button(WidgetRect rect,const std::wstring& text,std::string key,Json value={},bool enabled=true){
        Panel(rect,10,enabled?.08f:.03f);Text(text,{rect.x+8,rect.y,rect.width-16,rect.height},12,enabled?1.f:.4f,false,WidgetTextAlign::Center);
        Hit(rect,std::move(key),std::move(value),enabled);
    }
    void Tile(float x,float y,const std::string& key,const std::wstring& title,const std::wstring& subtitle,bool detail){
        const bool available=Available(key.c_str()),on=On(key);const float alpha=available?1.f:.45f;
        if(on&&available)canvas->Rounded({x,y,165,72},18,Accent());else Panel({x,y,165,72},18);
        canvas->Ellipse({x+12,y+19,34,34},0xffffff,.10f*alpha);Glyph(key,x+19,y+26,alpha);
        Text(title,{x+56,y+14,detail?70.f:100.f,24},13.5f,alpha,true);
        Text(subtitle,{x+56,y+41,detail?70.f:100.f,18},11.5f,alpha*.68f);
        Hit({x,y,detail?127.f:165.f,72},key,!on,available);
        if(detail){canvas->Line(x+127,y+1,x+127,y+71,0xffffff,.1f);Glyph("right",x+136,y+26,1.f);
            Hit({x+127,y,38,72},"page",key);}
    }
    void Slider(float y,const char* key,const char* group,const std::string& glyph){
        const auto info=Group(group);const bool available=fresh&&info.value("available",false);
        Panel({16,y,340,40},14,available?.06f:.04f);
        const bool muted=std::string(key)=="volume"&&Value("mute",info.value("muted",false)).get<bool>();
        Glyph(muted?"muted":glyph,28,y+10,available?1.f:.45f);
        if(!available){Text(std::string(key)=="volume"?T(L"没有可用的音频输出",L"No audio output"):T(L"当前显示器不支持调节亮度",L"Brightness unavailable"),{62,y,280,40},12,.45f);return;}
        const int value=std::clamp(Value(key,info.value(std::string(key)=="volume"?"volume":"level",0)).get<int>(),0,100);
        canvas->Rounded({64,y+17,225,6},3,0xffffff,.18f);canvas->Rounded({64,y+17,225*value/100.f,6},3,Accent());
        canvas->Ellipse({55+225*value/100.f,y+11,18,18},0xffffff);Text(std::to_wstring(value)+L"%",{300,y,44,40},12,.7f,false,WidgetTextAlign::Right);
        Hit({55,y+3,243,34},std::string("slider:")+key,{},available);
        if(std::string(key)=="volume")Hit({20,y+4,32,32},"mute",!muted);
    }
    void Main(){
        Text(T(L"控制中心",L"Control center"),{16,16,96,32},15,1,true);Glyph("settings",329,22);Hit({322,16,34,32},"settings","home");
        const auto wifi=Group("wifi");Panel({115,17,145,30},15,.08f);Text(settingsFailed?T(L"设置未能打开",L"Settings unavailable"):state.value("ethernet",false)?T(L"以太网",L"Ethernet"):Wide(wifi.value("ssid",std::string{})),{124,16,127,32},12,.8f);
        const auto battery=Group("battery");if(battery.value("available",false))Text(std::to_wstring(battery.value("percent",0))+L"%",{269,16,50,32},12,.75f);
        const auto sub=[&](const char* key){return !Available(key)?T(L"不可用",L"Unavailable"):On(key)?T(L"已开启",L"On"):T(L"已关闭",L"Off");};
        Tile(16,60,"wifi",L"Wi-Fi",wifi.value("ssid",std::string{}).empty()?sub("wifi"):Wide(wifi["ssid"].get<std::string>()),true);
        Tile(191,60,"bluetooth",T(L"蓝牙",L"Bluetooth"),sub("bluetooth"),true);
        const auto reason=Group("hotspot").value("reason",std::string{});
        const auto hotSub=Available("hotspot")?sub("hotspot"):reason=="no-network"?T(L"未连接网络",L"No network"):
            reason=="policy"?T(L"已被策略禁用",L"Blocked by policy"):reason=="hardware"?T(L"网卡不支持",L"Unsupported adapter"):sub("hotspot");
        Tile(16,142,"hotspot",T(L"移动热点",L"Mobile hotspot"),hotSub,false);
        Tile(191,142,"airplane",T(L"飞行模式",L"Airplane mode"),sub("airplane"),false);
        Slider(226,"volume","audio","volume");Slider(278,"brightness","brightness","sun");
        const auto audio=Group("audio"),brightness=Group("brightness");
        for(int i=0;i<2;++i){const auto& group=i?brightness:audio;const char* list=i?"displays":"outputs";const char* key=i?"display":"output";
            const auto choices=group.value(list,Json::array());std::wstring name;
            for(const auto& item:choices)if(item.value("id",std::string{})==group.value(key,std::string{}))name=Wide(item.value("name",std::string{}));
            Button({16+i*175.f,330,165,34},name.empty()?(i?T(L"显示器",L"Display"):T(L"音频输出",L"Audio output")):name,"picker",key,!choices.empty()&&fresh);}
        if(state.value("tray",false)){Glyph("tray",30,387);Button({16,376,340,40},trayBusy?T(L"正在打开托盘…",L"Opening notification icons…"):trayFailed?T(L"托盘未能打开，点击重试",L"Could not open icons. Retry"):T(L"系统托盘图标",L"Notification icons"),"tray",{},!trayBusy);}
        if(!picker.empty()){
            const auto group=Group(picker=="output"?"audio":"brightness");const auto choices=group.value(picker=="output"?"outputs":"displays",Json::array());
            const auto count=std::min<std::size_t>(choices.size(),8);pickerScroll=std::clamp(pickerScroll,0,std::max(0,static_cast<int>(choices.size())-8));const float y=326-static_cast<float>(count)*36;
            canvas->Rounded({20,y-4,332,static_cast<float>(count)*36+8},14,0x2a2a30);
            for(std::size_t i=0;i<count;++i){const auto& choice=choices[i+static_cast<std::size_t>(pickerScroll)];Button({24,y+i*36.f,324,34},Wide(choice.value("name",std::string{})),picker,choice.value("id",std::string{}));}
        }
    }
    void Details(float height){
        const bool wifi=page=="wifi";Glyph("back",20,22);Hit({16,16,32,32},"back");
        Text(wifi?L"Wi-Fi":T(L"蓝牙",L"Bluetooth"),{60,16,220,32},16,1,true);
        const bool on=On(page);Button({302,20,54,26},on?T(L"开",L"On"):T(L"关",L"Off"),page,!on,Available(page.c_str()));
        const float footer=height-44;
        Button({16,footer,wifi?248.f:340.f,34},settingsFailed?T(L"设置未能打开，重试",L"Couldn't open settings. Retry"):wifi?T(L"更多 Wi-Fi 设置",L"More Wi-Fi settings"):T(L"更多蓝牙设置",L"More Bluetooth settings"),"settings",wifi?"wifi-more":"bluetooth");
        if(wifi)Button({270,footer,86,34},T(L"刷新",L"Refresh"),"scan",{},Available("wifi"));
        const auto rows=state.value(wifi?"networks":"devices",Json::array());
        const int total=static_cast<int>(rows.size()),visible=std::max(1,static_cast<int>((footer-54)/48));scroll=std::clamp(scroll,0,std::max(0,total-visible));
        const auto message=!fresh?T(L"正在读取…",L"Loading…"):!Available(page.c_str())?T(L"当前不可用",L"Unavailable"):!on?T(L"已关闭",L"Off"):!state.contains(wifi?"networks":"devices")?T(L"正在读取…",L"Loading…"):rows.empty()?T(L"未找到设备或网络",L"Nothing found nearby"):L"";
        if(!message.empty()){Text(message,{16,54,340,std::max(20.f,footer-54)},13,.65f,false,WidgetTextAlign::Center);return;}
        for(int i=0;i<visible&&i+scroll<total;++i){const auto& row=rows[static_cast<std::size_t>(i+scroll)];const float y=54+i*48.f;
            const bool connected=row.value("connected",false);Panel({16,y,340,44},8,connected?.12f:.06f);
            const int signal=row.value("signal",0);Glyph(wifi?"wifi":"bluetooth",28,y+13,1,signal>=75?4:signal>=50?3:signal>=25?2:1);const auto name=row.value(wifi?"ssid":"name",std::string{});
            Text(Wide(name),{62,y+2,196,22},13,1,true);
            std::wstring subtitle=connected?T(L"已连接",L"Connected"):wifi?(row.value("secured",false)?T(L"安全",L"Secured"):T(L"开放",L"Open network")):row.value("paired",true)?T(L"已配对 · 未连接",L"Paired · Disconnected"):T(L"未配对",L"Not paired");
            const auto join=state.value("join",Json::object());if(wifi&&join.value("ssid",std::string{})==name){const auto status=join.value("status",std::string{});
                if(status=="started")subtitle=T(L"正在连接…",L"Connecting…");else if(status=="failed"||status=="needs-password")subtitle=T(L"连接失败，请重试",L"Could not connect. Try again.");else if(status=="unsupported")subtitle=T(L"请在 Windows 设置中连接",L"Use Windows settings");}
            const auto pair=state.value("pair",Json::object());if(!wifi&&pair.value("id",std::string{})==row.value("id",std::string{})&&!pair.value("success",true))subtitle=T(L"请在 Windows 设置中配对",L"Pair in Windows settings");
            Text(subtitle,{62,y+23,202,18},11.5f,.65f);
            if(wifi)Button({270,y+6,76,32},connected?T(L"断开",L"Disconnect"):T(L"连接",L"Connect"),connected?"disconnect":"network",row,Available("wifi"));
            else if(row.value("canPair",false))Button({270,y+6,76,32},T(L"配对",L"Pair"),"pair",row.value("id",std::string{}));
        }
    }
    void ChangePage(std::string target){
        page=std::move(target);picker.clear();scroll=0;settingsFailed=false;
        pressed=focus=-1;pressedTarget.reset();Push("view",page);
    }
    bool Render(){
        if(!window||closing||!shown||!canvas)return false;
        RECT client{};GetClientRect(window,&client);const float width=client.right*96.f/Dpi(),height=client.bottom*96.f/Dpi();
        if(!canvas->Begin(width,height,Dpi()))return false;
        canvas->Rounded({0,0,width,height},kCornerRadius,UseGlass()?0x000000:0x1e1e22,UseGlass()?.004f:1.f);
        hits.clear();if(page.empty())Main();else Details(height);
        if(focus>=0&&static_cast<std::size_t>(focus)<hits.size()){const auto r=hits[static_cast<std::size_t>(focus)].rect;canvas->Line(r.x,r.y+r.height-2,r.x+r.width,r.y+r.height-2,0x8ea5ff,1,2);}
        WidgetBitmap bitmap;if(!canvas->Finish(bitmap))return false;lastFrame=std::move(bitmap);
        if(UseGlass()){
            // The material presents glass and controls in the same frame. The
            // HWND only retains an almost transparent native input surface;
            // no provisional solid panel or separate foreground is exposed.
            if(inputMask.width!=lastFrame.width||inputMask.height!=lastFrame.height){
                inputMask.width=lastFrame.width;inputMask.height=lastFrame.height;inputMask.pixels.resize(lastFrame.pixels.size());
                for(std::size_t i=0;i<lastFrame.pixels.size();++i)inputMask.pixels[i]=(lastFrame.pixels[i]>>24)?0x01000000u:0;
            }
            if(!surface.Present(window,inputMask))return false;
            if(++contentGeneration==0)++contentGeneration;
            if(!UpdateMaterial())return Fallback();
            return true;
        }
        return surface.Present(window,lastFrame,nullptr,static_cast<BYTE>(std::clamp(opacity,0.f,1.f)*255));
    }
    bool Fallback(){
        if(!UseGlass())return false;solidFallback=true;KillTimer(window,kMaterialStartTimer);KillTimer(window,kContactTimer);material.Detach();
        return Render();
    }
    void CheckMaterial(bool expired=false){
        if(!shown||!UseGlass())return;const auto status=material.Status();
        const bool ready=status.frameReady&&status.frames;
        if(status.failed||(expired&&!ready))Fallback();else if(ready)KillTimer(window,kMaterialStartTimer);
    }
    void SetSlider(const std::string& key,float x){
        const auto value=std::clamp(static_cast<int>(std::lround((x-64)*100/225)),0,100);
        Push(key,value);Render();
    }
    void ClosePassword(){
        const auto old=passwordWindow;passwordWindow=nullptr;passwordEdit=nullptr;passwordSsid.clear();
        if(outside)outside->password=nullptr;
        if(old){SetWindowLongPtrW(old,GWLP_USERDATA,0);SetWindowTextW(GetDlgItem(old,1),L"");DestroyWindow(old);}
        passwordSurface.reset();if(shown&&IsWindow(window)){EnableWindow(window,TRUE);SetForegroundWindow(window);}
    }
    void ConnectPassword(){
        if(!passwordWindow||!passwordEdit)return;
        const auto password=Utf8(todo_ui::Text(passwordEdit));const auto ssid=passwordSsid;
        Push("connect",{{"ssid",ssid},{"password",password}});ClosePassword();
    }
    static LRESULT CALLBACK PasswordProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
        auto* raw=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){raw=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(raw));}
        if(!raw)return DefWindowProcW(hwnd,message,wp,lp);const auto self=raw->shared_from_this();
        switch(message){
        case WM_PAINT:case WM_PRINTCLIENT:{PAINTSTRUCT paint{};const auto dc=message==WM_PAINT?BeginPaint(hwnd,&paint):reinterpret_cast<HDC>(wp);RECT rect{};GetClientRect(hwnd,&rect);
            if(self->passwordSurface){self->passwordSurface->Clear(dc,rect);self->passwordSurface->Label(dc,Wide(self->passwordSsid),{20,14,280,24},DT_LEFT,16);self->passwordSurface->Label(dc,self->T(L"输入网络安全密钥",L"Enter the network security key"),{20,45,280,24});}
            if(message==WM_PAINT)EndPaint(hwnd,&paint);return 0;}
        case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:return self->passwordSurface?self->passwordSurface->Color(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)):0;
        case WM_DRAWITEM:if(self->passwordSurface){const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);self->passwordSurface->Button(item,false,CLR_INVALID,item.CtlID==2?todo_ui::Surface::Appearance::Filled:todo_ui::Surface::Appearance::Plain);return TRUE;}return FALSE;
        case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)PostMessageW(hwnd,kAction,LOWORD(wp),0);return 0;
        case kAction:if(wp==2)self->ConnectPassword();else self->ClosePassword();return 0;
        case todo_ui::KeyMessage:if(wp==VK_RETURN){PostMessageW(hwnd,kAction,2,0);return 1;}if(wp==VK_ESCAPE){PostMessageW(hwnd,kAction,3,0);return 1;}
            if(wp==VK_TAB){todo_ui::Fence({self->passwordEdit,GetDlgItem(hwnd,2),GetDlgItem(hwnd,3)},reinterpret_cast<HWND>(lp),GetKeyState(VK_SHIFT)<0);return 1;}return 0;
        case WM_CLOSE:self->ClosePassword();return 0;
        case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE&&self->shown&&self->passwordWindow==hwnd&&reinterpret_cast<HWND>(lp)!=self->window)self->Dismiss();return 0;
        case WM_DESTROY:if(self->passwordWindow==hwnd){self->passwordWindow=nullptr;self->passwordEdit=nullptr;self->passwordSurface.reset();if(self->shown)EnableWindow(self->window,TRUE);}return 0;
        default:return DefWindowProcW(hwnd,message,wp,lp);
        }
    }
    void Password(const std::string& ssid){
        if(passwordWindow)return;passwordSsid=ssid;
        calendar::Preferences appearance;appearance.textColor.rgb=0xffffff;passwordSurface=std::make_unique<todo_ui::Surface>(Dpi(),appearance);
        WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=PasswordProc;wc.lpszClassName=kPasswordClass;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){passwordSurface.reset();return;}
        RECT bounds{};GetWindowRect(window,&bounds);const auto dpi=Dpi();const int width=MulDiv(332,dpi,96),height=MulDiv(186,dpi,96);
        passwordWindow=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_CONTROLPARENT,kPasswordClass,T(L"连接 Wi-Fi",L"Connect to Wi-Fi").c_str(),WS_POPUP|WS_CLIPCHILDREN,
            bounds.left+(bounds.right-bounds.left-width)/2,bounds.top+MulDiv(90,dpi,96),width,height,window,nullptr,instance,this);
        if(!passwordWindow){passwordSurface.reset();return;}
        if(outside)outside->password=passwordWindow;
        window_layer::Register(passwordWindow,window_layer::Kind::ExplicitTool,window);
        ConfigureRoundedWindow(passwordWindow,16.f*dpi/96.f,true);
        passwordEdit=passwordSurface->Add(passwordWindow,passwordWindow,L"EDIT",L"",1,ES_PASSWORD|ES_AUTOHSCROLL);
        SendMessageW(passwordEdit,EM_SETLIMITTEXT,63,0);
        const auto connect=passwordSurface->Add(passwordWindow,passwordWindow,L"BUTTON",T(L"连接",L"Connect").c_str(),2,BS_OWNERDRAW);
        const auto cancel=passwordSurface->Add(passwordWindow,passwordWindow,L"BUTTON",T(L"取消",L"Cancel").c_str(),3,BS_OWNERDRAW);
        todo_ui::Move(passwordEdit,{20,78,292,32},dpi);todo_ui::Move(connect,{224,130,88,34},dpi);todo_ui::Move(cancel,{126,130,88,34},dpi);
        EnableWindow(window,FALSE);passwordSurface->AttachOpaque(passwordWindow,16.f);passwordSurface->Show(passwordEdit);SetForegroundWindow(passwordWindow);SetFocus(passwordEdit);
    }
    void Activate(const HitTarget& hit){
        if(!hit.enabled||!shown)return;
        const auto& key=hit.key;
        if(key=="page")ChangePage(hit.value.get<std::string>());
        else if(key=="back")ChangePage({});
        else if(key=="picker"){const auto next=hit.value.get<std::string>();picker=picker==next?"":next;pickerScroll=0;}
        else if(key=="settings"){
            const auto target=hit.value.get<std::string>();const auto uri=SettingsUri(target);if(!uri)return;
            const bool noWifi=(target=="wifi"||target=="wifi-more")&&fresh&&!Available("wifi")&&state.value("wifiAdapter",true)==false;
            const auto launch=openSettings?openSettings:LaunchSettings;
            bool opened=launch(window,noWifi?L"ms-settings:network-status":uri);
            if(!opened&&(target=="wifi"||target=="wifi-more")&&!noWifi)opened=launch(window,L"ms-settings:network-status");
            if(!opened&&target!="home")opened=launch(window,L"ms-settings:");
            if(opened){Close();return;}settingsFailed=true;
        }
        else if(key=="tray")OpenTray();
        else if(key=="network"){
            const auto& row=hit.value;const auto ssid=row.value("ssid",std::string{});
            if(row.value("secured",false)&&!row.value("known",false))Password(ssid);else Push("connect",{{"ssid",ssid}});
        }else {Push(key,key=="disconnect"?Json{}:hit.value);if(key=="output"||key=="display")picker.clear();}
        Render();
    }
    void Close(){
        if(!window||closing)return;shown=false;trayBusy=false;StopWatchingOutside();ShowWindow(window,SW_HIDE);
        ClosePassword();page.clear();KillTimer(window,kRefreshTimer);KillTimer(window,kContactTimer);KillTimer(window,kMaterialStartTimer);
        if(workerGeneration)worker->Unbind(std::exchange(workerGeneration,0));
        if(trayGeneration)trayWorker->Unbind(std::exchange(trayGeneration,0));
        if(GetCapture()==window)ReleaseCapture();dragging.clear();pressedTarget.reset();
        material.Detach();canvas.reset();surface.Release();lastFrame={};inputMask={};hits.clear();pending.clear();fresh=haveState;
    }
    bool Show(){
        settingsFailed=false;
        if(!window||closing)return false;shown=true;solidFallback=false;en=english.load();page.clear();picker.clear();scroll=0;pressed=focus=-1;fresh=haveState;
        BindWorker();canvas=std::make_unique<WidgetCanvas>();opacity=1.f;Place();
        if(!Render()){Close();return false;}
        const auto target=window;ShowWindow(target,SW_SHOW);if(closing||!IsWindow(target))return false;SetForegroundWindow(target);
        if(!shown)return true;WatchOutside();ReadState();SetTimer(target,kRefreshTimer,2000,nullptr);
        if(UseGlass()){SetTimer(target,kContactTimer,33,nullptr);if(!SetTimer(target,kMaterialStartTimer,1200,nullptr))Fallback();else CheckMaterial();}
        return true;
    }
    void Destroy(){
        auto keep=shared_from_this();if(closing)return;Close();closing=true;
        const auto old=window;window=nullptr;material.Detach();
        if(old){SetWindowLongPtrW(old,GWLP_USERDATA,0);DestroyWindow(old);}
    }
    static LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
        auto* raw=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){raw=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);raw->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(raw));}
        if(!raw)return DefWindowProcW(hwnd,message,wp,lp);const auto self=raw->shared_from_this();
        const auto point=[&]{return POINT{MulDiv(GET_X_LPARAM(lp),96,static_cast<int>(self->Dpi())),MulDiv(GET_Y_LPARAM(lp),96,static_cast<int>(self->Dpi()))};};
        switch(message){
        case WM_PAINT:{PAINTSTRUCT paint{};BeginPaint(hwnd,&paint);EndPaint(hwnd,&paint);self->Render();return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_PRINTCLIENT:if(!self->lastFrame.pixels.empty()){const auto& frame=self->lastFrame;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=static_cast<LONG>(frame.width);info.bmiHeader.biHeight=-static_cast<LONG>(frame.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;SetDIBitsToDevice(reinterpret_cast<HDC>(wp),0,0,frame.width,frame.height,0,0,0,frame.height,frame.pixels.data(),&info,DIB_RGB_COLORS);}return 0;
        case WM_LBUTTONDOWN:{const auto p=point();self->pressed=self->HitAt(static_cast<float>(p.x),static_cast<float>(p.y));self->focus=self->pressed;
            self->pressedTarget.reset();if(self->pressed>=0){const auto hit=self->hits[static_cast<std::size_t>(self->pressed)];self->pressedTarget=hit;if(hit.enabled&&hit.key.rfind("slider:",0)==0){self->dragging=hit.key.substr(7);SetCapture(hwnd);self->SetSlider(self->dragging,static_cast<float>(p.x));}}return 0;}
        case WM_MOUSEMOVE:if(!self->dragging.empty()){self->SetSlider(self->dragging,static_cast<float>(point().x));}return 0;
        case WM_LBUTTONUP:{if(!self->dragging.empty()){self->SetSlider(self->dragging,static_cast<float>(point().x));self->dragging.clear();ReleaseCapture();self->pressed=-1;return 0;}
            const auto p=point();const auto hit=self->HitAt(static_cast<float>(p.x),static_cast<float>(p.y));const auto previous=std::exchange(self->pressed,-1);
            if(hit>=0&&hit==previous&&self->pressedTarget){const auto action=self->hits[static_cast<std::size_t>(hit)];if(action.key==self->pressedTarget->key&&action.value==self->pressedTarget->value)self->Activate(action);}self->pressedTarget.reset();return 0;}
        case WM_CAPTURECHANGED:self->dragging.clear();self->pressed=-1;self->pressedTarget.reset();return 0;
        case WM_MOUSEWHEEL:if(!self->picker.empty()){self->pickerScroll=std::max(0,self->pickerScroll-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA);self->Render();}
            else if(!self->page.empty()){self->scroll=std::max(0,self->scroll-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA);self->focus=-1;self->Render();}return 0;
        case WM_KEYDOWN:
            if(wp==VK_ESCAPE){if(!self->picker.empty()){self->picker.clear();self->Render();}else if(!self->page.empty()){self->ChangePage({});self->Render();}else self->Close();}
            else if((wp==VK_TAB||(!self->page.empty()&&(wp==VK_UP||wp==VK_DOWN)))&&!self->hits.empty()){
                const int count=static_cast<int>(self->hits.size());const bool previous=wp==VK_UP||(wp==VK_TAB&&GetKeyState(VK_SHIFT)<0);
                for(int i=0;i<count;++i){self->focus=self->focus<0?(previous?count-1:0):(self->focus+(previous?count-1:1))%count;
                    if(self->hits[static_cast<std::size_t>(self->focus)].enabled)break;}self->Render();}
            else if((wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN||wp==VK_HOME||wp==VK_END)&&self->focus>=0&&static_cast<std::size_t>(self->focus)<self->hits.size()){
                const auto hit=self->hits[static_cast<std::size_t>(self->focus)];if(hit.enabled&&hit.key.rfind("slider:",0)==0){const auto key=hit.key.substr(7);const auto info=self->Group(key=="volume"?"audio":"brightness");
                    const int current=self->Value(key,info.value(key=="volume"?"volume":"level",0)).get<int>();
                    self->Push(key,wp==VK_HOME?0:wp==VK_END?100:std::clamp(current+((wp==VK_RIGHT||wp==VK_UP)?1:-1),0,100));self->Render();}}
            else if((wp==VK_RETURN||wp==VK_SPACE)&&self->focus>=0&&static_cast<std::size_t>(self->focus)<self->hits.size()){const auto action=self->hits[static_cast<std::size_t>(self->focus)];self->Activate(action);}return 0;
        case WM_ACTIVATE:
            // Disabling the active parent before showing its password popup
            // can send WA_INACTIVE with no destination. That popup handles its
            // own focus loss and remains protected by the outside-click watch.
            if(LOWORD(wp)==WA_INACTIVE&&self->shown){const auto to=reinterpret_cast<HWND>(lp);if(!(self->passwordWindow&&(!to||to==self->passwordWindow)))self->Dismiss();}return 0;
        case WM_ACTIVATEAPP:if(!wp&&self->shown)self->Dismiss();return 0;
        case kDismiss:if(self->shown&&static_cast<cc::PanelWorker::Generation>(wp)==self->workerGeneration)self->Dismiss();return 0;
        case WM_TIMER:
            if(wp==kRefreshTimer&&self->shown)self->ReadState();
            else if(wp==kContactTimer&&self->shown){POINT cursor{};GetCursorPos(&cursor);if(cursor.x!=self->cursor.x||cursor.y!=self->cursor.y){self->cursor=cursor;if(!self->UpdateMaterial())self->Fallback();}}
            else if(wp==kMaterialStartTimer&&self->shown)self->CheckMaterial(true);return 0;
        case LumeMaterialStatusChangedMessage:self->CheckMaterial();return 0;
        case kTrayState:if(self->trayGeneration&&static_cast<cc::PanelWorker::Generation>(wp)==self->trayGeneration){
            self->TrayCompleted(Json::parse(self->trayWorker->Take(self->trayGeneration),nullptr,false));if(self->shown)self->Render();}return 0;
        case kState:if(self->workerGeneration&&static_cast<cc::PanelWorker::Generation>(wp)==self->workerGeneration){const auto value=Json::parse(self->worker->Take(self->workerGeneration),nullptr,false);if(value.is_object()){self->state=value;self->fresh=self->haveState=true;
            self->TrayCompleted(value);
            if(self->shown)self->Render();}if(!self->shown)self->worker->Unbind(std::exchange(self->workerGeneration,0));}return 0;
        case WM_DPICHANGED:self->Place();self->Render();return 0;
        case WM_CLOSE:self->Close();return 0;
        case WM_DESTROY:if(self->window==hwnd){self->Close();self->window=nullptr;self->closing=true;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);}return 0;
        default:return DefWindowProcW(hwnd,message,wp,lp);
        }
    }
};

ControlCenterWindow::ControlCenterWindow()=default;
ControlCenterWindow::ControlCenterWindow(std::shared_ptr<cc::PanelWorker> worker,std::function<bool(HWND,const wchar_t*)> openSettings):worker_(std::move(worker)),openSettings_(std::move(openSettings)){}
ControlCenterWindow::~ControlCenterWindow(){if(impl_)impl_->Destroy();}
void ControlCenterWindow::SetEnglish(bool value){english=value;}
bool ControlCenterWindow::Create(HINSTANCE instance,HWND owner,RECT anchor,Material look,bool show,dock::Edge edge){
    if(impl_){impl_->Destroy();impl_.reset();}
    auto state=std::make_shared<Impl>();state->instance=instance;state->owner=owner;state->anchor=anchor;state->edge=edge;state->look=look;state->worker=worker_?worker_:SharedWorker();
    if(!worker_){
        static const auto tray=std::make_shared<cc::PanelWorker>([](auto,const cc::PanelWorker::Job& job){
            const auto& value=job.value;
            return Json{{"trayResult",cc::OpenTrayOverflow({value.value("x",0),value.value("y",0)})},{"trayRequest",value.value("request",0u)}}.dump();
        });
        state->trayWorker=tray;
    }
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=Impl::Proc;wc.lpszClassName=kClass;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    state->openSettings=openSettings_;
    if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    const auto window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED,kClass,english?L"Lume control center":L"Lume 控制中心",WS_POPUP,0,0,1,1,owner,nullptr,instance,state.get());
    if(!window)return false;impl_=state;window_layer::Register(window,window_layer::Kind::ExplicitTool,owner);
    ConfigureRoundedWindow(window,kCornerRadius*state->Dpi()/96.f,true);
    return !show||state->Show();
}
bool ControlCenterWindow::Prewarm(HINSTANCE instance,HWND owner,Material look){
    if(!IsWindow(owner))return false;
    if(!impl_||!impl_->window||impl_->owner!=owner||impl_->look!=look)if(!Create(instance,owner,{},look,false))return false;
    return impl_->Prewarm();
}
bool ControlCenterWindow::Toggle(HINSTANCE instance,HWND owner,RECT anchor,Material look,dock::Edge edge){
    if(IsOpen()){Close();return true;}
    if(GetTickCount64()-lastDismiss.load()<350)return true;
    if(impl_&&impl_->window&&impl_->owner==owner&&impl_->look==look){impl_->anchor=anchor;impl_->edge=edge;return impl_->Show();}
    return Create(instance,owner,anchor,look,true,edge);
}
void ControlCenterWindow::Close(){if(impl_)impl_->Close();}
bool ControlCenterWindow::IsOpen()const noexcept{return impl_&&impl_->window&&impl_->shown;}
HWND ControlCenterWindow::Handle()const noexcept{return impl_?impl_->window:nullptr;}
} // namespace lume::platform::windows
