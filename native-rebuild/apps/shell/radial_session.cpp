#include "apps/shell/radial_session.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/radial/radial_shortcut.h"
#include "platform/windows/radial/radial_window.h"
#include "platform/windows/screenshot/screenshot_editor.h"
#include "vendor/nlohmann/json.hpp"
#include <utility>

namespace lume::shell {
using namespace platform::windows;
namespace {
bool InputReleased(unsigned trigger){
    if(GetAsyncKeyState(static_cast<int>(trigger))&0x8000)return false;
    for(const int key:{VK_LBUTTON,VK_RBUTTON,VK_MENU,VK_CONTROL,VK_SHIFT,VK_LWIN,VK_RWIN})
        if(GetAsyncKeyState(key)&0x8000)return false;
    return true;
}
bool StartScreenshot(capture::ScreenshotEditor& editor,unsigned trigger){
    return InputReleased(trigger)&&editor.Start();
}
const char* MaterialStatus(WidgetMaterialState state){
    switch(state){case WidgetMaterialState::Ready:return "native-ready";case WidgetMaterialState::Starting:return "native-starting";
        case WidgetMaterialState::TransparentFallback:return "transparent-fallback";default:return "inactive";}
}
}
struct RadialSession::Impl {
    RadialSessionOptions options;Notify notify;std::function<void(bool)> settings;
    LocalSettingsFileStore local;settings::IFileStore& files;radial::Preferences prefs;
    std::string load="fresh",error;bool stopped=false,applying=false,ready=false,capturing=false;
    ULONGLONG screenshotDeadline=0;
    RadialShortcut shortcut;RadialWindow window;capture::ScreenshotEditor screenshot;
    Impl(HINSTANCE instance,RadialSessionOptions o,Notify n,std::function<void(bool)> s,settings::IFileStore* f):
        options(std::move(o)),notify(std::move(n)),settings(std::move(s)),files(f?*f:local),
        window(instance,[this](UINT command){Action(command);},[this]{Changed();}),screenshot(instance){Load();}
    ~Impl(){Shutdown();}
    void Load(){
        if(options.preferencesPath.empty()){load="failed";return;}
        std::string text;
        if(files.Exists(options.preferencesPath)){
            if(files.ReadBounded(options.preferencesPath,4096,text)&&radial::Decode(text,prefs)){load="loaded";return;}
            load="failed";
        }
        if(files.Exists(options.preferencesPath+".bak")){
            if(files.ReadBounded(options.preferencesPath+".bak",4096,text)&&radial::Decode(text,prefs))load="recovered";
            else load="failed";
        }
    }
    bool Save(const radial::Preferences& value){
        const auto& path=options.preferencesPath;
        std::string text;if(path.empty()||!radial::Encode(value,text))return false;
        std::string original;bool valid=false;
        if(files.Exists(path)){
            radial::Preferences old;
            valid=files.ReadBounded(path,4096,original)&&radial::Decode(original,old);
            if(!valid){
                bool isolated=false;
                for(unsigned i=0;i<1024;++i){const auto destination=path+".corrupt"+(i?"."+std::to_string(i):"");
                    if(!files.Exists(destination)){isolated=files.Rename(path,destination);break;}}
                if(!isolated)return false;
            }
        }
        const auto temporary=path+".tmp",backup=path+".bak.tmp";
        const auto failed=[&]{files.Remove(temporary);files.Remove(backup);return false;};
        if(!files.Write(temporary,text))return failed();
        if(valid&&(!files.Write(backup,original)||!files.Rename(backup,path+".bak")))return failed();
        if(!files.Rename(temporary,path))return failed();
        return true;
    }
    bool Running()const{return ready&&(options.verification||shortcut.Running());}
    std::wstring State(bool applied=false,bool saved=false)const{
        const auto text=nlohmann::json{{"version",1},{"type","radial-state"},{"enabled",prefs.enabled},
            {"running",Running()},{"visible",window.Visible()},{"material",prefs.frosted?"frosted-glass":"liquid-glass"},
            {"shortcutCode",prefs.shortcutCode},{"materialStatus",MaterialStatus(window.MaterialState())},
            {"loadStatus",load},{"error",error},{"result",applied?"applied":"snapshot"},{"saved",saved}}.dump();
        return std::wstring(text.begin(),text.end());
    }
    void Changed(){
        if(!stopped&&!applying){shortcut.Visible(window.Visible()?window.Handle():nullptr);if(notify)notify(State(),false);}
    }
    bool Runtime(const radial::Preferences& value){
        if(!value.enabled){ready=false;shortcut.Disable();screenshotDeadline=0;screenshot.Stop();window.Stop();return true;}
        if(!window.Create())return false;
        if(!options.verification&&!shortcut.Start(window.Handle(),value.shortcutCode,capturing))return false;
        shortcut.Configure(value.shortcutCode);ready=true;
        shortcut.Pause(capturing);
        return window.Paint(value.frosted);
    }
    bool Restore(){if(stopped)return false;applying=true;const bool ok=Runtime(prefs);applying=false;
        if(!ok){ready=false;shortcut.Stop();window.Stop();error="runtime";}return ok;}
    bool Apply(radial::Preferences next){
        if(stopped||applying||!radial::Valid(next))return false;
        applying=true;const auto previous=prefs;
        const bool ok=Runtime(next);
        if(!ok||!Save(next)){
            const bool rollback=Runtime(previous);error=rollback?(ok?"save":"runtime"):"rollback";applying=false;return false;
        }
        prefs=next;load="loaded";error.clear();applying=false;
        shortcut.Visible(window.Visible()?window.Handle():nullptr);return true;
    }
    bool Desktop(POINT point)const{
        return options.verification?(options.desktop&&options.desktop(point)):IsRadialDesktopPoint(point);
    }
    void Hide(){shortcut.Visible(nullptr);window.Hide();}
    void Action(UINT command){
        if(stopped||applying)return;
        if(command==RadialWindow::Dismiss){Hide();return;}
        if(command==RadialWindow::Toggle){
            if(screenshot.Active())return;
            if(window.Visible()){Hide();return;}
            POINT point{};
            if(Running()&&!capturing&&GetCursorPos(&point)&&Desktop(point)){
                if(!window.Show(point,prefs.frosted)){error="runtime";Changed();}
            }
            return;
        }
        if(command==RadialWindow::Screenshot){
            Hide();screenshotDeadline=GetTickCount64()+2000;
            if(!SetTimer(window.Handle(),101,25,nullptr)){screenshotDeadline=0;error="runtime";Changed();}
            return;
        }
        if(command==101){
            if(!screenshotDeadline)return;
            const bool released=options.verification||(!shortcut.NeedsRelease()&&InputReleased(prefs.shortcutCode&255));
            if(!released&&GetTickCount64()<screenshotDeadline)return;
            KillTimer(window.Handle(),101);screenshotDeadline=0;
            Hide();
            const bool ok=released&&(options.verification?(options.screenshot&&options.screenshot()):
                (!shortcut.NeedsRelease()&&StartScreenshot(screenshot,prefs.shortcutCode&255)));
            if(!ok){error="runtime";Changed();}return;
        }
        if(command==11||command==13){Hide();if(settings)settings(command==13);return;}
        auto next=prefs;
        if(command==1)next.enabled=false;
        else if(command==8)next.frosted=false;
        else if(command==9)next.frosted=true;
        else if(command==10){next={};next.enabled=prefs.enabled;}
        else return;
        const bool saved=Apply(next);if(notify)notify(State(saved,saved),!saved);
    }
    std::optional<std::wstring> HandlePage(ui::PageCommand command){
        if(stopped||applying)return {};
        using ui::PageCommand;
        if(command==PageCommand::Ready)return State();
        if(command==PageCommand::RadialCaptureBegin||command==PageCommand::RadialCaptureEnd){
            capturing=command==PageCommand::RadialCaptureBegin;shortcut.Pause(capturing);
            if(capturing)Hide();return State();
        }
        auto next=prefs;
        const int number=static_cast<int>(command),first=static_cast<int>(PageCommand::RadialShortcutFirst),last=static_cast<int>(PageCommand::RadialShortcutLast);
        if(number>=first&&number<=last)next.shortcutCode=static_cast<unsigned>(number-first+1);
        else switch(command){
            case PageCommand::RadialEnable:next.enabled=true;break;
            case PageCommand::RadialDisable:next.enabled=false;break;
            case PageCommand::RadialLiquidGlass:next.frosted=false;break;
            case PageCommand::RadialFrostedGlass:next.frosted=true;break;
            case PageCommand::RadialReset:next={};next.enabled=prefs.enabled;break;
            default:return {};
        }
        const bool saved=Apply(next);return State(saved,saved);
    }
    void Shutdown(){if(stopped)return;stopped=true;ready=false;screenshotDeadline=0;shortcut.Stop();screenshot.Stop();window.Stop();}
};
RadialSession::RadialSession(HINSTANCE instance,RadialSessionOptions options,Notify notify,std::function<void(bool)> settings,settings::IFileStore* files):
    impl_(std::make_unique<Impl>(instance,std::move(options),std::move(notify),std::move(settings),files)){}
RadialSession::~RadialSession()=default;
bool RadialSession::Restore(){return impl_->Restore();}
std::optional<std::wstring> RadialSession::HandlePage(ui::PageCommand command){return impl_->HandlePage(command);}
std::wstring RadialSession::State()const{return impl_->State();}
bool RadialSession::Running()const{return impl_->Running();}
HWND RadialSession::Handle()const{return impl_->window.Handle();}
const radial::Preferences& RadialSession::Snapshot()const{return impl_->prefs;}
void RadialSession::CancelCapture(){impl_->capturing=false;impl_->shortcut.Pause(false);}
void RadialSession::Shutdown(){impl_->Shutdown();}
}
