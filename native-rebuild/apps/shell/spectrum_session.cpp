#include "apps/shell/spectrum_session.h"
#include "apps/shell/widget_session_common.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/spectrum/spectrum_capture.h"
#include "platform/windows/spectrum/spectrum_view.h"
#include "platform/windows/widget_ui/native_widget_window.h"
#include "platform/windows/widget_ui/LumeWidgetColorPicker.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/widget_ui/resource.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <utility>
namespace lume::shell {
using namespace platform::windows;
using spectrum::WindowLimits;
struct SpectrumSession::Impl{
    Impl(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,settings::IFileStore* files):
        path(std::move(path)),notify(std::move(notify)),openSettings(std::move(settings)),
        store(files?*files:localFiles),loaded(store.Load(this->path)),prefs(loaded.value),
        window(instance,L"Lume 音频频谱",WindowLimits,Ports()),capture([this]{
            const auto hwnd=target.load();
            if(hwnd&&!pending.exchange(true)&&!PostMessageW(hwnd,NativeWidgetWindow::UpdateMessage,0,0))pending=false;
        }){}
    ~Impl(){Shutdown();}
    WidgetWindowPorts Ports(){
        WidgetWindowPorts ports{
            [this](WidgetCanvas& c,float w,float h){Draw(c,w,h);},
            [this](WidgetAction a,const auto& bounds){Native(a,bounds);},
            [this]{Updated();},
            [this]{if(!stopped&&!applying&&this->notify)this->notify(State(),false);}
        };
        // Bars float straight over the wallpaper, as in the reference look.
        // No hover pane either: nothing but the bars ever shows. Its large clear
        // window sits under the Dock and other widgets so it never takes their clicks.
        ports.glass=false;ports.hoverPane=false;ports.bottom=true;ports.showCloseButton=false;
        // The bars move every frame; the mouse is taken only by a fixed strip
        // along their baseline, so the clickable area never jumps with the
        // music and the bars above it let clicks through to the Dock or icons.
        ports.hitShape=[this](float w,float h){
            return std::vector<WidgetRect>{SpectrumGrip(prefs.style,w,h,prefs.Current())};
        };
        ports.menu=[this](HWND owner,POINT point,const widget::Preferences& bounds){
            Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
            if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return 0u;
            // The size submenu lists 50..300% in steps of ten.
            const int percent=(spectrum::ScalePercent(bounds)+5)/10*10;
            const auto result=LumeWidgetMenu::ShowSpectrum(owner,point,static_cast<int>(prefs.style),percent,prefs.window.locked);
            Gdiplus::GdiplusShutdown(token);return result;
        };
        ports.command=[this](UINT command,const widget::Preferences& bounds){MenuCommand(command,bounds);};
        return ports;
    }
    std::wstring State(bool applied=false,bool saved=false)const{
        const auto& w=prefs.window;
        auto json=widget_session::StateJson({"spectrum",w.enabled,Running(),w.locked,"none",window.MaterialState(),
            loaded.state,error,w.width,w.height,applied,saved});
        json.pop_back();
        const auto bar=[](const spectrum::BarStyle& b){
            return L"{\"barWidth\":"+std::to_wstring(b.barWidth)+L",\"barGap\":"+std::to_wstring(b.barGap)+L"}";
        };
        const std::string style=spectrum::StyleName(prefs.style);
        return json+L",\"style\":\""+std::wstring(style.begin(),style.end())+
            L"\",\"scale\":"+std::to_wstring(spectrum::ScalePercent(w))+
            L",\"pulse\":"+bar(prefs.pulse)+L",\"blocks\":"+bar(prefs.blocks)+L",\"waves\":"+bar(prefs.waves)+L"}";
    }
    bool Running()const{return window.Handle()!=nullptr&&capture.Running();}
    void Draw(WidgetCanvas& canvas,float w,float h){
        const auto& bar=prefs.Current();
        const auto count=spectrum::BarCount(w,bar.barWidth,bar.barGap);
        if(shown.size()!=count)shown.assign(count,0.f);
        if(prefs.style==spectrum::Style::Pulse)DrawSpectrumPulse(canvas,w,h,shown,bar);
        else if(prefs.style==spectrum::Style::Waves)DrawSpectrumWaves(canvas,w,h,shown,bar);
        else DrawSpectrumBlocks(canvas,w,h,shown,bar);
    }
    void StopRuntime(){
        target=nullptr;capture.Stop();pending=false;shown.clear();
        const auto hwnd=window.Handle();MSG message{};
        if(hwnd)while(PeekMessageW(&message,hwnd,NativeWidgetWindow::UpdateMessage,NativeWidgetWindow::UpdateMessage,PM_REMOVE)){}
        window.Stop();
    }
    bool Runtime(const spectrum::Preferences& value){
        if(!value.window.enabled){StopRuntime();return window.Configure(value.window);}
        if(!window.Configure(value.window)||!window.Start())return false;
        target=window.Handle();
        if(!capture.Start()){StopRuntime();return false;}
        return true;
    }
    bool Restore(){
        if(stopped)return false;
        applying=true;const bool ok=Runtime(prefs);applying=false;
        if(!ok){const bool layer=window.PlacementFailed();StopRuntime();error=layer?L"layer":L"runtime";}
        return ok;
    }
    bool Apply(spectrum::Preferences next){
        if(stopped||applying)return false;
        applying=true;const auto previous=prefs;
        // Draw reads prefs, so the new bar size is in place before the window paints.
        prefs=next;
        if(!Runtime(next)){
            prefs=previous;const bool layer=window.PlacementFailed();Runtime(previous);
            error=layer?L"layer":L"runtime";applying=false;return false;
        }
        if(next.window.enabled){
            const auto bounds=widget::Clamped(window.Bounds(),WindowLimits);auto& w=next.window;
            w.x=bounds.x;w.y=bounds.y;w.placed=bounds.placed;w.width=bounds.width;w.height=bounds.height;
        }
        if(!store.Save(path,next)){
            prefs=previous;const bool rollback=Runtime(previous);error=rollback?L"save":L"rollback";applying=false;return false;
        }
        prefs=next;loaded.state=widget::LoadState::Loaded;error.clear();applying=false;
        window.Paint();
        return true;
    }
    std::optional<std::wstring> HandlePage(ui::PageCommand command){
        using ui::PageCommand;
        if(stopped||applying)return {};
        if(command==PageCommand::Ready)return State();
        auto next=prefs;const int value=static_cast<int>(command);
        const auto in=[value](PageCommand first,PageCommand last){
            return value>=static_cast<int>(first)&&value<=static_cast<int>(last);
        };
        const auto offset=[value](PageCommand first){return value-static_cast<int>(first);};
        if(in(PageCommand::SpectrumSizeFirst,PageCommand::SpectrumSizeLast))
            next.window=spectrum::Scaled(prefs.window,spectrum::MinScale+offset(PageCommand::SpectrumSizeFirst));
        else if(in(PageCommand::SpectrumPulseBarWidthFirst,PageCommand::SpectrumPulseBarWidthLast))
            next.pulse.barWidth=spectrum::MinBarWidth+offset(PageCommand::SpectrumPulseBarWidthFirst);
        else if(in(PageCommand::SpectrumPulseBarGapFirst,PageCommand::SpectrumPulseBarGapLast))
            next.pulse.barGap=spectrum::MinBarGap+offset(PageCommand::SpectrumPulseBarGapFirst);
        else if(in(PageCommand::SpectrumBarWidthFirst,PageCommand::SpectrumBarWidthLast))
            next.blocks.barWidth=spectrum::MinBarWidth+value-static_cast<int>(PageCommand::SpectrumBarWidthFirst);
        else if(in(PageCommand::SpectrumBarGapFirst,PageCommand::SpectrumBarGapLast))
            next.blocks.barGap=spectrum::MinBarGap+value-static_cast<int>(PageCommand::SpectrumBarGapFirst);
        else if(in(PageCommand::SpectrumWavesBarWidthFirst,PageCommand::SpectrumWavesBarWidthLast))
            next.waves.barWidth=spectrum::MinBarWidth+offset(PageCommand::SpectrumWavesBarWidthFirst);
        else if(in(PageCommand::SpectrumWavesBarGapFirst,PageCommand::SpectrumWavesBarGapLast))
            next.waves.barGap=spectrum::MinBarGap+offset(PageCommand::SpectrumWavesBarGapFirst);
        else switch(command){
            case PageCommand::SpectrumEnable:next.window.enabled=true;break;
            case PageCommand::SpectrumDisable:next.window.enabled=false;break;
            case PageCommand::SpectrumStylePulse:next.style=spectrum::Style::Pulse;break;
            case PageCommand::SpectrumStyleBlocks:next.style=spectrum::Style::Blocks;break;
            case PageCommand::SpectrumStyleWaves:next.style=spectrum::Style::Waves;break;
            case PageCommand::SpectrumReset:next=spectrum::Preferences{};next.window.enabled=prefs.window.enabled;break;
            default:return {};
        }
        const bool saved=Apply(next);return State(true,saved);
    }
    void Native(WidgetAction action,const widget::Preferences& bounds){
        if(stopped||applying)return;
        if(action==WidgetAction::Settings){if(openSettings)openSettings();return;}
        if(action==WidgetAction::Liquid||action==WidgetAction::Frosted||action==WidgetAction::Solid)return;
        auto next=prefs;
        if(action==WidgetAction::Reset){next=spectrum::Preferences{};next.window.enabled=prefs.window.enabled;}
        else{
            const auto placed=widget_session::ActionPreferences(action,prefs.window,bounds,WindowLimits,false);
            if(!placed)return;
            next.window=*placed;
        }
        const bool ok=Apply(next);if(notify)notify(State(),!ok);
    }
    void MenuCommand(UINT command,const widget::Preferences& bounds){
        if(stopped||applying)return;
        auto next=prefs;
        if(command>=30&&command<=32)next.style=static_cast<spectrum::Style>(command-30);
        else if(command==IDM_LUME_WIDGET_COLOR){
            auto& b=next.blocks;
            LumeWidgetColor color{static_cast<BYTE>((b.color>>16)&255),static_cast<BYTE>((b.color>>8)&255),
                                  static_cast<BYTE>(b.color&255),static_cast<BYTE>(b.alpha)};
            if(!LumeWidgetColorPicker::Show(window.Handle(),color,RGB(255,255,255))||stopped||!window.Handle()||!prefs.window.enabled)return;
            next=prefs;next.blocks.color=(color.red<<16)|(color.green<<8)|color.blue;next.blocks.alpha=color.alpha;
        }
        else if(command>=IDM_LUME_WIDGET_SIZE_FIRST&&command<=IDM_LUME_WIDGET_SIZE_LAST){
            // Keep a position the user just dragged to.
            next.window.x=bounds.x;next.window.y=bounds.y;next.window.placed=bounds.placed;
            next.window=spectrum::Scaled(next.window,50+static_cast<int>(command-IDM_LUME_WIDGET_SIZE_FIRST)*10);
        }
        else return;
        const bool ok=Apply(next);if(notify)notify(State(),!ok);
    }
    void Updated(){
        pending=false;if(stopped||!window.Handle())return;
        const auto now=std::chrono::steady_clock::now();
        const float seconds=last.time_since_epoch().count()?std::chrono::duration<float>(now-last).count():0.f;last=now;
        const auto frame=capture.Latest();
        const auto count=shown.size();
        const auto levels=frame.left.empty()?std::vector<float>(count,0.f):
            spectrum::StereoBands(frame.left,frame.right,frame.sampleRate,count);
        motion.Step(shown,levels,std::min(seconds,.1f));
        window.Paint();
    }
    void Shutdown(){if(stopped)return;stopped=true;StopRuntime();}
    std::string path;Notify notify;std::function<void()> openSettings;
    LocalSettingsFileStore localFiles;spectrum::PreferencesStore store;spectrum::LoadedPreferences loaded;spectrum::Preferences prefs;
    std::vector<float> shown;spectrum::Motion motion;std::chrono::steady_clock::time_point last{};
    bool stopped=false,applying=false;std::wstring error;
    std::atomic<HWND> target{nullptr};std::atomic<bool> pending{false};
    NativeWidgetWindow window;SpectrumCapture capture;
};
SpectrumSession::SpectrumSession(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,settings::IFileStore* files):
    impl_(std::make_unique<Impl>(instance,std::move(path),std::move(notify),std::move(settings),files)){}
SpectrumSession::~SpectrumSession()=default;
bool SpectrumSession::Restore(){return impl_->Restore();}
std::optional<std::wstring> SpectrumSession::HandlePage(ui::PageCommand command){return impl_->HandlePage(command);}
std::wstring SpectrumSession::State()const{return impl_->State();}
void SpectrumSession::Shutdown(){impl_->Shutdown();}
bool SpectrumSession::Running()const{return impl_->Running();}
HWND SpectrumSession::Handle()const{return impl_->window.Handle();}
const spectrum::Preferences& SpectrumSession::Snapshot()const{return impl_->prefs;}
}
