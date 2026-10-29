#include "apps/shell/almanac_session.h"
#include "apps/shell/widget_session_common.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/almanac/almanac_view.h"
#include "platform/windows/widget_ui/native_widget_window.h"
#include <windowsx.h>
#include <algorithm>
#include <limits>
#include <utility>
namespace lume::shell {
using namespace platform::windows;
namespace {
constexpr widget::Limits Limits{320,152,320,152,720,720};
constexpr UINT_PTR DateTimer=0xA11;
SYSTEMTIME LocalNow(){SYSTEMTIME value{};GetLocalTime(&value);return value;}
}
struct AlmanacSession::Impl {
    Impl(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,
         settings::IFileStore* files,Now now):
        path(std::move(path)),notify(std::move(notify)),openSettings(std::move(settings)),
        now(now?std::move(now):Now{LocalNow}),
        store(files?*files:localFiles,Limits),loaded(store.Load(this->path)),prefs(loaded.value),
        window(instance,L"Lume 黄历",Limits,WidgetWindowPorts{
            [this](WidgetCanvas& c,float w,float h){view.Draw(c,w,h);},
            [this](WidgetAction a,const auto& bounds){Native(a,bounds);},
            {},
            [this]{if(!stopped&&!applying&&this->notify)this->notify(State(),false);},
            [this](HWND h,UINT m,WPARAM w,LPARAM l){return Event(h,m,w,l);},
            [this](float w,float h){return view.Panels(w,h);}
        }){}
    ~Impl(){Shutdown();}
    std::wstring State(bool applied=false,bool saved=false)const {
        return widget_session::StateJson({"almanac",prefs.enabled,Running(),prefs.locked,prefs.frosted?"frosted-glass":"liquid-glass",window.MaterialState(),loaded.state,error,prefs.width,prefs.height,applied,saved});
    }
    bool Running()const{return window.Handle()!=nullptr&&timer;}
    // Re-evaluate local civil time at least hourly, including DST changes. No
    // worker or per-second polling; repeated checks leave the view untouched.
    bool CheckDate(){
        const auto time=now();
        const auto revision=view.Revision();
        view.SetDate({time.wYear,time.wMonth,time.wDay});
        const unsigned elapsed=((time.wHour*60u+time.wMinute)*60u+time.wSecond)*1000u+time.wMilliseconds;
        const unsigned delay=std::clamp(86400000u-std::min(elapsed,86400000u),100u,3600000u);
        timer=SetTimer(window.Handle(),DateTimer,delay,nullptr)!=0;
        if(view.Revision()!=revision)window.Paint();
        return timer;
    }
    void StopRuntime(){
        if(const auto hwnd=window.Handle())KillTimer(hwnd,DateTimer);
        timer=false;focused=-1;window.Stop();view=AlmanacView{};
    }
    bool Runtime(const widget::Preferences& value){
        if(!value.enabled){StopRuntime();return window.Configure(value);}
        // Have real data ready for the first paint, before showing the window.
        const auto time=now();view.SetDate({time.wYear,time.wMonth,time.wDay});
        if(!window.Configure(value)||!window.Start())return false;
        if(!CheckDate()){StopRuntime();return false;}
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
        if(!Runtime(next)){
            const bool layer=window.PlacementFailed();
            const bool rollback=Runtime(previous);error=layer?L"layer":rollback?L"runtime":L"rollback";applying=false;return false;
        }
        if(next.enabled){
            const auto bounds=widget::Clamped(window.Bounds(),Limits);next.x=bounds.x;next.y=bounds.y;next.placed=bounds.placed;
            next.width=bounds.width;next.height=bounds.height;
        }
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
            case ui::PageCommand::AlmanacEnable:next.enabled=true;break;
            case ui::PageCommand::AlmanacDisable:next.enabled=false;break;
            case ui::PageCommand::AlmanacLiquidGlass:next.frosted=false;break;
            case ui::PageCommand::AlmanacFrostedGlass:next.frosted=true;break;
            case ui::PageCommand::AlmanacReset:next=widget::Defaults(Limits);next.enabled=prefs.enabled;break;
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
    std::optional<LRESULT> Event(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        if(stopped)return {};
        if(message==WM_TIMER&&w!=DateTimer)return {};
        if(message==WM_TIMECHANGE||message==WM_SETTINGCHANGE||message==WM_POWERBROADCAST||message==WM_TIMER){
            if(timer&&!CheckDate()){
                error=L"runtime";if(notify)notify(State(),false);  // A timer, not the user: never pop up.
            }
            return message==WM_POWERBROADCAST?TRUE:0;
        }
        if(message==WM_KILLFOCUS){focused=-1;view.Focus(-1);window.Paint();return 0;}
        if(message==WM_LBUTTONDOWN||message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL){
            POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            if(message!=WM_LBUTTONDOWN)ScreenToClient(hwnd,&point);
            const auto dpi=GetDpiForWindow(hwnd);const float scale=96.f/(dpi?dpi:96);
            const int row=view.ActivityAt(point.x*scale,point.y*scale);
            if(row<0)return {};
            if(message==WM_LBUTTONDOWN){
                focused=row;view.Focus(row);SetFocus(hwnd);window.Paint();
            }else{
                const float direction=message==WM_MOUSEHWHEEL?1.f:-1.f;
                if(view.Scroll(row,GET_WHEEL_DELTA_WPARAM(w)*direction*40.f/WHEEL_DELTA))window.Paint();
            }
            return 0;
        }
        if(message==WM_KEYDOWN&&focused>=0){
            bool changed=false;
            switch(w){
                case VK_LEFT:case VK_UP:changed=view.Scroll(focused,-40);break;
                case VK_RIGHT:case VK_DOWN:changed=view.Scroll(focused,40);break;
                case VK_HOME:changed=view.Scroll(focused,0,true);break;
                case VK_END:changed=view.Scroll(focused,std::numeric_limits<float>::max(),true);break;
                default:return {};
            }
            if(changed)window.Paint();
            return 0;
        }
        return {};
    }
    void Shutdown(){if(stopped)return;stopped=true;StopRuntime();}
    std::string path;Notify notify;std::function<void()> openSettings;Now now;
    LocalSettingsFileStore localFiles;widget::PreferencesStore store;widget::LoadedPreferences loaded;widget::Preferences prefs;
    AlmanacView view;bool stopped=false,applying=false,timer=false;int focused=-1;std::wstring error;
    NativeWidgetWindow window;
};
AlmanacSession::AlmanacSession(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,settings::IFileStore* files,Now now):
    impl_(std::make_unique<Impl>(instance,std::move(path),std::move(notify),std::move(settings),files,std::move(now))){}
AlmanacSession::~AlmanacSession()=default;
bool AlmanacSession::Restore(){return impl_->Restore();}
std::optional<std::wstring> AlmanacSession::HandlePage(ui::PageCommand command){return impl_->HandlePage(command);}
std::wstring AlmanacSession::State()const{return impl_->State();}
void AlmanacSession::Shutdown(){impl_->Shutdown();}
bool AlmanacSession::Running()const{return impl_->Running();}
HWND AlmanacSession::Handle()const{return impl_->window.Handle();}
const widget::Preferences& AlmanacSession::Snapshot()const{return impl_->prefs;}
calendar::Date AlmanacSession::DisplayedDate()const{return impl_->view.Date();}
unsigned AlmanacSession::DataRevision()const{return impl_->view.Revision();}
}
