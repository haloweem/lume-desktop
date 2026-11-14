// Real DesktopApplication -> WebView -> production bridge/reducer -> controls
// -> native preferences -> durable reply. All storage belongs to this fixture.
// The island stays off, both messaging sources stay opted out, and no access
// request or application notification content is involved.
#include "apps/shell/desktop_application.h"
#include "apps/shell/island_notification_preferences.h"
#include "platform/windows/local_settings_file_store.h"
#include "unique_test_directory.h"
#include "vendor/nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
using Json = nlohmann::json;
struct Probe {
    lume::shell::DesktopApplication* app = nullptr;
    ULONGLONG deadline = 0;
    bool failed = false, initial = false, complete = false;
    unsigned saves = 0;
    Json latest;
    std::ofstream evidence;

    void Check(bool value, const char* reason) {
        if (!value) { failed = true; std::cerr << "FAIL: " << reason << std::endl; }
    }
    void Message(const std::wstring& text) {
        const auto state = Json::parse(text.begin(), text.end(), nullptr, false);
        if (!state.is_object() || state.value("type", "") != "island-notifications-state") return;
        latest = state;
        evidence << state.dump() << '\n'; evidence.flush();
        const auto& runtime = state.at("runtime");
        const auto& preferences = state.at("preferences");
        Check(runtime.size() == 6 && runtime.contains("qqRealtime") && runtime.at("qqRealtime").is_boolean(),
            "real native state contains the complete runtime including qqRealtime");
        Check(runtime.at("islandEnabled") == false && runtime.at("qqRealtime") == false &&
            runtime.at("qq") == "disabled" && runtime.at("wechat") == "disabled" &&
            state.at("availability") == "not-connected" && state.at("receiving") == false,
            "fixture stays paused with both real messaging receivers disabled");
        Check(preferences.at("messagesQQ") == false && preferences.at("messagesWeChat") == false &&
            preferences.at("messagePreview") == false,
            "page interactions never opt in to a messaging source or notification contents");
        Check(state.at("error") == "", "notification preferences do not report load or save failure");
        const auto revision = state.at("revision").get<unsigned>();
        if (revision == 0 && state.at("result") == "snapshot") {
            initial = true;
            Check(preferences.size() == 18 && !preferences.contains("careEnabled") && !preferences.contains("messagesEnabled") &&
                !preferences.contains("weatherEnabled") && preferences.at("waterEnabled") == false &&
                preferences.at("offWorkEnabled") == false && preferences.at("offWorkTime") == "18:00" &&
                preferences.at("offWorkStyle") == "default",
                "real native snapshot begins with opt-in defaults");
        }
        if (state.at("result") == "applied") {
            Check(initial && state.at("saved") == true && state.at("loaded") == "loaded" &&
                revision == saves + 1 && revision <= 6,
                "each real page action receives an ordered durable native acknowledgement");
            Check(preferences.at("waterEnabled") == true &&
                preferences.at("waterIntervalMinutes") == (revision == 1 ? 60 : 75) &&
                preferences.at("waterGoal") == (revision >= 3 ? 13 : 8) &&
                preferences.at("offWorkEnabled") == (revision >= 4) &&
                preferences.at("offWorkTime") == (revision >= 5 ? "18:45" : "18:00") &&
                preferences.at("offWorkStyle") == (revision == 6 ? "squidward" : "default"),
                "native reply contains the values selected through real page controls");
            ++saves;
            std::cout << "notification DOM save acknowledged: " << revision << std::endl;
        } else if (saves == 6 && revision == 6 && state.at("result") == "snapshot") {
            // The script sends this request only after the production bridge
            // has consumed the last save reply and restored usable controls.
            Check(preferences.at("waterEnabled") == true && preferences.at("waterIntervalMinutes") == 75 &&
                preferences.at("waterGoal") == 13 && preferences.at("offWorkEnabled") == true &&
                preferences.at("offWorkTime") == "18:45" && preferences.at("offWorkStyle") == "squidward",
                "post-DOM-confirmation snapshot closes the page/native acknowledgement loop");
            complete = true;
        }
    }
    void Tick() {
        if (failed || complete) { app->Exit(); return; }
        if (GetTickCount64() >= deadline) {
            std::cerr << "notification integration state: " << latest.dump() << std::endl;
            Check(false, "real notification page bridge/control/save deadline");
            app->Exit();
        }
    }
};
Probe* active = nullptr;
void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) {
    if (active) active->Tick();
}
}

int main() try {
    using namespace lume;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto root = UniqueTestDirectory(std::filesystem::path(executable).parent_path() /
        L"desktop-island-notifications-artifacts");
    platform::windows::LocalSettingsFileStore files;
    shell::DesktopApplicationOptions options;
    options.verification = true;
    options.radialVisible = options.notesVisible = options.minimizeVisible = false;
    options.clockPreferencesPath = (root / L"clock.json").u8string();
    options.calendarPreferencesPath = (root / L"calendar.json").u8string();
    options.performancePreferencesPath = (root / L"performance.json").u8string();
    options.spectrumPreferencesPath = (root / L"spectrum.json").u8string();
    options.almanacPreferencesPath = (root / L"almanac.json").u8string();
    options.weatherPreferencesPath = (root / L"weather.json").u8string();
    options.todayTodoPreferencesPath = (root / L"today-todo.json").u8string();
    options.dynamicIslandPreferencesPath = (root / L"island.json").u8string();
    options.organizerPreferencesPath = (root / L"organizer.json").u8string();
    options.radial.preferencesPath = (root / L"radial.json").u8string();
    options.dock.preferencesPath = (root / L"dock.json").u8string();
    options.desktopIconsPreferencesPath = (root / L"desktop-icons.json").u8string();
    options.taskbarPaths.stateFile = (root / L"taskbar.json").u8string();
    options.taskbarPaths.payloadRoot = root / L"taskbar-payload";
    options.taskbarPaths.cacheRoot = root / L"taskbar-cache";
    options.minimizePreferencesPath = (root / L"minimize.json").u8string();
    options.diagnosticsPath = (root / L"diagnostics.json").u8string();
    options.applicationSettings.preferencesPath = (root / L"application.json").u8string();
    options.updateNotification.statePath = (root / L"update-notification.json").u8string();
    options.accountOptions.storageDirectory = (root / L"account").u8string();
    options.wallpaperOptions.root = (root / L"wallpapers").u8string();
    options.musicPreferencesPath = (root / L"music.json").u8string();
    options.musicBackground.root = (root / L"music-background").u8string();
    options.musicAccounts.root = root / L"music-accounts";
    options.musicFonts.root = root / L"music-fonts";
    options.musicDesktop.path = (root / L"music-desktop.json").u8string();
    options.notesPath = (root / L"notes.json").u8string();
    options.notesWidgetPath = (root / L"notes-widget.json").u8string();
    const auto islandPath = options.dynamicIslandPreferencesPath;
    const widget::Limits limits{260, 48, 220, 44, 560, 220};
    const auto island = widget::Defaults(limits);
    std::string islandBefore;
    if (!widget::Encode(island, islandBefore, limits) || !files.Write(islandPath, islandBefore)) return 1;

    Probe probe;
    probe.evidence.open(root / L"native-notification-states.jsonl", std::ios::binary);
    if (!probe.evidence) return 1;
    options.onMessage = [&](const auto& text) { probe.Message(text); };
    options.readyScript = LR"JS((() => {
      const webview=window.chrome.webview;
      let nativeState=null,phase=0,opened=false,scheduled=false,stableSince=0,stableFailed=false;
      const usable=control=>control&&!control.disabled&&control.getAttribute('aria-disabled')!=='true';
      const checked=control=>control?.getAttribute('aria-checked')==='true';
      const queue=()=>{
        if(scheduled)return;
        scheduled=true;setTimeout(()=>{scheduled=false;act();},0);
      };
      const act=()=>{
        const page=document.querySelector('[data-island-notifications]');
        if(!page){
          const tab=document.querySelector('[data-tab="notifications"]');
          if(tab&&!opened){opened=true;tab.click();}
          return;
        }
        // Observe only actual host messages. Production app.js owns accepting
        // those messages and updating this DOM; no host state is injected.
        const r=nativeState?.runtime;
        if(!r||r.qqRealtime!==false||r.islandEnabled!==false||r.qq!=='disabled'||r.wechat!=='disabled')return;
        const field=key=>page.querySelector('[data-island-notification-key="'+key+'"]');
        const water=field('waterEnabled'),offWork=field('offWorkEnabled');
        const status=page.querySelector('[data-island-notification-status]');
        if(!usable(water)||!usable(offWork)||!status?.hidden){
          if(phase===4)stableFailed=true;
          return;
        }
        if(phase===0){
          if(checked(water)||checked(offWork)||nativeState.revision!==0||
            page.querySelector('.in-module__head [role="switch"]'))return;
          phase=1;water.click();return;
        }
        const ack=revision=>nativeState.revision===revision&&nativeState.result==='applied'&&nativeState.saved&&nativeState.error==='';
        if(phase===1&&ack(1)&&checked(water)){
          const edit=page.querySelector('[data-island-notification-editor="water"]');
          if(!usable(edit))return;
          edit.click();
          const input=field('waterIntervalMinutes');
          const panel=page.querySelector('[data-island-notification-panel="water"]');
          if(!usable(input)||panel?.hidden)return;
          phase=2;input.value='75';
          input.dispatchEvent(new Event('input',{bubbles:true}));
          input.dispatchEvent(new Event('change',{bubbles:true}));return;
        }
        if(phase===2&&ack(2)&&checked(water)){
          const input=field('waterIntervalMinutes');
          const goal=field('waterGoal');
          if(!usable(input)||input.value!=='75'||input.hasAttribute('data-notification-submitted')||!usable(goal))return;
          phase=3;goal.value='13';
          goal.dispatchEvent(new Event('input',{bubbles:true}));
          goal.dispatchEvent(new Event('change',{bubbles:true}));return;
        }
        const sourcesReady=()=>{
          const qq=field('messagesQQ'),wechat=field('messagesWeChat'),preview=field('messagePreview');
          const input=field('waterIntervalMinutes');
          return usable(qq)&&usable(wechat)&&usable(preview)&&!checked(qq)&&!checked(wechat)&&!checked(preview)&&input?.value==='75';
        };
        if(phase===3&&ack(3)&&checked(water)&&sourcesReady()&&field('waterGoal')?.value==='13'){
          // The production request timeout is 5 s. Keep the live controls
          // usable beyond it before writing a distinct completion marker.
          phase=4;stableSince=performance.now();setTimeout(queue,100);return;
        }
        if(phase===4){
          if(stableFailed||!ack(3)||!checked(water)||!sourcesReady()){
            stableFailed=true;return;
          }
          if(performance.now()-stableSince<5500){setTimeout(queue,100);return;}
          if(!usable(offWork)||checked(offWork)){stableFailed=true;return;}
          phase=5;offWork.click();return;
        }
        if(phase===5&&ack(4)&&checked(offWork)&&sourcesReady()){
          const edit=page.querySelector('[data-island-notification-editor="off-work"]');
          if(!usable(edit))return;
          edit.click();
          // The page-owned chooser replaces the platform's gray time popup.
          // Wait for its actual DOM wrapper before driving visible controls.
          phase=6;queue();return;
        }
        if(phase===6&&ack(4)&&checked(offWork)&&sourcesReady()){
          const input=field('offWorkTime');
          const panel=page.querySelector('[data-island-notification-panel="off-work"]');
          if(!usable(input)||panel?.hidden)return;
          const trigger=input.closest('[data-settings-choice]')?.querySelector('[data-settings-choice-trigger]');
          if(!usable(trigger))return;
          phase=7;trigger.click();queue();return;
        }
        if(phase===7&&ack(4)&&checked(offWork)&&sourcesReady()){
          const popup=document.querySelector('[data-settings-choice-popup]');
          const hour=popup?.querySelector('[data-settings-choice-hour="18"]');
          const minute=popup?.querySelector('[data-settings-choice-minute="45"]');
          const apply=popup?.querySelector('[data-settings-choice-apply]');
          if(popup?.hidden||!usable(hour)||!usable(minute)||!usable(apply))return;
          hour.click();
          document.querySelector('[data-settings-choice-popup] [data-settings-choice-minute="45"]')?.click();
          phase=8;document.querySelector('[data-settings-choice-popup] [data-settings-choice-apply]')?.click();return;
        }
        if(phase===8&&ack(5)&&checked(offWork)&&sourcesReady()){
          const input=field('offWorkTime');
          const style=field('offWorkStyle');
          if(nativeState.preferences?.offWorkTime!=='18:45'||nativeState.preferences?.offWorkStyle!=='default'||
            !usable(input)||input.value!=='18:45'||input.hasAttribute('data-notification-submitted')||!usable(style))return;
          const trigger=style.closest('[data-settings-choice]')?.querySelector('[data-settings-choice-trigger]');
          if(!usable(trigger))return;
          phase=9;trigger.click();queue();return;
        }
        if(phase===9&&ack(5)&&checked(offWork)&&sourcesReady()){
          const popup=document.querySelector('[data-settings-choice-popup]');
          const option=popup?.querySelector('[data-settings-choice-option="squidward"]');
          if(popup?.hidden||!usable(option))return;
          phase=10;option.click();return;
        }
        if(phase===10&&ack(6)&&checked(offWork)&&sourcesReady()){
          const style=field('offWorkStyle');
          if(nativeState.preferences?.offWorkTime!=='18:45'||nativeState.preferences?.offWorkStyle!=='squidward'||
            !usable(style)||style.value!=='squidward'||style.hasAttribute('data-notification-submitted'))return;
          phase=11;
          // Native completion is observed only after all DOM acknowledgements.
          webview.postMessage({version:1,scope:'island-notifications',action:'snapshot'});
        }
      };
      webview.addEventListener('message',({data})=>{
        if(data?.type==='island-notifications-state')nativeState=data;
        queue();
      });
      const observer=new MutationObserver(queue);
      observer.observe(document.querySelector('#content'),{subtree:true,childList:true,attributes:true});
      window.addEventListener('beforeunload',()=>observer.disconnect(),{once:true});
      queue();
    })();)JS";

    {
        shell::DesktopApplication app(GetModuleHandleW(nullptr), std::move(options));
        probe.app = &app;
        probe.deadline = GetTickCount64() + 30000;
        active = &probe;
        app.Open(shell::ManagerPage::DynamicIsland);
        const auto timer = SetTimer(nullptr, 0, 40, Tick);
        probe.Check(timer != 0, "create bounded notification integration timer");
        if (timer) probe.Check(app.Run() == 0, "real desktop shell exits cleanly");
        KillTimer(nullptr, timer); active = nullptr;
        probe.Check(!app.ManagerHandle() && !app.ClockRunning() && !app.CalendarRunning(),
            "fixture releases the real manager and never enables unrelated desktop widgets");
    }
    probe.Check(probe.initial && probe.saves == 6 && probe.complete,
        "real notification page accepts native qqRealtime state and completes control/save/reply loop");
    shell::IslandNotificationPreferences reopened(files, islandPath);
    const auto restored = Json::parse(reopened.State());
    const auto& saved = restored.at("preferences");
    probe.Check(restored.at("loaded") == "loaded" && saved.at("waterEnabled") == true &&
        saved.at("waterIntervalMinutes") == 75 && saved.at("waterGoal") == 13 &&
        saved.at("messagesQQ") == false && saved.at("messagesWeChat") == false && saved.at("messagePreview") == false &&
        saved.at("offWorkEnabled") == true && saved.at("offWorkTime") == "18:45" && saved.at("offWorkStyle") == "squidward",
        "fresh native preference owner restores only fixture changes from durable storage");
    std::string islandAfter;
    probe.Check(files.Read(islandPath, islandAfter) && islandAfter == islandBefore &&
        !files.Exists((root / L"island-notifications.json.tmp").u8string()),
        "notification controls leave the island disabled and no temporary preference transaction behind");
    std::cout << (probe.failed ? "FAIL" : "PASS")
        << " real desktop notification protocol, usable controls and durable replies; evidence="
        << root.u8string() << std::endl;
    return probe.failed ? 1 : 0;
} catch (const std::exception& error) {
    active = nullptr;
    std::cerr << "FAIL notification integration: " << error.what() << std::endl;
    return 1;
}
