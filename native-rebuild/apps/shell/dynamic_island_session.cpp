#include "apps/shell/dynamic_island_session.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/dynamic_island/island_window.h"
#include "vendor/nlohmann/json.hpp"
#include <utility>
#include <algorithm>

namespace lume::shell {
using namespace platform::windows;
struct DynamicIslandSession::Impl {
    DynamicIslandSessionOptions options;
    LocalSettingsFileStore local;
    settings::IFileStore& files;
    widget::Limits limits{260,48,220,44,560,220};
    widget::PreferencesStore store;
    widget::Preferences prefs;
    widget::LoadState load = widget::LoadState::Fresh;
    dynamic_island::Model model;
    IslandWindow window;
    std::string error;
    IslandDisplay display;
    bool stopped = false, english = false;
    // True while the island shows another player's Windows media session.
    bool external = false;
    // Play/pause shows at once; players confirm a few hundred ms later. Until
    // a snapshot agrees (or 2.5 s pass) older snapshots do not flip it back.
    std::optional<bool> expectedPlaying;
    ULONGLONG expectedUntil = 0;
    void Expect(dynamic_island::Media& media) {
        if (!expectedPlaying) return;
        if (!media.available || media.playing == *expectedPlaying || GetTickCount64() >= expectedUntil) { expectedPlaying.reset(); return; }
        media.playing = *expectedPlaying;
    }
    Impl(HINSTANCE instance, DynamicIslandSessionOptions o, settings::IFileStore* f)
        : options(std::move(o)), files(f?*f:local), store(files,limits), prefs(widget::Defaults(limits)),
          window(instance,[this](dynamic_island::Action action){Action(action);}) {
        window.SetTodoSource(options.todos);
        const auto loaded=store.Load(options.preferencesPath);prefs=loaded.value;load=loaded.state;
        prefs.locked = true;
        LoadDisplay();
    }
    // Display options live beside the shared widget preferences, whose format
    // other widgets use; missing or unreadable files keep the defaults.
    std::string DisplayPath()const {
        auto path=options.preferencesPath;
        if(path.empty())return {};
        const auto dot=path.rfind(".json");
        return dot!=std::string::npos&&dot+5==path.size()?path.substr(0,dot)+"-display.json":path+".display";
    }
    static const char* MaterialName(IslandMaterial value){return value==IslandMaterial::Liquid?"liquid-glass":value==IslandMaterial::Frosted?"frosted-glass":"none";}
    static const char* SizeName(IslandSize size){return size==IslandSize::Small?"small":size==IslandSize::Large?"large":"standard";}
    void LoadDisplay() {
        std::string text;const auto path=DisplayPath();
        if(path.empty()||!files.Exists(path)||!files.ReadBounded(path,4096,text))return;
        const auto json=nlohmann::json::parse(text,nullptr,false);
        if(!json.is_object()||json.value("version",0)!=1)return;
        const auto flag=[&](const char* key,bool& out){const auto it=json.find(key);if(it!=json.end()&&it->is_boolean())out=it->get<bool>();};
        flag("hideFullscreen",display.hideFullscreen);
        flag("topmost",display.topmost);flag("hoverExpand",display.hoverExpand);flag("autoHide",display.autoHide);
        flag("messages",display.messages);
        const auto size=json.value("size",std::string{});
        if(size=="small")display.size=IslandSize::Small;else if(size=="large")display.size=IslandSize::Large;
        else if(size=="standard")display.size=IslandSize::Standard;
        const auto material=json.value("material",std::string{});
        if(material=="liquid-glass")display.material=IslandMaterial::Liquid;
        else if(material=="frosted-glass")display.material=IslandMaterial::Frosted;
        else if(material=="none")display.material=IslandMaterial::None;
    }
    bool SaveDisplay(const IslandDisplay& next) {
        const auto path=DisplayPath();if(path.empty())return true;
        const auto text=nlohmann::json{{"version",1},{"hideFullscreen",next.hideFullscreen},
            {"topmost",next.topmost},{"hoverExpand",next.hoverExpand},{"autoHide",next.autoHide},{"messages",next.messages},{"size",SizeName(next.size)},
            {"material",MaterialName(next.material)}}.dump();
        const auto temporary=path+".tmp";
        if(files.Write(temporary,text)&&files.Rename(temporary,path))return true;
        files.Remove(temporary);return false;
    }
    bool CommitDisplay(const IslandDisplay& next) {
        if(stopped)return false;
        // Apply first: a failed window rebuild must leave the saved file untouched.
        const auto previous=display;
        if(prefs.enabled&&!window.SetDisplay(next)){error="runtime";return false;}
        if(!SaveDisplay(next)){if(prefs.enabled)window.SetDisplay(previous);error="save";return false;}
        display=next;error.clear();return true;
    }
    std::uint64_t Now()const {return options.monotonicMillis?options.monotonicMillis():GetTickCount64();}
    const char* LoadStatus()const {
        switch(load) {
        case widget::LoadState::Loaded:return "loaded";
        case widget::LoadState::Recovered:return "recovered";
        case widget::LoadState::Failed:return "failed";
        default:return "fresh";
        }
    }
    std::wstring State(bool applied=false,bool saved=false)const {
        const auto json=nlohmann::json{{"version",1},{"type","dynamicIsland-state"},
            {"enabled",prefs.enabled},{"running",window.Handle()!=nullptr},{"locked",prefs.locked},
            {"expanded",window.Expanded()},{"focusing",model.Focusing()},{"finished",model.Finished()},
            {"remaining",model.RemainingSeconds()},{"hideFullscreen",display.hideFullscreen},
            {"topmost",display.topmost},{"hover",display.hoverExpand},{"autoHide",display.autoHide},{"size",SizeName(display.size)},
            {"material",MaterialName(display.material)},
            {"loadStatus",LoadStatus()},{"error",error},
            {"result",applied?"applied":"snapshot"},{"saved",saved}}.dump();
        return std::wstring(json.begin(),json.end());
    }
    void Notify() {if(!stopped&&options.notify)options.notify(State());}
    void Paint() {
        if(!window.Handle())return;
        SYSTEMTIME time{};GetLocalTime(&time);
        wchar_t clock[16]{},date[96]{};
        swprintf_s(clock,L"%02u:%02u",time.wHour,time.wMinute);
        GetDateFormatEx(english?L"en-US":L"zh-CN",DATE_LONGDATE,&time,nullptr,date,96,nullptr);
        window.Update({model.Playback(),clock,date,model.RemainingSeconds(),model.Focusing(),model.Finished(),english});
    }
    bool Runtime(const widget::Preferences& next) {
        if(!next.enabled){window.Stop();if(options.weatherActive)options.weatherActive(false);return true;}
        if(!window.Start(next,display))return false;
        if(options.weatherActive)options.weatherActive(true);
        RefreshWeather();Paint();return true;
    }
    void RefreshWeather() {
        if(stopped)return;
        window.SetWeather(options.weather?options.weather():dynamic_island::Weather{});
    }
    bool Commit(widget::Preferences next) {
        if(stopped)return false;
        next.locked = true;
        next.placed = false;
        const auto previous=prefs;
        if(!Runtime(next)) {Runtime(previous);error="runtime";return false;}
        if(!store.Save(options.preferencesPath,next)) {
            error=Runtime(previous)?"save":"rollback";return false;
        }
        prefs=next;load=widget::LoadState::Loaded;error.clear();
        if(!prefs.enabled)model.CancelFocus();
        return true;
    }
    bool Restore() {return !stopped&&Runtime(prefs);}
    void Action(dynamic_island::Action action) {
        if(stopped)return;
        using dynamic_island::Action;
        switch(action) {
        case Action::ExpansionChanged:RefreshWeather();Notify();return;
        case Action::Tick:{
            // Lume Music has priority; otherwise follow the current Windows media
            // session every second so position, track and state stay current.
            if((model.Playback().available&&!external)||!options.externalMedia)return;
            if(auto media=options.externalMedia()) { external=true;Expect(*media);model.Update(std::move(*media)); Paint(); }
            else if(external) { external=false;model.Update({});Paint(); }
            return;
        }
        case Action::Settings:if(options.openSettings)options.openSettings();return;
        case Action::WeatherSettings:if(options.openWeatherSettings)options.openWeatherSettings();return;
        case Action::Close:{auto next=prefs;next.enabled=false;Commit(next);Notify();return;}
        case Action::MaterialNone:case Action::MaterialLiquid:case Action::MaterialFrosted:{
            auto value=display;
            value.material=action==Action::MaterialLiquid?IslandMaterial::Liquid:
                action==Action::MaterialFrosted?IslandMaterial::Frosted:IslandMaterial::None;
            if(CommitDisplay(value))Paint();
            Notify();return;
        }
        case Action::ToggleMessages:{auto value=display;value.messages=!value.messages;if(CommitDisplay(value))Paint();Notify();return;}
        case Action::Reset:{if(const auto reply=HandlePage(ui::PageCommand::IslandReset);reply&&options.notify)options.notify(*reply);return;}
        case Action::Focus:case Action::CancelFocus:return;
        default:{
            if(!prefs.enabled)return;
            const bool toggle=action==Action::Toggle&&model.Playback().available;
            const bool play=toggle&&!model.Playback().playing;
            if(model.Playback().available&&!external&&options.control)options.control(action,model.Playback());
            else if(toggle&&options.externalSetPlaying)options.externalSetPlaying(play);
            else if(options.externalControl)options.externalControl(action);
            if(toggle){
                expectedPlaying=play;expectedUntil=GetTickCount64()+2500;
                auto media=model.Playback();media.playing=play;model.Update(std::move(media));Paint();
            }
            return;
        }
        }
    }
    std::optional<std::wstring> HandlePage(ui::PageCommand command) {
        if(stopped)return {};
        using ui::PageCommand;
        auto next=prefs;
        switch(command) {
        case PageCommand::Ready:return State();
        case PageCommand::IslandEnable:next.enabled=true;break;
        case PageCommand::IslandDisable:next.enabled=false;break;
        case PageCommand::IslandLock:case PageCommand::IslandUnlock:next.locked=true;break;
        case PageCommand::IslandReset:{
            next=widget::Defaults(limits);next.enabled=prefs.enabled;
            const bool shown=CommitDisplay(IslandDisplay{});const bool saved=Commit(next)&&shown;return State(true,saved);
        }
        case PageCommand::IslandHideFullscreenOn:case PageCommand::IslandHideFullscreenOff:
        case PageCommand::IslandTopmostOn:case PageCommand::IslandTopmostOff:
        case PageCommand::IslandHoverOn:case PageCommand::IslandHoverOff:
        case PageCommand::IslandAutoHideOn:case PageCommand::IslandAutoHideOff:
        case PageCommand::IslandSizeSmall:case PageCommand::IslandSizeStandard:case PageCommand::IslandSizeLarge:
        case PageCommand::IslandMaterialNone:case PageCommand::IslandMaterialLiquid:case PageCommand::IslandMaterialFrosted:{
            auto value=display;
            switch(command) {
            case PageCommand::IslandHideFullscreenOn:value.hideFullscreen=true;break;
            case PageCommand::IslandHideFullscreenOff:value.hideFullscreen=false;break;
            case PageCommand::IslandTopmostOn:value.topmost=true;break;
            case PageCommand::IslandTopmostOff:value.topmost=false;break;
            case PageCommand::IslandHoverOn:value.hoverExpand=true;break;
            case PageCommand::IslandHoverOff:value.hoverExpand=false;break;
            case PageCommand::IslandAutoHideOn:value.autoHide=true;break;
            case PageCommand::IslandAutoHideOff:value.autoHide=false;break;
            case PageCommand::IslandSizeSmall:value.size=IslandSize::Small;break;
            case PageCommand::IslandSizeLarge:value.size=IslandSize::Large;break;
            case PageCommand::IslandSizeStandard:value.size=IslandSize::Standard;break;
            case PageCommand::IslandMaterialNone:value.material=IslandMaterial::None;break;
            case PageCommand::IslandMaterialLiquid:value.material=IslandMaterial::Liquid;break;
            case PageCommand::IslandMaterialFrosted:value.material=IslandMaterial::Frosted;break;
            default:break;
            }
            const bool saved=CommitDisplay(value);
            if(saved){load=widget::LoadState::Loaded;Paint();}
            return State(true,saved);
        }
        case PageCommand::IslandExpand:case PageCommand::IslandCollapse:
            window.Expand(command==PageCommand::IslandExpand);return State(true);
        case PageCommand::IslandFocus:case PageCommand::IslandStopFocus:return {};
        default:return {};
        }
        const bool saved=Commit(next);return State(true,saved);
    }
    void Open() {if(stopped)return;auto next=prefs;next.enabled=true;if(Commit(next))window.Expand(true);Notify();}
    void Shutdown() {if(stopped)return;stopped=true;window.SetTodoSource({});window.Stop();if(options.weatherActive)options.weatherActive(false);model.CancelFocus();options.notify={};options.control={};options.openSettings={};options.weatherActive={};}
    ~Impl(){Shutdown();}
};
DynamicIslandSession::DynamicIslandSession(HINSTANCE instance,DynamicIslandSessionOptions options,settings::IFileStore* files)
    : impl_(std::make_unique<Impl>(instance,std::move(options),files)) {}
DynamicIslandSession::~DynamicIslandSession()=default;
bool DynamicIslandSession::Restore(){const bool ok=impl_->Restore();if(!ok&&!impl_->stopped)impl_->error="runtime";return ok;}
void DynamicIslandSession::Open(){impl_->Open();}
std::optional<std::wstring> DynamicIslandSession::HandlePage(ui::PageCommand command){return impl_->HandlePage(command);}
std::wstring DynamicIslandSession::State()const{return impl_->State();}
void DynamicIslandSession::Update(dynamic_island::Media media){
    if(impl_->stopped)return;
    // Keep a Windows media session on screen while Lume has nothing to play.
    if(!media.available&&impl_->external)return;
    if(media.available)impl_->external=false;
    impl_->Expect(media);
    const auto old=impl_->model.Playback();impl_->model.Update(std::move(media));
    const auto& next=impl_->model.Playback();
    if(old.session!=next.session||old.id!=next.id||old.artwork!=next.artwork||old.title!=next.title||old.artist!=next.artist||
       old.playing!=next.playing||old.available!=next.available||old.duration!=next.duration||old.lyrics!=next.lyrics||
       static_cast<std::uint64_t>(old.position)!=static_cast<std::uint64_t>(next.position)||
       old.energy!=next.energy||old.spectrum!=next.spectrum)impl_->Paint();
}
void DynamicIslandSession::SetEnglish(bool english){impl_->english=english;if(!impl_->stopped)impl_->Paint();}
void DynamicIslandSession::Shutdown(){impl_->Shutdown();}
bool DynamicIslandSession::ShowNotice(const std::string& id,const std::wstring& title,const std::wstring& time){
    return !impl_->stopped&&impl_->prefs.enabled&&impl_->window.ShowNotice(id,title,time);
}
void DynamicIslandSession::RefreshTodos(){if(!impl_->stopped)impl_->window.RefreshTodos();}
void DynamicIslandSession::RefreshWeather(){impl_->RefreshWeather();}
bool DynamicIslandSession::Running()const{return impl_->window.Handle()!=nullptr;}
HWND DynamicIslandSession::Handle()const{return impl_->window.Handle();}
const widget::Preferences& DynamicIslandSession::Snapshot()const{return impl_->prefs;}
}
