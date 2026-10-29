#include "platform/windows/calendar_runtime.h"
#include <utility>
namespace lume::platform::windows {
NativeCalendarRuntime::NativeCalendarRuntime(HINSTANCE instance,std::function<void(calendar::Action)> handler,std::function<void()> settings):window_(instance,std::move(handler),std::move(settings)){}
NativeCalendarRuntime::~NativeCalendarRuntime(){Shutdown();}
bool NativeCalendarRuntime::Apply(const calendar::Preferences& prefs) {
    return ApplyCandidate(prefs,false);
}
bool NativeCalendarRuntime::ApplyBounds(const calendar::Preferences& prefs) {
    return ApplyCandidate(prefs,true);
}
bool NativeCalendarRuntime::ApplyTheme(calendar::Preferences& prefs,bool useCurrentPosition) {
    if(!calendar::ValidPreferences(prefs))return false;
    if(useCurrentPosition&&prefs.enabled&&window_.IsRunning()) {
        const auto confirmed=window_.ConfirmedBounds();
        if(confirmed){prefs.Active()->x=confirmed->x;prefs.Active()->y=confirmed->y;prefs.Active()->hasPosition=true;}
    }
    return ApplyCandidate(prefs,false);
}
bool NativeCalendarRuntime::ApplyCandidate(const calendar::Preferences& prefs,bool explicitBounds) {
    if(!calendar::ValidPreferences(prefs)) return false;
    rollbackBounds_=window_.IsRunning()?window_.ConfirmedBounds():appliedBounds_;
    if(!prefs.enabled) window_.Stop();
    if(!window_.Configure(prefs,false,explicitBounds)) return false;
    const bool ok=!prefs.enabled||window_.Start();
    if(ok)appliedBounds_=prefs.enabled?std::optional<calendar::Profile>(window_.CurrentBounds()):std::nullopt;
    return ok;
}
bool NativeCalendarRuntime::IsRunning()const{return window_.IsRunning();}
bool NativeCalendarRuntime::ApplyReset(calendar::Preferences& prefs) {
    if(!calendar::ValidPreferences(prefs))return false;
    if(!prefs.enabled||!window_.IsRunning())return Apply(prefs);
    rollbackBounds_=window_.ConfirmedBounds();
    const auto live=window_.CurrentBounds();prefs.Active()->x=live.x;prefs.Active()->y=live.y;prefs.Active()->hasPosition=true;
    const bool ok=window_.Configure(prefs,true);
    if(ok)appliedBounds_=window_.CurrentBounds();
    return ok;
}
bool NativeCalendarRuntime::ApplyRollback(const calendar::Preferences& prefs) {
    if(!calendar::ValidPreferences(prefs))return false;
    auto restored=prefs;
    if(restored.enabled&&rollbackBounds_)*restored.Active()=*rollbackBounds_;
    if(!restored.enabled)window_.Stop();
    if(!window_.Configure(restored,true)|| (restored.enabled&&!window_.Start(true)))return false;
    appliedBounds_=restored.enabled?std::optional<calendar::Profile>(window_.CurrentBounds()):std::nullopt;
    rollbackBounds_.reset();return true;
}
void NativeCalendarRuntime::Shutdown(){window_.Stop();appliedBounds_.reset();rollbackBounds_.reset();}
}
