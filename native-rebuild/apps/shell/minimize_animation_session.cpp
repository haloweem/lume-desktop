#include "apps/shell/minimize_animation_session.h"
#include "platform/windows/local_settings_file_store.h"
#include "vendor/nlohmann/json.hpp"

namespace lume::shell {
using namespace platform::windows;
namespace {
bool Integer(std::wstring_view text,int low,int high,int& output) {
    if (text.empty() || text.size()>4 || (text.size()>1 && text.front()==L'0')) return false;
    int value=0;
    for (const wchar_t ch:text) {
        if (ch<L'0' || ch>L'9') return false;
        value=value*10+(ch-L'0');
    }
    if (value<low || value>high) return false;
    output=value;
    return true;
}
}
struct MinimizeAnimationSession::Impl {
    Impl(std::string value,Notify callback,settings::IFileStore* files,MinimizeAnimationScope scope)
        :path(std::move(value)),notify(std::move(callback)),store(files?*files:local),
         loaded(store.Load(path)),preferences(loaded.value),runtime(std::move(scope)) {}
    std::string path,error;
    Notify notify;
    LocalSettingsFileStore local;
    minimize_animation::PreferencesStore store;
    minimize_animation::LoadedPreferences loaded;
    minimize_animation::Preferences preferences;
    MinimizeAnimationRuntime runtime;
    unsigned saves=0;
    bool stopped=false,restored=false;
    std::wstring State(bool applied=false,bool saved=false) const {
        const auto snapshot=runtime.Snapshot();
        const auto status=saves ? minimize_animation::LoadState::Loaded : loaded.state;
        const char* load=status==minimize_animation::LoadState::Loaded?"loaded":
            status==minimize_animation::LoadState::Recovered?"recovered":
            status==minimize_animation::LoadState::Failed?"failed":"fresh";
        const auto reportedError=error.empty() ?
            std::string(MinimizeAnimationErrorName(snapshot.error)):error;
        const auto json=nlohmann::json{
            {"version",1},{"type","minimize-state"},{"enabled",preferences.enabled},
            {"running",snapshot.running},{"durationMs",preferences.durationMs},
            {"restore",preferences.restore},{"launch",preferences.launch},
            {"multiMonitor",preferences.multiMonitor},{"unhideTaskbar",preferences.unhideTaskbar},
            {"unhideDurationMs",preferences.unhideDurationMs},
            {"engine",preferences.engine==minimize_animation::Engine::Modern?"modern":"classic"},
            {"x64",snapshot.x64},{"x86",snapshot.x86},{"hosts",snapshot.hostCount},
            {"hooks",snapshot.hooks},{"workers",snapshot.workers},{"entries",snapshot.entries},
            {"pendingCleanup",snapshot.pendingCleanup},
            {"loadStatus",load},{"error",reportedError},{"systemError",snapshot.systemError},
            {"result",applied?"applied":"snapshot"},{"saved",saved}
        }.dump();
        return std::wstring(json.begin(),json.end());
    }
    bool Restore() {
        if (stopped) return false;
        if (restored) return !preferences.enabled || runtime.Snapshot().running;
        restored=true;
        if (!preferences.enabled) return true;
        const bool ok=runtime.Start(preferences);
        if (!ok) error=MinimizeAnimationErrorName(runtime.Snapshot().error);
        if (notify) notify(State(),false);  // Startup: report in the page, don't open it.
        return ok;
    }
    bool Apply(const minimize_animation::Preferences& next) {
        if (stopped || !minimize_animation::Valid(next)) return false;
        const auto previous=preferences;
        bool ok=next.enabled ? runtime.Configure(next) : runtime.Stop();
        if (!ok) {
            error=MinimizeAnimationErrorName(runtime.Snapshot().error);
            if (notify) notify(State(true,false),true);
            return false;
        }
        if (!store.Save(path,next)) {
            error="save";
            const bool reverted=previous.enabled ? runtime.Configure(previous) : runtime.Stop();
            if (!reverted) error="rollback";
            if (notify) notify(State(true,false),true);
            return false;
        }
        preferences=next;error.clear();++saves;restored=true;
        if (notify) notify(State(true,true),false);
        return true;
    }
    std::optional<std::wstring> Handle(std::wstring_view message) {
        if (stopped) return {};
        if (message==L"v1|ready") return State();
        constexpr std::wstring_view prefix=L"v1|minimize|";
        if (message.size()>56 || message.substr(0,prefix.size())!=prefix) return {};
        const auto command=message.substr(prefix.size());
        auto next=preferences;
        if (command==L"reset") {const bool enabled=next.enabled;next={};next.enabled=enabled;}
        else {
            const auto split=command.find(L'|');
            if (split==std::wstring_view::npos) return {};
            const auto key=command.substr(0,split),value=command.substr(split+1);
            if (key==L"durationMs") {if (!Integer(value,50,2000,next.durationMs)) return {};}
            else if (key==L"unhideDurationMs") {if (!Integer(value,0,5000,next.unhideDurationMs)) return {};}
            else if (key==L"engine") {
                if (value!=L"modern" && value!=L"classic") return {};
                next.engine=value==L"modern"?minimize_animation::Engine::Modern:minimize_animation::Engine::Classic;
            } else {
                if (value!=L"true" && value!=L"false") return {};
                const bool state=value==L"true";
                if (key==L"enabled") next.enabled=state;
                else if (key==L"restore") next.restore=state;
                else if (key==L"launch") next.launch=state;
                else if (key==L"multiMonitor") next.multiMonitor=state;
                else if (key==L"unhideTaskbar") next.unhideTaskbar=state;
                else return {};
            }
        }
        const bool ok=Apply(next);
        return State(true,ok);
    }
};
MinimizeAnimationSession::MinimizeAnimationSession(HINSTANCE,std::string path,Notify notify,
    settings::IFileStore* files,MinimizeAnimationScope scope)
    :impl_(std::make_unique<Impl>(std::move(path),std::move(notify),files,std::move(scope))) {}
MinimizeAnimationSession::~MinimizeAnimationSession() {impl_->notify={};Shutdown();}
bool MinimizeAnimationSession::Restore() {return impl_->Restore();}
bool MinimizeAnimationSession::Apply(const minimize_animation::Preferences& p) {return impl_->Apply(p);}
std::optional<std::wstring> MinimizeAnimationSession::HandleMessage(std::wstring_view m) {return impl_->Handle(m);}
std::wstring MinimizeAnimationSession::State() const {return impl_->State();}
bool MinimizeAnimationSession::Running() const {return impl_->runtime.Snapshot().running;}
bool MinimizeAnimationSession::Shutdown() {impl_->stopped=true;return impl_->runtime.Stop();}
const minimize_animation::Preferences& MinimizeAnimationSession::Preferences() const {return impl_->preferences;}
MinimizeAnimationSnapshot MinimizeAnimationSession::Snapshot() const {return impl_->runtime.Snapshot();}
}
