#include "apps/shell/weather_session.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/weather/weather_backend.h"
#include "platform/windows/weather/weather_view.h"
#include "platform/windows/weather/weather_tooltip.h"
#include "platform/windows/widget_ui/native_widget_window.h"
#include "vendor/nlohmann/json.hpp"
#include <shellapi.h>
#include <optional>
#include <string>
#include <atomic>
#include <filesystem>
#include <utility>

namespace lume::shell {
using namespace platform::windows;
namespace {
WeatherBackendFactory DefaultBackend(const std::string& path,const weather::Clock& clock){
    std::filesystem::path directory;
    if(!path.empty())directory=std::filesystem::u8path(path).parent_path();
    return [directory,clock]{
        WeatherBackendOptions options;options.clock=clock;
        if(!directory.empty())options.cache=LocalWeatherCache(directory);
        return CreateWeatherBackend(std::move(options));
    };
}
// Whether Windows lets desktop apps use location (the settings the page links
// to): device-wide switch, the user's switch and "let desktop apps access".
const char* SystemLocationAccess(){
    const auto read=[](HKEY root,const wchar_t* path)->std::optional<std::wstring>{
        wchar_t value[16]{};DWORD size=sizeof(value);
        if(RegGetValueW(root,path,L"Value",RRF_RT_REG_SZ,nullptr,value,&size)!=ERROR_SUCCESS)return {};
        return std::wstring(value);
    };
    constexpr wchar_t store[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location";
    constexpr wchar_t desktop[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location\\NonPackaged";
    const auto device=read(HKEY_LOCAL_MACHINE,store),user=read(HKEY_CURRENT_USER,store),apps=read(HKEY_CURRENT_USER,desktop);
    if((device&&*device==L"Deny")||(user&&*user==L"Deny")||(apps&&*apps==L"Deny"))return "off";
    if(!device&&!user&&!apps)return "unknown";
    return "on";
}
const char* DataError(weather::ProviderError error){
    switch(error){
        case weather::ProviderError::None:return "";
        case weather::ProviderError::InvalidLocation:return "location";
        case weather::ProviderError::Unavailable:return "unavailable";
        case weather::ProviderError::RetryDelayed:return "retry-delayed";
        case weather::ProviderError::Cancelled:return "cancelled";
    }
    return "unavailable";
}
}
struct WeatherSession::Impl {
    Impl(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,
         settings::IFileStore* files,WeatherBackendFactory factory,weather::Clock clock):
        instance(instance),path(std::move(path)),notify(std::move(notify)),openSettings(std::move(settings)),
        store(files?*files:localFiles),loaded(store.Load(this->path)),prefs(loaded.value),
        runtimeCity(prefs.city),
        window(instance,L"Lume 天气",weather::WindowLimits,WidgetWindowPorts{
            [this](WidgetCanvas& canvas,float w,float h){DrawWeather(canvas,w,h,reading,runtimeCity);tooltip.Resize(w,h);},
            [this](WidgetAction action,const auto& bounds){Native(action,bounds);},
            [this]{Updated();},
            [this]{if(!stopped&&!applying&&this->notify)this->notify(State(),false);},
            [this](HWND,UINT message,WPARAM w,LPARAM)->std::optional<LRESULT>{
                if(!stopped&&message==WM_KEYDOWN&&w==VK_F5){Refresh();return 0;}
                return {};
            },
            {},
            {},
            true
        }),
        worker(factory?std::move(factory):DefaultBackend(this->path,clock?clock:weather::Clock{WeatherNow}),
            [this]{
                const auto hwnd=target.load();
                if(hwnd&&!pending.exchange(true)&&!PostMessageW(hwnd,NativeWidgetWindow::UpdateMessage,0,0))pending=false;
            },clock?std::move(clock):weather::Clock{WeatherNow}){}
    ~Impl(){Shutdown();}
    std::wstring State(bool applied=false,bool saved=false)const{
        const auto material=window.MaterialState();
        const char* status=material==WidgetMaterialState::Ready?"native-ready":material==WidgetMaterialState::Starting?"native-starting":
            material==WidgetMaterialState::Detached?"inactive":"transparent-fallback";
        const char* load=loaded.state==widget::LoadState::Loaded?"loaded":loaded.state==widget::LoadState::Recovered?"recovered":
            loaded.state==widget::LoadState::Failed?"failed":"fresh";
        nlohmann::json value{
            {"version",1},{"type","weather-state"},{"enabled",prefs.window.enabled},{"running",Running()},
            {"locked",prefs.window.locked},{"material",prefs.window.frosted?"frosted-glass":"liquid-glass"},
            {"materialStatus",status},{"loadStatus",load},{"error",error},{"city",prefs.city},
            {"width",prefs.window.width},{"height",prefs.window.height},{"result",applied?"applied":"snapshot"},{"saved",saved},
            {"loading",reading.loading},{"weatherError",DataError(reading.error)},{"cacheWriteFailed",reading.cacheWriteFailed},
            {"hasWeather",reading.snapshot.has_value()},{"systemLocation",SystemLocationAccess()}
        };
        if(reading.snapshot){
            const auto& data=*reading.snapshot;
            value["source"]=data.source==weather::Source::Qweather?"qweather":"met-norway";
            value["fetchedAt"]=data.updatedAt;value["sourceUpdatedAt"]=data.sourceUpdatedAt;
            value["validAt"]=data.validAt;value["remainingDay"]=data.remainingDay;
        }
        return WeatherText(value.dump());
    }
    bool Running()const{return window.Handle()!=nullptr&&worker.Running();}
    static LRESULT CALLBACK Dispatch(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){
            self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(self&&message==NativeWidgetWindow::UpdateMessage){self->Updated();return 0;}
        return DefWindowProcW(hwnd,message,w,l);
    }
    bool EnsureReceiver(){
        if(receiver)return true;
        WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.hInstance=instance;cls.lpfnWndProc=Dispatch;
        cls.lpszClassName=L"LumeWeatherDataReceiver";
        if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        receiver=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,this);
        return receiver!=nullptr;
    }
    void StopWidget(){tooltip.Stop();window.Stop();}
    void StopRuntime(){
        target=nullptr;worker.Stop();pending=false;
        const auto hwnd=receiver;MSG message{};
        if(hwnd)while(PeekMessageW(&message,hwnd,NativeWidgetWindow::UpdateMessage,NativeWidgetWindow::UpdateMessage,PM_REMOVE)){}
        reading={};StopWidget();
        if(receiver){DestroyWindow(receiver);receiver=nullptr;}
    }
    bool Runtime(const weather::Preferences& value){
        if(!value.window.enabled&&!sharedConsumer){StopRuntime();runtimeCity=value.city;return window.Configure(value.window);}
        // Clear old-city data before Configure can paint the new window state.
        if(runtimeCity!=value.city||!worker.Running()){
            reading={};reading.loading=true;runtimeCity=value.city;
        }
        if(value.window.enabled){
            if(!window.Configure(value.window)||!window.Start())return false;
            // The hover tooltip is optional, as on the calendar; a refused
            // tooltip window must not turn the whole widget off.
            if(tooltip.Attach(window.Handle()))tooltip.Update(reading);
        }else{
            StopWidget();if(!window.Configure(value.window))return false;
        }
        if(!EnsureReceiver()){StopRuntime();return false;}
        target=receiver;
        if(!worker.Start(value.city)){StopRuntime();return false;}
        return true;
    }
    bool SetSharedConsumer(bool active){
        if(stopped)return false;
        if(sharedConsumer==active)return true;
        const bool previous=sharedConsumer;sharedConsumer=active;
        if(!Runtime(prefs)){sharedConsumer=previous;Runtime(prefs);return false;}
        if(error=="runtime")error.clear();  // a failed startup has recovered
        if(notify)notify(State(),false);
        return true;
    }
    bool Restore(){
        if(stopped)return false;
        applying=true;const bool ok=Runtime(prefs);applying=false;
        if(!ok){const bool layer=window.PlacementFailed();StopRuntime();error=layer?"layer":"runtime";}
        return ok;
    }
    bool Apply(weather::Preferences next){
        if(stopped||applying)return false;
        applying=true;const auto previous=prefs;
        if(!Runtime(next)){
            const bool layer=window.PlacementFailed();
            const bool rollback=Runtime(previous);error=layer?"layer":rollback?"runtime":"rollback";applying=false;return false;
        }
        if(next.window.enabled)next.window=widget::Clamped(window.Bounds(),weather::WindowLimits);
        if(!store.Save(path,next)){
            const bool rollback=Runtime(previous);error=rollback?"save":"rollback";applying=false;return false;
        }
        prefs=std::move(next);loaded.state=widget::LoadState::Loaded;error.clear();applying=false;return true;
    }
    void Refresh(){
        if(stopped||applying||(!window.Handle()&&!sharedConsumer))return;
        // Also permits recovery if backend construction failed asynchronously.
        if(!worker.Running()&&!worker.Start(runtimeCity))return;
        worker.Refresh();reading=worker.Latest();tooltip.Update(reading);window.Paint();
        if(notify)notify(State(),false);
    }
    std::optional<std::wstring> HandlePage(ui::PageCommand command){
        if(stopped||applying)return {};
        if(command==ui::PageCommand::Ready)return State();
        if(command==ui::PageCommand::WeatherRefresh){Refresh();return State();}
        if(command==ui::PageCommand::WeatherLocationSettings){
            // Opens Windows' location privacy page; the user decides there.
            ShellExecuteW(nullptr,L"open",L"ms-settings:privacy-location",nullptr,nullptr,SW_SHOWNORMAL);
            return State();
        }
        auto next=prefs;
        using ui::PageCommand;
        switch(command){
            case PageCommand::WeatherEnable:next.window.enabled=true;break;
            case PageCommand::WeatherDisable:next.window.enabled=false;break;
            case PageCommand::WeatherLiquidGlass:next.window.frosted=false;break;
            case PageCommand::WeatherFrostedGlass:next.window.frosted=true;break;
            case PageCommand::WeatherReset:next=weather::Preferences{};next.window.enabled=prefs.window.enabled;break;
            case PageCommand::WeatherCityAuto:next.city="auto";break;
            case PageCommand::WeatherCityBeijing:next.city="beijing";break;
            case PageCommand::WeatherCityShanghai:next.city="shanghai";break;
            case PageCommand::WeatherCityGuangzhou:next.city="guangzhou";break;
            case PageCommand::WeatherCityShenzhen:next.city="shenzhen";break;
            case PageCommand::WeatherCityHangzhou:next.city="hangzhou";break;
            case PageCommand::WeatherCityChengdu:next.city="chengdu";break;
            case PageCommand::WeatherCityWuhan:next.city="wuhan";break;
            case PageCommand::WeatherCityNanjing:next.city="nanjing";break;
            default:return {};
        }
        const bool saved=Apply(std::move(next));return State(true,saved);
    }
    void Native(WidgetAction action,const widget::Preferences& bounds){
        if(stopped||applying)return;
        if(action==WidgetAction::Settings){if(openSettings)openSettings();return;}
        if(action==WidgetAction::Refresh){Refresh();return;}
        auto next=prefs;
        switch(action){
            case WidgetAction::Close:next.window.enabled=false;break;
            case WidgetAction::Lock:next.window.locked=!next.window.locked;break;
            case WidgetAction::Liquid:next.window.frosted=false;break;
            case WidgetAction::Frosted:next.window.frosted=true;break;
            case WidgetAction::Reset:next=weather::Preferences{};next.window.enabled=prefs.window.enabled;break;
            case WidgetAction::Bounds:next.window=bounds;break;
            case WidgetAction::Settings:case WidgetAction::Refresh:return;
        }
        const bool ok=Apply(std::move(next));if(notify)notify(State(),!ok);
    }
    void Updated(){
        pending=false;if(stopped||!receiver)return;
        reading=worker.Latest();
        if(reading.snapshot&&reading.snapshot->cityId!=runtimeCity)reading.snapshot.reset();
        tooltip.Update(reading);window.Paint();if(!applying&&notify)notify(State(),false);
    }
    void Shutdown(){if(stopped)return;stopped=true;StopRuntime();}
    HINSTANCE instance;HWND receiver=nullptr;
    std::string path;Notify notify;std::function<void()> openSettings;
    LocalSettingsFileStore localFiles;weather::PreferencesStore store;
    weather::LoadedPreferences loaded;weather::Preferences prefs;
    std::string runtimeCity,error;WeatherReading reading;
    bool stopped=false,applying=false,sharedConsumer=false;
    std::atomic<HWND> target{nullptr};std::atomic<bool> pending{false};
    WeatherTooltip tooltip;NativeWidgetWindow window;WeatherWorker worker;
};
WeatherSession::WeatherSession(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,
    settings::IFileStore* files,WeatherBackendFactory factory,weather::Clock clock):
    impl_(std::make_unique<Impl>(instance,std::move(path),std::move(notify),std::move(settings),files,std::move(factory),std::move(clock))){}
WeatherSession::~WeatherSession()=default;
bool WeatherSession::Restore(){return impl_->Restore();}
bool WeatherSession::SetSharedConsumer(bool active){return impl_->SetSharedConsumer(active);}
std::optional<std::wstring> WeatherSession::HandlePage(ui::PageCommand command){return impl_->HandlePage(command);}
std::wstring WeatherSession::State()const{return impl_->State();}
void WeatherSession::Shutdown(){impl_->Shutdown();}
bool WeatherSession::Running()const{return impl_->Running();}
HWND WeatherSession::Handle()const{return impl_->window.Handle();}
const weather::Preferences& WeatherSession::Snapshot()const{return impl_->prefs;}
WeatherReading WeatherSession::Reading()const{return impl_->reading;}
}
