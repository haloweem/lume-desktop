#include "apps/shell/dynamic_island_session.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/dynamic_island/island_window.h"
#include "platform/windows/dynamic_island/island_preview.h"
#include "vendor/nlohmann/json.hpp"
#include <utility>
#include <algorithm>

namespace lume::shell {
using namespace platform::windows;
struct DynamicIslandSession::Impl {
    DynamicIslandSessionOptions options;
    HINSTANCE instance;
    LocalSettingsFileStore local;
    settings::IFileStore& files;
    widget::Limits limits{260,48,220,44,560,220};
    widget::PreferencesStore store;
    widget::Preferences prefs;
    widget::LoadState load = widget::LoadState::Fresh;
    dynamic_island::Model model;
    IslandWindow window;
    std::vector<std::unique_ptr<IslandWindow>> mirrors;
    std::vector<IslandMonitor> screens;
    ULONGLONG screensChecked=0;
    std::string error;
    IslandDisplay display;
    std::unique_ptr<IslandPreviewRenderer> previewRenderer;
    bool stopped = false, english = false;
    bool weatherRequested = false, previewActive = false;
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
    Impl(HINSTANCE module, DynamicIslandSessionOptions o, settings::IFileStore* f)
        : options(std::move(o)), instance(module), files(f?*f:local), store(files,limits), prefs(widget::Defaults(limits)),
          window(module,[this](dynamic_island::Action action){Action(action);}) {
        window.SetTodoSource(options.todos);
        const auto loaded=store.Load(options.preferencesPath);prefs=loaded.value;load=loaded.state;
        prefs.locked = true;
        screens=ReadScreens();
        LoadDisplay();
    }
    std::vector<IslandMonitor> ReadScreens() const {
        return options.monitors?options.monitors():ConnectedIslandMonitors();
    }
    int PrimaryScreen() const {
        const auto found=std::find_if(screens.begin(),screens.end(),[](const auto& screen){return screen.primary;});
        return found==screens.end()?1:static_cast<int>(found-screens.begin())+1;
    }
    template<class F> void EachWindow(F action) {
        action(window);for(const auto& mirror:mirrors)action(*mirror);
    }
    bool Expanded() const {
        return window.Expanded()||std::any_of(mirrors.begin(),mirrors.end(),[](const auto& mirror){return mirror->Expanded();});
    }
    void StopWindows() {mirrors.clear();window.Stop();}
    bool ProjectDisplay(const widget::Preferences& next,const IslandDisplay& value) {
        if(!next.enabled){StopWindows();return true;}
        if(screens.empty())return false;
        auto targets=screens;
        if(value.screen!=0){
            const int selected=value.screen>0&&value.screen<=static_cast<int>(screens.size())?value.screen:PrimaryScreen();
            targets={screens[static_cast<std::size_t>(selected-1)]};
        }
        while(mirrors.size()+1<targets.size()){
            auto mirror=std::make_unique<IslandWindow>(instance,[this](dynamic_island::Action action){
                // Only the first window polls media and the display inventory.
                if(action!=dynamic_island::Action::Tick)Action(action);
            });
            mirror->SetTodoSource(options.todos);mirrors.push_back(std::move(mirror));
        }
        for(std::size_t index=0;index<targets.size();++index){
            auto& target=index==0?window:*mirrors[index-1];
            target.SetMonitor(targets[index]);
            if(!target.SetDisplay(value)||!target.Start(next,value))return false;
        }
        mirrors.resize(targets.size()-1);
        return true;
    }
    void RefreshScreens() {
        screensChecked=GetTickCount64();
        auto current=ReadScreens();
        if(current.empty()||current==screens)return;
        const auto previous=screens;screens=std::move(current);
        if(prefs.enabled&&!ProjectDisplay(prefs,display)){
            screens=previous;error=ProjectDisplay(prefs,display)?"runtime":"rollback";
        }
        RefreshWeather();Paint();Notify();
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
    static const char* ShapeName(IslandShape shape){return shape==IslandShape::Standard?"standard":"capsule";}
    static const char* SpeedName(IslandAnimationSpeed speed){return speed==IslandAnimationSpeed::Slow?"slow":speed==IslandAnimationSpeed::Fast?"fast":"normal";}
    static const char* BounceName(IslandBounce bounce){return bounce==IslandBounce::Subtle?"subtle":bounce==IslandBounce::Bouncy?"bouncy":"normal";}
    void LoadDisplay() {
        std::string text;const auto path=DisplayPath();
        // Earlier builds included custom side/workspace data. Ignore those
        // fields while retaining the ordinary display options in the same file.
        if(path.empty()||!files.Exists(path)||!files.ReadBounded(path,8192,text))return;
        const auto json=nlohmann::json::parse(text,nullptr,false);
        if(!json.is_object()||json.value("version",0)!=1)return;
        const auto screen=json.find("screen");
        if(screen!=json.end()&&screen->is_number_integer()&&*screen>=-1&&*screen<=64)display.screen=screen->get<int>();
        const auto flag=[&](const char* key,bool& out){const auto it=json.find(key);if(it!=json.end()&&it->is_boolean())out=it->get<bool>();};
        flag("hideFullscreen",display.hideFullscreen);
        flag("topmost",display.topmost);flag("hoverExpand",display.hoverExpand);flag("autoHide",display.autoHide);
        flag("messages",display.messages);
        const auto delay=json.find("hoverDelayMs");
        if(delay!=json.end()&&delay->is_number_integer()&&*delay>=100&&*delay<=1000)display.hoverDelayMs=delay->get<unsigned>();
        const auto speed=json.find("animationSpeed");
        if(speed!=json.end()&&speed->is_string()){
            if(*speed=="slow")display.animationSpeed=IslandAnimationSpeed::Slow;
            else if(*speed=="fast")display.animationSpeed=IslandAnimationSpeed::Fast;
        }
        const auto bounce=json.find("bounce");
        if(bounce!=json.end()&&bounce->is_string()){
            if(*bounce=="subtle")display.bounce=IslandBounce::Subtle;
            else if(*bounce=="bouncy")display.bounce=IslandBounce::Bouncy;
        }
        for(const auto* key:{"leftWidget","rightWidget"}){
            const auto field=json.find(key);if(field==json.end()||!field->is_string())continue;
            const auto value=dynamic_island::ParseCompactWidget(field->get<std::string>());
            if(value==dynamic_island::CompactWidget::Count)continue;
            (std::string_view(key)=="leftWidget"?display.leftWidget:display.rightWidget)=value;
        }
        const auto size=json.value("size",std::string{});
        if(size=="small")display.size=IslandSize::Small;else if(size=="large")display.size=IslandSize::Large;
        else if(size=="standard")display.size=IslandSize::Standard;
        const auto material=json.value("material",std::string{});
        if(material=="liquid-glass")display.material=IslandMaterial::Liquid;
        else if(material=="frosted-glass")display.material=IslandMaterial::Frosted;
        else if(material=="none")display.material=IslandMaterial::None;
        const auto shape=json.find("shape");
        if(shape!=json.end()&&shape->is_string()) {
            if(*shape=="standard")display.shape=IslandShape::Standard;
            else if(*shape=="capsule")display.shape=IslandShape::Capsule;
        }
    }
    bool SaveDisplay(const IslandDisplay& next) {
        const auto path=DisplayPath();if(path.empty())return true;
        const auto text=nlohmann::json{{"version",1},{"screen",next.screen},{"hideFullscreen",next.hideFullscreen},
            {"topmost",next.topmost},{"hoverExpand",next.hoverExpand},{"autoHide",next.autoHide},{"messages",next.messages},{"size",SizeName(next.size)},
            {"material",MaterialName(next.material)},{"shape",ShapeName(next.shape)},
            {"hoverDelayMs",next.hoverDelayMs},{"animationSpeed",SpeedName(next.animationSpeed)},{"bounce",BounceName(next.bounce)},
            {"leftWidget",dynamic_island::CompactWidgetId(next.leftWidget)},
            {"rightWidget",dynamic_island::CompactWidgetId(next.rightWidget)}}.dump();
        const auto temporary=path+".tmp";
        if(files.Write(temporary,text)&&files.Rename(temporary,path))return true;
        files.Remove(temporary);return false;
    }
    bool CommitDisplay(const IslandDisplay& next) {
        if(stopped)return false;
        // Apply first: a failed window rebuild must leave the saved file untouched.
        const auto previous=display;
        if(prefs.enabled&&!ProjectDisplay(prefs,next)){error=ProjectDisplay(prefs,previous)?"runtime":"rollback";Paint();return false;}
        if(!SaveDisplay(next)){error=!prefs.enabled||ProjectDisplay(prefs,previous)?"save":"rollback";Paint();return false;}
        display=next;SyncWeatherConsumer(prefs.enabled);RefreshWeather();error.clear();return true;
    }
    void SyncWeatherConsumer(bool enabled) {
        using W=dynamic_island::CompactWidget;
        const bool wanted=previewActive||(enabled&&(display.leftWidget>=W::Weather||display.rightWidget>=W::Weather));
        if(wanted==weatherRequested)return;
        weatherRequested=wanted;
        if(options.weatherActive)options.weatherActive(wanted);
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
        auto ids=nlohmann::json::array();
        for(std::size_t index=0;index<screens.size();++index)ids.push_back(index+1);
        if(ids.empty())ids.push_back(1);
        const auto json=nlohmann::json{{"version",1},{"type","dynamicIsland-state"},
            {"enabled",prefs.enabled},{"running",window.Handle()!=nullptr},{"locked",prefs.locked},
            {"expanded",Expanded()},{"focusing",model.Focusing()},{"finished",model.Finished()},
            {"screen",display.screen<0?PrimaryScreen():display.screen},{"screens",std::move(ids)},
            {"remaining",model.RemainingSeconds()},{"hideFullscreen",display.hideFullscreen},
            {"topmost",display.topmost},{"hover",display.hoverExpand},{"autoHide",display.autoHide},{"size",SizeName(display.size)},
            {"material",MaterialName(display.material)},{"shape",ShapeName(display.shape)},
            {"hoverDelayMs",display.hoverDelayMs},{"animationSpeed",SpeedName(display.animationSpeed)},{"bounce",BounceName(display.bounce)},
            {"leftWidget",static_cast<int>(display.leftWidget)},{"rightWidget",static_cast<int>(display.rightWidget)},
            {"loadStatus",LoadStatus()},{"error",error},
            {"result",applied?"applied":"snapshot"},{"saved",saved}}.dump();
        return std::wstring(json.begin(),json.end());
    }
    void Notify() {if(!stopped&&options.notify)options.notify(State());}
    void Paint() {
        SYSTEMTIME time{};GetLocalTime(&time);
        wchar_t clock[16]{},date[96]{};
        swprintf_s(clock,L"%02u:%02u",time.wHour,time.wMinute);
        GetDateFormatEx(english?L"en-US":L"zh-CN",DATE_LONGDATE,&time,nullptr,date,96,nullptr);
        const IslandPresentation view{model.Playback(),clock,date,model.RemainingSeconds(),model.Focusing(),model.Finished(),english};
        EachWindow([&](auto& target){target.Update(view);});
    }
    bool Runtime(const widget::Preferences& next) {
        if(!ProjectDisplay(next,display))return false;
        SyncWeatherConsumer(next.enabled);
        RefreshWeather();Paint();return true;
    }
    void RefreshWeather() {
        if(stopped)return;
        const auto weather=options.weather?options.weather():dynamic_island::Weather{};
        EachWindow([&](auto& target){target.SetWeather(weather);});
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
            RefreshScreens();
            // Lume Music has priority; otherwise follow the current Windows media
            // session every second so position, track and state stay current.
            if((model.Playback().available&&!external)||!options.externalMedia)return;
            if(auto media=options.externalMedia()) { external=true;Expect(*media);model.Update(std::move(*media)); Paint(); }
            else if(external) { external=false;model.Update({});Paint(); }
            return;
        }
        case Action::Settings:if(options.openSettings)options.openSettings();return;
        case Action::WeatherSettings:if(options.openWeatherSettings)options.openWeatherSettings();return;
        case Action::PerformanceSettings:if(options.openPerformanceSettings)options.openPerformanceSettings();return;
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
        const int number=static_cast<int>(command);
        if(number>=static_cast<int>(PageCommand::IslandHoverDelayFirst)&&number<=static_cast<int>(PageCommand::IslandHoverDelayLast)){
            auto value=display;value.hoverDelayMs=100+number-static_cast<int>(PageCommand::IslandHoverDelayFirst);
            const bool saved=CommitDisplay(value);if(saved){load=widget::LoadState::Loaded;Paint();}
            return State(true,saved);
        }
        if(number>=static_cast<int>(PageCommand::IslandScreenFirst)&&number<=static_cast<int>(PageCommand::IslandScreenLast)){
            RefreshScreens();
            const int screen=number-static_cast<int>(PageCommand::IslandScreenFirst);
            if(screen>static_cast<int>(screens.size())){error="screen";return State(true,false);}
            auto value=display;value.screen=screen;
            const bool saved=CommitDisplay(value);if(saved){load=widget::LoadState::Loaded;Paint();}
            return State(true,saved);
        }
        const bool left=number>=static_cast<int>(PageCommand::IslandLeftWidgetFirst)&&number<=static_cast<int>(PageCommand::IslandLeftWidgetLast);
        const bool right=number>=static_cast<int>(PageCommand::IslandRightWidgetFirst)&&number<=static_cast<int>(PageCommand::IslandRightWidgetLast);
        if(left||right){
            auto value=display;const auto selected=static_cast<dynamic_island::CompactWidget>(number-
                static_cast<int>(left?PageCommand::IslandLeftWidgetFirst:PageCommand::IslandRightWidgetFirst));
            (left?value.leftWidget:value.rightWidget)=selected;
            const bool saved=CommitDisplay(value);if(saved){load=widget::LoadState::Loaded;Paint();}
            return State(true,saved);
        }
        switch(command) {
        case PageCommand::Ready:RefreshScreens();return State();
        case PageCommand::IslandPreviewStop:ReleasePreview();return std::wstring{};
        case PageCommand::IslandPreviewCompact:case PageCommand::IslandPreviewExpanded:{
            if(GetTickCount64()-screensChecked>=1000)RefreshScreens();
            const bool expanded=command==PageCommand::IslandPreviewExpanded;
            previewActive=true;SyncWeatherConsumer(prefs.enabled);
            RefreshWeather();
            if(!previewRenderer)previewRenderer=std::make_unique<IslandPreviewRenderer>();
            const auto foreground=window.Preview(display,expanded,1.f/30);
            const auto frame=previewRenderer->Render(foreground,display,options.previewWallpaper?options.previewWallpaper():std::wstring{});
            const auto image=IslandPreviewRenderer::JpegDataUrl(frame);
            auto readings=nlohmann::json::parse(window.CompactReadings(),nullptr,false);
            if(!readings.is_array())readings=nlohmann::json::array();
            const auto value=nlohmann::json{{"version",1},{"type","dynamicIsland-preview"},{"mode",expanded?"expanded":"compact"},
                {"shape",ShapeName(display.shape)},{"material",MaterialName(display.material)},{"size",SizeName(display.size)},
                {"animationSpeed",SpeedName(display.animationSpeed)},{"bounce",BounceName(display.bounce)},
                {"width",image.empty()?0u:frame.width},{"height",image.empty()?0u:frame.height},{"image",image},
                {"animating",foreground.animating&&!image.empty()},
                {"leftWidget",static_cast<int>(display.leftWidget)},{"rightWidget",static_cast<int>(display.rightWidget)},
                {"readings",std::move(readings)},{"error",image.empty()?"render":""}}.dump(-1,' ',true);
            return std::wstring(value.begin(),value.end());
        }
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
        case PageCommand::IslandAnimationSlow:case PageCommand::IslandAnimationNormal:case PageCommand::IslandAnimationFast:
        case PageCommand::IslandBounceSubtle:case PageCommand::IslandBounceNormal:case PageCommand::IslandBounceBouncy:
        case PageCommand::IslandSizeSmall:case PageCommand::IslandSizeStandard:case PageCommand::IslandSizeLarge:
        case PageCommand::IslandMaterialNone:case PageCommand::IslandMaterialLiquid:case PageCommand::IslandMaterialFrosted:
        case PageCommand::IslandShapeStandard:case PageCommand::IslandShapeCapsule:case PageCommand::IslandResetAppearance:{
            auto value=display;
            switch(command) {
            case PageCommand::IslandHideFullscreenOn:value.hideFullscreen=true;break;
            case PageCommand::IslandHideFullscreenOff:value.hideFullscreen=false;break;
            case PageCommand::IslandTopmostOn:value.topmost=true;break;
            case PageCommand::IslandTopmostOff:value.topmost=false;break;
            case PageCommand::IslandHoverOn:value.hoverExpand=true;break;
            case PageCommand::IslandHoverOff:value.hoverExpand=false;break;
            case PageCommand::IslandAnimationSlow:value.animationSpeed=IslandAnimationSpeed::Slow;break;
            case PageCommand::IslandAnimationNormal:value.animationSpeed=IslandAnimationSpeed::Normal;break;
            case PageCommand::IslandAnimationFast:value.animationSpeed=IslandAnimationSpeed::Fast;break;
            case PageCommand::IslandBounceSubtle:value.bounce=IslandBounce::Subtle;break;
            case PageCommand::IslandBounceNormal:value.bounce=IslandBounce::Normal;break;
            case PageCommand::IslandBounceBouncy:value.bounce=IslandBounce::Bouncy;break;
            case PageCommand::IslandAutoHideOn:value.autoHide=true;break;
            case PageCommand::IslandAutoHideOff:value.autoHide=false;break;
            case PageCommand::IslandSizeSmall:value.size=IslandSize::Small;break;
            case PageCommand::IslandSizeLarge:value.size=IslandSize::Large;break;
            case PageCommand::IslandSizeStandard:value.size=IslandSize::Standard;break;
            case PageCommand::IslandMaterialNone:value.material=IslandMaterial::None;break;
            case PageCommand::IslandMaterialLiquid:value.material=IslandMaterial::Liquid;break;
            case PageCommand::IslandMaterialFrosted:value.material=IslandMaterial::Frosted;break;
            case PageCommand::IslandShapeStandard:value.shape=IslandShape::Standard;break;
            case PageCommand::IslandShapeCapsule:value.shape=IslandShape::Capsule;break;
            case PageCommand::IslandResetAppearance:value.shape=IslandShape::Capsule;value.size=IslandSize::Standard;value.material=IslandMaterial::None;break;
            default:break;
            }
            const bool saved=CommitDisplay(value);
            if(saved){load=widget::LoadState::Loaded;Paint();}
            return State(true,saved);
        }
        case PageCommand::IslandExpand:case PageCommand::IslandCollapse:
            EachWindow([&](auto& target){target.Expand(command==PageCommand::IslandExpand);});return State(true);
        case PageCommand::IslandFocus:case PageCommand::IslandStopFocus:return {};
        default:return {};
        }
        const bool saved=Commit(next);return State(true,saved);
    }
    void Open() {if(stopped)return;auto next=prefs;next.enabled=true;if(Commit(next))EachWindow([](auto& target){target.Expand(true);});Notify();}
    void ReleasePreview(){previewRenderer.reset();window.ReleasePreview();previewActive=false;SyncWeatherConsumer(prefs.enabled);}
    void Shutdown() {if(stopped)return;ReleasePreview();EachWindow([](auto& target){target.SetTodoSource({});});StopWindows();SyncWeatherConsumer(false);stopped=true;model.CancelFocus();options.notify={};options.control={};options.openSettings={};options.weatherActive={};}
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
    if(impl_->stopped||!impl_->prefs.enabled)return false;
    bool shown=false;impl_->EachWindow([&](auto& target){shown=target.ShowNotice(id,title,time)||shown;});return shown;
}
void DynamicIslandSession::RefreshTodos(){if(!impl_->stopped)impl_->EachWindow([](auto& target){target.RefreshTodos();});}
void DynamicIslandSession::RefreshWeather(){impl_->RefreshWeather();}
void DynamicIslandSession::ReleasePreview(){impl_->ReleasePreview();}
bool DynamicIslandSession::Running()const{return impl_->window.Handle()!=nullptr;}
HWND DynamicIslandSession::Handle()const{return impl_->window.Handle();}
const widget::Preferences& DynamicIslandSession::Snapshot()const{return impl_->prefs;}
}
