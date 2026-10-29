#include "apps/shell/performance_session.h"
#include "apps/shell/widget_session_common.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/performance/performance_monitor.h"
#include "platform/windows/performance/performance_view.h"
#include "platform/windows/widget_ui/native_widget_window.h"
#include <atomic>
#include <utility>
namespace lume::shell {
using namespace platform::windows;
namespace {constexpr widget::Limits Limits{340,216,320,204,720,720};}
struct PerformanceSession::Impl{
    Impl(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,settings::IFileStore* files):
        path(std::move(path)),notify(std::move(notify)),openSettings(std::move(settings)),
        store(files?*files:localFiles,Limits),loaded(store.Load(this->path)),prefs(loaded.value),
        window(instance,L"Lume 性能",Limits,WidgetWindowPorts{
            [this](WidgetCanvas& c,float w,float h){DrawPerformance(c,w,h,sample,sampleFailed);},
            [this](WidgetAction a,const auto& bounds){Native(a,bounds);},
            [this]{Updated();},
            [this]{if(!stopped&&!applying&&this->notify)this->notify(State(),false);}
        }),monitor([this]{
            const auto hwnd=target.load();
            if(hwnd&&!pending.exchange(true)&&!PostMessageW(hwnd,NativeWidgetWindow::UpdateMessage,0,0))pending=false;
        }){}
    ~Impl(){Shutdown();}
    std::wstring State(bool applied=false,bool saved=false)const{
        return widget_session::StateJson({"performance",prefs.enabled,Running(),prefs.locked,prefs.frosted?"frosted-glass":"liquid-glass",window.MaterialState(),loaded.state,error,prefs.width,prefs.height,applied,saved});
    }
    bool Running()const{return window.Handle()!=nullptr&&monitor.Running();}
    void StopRuntime(){
        target=nullptr;monitor.Stop();pending=false;sample.reset();sampleFailed=false;
        const auto hwnd=window.Handle();MSG message{};
        if(hwnd)while(PeekMessageW(&message,hwnd,NativeWidgetWindow::UpdateMessage,NativeWidgetWindow::UpdateMessage,PM_REMOVE)){}
        window.Stop();
    }
    bool Runtime(const widget::Preferences& value){
        if(!value.enabled){StopRuntime();return window.Configure(value);}
        if(!window.Configure(value)||!window.Start())return false;
        target=window.Handle();
        if(!monitor.Start()){StopRuntime();return false;}
        return true;
    }
    bool Restore(){
        if(stopped)return false;
        applying=true;const bool ok=Runtime(prefs);applying=false;
        if(!ok){const bool layer=window.PlacementFailed();StopRuntime();error=layer?L"layer":L"runtime";}
        return ok;
    }
    bool Apply(widget::Preferences next){
        if(stopped||applying)return false;
        applying=true;const auto previous=prefs;
        if(!Runtime(next)){const bool layer=window.PlacementFailed();Runtime(previous);error=layer?L"layer":L"runtime";applying=false;return false;}
        if(next.enabled){const auto bounds=widget::Clamped(window.Bounds(),Limits);next.x=bounds.x;next.y=bounds.y;next.placed=bounds.placed;next.width=bounds.width;next.height=bounds.height;}
        if(!store.Save(path,next)){
            const bool rollback=Runtime(previous);error=rollback?L"save":L"rollback";applying=false;return false;
        }
        prefs=next;loaded.state=widget::LoadState::Loaded;error.clear();applying=false;return true;
    }
    std::optional<std::wstring> HandlePage(ui::PageCommand command){
        if(stopped||applying)return {};
        if(command==ui::PageCommand::Ready)return State();
        auto next=prefs;
        switch(command){
            case ui::PageCommand::PerformanceEnable:next.enabled=true;break;
            case ui::PageCommand::PerformanceDisable:next.enabled=false;break;
            case ui::PageCommand::PerformanceLiquidGlass:next.frosted=false;break;
            case ui::PageCommand::PerformanceFrostedGlass:next.frosted=true;break;
            case ui::PageCommand::PerformanceReset:next=widget::Defaults(Limits);next.enabled=prefs.enabled;break;
            default:return {};
        }
        const bool saved=Apply(next);return State(true,saved);
    }
    void Native(WidgetAction action,const widget::Preferences& bounds){
        if(stopped||applying)return;
        if(action==WidgetAction::Settings){if(openSettings)openSettings();return;}
        const auto next=widget_session::ActionPreferences(action,prefs,bounds,Limits,false);
        if(!next)return;
        const bool ok=Apply(*next);if(notify)notify(State(),!ok);
    }
    void Updated(){
        pending=false;if(stopped||!window.Handle())return;
        sample=monitor.Latest();const bool failed=!sample;
        const bool changed=failed!=sampleFailed;sampleFailed=failed;
        window.Paint();if(changed&&notify)notify(State(),false);
    }
    void Shutdown(){if(stopped)return;stopped=true;StopRuntime();}
    std::string path;Notify notify;std::function<void()> openSettings;
    LocalSettingsFileStore localFiles;widget::PreferencesStore store;widget::LoadedPreferences loaded;widget::Preferences prefs;
    std::optional<performance::Snapshot> sample;bool sampleFailed=false,stopped=false,applying=false;std::wstring error;
    std::atomic<HWND> target{nullptr};std::atomic<bool> pending{false};
    NativeWidgetWindow window;PerformanceMonitor monitor;
};
PerformanceSession::PerformanceSession(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,settings::IFileStore* files):
    impl_(std::make_unique<Impl>(instance,std::move(path),std::move(notify),std::move(settings),files)){}
PerformanceSession::~PerformanceSession()=default;
bool PerformanceSession::Restore(){return impl_->Restore();}
std::optional<std::wstring> PerformanceSession::HandlePage(ui::PageCommand command){return impl_->HandlePage(command);}
std::wstring PerformanceSession::State()const{return impl_->State();}
void PerformanceSession::Shutdown(){impl_->Shutdown();}
bool PerformanceSession::Running()const{return impl_->Running();}
HWND PerformanceSession::Handle()const{return impl_->window.Handle();}
const widget::Preferences& PerformanceSession::Snapshot()const{return impl_->prefs;}
std::optional<performance::Snapshot> PerformanceSession::Sample()const{return impl_->sample;}
}
