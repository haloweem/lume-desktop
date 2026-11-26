import { createBridge } from "./bridge.js";
import { prepareInitialView } from "./startup.js";
import {mountSettingsSelect} from './settings-frame.js';
import {mountSettingsChoice} from './settings-choice.js';
import {mountSettingsRanges} from './settings-range.js';
import {mountInteractionMotion} from './interaction-motion.js';
import {
  initialState,
  reduce,
  createVisibilityTimer,
  createClockPreviewTimer,
  createVisibilityCleanup,
} from "./state.js";
import { mountTitlebar } from "./titlebar.js";
import {
  home,
  catalog,
  syncMusicDesktopControls,
  library,
  search,
  wallpaperDetail,
  syncWallpaperControls,
  updateWallpaperPreviewButton,
  updateWallpaperDetailLayout,
  updateWallpaperDownloadControls,
  unavailablePage,
  updateHomeCarousel,
  rememberedHomeSlide,
} from "./pages.js";
import {updateClockPreview} from './clock.js';
import {mountRadialShortcut} from './radial.js';
import {mountDockSummonShortcut,syncDockControls} from './dock.js';
import {islandStatus,syncIslandControls,validIslandHoverDelay,previewIslandHoverDelay} from './dynamic-island.js';
import {createComponentSettingsController} from './component-settings.js';
import {createComponentSettingsCommands} from './component-settings-commands.js';
import {islandStyleWidgets} from './island-styles.js';
import {mountExpandedWidgetPicker} from './expanded-widgets.js';
import {mountTodoBoards} from './todo-board.js';
import {mountIslandNotifications,syncIslandNotifications} from './island-notifications.js';
import {
  selectedWallpaper,
  selectedWallpaperEngagement,
  canLikeWallpaper,
  wallpaperHomeItems,
  wallpaperHeroImageUrl,
  wallpaperAssetCandidates,
  wallpaperAssetPreferredUrl,
  rememberWallpaperAssetFailure,
  rememberWallpaperAssetSuccess,
  wallpaperPreloadTargets,
  wallpaperHomePreloadTargets,
  createWallpaperPreviewController,
  wallpaperNotice,
  wallpaperEngagementError,
} from "./wallpapers.js";
import { modules, widgets, localizedItem } from "./catalog.js";
import { isWindowDragTarget } from "./window-interaction.js";
import {mountWallpaperImages} from "./wallpaper-images.js";
import {createWallpaperPreloader} from './wallpaper-preloader.js';
import { mountWallpaperDrop } from "./wallpaper-drop.js";
import { mountMusicDrop } from "./music-drop.js";
import { createWallpaperScrollMemory } from "./wallpaper-navigation.js";
import { mountApplicationSettings } from "./application-settings.js";
import { createNoticeController } from "./notice.js";
import { createLumeModalController } from "./lume-modal.js";
import { setLanguage, t } from "./localization.js";
import { initialMusicState, normalizeMusicState, applyMusicLyrics, applyMusicScore } from "./music-runtime-state.js";
import { DIAGNOSTICS_ENABLED, createDiagnosticsRecorder, buildDiagnosticReport, diagnosticReportText } from "./diagnostics.js";
const bridge = createBridge();
const diagnosticsRecorder = createDiagnosticsRecorder({enabled:DIAGNOSTICS_ENABLED});
const announcedDiagnosticMismatches=new Set();
const announcedDiagnosticFaults=new Set();
const diagnostics = {
  ...diagnosticsRecorder,
  clear() {
    announcedDiagnosticMismatches.clear();
    announcedDiagnosticFaults.clear();
    diagnosticsRecorder.clear();
  },
  buildReport: options => buildDiagnosticReport({
    ...options,
    version: "1.1.4",
    events: diagnosticsRecorder.list(),
    health: diagnosticsRecorder.health(),
    native: diagnosticsRecorder.native(),
    mismatches: diagnosticsRecorder.mismatches(),
  }),
  text: diagnosticReportText,
};
let state = initialState(),
  musicState = initialMusicState(),
  musicLyricsRequestKey = "",
  musicNativeVisible = null,
  musicViewState = null,
  musicDesktopState = {enabled:false,pending:false},
  musicDesktopPromise = null,
  musicDesktopController = null,
  musicRuntime = null,
  musicRuntimePromise = null,
  musicOpenGeneration = 0,
  unavailableTitle = "",
  tab = "settings",
  activeDropZone = "",
  applicationSettingsSection = "general",
  initialized = false;
const content = document.querySelector("#content"),
  app = document.querySelector("#app"),
  notice = document.querySelector("#notice");
const interactionMotion=mountInteractionMotion(app);
const notices = createNoticeController(notice);
function exitMusicFullscreen() {
  if(!state.fullscreen)return false;
  bridge.send('window','maximize');
  return true;
}
let wallpaperPreviewLease=null,wallpaperPreviewIntent=null,wallpaperPendingRemove=null;
let wallpaperStatsId="";
const wallpaperPreloader=createWallpaperPreloader({onSuccess:rememberWallpaperAssetSuccess,onFailure:rememberWallpaperAssetFailure});
let rememberedHomeView=null;
const wallpaperScroll=createWallpaperScrollMemory();
const unmountWallpaperImages=mountWallpaperImages(content);
const unmountWallpaperDrop=mountWallpaperDrop(document,{
  isEnabled:()=>state.view==="wallpaperLocal" && state.wallpaper.status!=="busy" &&
    ["idle","error"].includes(state.wallpaperStatus),
  onError:text=>showNotice(text==="一次最多导入 32 个壁纸文件"
    ? t("一次最多导入 32 个壁纸文件","You can import up to 32 wallpaper files at a time.") : text),
  onFiles:files=>{
    if (bridge.dropFiles(files)) {
      notices.dismiss();
      dispatch({type:"request",scope:"wallpaper",key:"import"});
    } else showNotice(t("无法接收拖入的文件，请使用添加壁纸或更新 WebView2 Runtime","Could not receive dropped files. Use Add wallpaper or update WebView2 Runtime."));
  },
});
const unmountMusicDrop=mountMusicDrop(document,{
  isEnabled:()=>state.view==="music",
  onError:text=>showNotice(text==="一次最多导入 32 个音乐文件"
    ? t("一次最多导入 32 个音乐文件","You can import up to 32 music files at a time.") : text),
  onFiles:files=>{
    if (bridge.musicDropFiles(files)) notices.dismiss();
    else showNotice(t("无法接收拖入的音乐文件，请使用导入本地音乐或更新 WebView2 Runtime","Could not receive music files. Use Import local music or update WebView2 Runtime."));
  },
});
const wallpaperPreview=createWallpaperPreviewController({
  getVideo:()=>content.querySelector(".wallpaper-preview-video"),
  getImage:()=>content.querySelector(".wallpaper-detail-poster"),
  onChange:playing=>updateWallpaperPreviewButton(content,playing),
  onError:failWallpaperPreview,
});
const visibilityCleanup=createVisibilityCleanup(
  releaseHiddenPageResources,
  generation=>bridge.lifecycle("cleanup",generation),
);
const componentCommands=createComponentSettingsCommands({getState:()=>state,request,
  send:(...command)=>bridge.send(...command),showNotice});
const unmountSelect=mountSettingsSelect(content,(id,value)=>componentCommands.select(id,value));
const unmountChoice=mountSettingsChoice(content);
const radialShortcut=mountRadialShortcut(content,{
  getState:()=>state,onShortcut:code=>request('radial','shortcut',code),
  onCaptureChange:active=>bridge.send('radial','capture',active),
});
const dockSummonShortcut=mountDockSummonShortcut(content,{
  getState:()=>state,onShortcut:code=>request('dock','summonShortcut',code),
  onCaptureChange:active=>bridge.send('dock','summonCapture',active),
});
const componentSettings=createComponentSettingsController({
  root:content,showNotice,
  afterSync:(scope,nextState)=>{
    if(scope==='radial'||scope==='all')radialShortcut.sync();
    if(scope==='dock'||scope==='all')dockSummonShortcut.sync();
    if(scope==='all')syncWallpaperControls(content,nextState,wallpaperPreview.playing);
    unmountChoice.refresh();
  },
});
// A per-style key such as "pulse.barWidth" becomes v1|spectrum|pulse|barWidth|N.
const unmountRanges=mountSettingsRanges(content,(scope,key,value)=>request(scope,...key.split('.'),value));
const todoBoards=mountTodoBoards(content,{bridge});
let islandNotificationTimeout=null;
function sendIslandNotificationCommand(command){
  const {action,...fields}=command;
  delete fields.version;delete fields.scope;
  if(action!=='snapshot'&&['loading','saving','unavailable'].includes(state.islandNotificationsStatus))return false;
  if(action==='snapshot'&&state.islandNotificationsStatus==='saving')return false;
  clearTimeout(islandNotificationTimeout);
  dispatch({type:'request',scope:'island-notifications',key:action});
  const sent=bridge.islandNotifications(action,fields);
  if(!sent){dispatch({type:'unavailable',scope:'island-notifications'});return false;}
  if(['loading','saving'].includes(state.islandNotificationsStatus))islandNotificationTimeout=setTimeout(()=>{
    islandNotificationTimeout=null;dispatch({type:'unavailable',scope:'island-notifications'});
  },5000);
  return true;
}
const unmountIslandNotifications=mountIslandNotifications(content,{getState:()=>state,send:sendIslandNotificationCommand});
const unmountExpandedWidgets=mountExpandedWidgetPicker(content,{
  getState:()=>state,
  onChange:code=>request('dynamicIsland','expandedLayout',code),
});
function notify(title) {
  showNotice(t(`${title}暂未接入`,`${title} is not yet available`));
}
function showNotice(text, options) {
  notices.show(text, options);
}
const titlebar = mountTitlebar(
  document.querySelector("#titlebar"),
  bridge,
  notify,
  { onSettings: showApplicationSettings, onMusic: () => {
    if(state.view!=="music"&&musicViewState?.screen==="desktop")musicViewState={...musicViewState,screen:"home"};
    dispatch(state.view==="music"?{type:"music-leave"}:{type:"navigate",view:"music"});
  }, onLogout: () => applicationSettings.logout() },
);
const modal = createLumeModalController(document, {
  onLogin: () => showApplicationSettings("user"),
  onUpdateDownload: () => {
    const accepted = applicationSettings.downloadUpdate();
    if (!accepted)
      showNotice(t("暂时无法开始更新，请稍后重试","Could not start the update. Please try again."));
    return accepted;
  },
  onUpdateInstall: () => {
    const accepted = applicationSettings.installUpdate();
    if (!accepted)
      showNotice(t("暂时无法安装更新，请稍后重试","Could not install the update. Please try again."));
    return accepted;
  },
});
const applicationSettings = mountApplicationSettings(content, {
  bridge,
  diagnostics,
  getDiagnosticsContext: () => ({
    language: document.documentElement.lang || "zh-CN",
    view: state.view,
  }),
  // Availability prompts use the native daily/version ledger; snapshots drive progress.
  onUpdateChange: (update, busy) => {
    modal.syncUpdate(update, busy, false);
  },
  onAccountBusyChange: (busy) => titlebar.setAccountBusy(busy),
  onBack: () => dispatch({ type: "back" }),
  onLanguage: (language) => {
    setLanguage(language);
    document.documentElement.lang = language;
    titlebar.setLanguage(language);
    modal.setLanguage(language);
    if (initialized && state.view !== "applicationSettings") render();
  },
});
function showApplicationSettings(section = "general") {
  applicationSettingsSection = ["general", "user", "autostart", "diagnostics", "support", "about"].includes(section) ? section : "general";
  if (state.view === "applicationSettings") applicationSettings.show(applicationSettingsSection);
  else dispatch({ type: "navigate", view: "applicationSettings" });
}
function syncMusicDesktopState(value) {
  musicDesktopState = {...value,pending:false};
  syncMusicDesktopControls(content,musicDesktopState);
}
function ensureMusicDesktopController() {
  if (musicDesktopController) return Promise.resolve(musicDesktopController);
  if (!musicDesktopPromise) {
    musicDesktopPromise = import('./music-desktop.js').then(({createMusicDesktop}) => {
      const controller=createMusicDesktop({
        send:(action,fields)=>bridge.musicDesktop(action,fields),
        onChange:value=>syncMusicDesktopState(value),
        onError:error=>showNotice(error==='smtc'
          ? '系统媒体控制暂不可用，应用内播放不受影响' : '桌面音乐组件操作失败，请重试'),
      });
      if (musicDesktopState.type === 'music-desktop-state') controller.receive(musicDesktopState);
      musicDesktopController=controller;
      return controller;
    });
  }
  return musicDesktopPromise;
}
// Music immersive mode tucks the window bar away like fullscreen does, only on the music page.
let musicImmersive=false;
function syncTitlebarImmersive() {titlebar.setImmersive(state.view==="music"&&musicImmersive);}
function loadMusicRuntime() {
  if (musicRuntime) return Promise.resolve(musicRuntime);
  if (!musicRuntimePromise) {
    musicRuntimePromise = import('./music-entry.js').then(({createMusicRuntime}) => {
      const runtime=createMusicRuntime({
        bridge,content,showNotice,translate:t,
        syncDesktopControls:syncMusicDesktopState,onExitFullscreen:exitMusicFullscreen,
        onImmersiveChange:value=>{musicImmersive=value;syncTitlebarImmersive();},
        initialDesktopState:musicDesktopState,initialViewState:musicViewState,
      });
      runtime.setCloseAndNavigate(()=>dispatch({type:'music-leave'}));
      return runtime;
    });
  }
  return musicRuntimePromise;
}
function openMusicDesktopSettings() {
  musicViewState={...musicViewState,screen:'desktop'};
  musicRuntime?.setViewState(musicViewState);
  dispatch({type:'navigate',view:'music'});
}
function openMusicPage() {
  if (musicRuntime) {
    musicRuntime.render(musicState);
    updateVisibility();
    return Promise.resolve(true);
  }
  const generation=++musicOpenGeneration;
  return loadMusicRuntime().then(runtime=>{
    if(generation!==musicOpenGeneration || state.view!=='music') {
      runtime.dispose();
      return false;
    }
    musicRuntime=runtime;
    runtime.render(musicState);
    updateVisibility();
    return true;
  }).catch(error=>{
    if(generation===musicOpenGeneration) showNotice(t('音乐模块加载失败，请重试','Could not load the music module. Please try again.'));
    return false;
  });
}
function closeMusicPage() {
  ++musicOpenGeneration;
  if (musicRuntime) {
    musicViewState=musicRuntime.close(musicState) || musicViewState;
    musicRuntime.dispose();
    musicRuntime=null;
  }
  musicRuntimePromise=null;
  musicState = {...musicState,lyrics:[]};
  musicLyricsRequestKey = "";
  musicNativeVisible = null;
}
function requestMusicLyrics() {
  const track=musicState.current;
  if(!musicRuntime || state.view!=="music" || !musicState.pageVisible || !state.visible || document.hidden || !track) return;
  const key=`${musicState.session}:${track.id}:${track.source}`;
  if(key===musicLyricsRequestKey) return;
  if(bridge.music("loadLyrics",{session:musicState.session,id:track.id})) musicLyricsRequestKey=key;
}
function dispatch(event) {
  if (event.type==="wallpaper-filter" || event.type==="wallpaper-page") wallpaperScroll.reset();
  const previousState=state;
  const previousWallpaper=state.wallpaper;
  state = reduce(state, event);
  if((event.type==='host'&&event.message.type==='island-notifications-state')||
     (['request','unavailable'].includes(event.type)&&event.scope==='island-notifications')){
    if(event.type==='host'&&state.islandNotificationsStatus!=='saving'){clearTimeout(islandNotificationTimeout);islandNotificationTimeout=null;}
    syncIslandNotifications(content,state);unmountChoice.refresh();return;
  }
  if(event.type==='host'&&event.message.type==='wallpaper-state'&&!previousWallpaper.catalog.length&&state.wallpaper.catalog.length)
    state={...state,slide:rememberedHomeSlide(state)};
  if((event.type==='host'&&event.message.type==='dynamicIsland-state') ||
     (['request','unavailable'].includes(event.type)&&event.scope==='dynamicIsland')) {
    syncIslandControls(content,state);
    if(['error','unavailable','recovered'].includes(state.dynamicIslandStatus) &&
      !content.querySelector('[data-settings-scope="dynamicIsland"]'))showNotice(islandStatus(state));
    return;
  }
  if ((event.type==="host" && ["wallpaper-engagement-state","account-state"].includes(event.message.type)) ||
    event.type==="wallpaper-like-request") {
    syncWallpaperControls(content,state,wallpaperPreview.playing);
    return;
  }
  if(event.type==='host' && event.message.type==='music-desktop-state') {
    musicDesktopState=event.message;
    musicDesktopController?.receive(event.message);
    syncMusicDesktopControls(content,musicDesktopState);
    musicRuntime?.receive(event.message);
    return;
  }
  if(event.type==='host' && musicRuntime?.receive(event.message)) return;
  if (event.type === "host" && event.message.type === "music-state") {
    const previousKey=musicState.current ? `${musicState.session}:${musicState.current.id}:${musicState.current.source}` : "";
    musicState = normalizeMusicState(event.message, musicState);
    musicRuntime?.updateMusicState(musicState);
    if(previousKey!==`${musicState.session}:${musicState.current?.id || ""}:${musicState.current?.source || ""}`) musicLyricsRequestKey="";
    requestMusicLyrics();
    return;
  }
  if (event.type === "host" && event.message.type === "resource-snapshot") {
    return;
  }
  if (event.type === "host" && event.message.type === "music-score") {
    if(musicRuntime) {
      musicState=applyMusicScore(musicState,event.message);
      musicRuntime.updateMusicState(musicState);
    }
    return;
  }
  if (event.type === "host" && event.message.type === "music-lyrics") {
    if(musicRuntime) {
      musicState=applyMusicLyrics(musicState,event.message);
      musicRuntime.updateMusicState(musicState);
    }
    return;
  }
  if (event.type==="host" && event.message.type==="wallpaper-state" && wallpaperPendingRemove &&
    state.wallpaper.revision>wallpaperPendingRemove.revision) {
    if (event.message.error) wallpaperPendingRemove=null;
    else if (Object.hasOwn(event.message,"library") &&
      !state.wallpaper.library.some(item=>item.id===wallpaperPendingRemove.id)) {
      const selection=wallpaperPendingRemove.selection;
      wallpaperPendingRemove=null;
      if (state.view==="wallpaperDetail" && state.wallpaperSelection?.id===selection.id &&
        state.wallpaperSelection?.source===selection.source) {
        state=reduce(state,{type:"back"});
        render();
      } else syncWallpaperControls(content,state,wallpaperPreview.playing);
      showNotice(t("壁纸已从我的壁纸移除","Wallpaper removed from My wallpapers."));
      return;
    }
  }
  if (event.type==="host" && event.message.type==="wallpaper-progress") {
    if (previousWallpaper.download!==state.wallpaper.download)
      updateWallpaperDownloadControls(content,state);
  } else if ((event.type==="host" && event.message.type==="wallpaper-state") ||
    (["request","unavailable"].includes(event.type) && event.scope==="wallpaper") ||
    ["wallpaper-display","wallpaper-panel","wallpaper-apply-target"].includes(event.type)) {
    if (event.type==="host" && event.message.type==="wallpaper-state" &&
        state.wallpaper.download?.error==="WALLPAPER_DOWNLOAD_QUOTA" &&
        previousWallpaper.download?.error!=="WALLPAPER_DOWNLOAD_QUOTA") {
      notices.dismiss();modal.showQuota(state.wallpaperAccount.status==="signed-in");
    }
    syncWallpaperStats();
    if (state.view==="dynamicIsland") syncIslandControls(content,state);
    if ((state.view==="search" || state.view==="home") &&
      (previousWallpaper.catalog!==state.wallpaper.catalog ||
       previousWallpaper.library!==state.wallpaper.library)) render();
    else syncWallpaperControls(content,state,wallpaperPreview.playing);
    const feedback=wallpaperNotice(state);
    if (feedback && !["wallpaperLocal","wallpaperOfficial","wallpaperDetail"].includes(state.view)) showNotice(feedback);
    updateVisibility();
    if (event.type==="host" && event.message.type==="wallpaper-state") continueWallpaperPreview();
    ensureWallpaperCatalog();
  } else if (event.type === "carousel" && state.view === "home") {
    updateHomeCarousel(content, state);
    carouselTimer.dispose();
    updateVisibility();
  } else if (componentSettings.receive(event,previousState,state)) {
    return;
  } else if (event.type === "host" && event.message.type === "window") {
    app.classList.toggle("fullscreen", state.fullscreen);
    titlebar.update(state.fullscreen);
  } else render();
}
function syncNotesControls() {
  syncMusicDesktopControls(content, state.notes, "notes-toggle");
}
// The notes window is native; the card shows whether it or the desktop note is open.
function receiveNotes(message) {
  state = {...state, notes:{enabled:message.enabled || message.windowOpen, pending:false}};
  syncNotesControls();
}
function render() {
  interactionMotion.capture();
  radialShortcut.cancel();
  dockSummonShortcut.cancel();
  syncWallpaperStats();
  wallpaperScroll.capture(content);
  if (state.view !== "music") closeMusicPage();
  if (state.view !== "applicationSettings") applicationSettings.hide();
  stopWallpaperPreview();
  const previewItem=state.view==="wallpaperDetail" ? selectedWallpaper(state) : null;
  wallpaperPreviewIntent=previewItem && ["video","image"].includes(previewItem.kind)
    ? {selectionId:previewItem.id,wantsPlay:true,failed:false} : null;
  app.classList.toggle("fullscreen", state.fullscreen);
  titlebar.update(state.fullscreen);
  const homeRoot=content.querySelector('.HomeRoot:has(> .HomeHero)');
  if(homeRoot)rememberedHomeView={root:homeRoot,key:homeRoot.dataset.homeKey};
  switch (state.view) {
    case "applicationSettings":
      applicationSettings.show(applicationSettingsSection);
      break;
    case "home":
      {
        const key=JSON.stringify([t('首页','Home'),state.wallpaper.catalogStatus,
          wallpaperHomeItems(state).map(item=>[item.id,item.title,item.width,item.height,item.kind,wallpaperHeroImageUrl(item)])]);
        if(rememberedHomeView?.key===key){
          if(!content.contains(rememberedHomeView.root))content.replaceChildren(rememberedHomeView.root);
          updateHomeCarousel(content,state);
        }else{
          const template=document.createElement('template');template.innerHTML=home(state);
          const oldImage=rememberedHomeView?.root.querySelector('.preview.current img');
          const nextImage=template.content.querySelector('.preview.current img');
          if(oldImage&&nextImage&&oldImage.getAttribute('src')===nextImage.getAttribute('src'))nextImage.replaceWith(oldImage);
          content.replaceChildren(template.content);
          const root=content.querySelector('.HomeRoot');root.dataset.homeKey=key;
          rememberedHomeView={root,key};
        }
      }
      break;
    case "desktopBeauty":
      content.innerHTML = catalog(state, false);
      break;
    case "widgets":
      content.innerHTML = catalog({...state,musicDesktop:musicDesktopState}, true);
      break;
    case "dynamicIsland":
      content.innerHTML = componentSettings.render(state.view,state,tab);
      break;
    case "music":
      openMusicPage();
      break;
    case "wallpaperLocal":
    case "wallpaperOfficial":
      content.innerHTML = library(state);
      break;
    case "search":
      content.innerHTML = search({...state,musicDesktop:musicDesktopState});
      break;
    case "wallpaperDetail":
      content.innerHTML = wallpaperDetail(state);
      updateWallpaperDetailLayout(content);
      break;
    default: {
      const page=componentSettings.render(state.view,state,tab);
      if(page!==undefined){content.innerHTML=page;break;}
      const item = [...modules, ...widgets].find(item => item.title === unavailableTitle);
      content.innerHTML = unavailablePage(item ? localizedItem(item).title
        : unavailableTitle === "此功能" ? t("此功能","This feature") : unavailableTitle);
    }
  }
  wallpaperScroll.restore(content,state);
  componentSettings.syncScopes(['radial','dock','dynamicIsland'],state);
  unmountChoice.refresh();
  titlebar.setSurface(state.view==="music"?"music":"panel");
  syncTitlebarImmersive();
  syncDropZone();
  updateVisibility();
  ensureWallpaperCatalog();
}
function syncDropZone() {
  const next=state.view==="wallpaperLocal"?"local":state.view==="music"?"music":"none";
  if (next === activeDropZone) return;
  const action=next==="local"?"dropzone-local":next==="music"?"dropzone-music":"dropzone-none";
  if (bridge.send("window",action)) activeDropZone=next;
}
function updateClock() {
  updateClockPreview(
    content.querySelector(".clock-preview"),
    state.clock.style,
    new Date(),
    state.clock,
  );
}
// Timers exist only while their surface is visible; no polling while minimized.
const clockTimer = createClockPreviewTimer(updateClock);
const carouselTimer = createVisibilityTimer(
  () => dispatch({ type: "carousel", delta: 1, count: wallpaperHomeItems(state).length }),
  10000,
);
function preloadWallpaperAssets() {
  wallpaperPreloader.warm([...wallpaperPreloadTargets(state),...wallpaperHomePreloadTargets(state)]);
}
function updateVisibility() {
  const visible = document.visibilityState === "visible" && state.visible;
  unmountWallpaperImages.setVisible(visible);
  wallpaperPreloader.setVisible(visible);
  if(!visible)radialShortcut.cancel();
  musicRuntime?.setVisible(visible,musicState);
  if(musicRuntime && musicNativeVisible!==visible) {
    musicNativeVisible=visible;
    if(!visible)musicLyricsRequestKey="";
    if(visible)requestMusicLyrics();
  }
  const previewItem=selectedWallpaper(state);
  const canPreview=visible && state.view==="wallpaperDetail" && ["video","image"].includes(previewItem?.kind) &&
    !previewItem.missing && state.wallpaperStatus!=="unavailable";
  clockTimer.update({
    nativeVisible: state.visible,
    documentVisible: document.visibilityState === "visible",
    view: state.view,
    tab,
    previewOpen: !!content.querySelector(".clock-preview-disclosure")?.open,
  });
  carouselTimer.update(
    visible,
    state.view === "home" &&
      wallpaperHomeItems(state).length > 1 &&
      !matchMedia("(prefers-reduced-motion: reduce)").matches,
  );
  app.classList.toggle("suspended", !visible);
  wallpaperPreview.update({
    nativeVisible:state.visible,documentVisible:document.visibilityState==="visible",
    view:state.view,id:previewItem && !previewItem.missing ? previewItem.id : "",
  });
  if (wallpaperPreviewLease && (!canPreview ||
    previewItem?.id!==wallpaperPreviewLease.selectionId)) stopWallpaperPreview();
  if (canPreview && wallpaperPreviewIntent?.wantsPlay && !wallpaperPreviewIntent.failed &&
    wallpaperPreviewIntent.selectionId===previewItem.id && !wallpaperPreviewLease) beginWallpaperPreview(previewItem);
  if (visible) preloadWallpaperAssets();
}
function releaseHiddenPageResources() {
  // Hidden manager pages do not need decoded hero images. Browser disk cache
  // keeps the next restore fast while releasing the JS-owned Image references.
  wallpaperPreloader.setVisible(false);
  if(rememberedHomeView&&!content.contains(rememberedHomeView.root))rememberedHomeView=null;
}
function stopWallpaperPreview() {
  const leased=wallpaperPreviewLease!==null;
  wallpaperPreviewLease=null;
  wallpaperPreview.stop();
  if (leased) bridge.wallpaper("endPreview");
}
function failWallpaperPreview(text) {
  if (wallpaperPreviewIntent) wallpaperPreviewIntent.failed=true;
  stopWallpaperPreview();
  showNotice(text);
}
function beginWallpaperPreview(item) {
  wallpaperPreviewLease={selectionId:item.id,revision:state.wallpaper.revision,started:false,
    retryAttempted:false,failedUrls:new Set(),assetSource:"",mediaUrl:""};
  if (!bridge.wallpaper("preview",{id:item.libraryItem?.id || item.id}))
    failWallpaperPreview(t("宿主未连接，无法播放预览","Host disconnected. Preview playback is unavailable."));
}
function wallpaperPreviewSurface() {
  return {
    nativeVisible:state.visible,
    documentVisible:document.visibilityState==="visible",
    view:state.view,
    id:selectedWallpaper(state)?.id || "",
  };
}
function handleWallpaperPreviewFailure(lease,media,source,failedUrl) {
  if (wallpaperPreviewLease!==lease) return;
  const failed=failedUrl || lease.mediaUrl;
  if (failed && !lease.failedUrls.has(failed)) {
    lease.failedUrls.add(failed);
    rememberWallpaperAssetFailure(source,failed);
  }
  const retry=wallpaperAssetPreferredUrl(source);
  if (!lease.retryAttempted && retry && retry!==failed) {
    lease.retryAttempted=true;
    lease.mediaUrl=retry;
    void playWallpaperPreviewAttempt(lease,media,source,retry);
    return;
  }
  failWallpaperPreview(t("预览播放失败，请检查网络或媒体文件","Preview playback failed. Check the network or media file."));
}
function playWallpaperPreviewAttempt(lease,media,source,url) {
  if (wallpaperPreviewLease!==lease) return;
  lease.assetSource=source;
  lease.mediaUrl=url;
  void wallpaperPreview.play({...media,url,assetSource:source},wallpaperPreviewSurface(),
    {reportError:false}).then(playing=>{
      if (wallpaperPreviewLease!==lease || lease.mediaUrl!==url) return;
      if (playing) rememberWallpaperAssetSuccess(source,url);
      else handleWallpaperPreviewFailure(lease,media,source,url);
    });
}
function continueWallpaperPreview() {
  const lease=wallpaperPreviewLease;
  if (!lease || state.wallpaper.revision<=lease.revision) return;
  const item=selectedWallpaper(state);
  if (!item || item.missing) {stopWallpaperPreview();return;}
  if(lease.started){
    // A refreshed catalog can replace an old full-size URL with a short clip;
    // a finished download can replace it with local media. Keep playback intent.
    const source=item.libraryItem&&!item.libraryItem.missing?item.libraryItem.url:item.url;
    if(source&&source!==lease.assetSource){
      stopWallpaperPreview();beginWallpaperPreview(item);
    }
    return;
  }
  if (state.wallpaper.error) {
    failWallpaperPreview(wallpaperNotice(state) || t("壁纸预览暂时不可用，请重试","Wallpaper preview is temporarily unavailable. Please try again."));
    return;
  }
  const media=item.libraryItem && !item.libraryItem.missing ? {...item,url:item.libraryItem.url} : item;
  if (!media.url) return;
  const source=media.url,candidate=wallpaperAssetPreferredUrl(source);
  if (!candidate) {
    failWallpaperPreview(t("预览播放失败，请检查网络或媒体文件","Preview playback failed. Check the network or media file."));
    return;
  }
  lease.started=true;
  playWallpaperPreviewAttempt(lease,media,source,candidate);
}
function ensureWallpaperCatalog() {
  if (!["home","wallpaperOfficial","wallpaperDetail","search","music"].includes(state.view) || state.wallpaperCatalogRequested ||
    ["loading","saving","unavailable"].includes(state.wallpaperStatus) || state.wallpaper.catalogStatus==="loading") return;
  state=reduce(state,{type:"wallpaper-catalog-requested"});
  if (state.wallpaper.catalogStatus==="idle" || state.wallpaper.catalogStatus==="failed") requestWallpaper("refresh");
  else if (state.wallpaper.catalogError==="WALLPAPER_CATALOG_CACHED") requestWallpaper("refreshVideos");
}
function syncWallpaperStats() {
  const id=state.view==="wallpaperDetail" ? selectedWallpaper(state)?.officialId || "" : "";
  if(id===wallpaperStatsId)return;
  wallpaperStatsId=id;
  state=reduce(state,{type:"wallpaper-engagement-reset"});
  bridge.wallpaper(id ? "stats" : "endStats",id ? {id} : {});
}
function requestWallpaper(action,fields={}) {
  if (state.wallpaperStatus==="saving" && !["cancel","stopAll"].includes(action)) return false;
  if (bridge.wallpaper(action,fields)) {
    dispatch({type:"request",scope:"wallpaper",key:action});
    return true;
  }
  dispatch({type:"unavailable",scope:"wallpaper"});
  return false;
}
function setWallpaperPanel(panel,{restoreFocus=false}={}) {
  const previous=state.wallpaperPanel;
  dispatch({type:"wallpaper-panel",panel});
  if (panel) {
    const overlay=content.querySelector(`[data-wallpaper-overlay="${panel}"]`);
    (panel==="targets" ? overlay?.querySelector("button:not(:disabled)") : overlay)?.focus({preventScroll:true});
  } else if (restoreFocus && previous) {
    content.querySelector(`[data-wallpaper-panel="${previous}"]`)?.focus({preventScroll:true});
  }
}
function closeWallpaperSelectMenus() {
  content.querySelectorAll(".wallpaper-select-menu:not([hidden])").forEach(menu => {
    menu.hidden = true;
    menu.closest(".wallpaper-select")?.querySelector(".wallpaper-select-trigger")?.setAttribute("aria-expanded","false");
  });
}
function wallpaperAction(button) {
  const action=button.dataset.wallpaperAction;
  if (action==="preview") {
    if (wallpaperPreview.playing || wallpaperPreviewLease) {
      if (wallpaperPreviewIntent) wallpaperPreviewIntent.wantsPlay=false;
      stopWallpaperPreview();
    }
    else {
      const item=selectedWallpaper(state);
      if (!item) return;
      wallpaperPreviewIntent={selectionId:item.id,wantsPlay:true,failed:false};
      updateVisibility();
    }
  } else if (action==="remove") {
    if (wallpaperPreviewIntent) wallpaperPreviewIntent.wantsPlay=false;
    stopWallpaperPreview();
    wallpaperPendingRemove={id:button.dataset.wallpaperId,revision:state.wallpaper.revision,
      selection:{...state.wallpaperSelection}};
    if (!requestWallpaper("remove",{id:button.dataset.wallpaperId})) wallpaperPendingRemove=null;
  } else if (action==="download") requestWallpaper(action,{id:button.dataset.wallpaperId});
  else if (action==="like") {
    const item=selectedWallpaper(state);
    if (!canLikeWallpaper(state,item) || button.dataset.wallpaperId!==item.officialId)return;
    if (bridge.wallpaper("like",{id:item.officialId,value:!selectedWallpaperEngagement(state,item).liked}))
      dispatch({type:"wallpaper-like-request",id:item.officialId});
    else dispatch({type:"unavailable",scope:"wallpaper"});
  }
  else if (action==="apply" || action==="applyAll") {
    state=reduce(state,{type:"wallpaper-apply-target",target:action==="applyAll"?"all":"display"});
    if (action==="apply") state=reduce(state,{type:"wallpaper-display",display:button.dataset.wallpaperDisplay});
    setWallpaperPanel(null,{restoreFocus:true});
    requestWallpaper(action,{id:button.dataset.wallpaperId,
      ...(action==="apply"?{display:state.wallpaperDisplay}:{})});
  }
  else if (["pause","resume","stop"].includes(action)) {
    setWallpaperPanel(null,{restoreFocus:true});
    requestWallpaper(action,{display:button.dataset.wallpaperDisplay || state.wallpaperDisplay});
  }
  else if (action==="pauseFullscreen") requestWallpaper(action,{value:!state.wallpaper.pauseFullscreen});
  else if (["refresh","import","stopAll","cancel"].includes(action)) requestWallpaper(action);
}
function request(scope, key, ...values) {
  if ((scope === 'clock' ? state.status : state[`${scope}Status`]) === 'saving') return;
  if (['taskbar','minimize','radial','dock','dynamicIsland'].includes(scope) && state[`${scope}Status`]==='loading') return;
  if (bridge.send(scope, key, ...values))
    dispatch({ type: "request", scope, key, value:values[0] });
  else dispatch({ type: "unavailable", scope });
}
content.addEventListener("click", (event) => {
  const button = event.target.closest("button");
  if (!button || button.disabled || button.getAttribute('aria-disabled')==='true') return;
  notices.dismiss();
  if (button.closest("[data-music-root]")) return;
  if (button.dataset.wallpaperSelectTrigger) {
    const selectRoot=button.closest("[data-wallpaper-select]");
    const menu=selectRoot?.querySelector(".wallpaper-select-menu");
    const open=!!menu && !menu.hidden;
    closeWallpaperSelectMenus();
    if (menu) {
      menu.hidden=open;
      button.setAttribute("aria-expanded",String(!open));
    }
  } else if (button.dataset.wallpaperOption) {
    const selectRoot=button.closest("[data-wallpaper-select]");
    const select=selectRoot?.querySelector(".wallpaper-select-native");
    if (select && !select.disabled) {
      select.value=button.dataset.value;
      select.dispatchEvent(new Event("change",{bubbles:true}));
    }
    closeWallpaperSelectMenus();
  } else if (button.dataset.wallpaperPanel) {
    setWallpaperPanel(state.wallpaperPanel===button.dataset.wallpaperPanel?null:button.dataset.wallpaperPanel,
      {restoreFocus:true});
  } else if (button.dataset.wallpaperAction) {
    wallpaperAction(button);
  } else if (button.dataset.wallpaperPage !== undefined) {
    dispatch({type:"wallpaper-page",page:Number(button.dataset.wallpaperPage)});
  } else if (button.dataset.wallpaperId && button.dataset.wallpaperSource) {
    state=reduce(state,{type:"wallpaper-select",source:button.dataset.wallpaperSource,id:button.dataset.wallpaperId});
    dispatch({type:"navigate",view:"wallpaperDetail"});
  } else if (button.dataset.view) {
    tab = button.dataset.view === "todayTodo" ? "todo" : button.dataset.view === "dock" ? "dock" : "settings";
    dispatch({ type: "navigate", view: button.dataset.view });
  } else if (button.dataset.action === "back") {
    tab = "settings";
    dispatch({ type: "back" });
  } else if (button.dataset.slide)
    dispatch({
      type: "carousel",
      delta: Number(button.dataset.slide),
      count: wallpaperHomeItems(state).length,
    });
  else if (button.dataset.dot !== undefined)
    dispatch({
      type: "carousel",
      delta: Number(button.dataset.dot) - state.slide,
      count: wallpaperHomeItems(state).length,
    });
  else if (button.dataset.open) {
    const id = button.dataset.open;
    if(id==="music")openMusicDesktopSettings();
    else if(id==="notes"){if(!bridge.notes("openWindow"))showNotice(t("便签暂不可用","Notes are unavailable"));}
    else if (["widgets", "clock", "calendar", "performance", "spectrum", "almanac", "todayTodo", "weather","organizer","icons","taskbar","minimize","radial","dock","dynamicIsland"].includes(id)) {
      if(id==="dynamicIsland")tab="home";
      else if(id==="todayTodo")tab="todo";
      else if(id==="dock")tab="dock";
      else tab="settings";
      dispatch({ type: "navigate", view: id });
    }
    else {
      unavailableTitle =
        [...modules, ...widgets].find((i) => i.id === id)?.title || "此功能";
      dispatch({ type: "navigate", view: "unavailable" });
    }
  } else if (button.dataset.action === "notes-toggle") {
    if (!state.notes.pending && bridge.notes("setActive", {value:!state.notes.enabled})) {
      state = {...state, notes:{...state.notes, pending:true}};
      syncNotesControls();
    }
  } else if(button.dataset.islandStyleWidget){
    if(islandStyleWidgets.includes(button.dataset.islandStyleWidget)){
      state={...state,islandStyleWidget:button.dataset.islandStyleWidget};syncIslandControls(content,state);
    }
  } else if(button.dataset.islandClockStyle){
    const style=button.dataset.islandClockStyle;
    if(['simple','progress','digital','date','flip','pixel'].includes(style)&&style!==(state.dynamicIsland.clockStyle??'simple'))request('dynamicIsland','clockStyle',style);
  } else if(button.dataset.islandCalendarStyle){
    const style=button.dataset.islandCalendarStyle;
    if(['default','chinese-almanac'].includes(style)&&style!==(state.dynamicIsland.calendarStyle??'default'))request('dynamicIsland','calendarStyle',style);
  } else if(button.dataset.islandCountdownStyle){
    const style=button.dataset.islandCountdownStyle;
    if(['standard','violet'].includes(style)&&style!==(state.dynamicIsland.countdownStyle??'standard'))request('dynamicIsland','countdownStyle',style);
  } else if (button.dataset.islandPreference) {
    const key=button.dataset.islandPreference;
    if(['material','size','animationSpeed','bounce'].includes(key)&&state.dynamicIsland[key]!==button.dataset.value)request('dynamicIsland',key,button.dataset.value);
  } else if (button.dataset.islandScreen!==undefined) {
    const screen=Number(button.dataset.islandScreen);
    if(Number.isInteger(screen)&&screen>=0&&screen<=64&&screen!==state.dynamicIsland.screen)request('dynamicIsland','screen',screen);
  } else if (button.dataset.islandExpansion) {
    const mode=button.dataset.islandExpansion;
    if(['click','hover'].includes(mode)&&state.dynamicIsland.hover!==(mode==='hover'))request('dynamicIsland','hover',mode==='hover');
  } else if (button.matches('button[data-island-shape]')) {
    if(button.dataset.islandShape!==state.dynamicIsland.shape)request('dynamicIsland','shape',button.dataset.islandShape);
  } else if (button.dataset.dockScreen!==undefined) {
    const screen=Number(button.dataset.dockScreen);
    if(Number.isInteger(screen)&&screen>=0&&screen<=state.dock.screenCount&&screen!==state.dock.screen)request('dock','screen',screen);
  } else if (button.dataset.dockRunningPreview!==undefined) {
    const preview=button.dataset.dockRunningPreview==='true';
    if(state.dock.showRunning&&preview!==state.dock.runningPreview)request('dock','runningPreview',preview);
  } else if (button.dataset.dockMaterial) {
    if(button.dataset.dockMaterial!==state.dock.material)request('dock','material',button.dataset.dockMaterial);
  } else if (button.dataset.dockPosition) {
    if(button.dataset.dockPosition!==state.dock.positionMode)request('dock','positionMode',button.dataset.dockPosition);
  } else if (componentCommands.action(button.dataset.action)) return;
  else if (button.dataset.action === "music-toggle")
    ensureMusicDesktopController().then(controller=>{
      controller.setEnabled(!musicDesktopState.enabled);
    });
  else if (button.dataset.action === "clear") {
    const input = button.closest("form").querySelector("input");
    input.value = "";
    input.focus();
    button.hidden = true;
    if (["wallpaperLocal","wallpaperOfficial"].includes(state.view)) {
      dispatch({type:"wallpaper-filter",query:""});
      content.querySelector(".SearchInput")?.focus();
    }
  } else if (button.dataset.tab) {
    tab = button.dataset.tab;
    render();
    if(state.view==='dynamicIsland'&&tab==='notifications')sendIslandNotificationCommand({version:1,scope:'island-notifications',action:'snapshot'});
  }
});
content.addEventListener("pointerdown", (event) => {
  if (event.button !== 0 || !isWindowDragTarget(event.target)) return;
  if (bridge.send("window", "drag")) event.preventDefault();
}, true);
document.addEventListener("click",event=>{
  if (!event.target.closest("[data-wallpaper-select]")) closeWallpaperSelectMenus();
});
document.addEventListener("click",event=>{
  const panel=state.wallpaperPanel;
  if (!panel || state.view!=="wallpaperDetail" ||
    event.target.closest("[data-wallpaper-panel], [data-wallpaper-overlay]")) return;
  setWallpaperPanel(null,{restoreFocus:true});
});
document.addEventListener("keydown",event=>{
  if(state.view==='music'&&event.key==='Escape'&&!event.defaultPrevented&&!event.repeat&&
    !document.querySelector('dialog[open]')&&exitMusicFullscreen()){
    event.preventDefault();
    return;
  }
  if (state.view!=="wallpaperDetail" || !state.wallpaperPanel) return;
  if (event.key==="Escape") {
    event.preventDefault();
    setWallpaperPanel(null,{restoreFocus:true});
  } else if (state.wallpaperPanel==="targets" && ["ArrowDown","ArrowUp","Home","End"].includes(event.key)) {
    const buttons=[...content.querySelectorAll("#wallpaper-target-menu button:not(:disabled)")];
    if (!buttons.length) return;
    event.preventDefault();
    const current=buttons.indexOf(document.activeElement);
    const index=event.key==="Home"?0:event.key==="End"?buttons.length-1:
      (current+(event.key==="ArrowDown"?1:-1)+buttons.length)%buttons.length;
    buttons[index].focus();
  }
});
content.addEventListener("input", (event) => {
  if(event.target.matches('[data-island-hover-delay]')){
    if(event.target.disabled||event.target.getAttribute('aria-disabled')==='true')syncIslandControls(content,state);
    else previewIslandHoverDelay(event.target,event.target.valueAsNumber);
    return;
  }
  if(componentCommands.input(event.target))return;
  if (event.target.matches(".SearchInput"))
    event.target.closest("form").querySelector(".SearchClear").hidden =
      !event.target.value;
});
content.addEventListener("submit", (event) => {
  if (!event.target.matches(".ModuleSearch")) return;
  event.preventDefault();
  const query = event.target.querySelector("input").value.trim();
  if (["wallpaperLocal","wallpaperOfficial"].includes(state.view)) dispatch({type:"wallpaper-filter",query});
  else if (query) dispatch({ type: "navigate", view: "search", query });
});
content.addEventListener("change", (event) => {
  if(event.target.id==='dock-edge'){
    const edge=event.target.value;
    if(event.target.disabled||!['top','bottom','left','right'].includes(edge)){syncDockControls(content,state);return;}
    if(edge!==(state.dock.edge??'bottom'))request('dock','edge',edge);
    return;
  }
  if(event.target.matches('[data-island-hover-delay]')){
    const slider=event.target,value=slider.valueAsNumber;
    if(slider.disabled||slider.getAttribute('aria-disabled')==='true'||!validIslandHoverDelay(value)){syncIslandControls(content,state);return;}
    if(value!==(state.dynamicIsland.hoverDelayMs??300)){
      slider.dataset.submitted=String(value);request('dynamicIsland','hoverDelayMs',value);
    }
    return;
  }
  if(componentCommands.change(event.target))return;
  if (event.target.id === "wallpaper-kind") {
    dispatch({type:"wallpaper-filter",kind:event.target.value});
  }
  if (event.target.id==="wallpaper-page-jump") {
    const value=event.target.valueAsNumber;
    if (Number.isInteger(value)) dispatch({type:"wallpaper-page",page:Math.min(Number(event.target.max),Math.max(1,value))});
  }
});
content.addEventListener("error",event=>{
  if (event.target.matches?.(".wallpaper-preview-video")) {
    const lease=wallpaperPreviewLease,item=selectedWallpaper(state);
    if (lease && item) {
      const media=item.libraryItem && !item.libraryItem.missing ? {...item,url:item.libraryItem.url} : item;
      handleWallpaperPreviewFailure(lease,media,lease.assetSource || media.url,
        lease.mediaUrl || event.target.currentSrc || event.target.src);
    } else failWallpaperPreview(t("预览播放失败，请检查网络或媒体文件","Preview playback failed. Check the network or media file."));
  }
},true);
// Native <details> toggle events do not bubble; capture also covers new pages.
content.addEventListener(
  "toggle",
  (event) => {
    if (event.target.matches(".clock-preview-disclosure")) updateVisibility();
  },
  true,
);
document.addEventListener("visibilitychange", updateVisibility);
const unsubscribe = bridge.subscribe((message) => {
  interactionMotion.capture();
  if(message.type==='dynamicIsland-preview')return;
  if(message.type==='todo-board-state'){todoBoards.receive(message);return;}
  if (DIAGNOSTICS_ENABLED) {
  const previousEvents=new Set(diagnosticsRecorder.list().map(event=>
    `${event.type}|${event.firstAt}|${JSON.stringify(event.details)}`));
  const previousNativeEvents=new Set(diagnosticsRecorder.native().events.map(event=>
    `${event.module||''}|${event.kind}|${event.firstAt||event.at}|${event.error||''}|${event.expected||''}|${event.actual||''}`));
  diagnosticsRecorder.record(message);
  const newLocalIssue=diagnosticsRecorder.list().find(event=>{
    const key=`${event.type}|${event.firstAt}|${JSON.stringify(event.details)}`;
    return !previousEvents.has(key) &&
      (event.details?.error||event.details?.catalogError||event.details?.kind==='effect-mismatch');
  });
  const newNativeIssue=diagnosticsRecorder.native().events.find(event=>{
    if(event.kind!=='error'&&event.kind!=='effect-mismatch')return false;
    const key=`${event.module||''}|${event.kind}|${event.firstAt||event.at}|${event.error||''}|${event.expected||''}|${event.actual||''}`;
    return !previousNativeEvents.has(key);
  });
  let showedMismatchNotice=false;
  if(message.type==='diagnostics-state'||message.type?.endsWith('-state')) {
    const current=diagnosticsRecorder.mismatches();
    const currentKeys=new Set(current.map(item=>JSON.stringify([item.component,item.expected,item.actual])));
    const newlyDetected=[];
    for(const mismatch of current) {
      const key=JSON.stringify([mismatch.component,mismatch.expected,mismatch.actual]);
      if(!announcedDiagnosticMismatches.has(key)) {
        announcedDiagnosticFaults.add([mismatch.component,'effect-mismatch',mismatch.expected,mismatch.actual].join('|'));
        newlyDetected.push(mismatch);
      }
      announcedDiagnosticMismatches.add(key);
    }
    if(newlyDetected.length) {
      showedMismatchNotice=true;
      const mismatch=newlyDetected[0];
      const component=({dock:'Dock',taskbar:t('任务栏','Taskbar'),wallpaper:t('壁纸','Wallpaper')})[mismatch.component]||mismatch.component;
      showNotice(newlyDetected.length>1
        ? t(`诊断发现 ${newlyDetected.length} 个效果不一致，包括 ${component}（当前状态：${mismatch.actual}）`,
          `Diagnostics found ${newlyDetected.length} effect mismatches, including ${component} (observed: ${mismatch.actual}).`)
        : t(`诊断发现 ${component} 已启用，但当前状态与预期不符（${mismatch.actual}）`,
          `Diagnostics found ${component} enabled, but its observed state differs from the expected effect (${mismatch.actual}).`));
    }
    for(const key of [...announcedDiagnosticMismatches])if(!currentKeys.has(key))announcedDiagnosticMismatches.delete(key);
    for(const key of [...announcedDiagnosticFaults]) {
      const parts=key.split('|');
      if(parts[1]==='effect-mismatch'&&!currentKeys.has(JSON.stringify([parts[0],parts[2],parts[3]])))
        announcedDiagnosticFaults.delete(key);
    }
  }
  const issue=newLocalIssue ? {
    module:newLocalIssue.type.replace(/-state$/,''),kind:newLocalIssue.details?.kind||'error',
    expected:newLocalIssue.details?.expected||'',actual:newLocalIssue.details?.actual||'',
    error:newLocalIssue.details?.error||newLocalIssue.details?.catalogError||newLocalIssue.details?.status||'',
  } : newNativeIssue;
  const issueKey=issue?(issue.kind==='effect-mismatch'
    ? [issue.module||'',issue.kind||'',issue.expected||'',issue.actual||''].join('|')
    : [issue.module||'',issue.kind||'',issue.expected||'',issue.actual||'',issue.error||''].join('|')):'';
  if(issue&&!showedMismatchNotice&&!announcedDiagnosticFaults.has(issueKey)) {
    announcedDiagnosticFaults.add(issueKey);
    showNotice(t('Lume 发现组件运行异常，诊断记录已保存。打开“诊断与维护”查看详情。',
      'Lume detected a component fault and saved diagnostic details. Open Diagnostics & maintenance to review them.'));
  }
  if(message.type==='diagnostics-state'&&diagnosticsRecorder.native().events.length===0&&
      diagnosticsRecorder.native().mismatches.length===0)announcedDiagnosticFaults.clear();
  }
  if (message.type==="wallpaper-engagement-notice") {
    if (message.error === "WALLPAPER_DOWNLOAD_STATS_QUOTA") {
      notices.dismiss();
      modal.showQuota(state.wallpaperAccount.status === "signed-in");
    } else showNotice(wallpaperEngagementError(message.error));
    return;
  }
  if (message.type === "app-update-notice") {
    if (message.manual !== true) return;
    notices.dismiss();
    modal.showUpdateResult(message);
    return;
  }
  if (message.type==="account-state") {
    dispatch({type:"host",message});
    modal.syncAccount(state.wallpaperAccount.status === "signed-in");
  }
  if (message.type === "notes-state") {
    receiveNotes(message);
    return;
  }
  if (applicationSettings.handleMessage(message)) {
    if (message.type === "account-state") {
      titlebar.updateAccount(message);
    }
  } else if (message.type === "navigate") {
    if (message.page === "applicationSettings") {
      showApplicationSettings(message.section);
      return;
    }
    if(message.page==="music") {
      if(message.section==="desktop"){openMusicDesktopSettings();return;}
      dispatch({type:"navigate",view:"music"});
      return;
    }
    tab = message.page==="dynamicIsland" ? "home" : message.page==="todayTodo" ? "todo" : message.page==="dock" ? "dock" : "settings";
    if(message.page==='dynamicIsland'&&message.section)state={...state,islandStyleWidget:message.section};
    if (state.view !== message.page) {
      state = {...state, view:message.page, history:["icons","taskbar","minimize","radial","dock","dynamicIsland"].includes(message.page)?["home","desktopBeauty"]:["home","desktopBeauty","widgets"]};
      state.musicReturn=null;
    }
    render();
    if(message.page==='dynamicIsland'&&message.section)content.querySelector('[data-island-styles]')?.scrollIntoView({block:'nearest'});
    if(message.page==='radial'&&message.section==='shortcut')radialShortcut.start({waitForState:true});
  } else if (message.type === "visibility") {
    state = reduce(state, { type: "host", message });
    updateVisibility();
    if (!message.visible && state.visible === false &&
      state.visibilityGeneration === message.generation)
      visibilityCleanup.acknowledge(message.generation);
  } else dispatch({ type: "host", message });
});
window.addEventListener("beforeunload", () => {
  clearTimeout(islandNotificationTimeout);unmountIslandNotifications();
  interactionMotion.dispose();
  wallpaperPreloader.dispose();rememberedHomeView=null;
  unmountExpandedWidgets();
  todoBoards.dispose();
  notices.dispose();
  modal.dispose();
  wallpaperPreviewIntent=null;
  unmountWallpaperImages();
  unmountWallpaperDrop();
  unmountMusicDrop();
  closeMusicPage();
  musicDesktopController?.dispose();
  unmountSelect();
  unmountChoice();
  unmountRanges();
  radialShortcut.dispose();
  clockTimer.dispose();
  carouselTimer.dispose();
  stopWallpaperPreview();
  if(wallpaperStatsId) {
    wallpaperStatsId="";
    bridge.wallpaper("endStats");
  }
  applicationSettings.dispose();
  titlebar.dispose();
  unsubscribe();
});
render();
initialized = true;
prepareInitialView(content).then(() => {
  if (!bridge.send("ready")) dispatch({ type: "unavailable", scope:"all" });
}).catch(() => {
  // Keep the native startup deadline active; its visible error path handles
  // missing local assets instead of declaring an incomplete first page ready.
  showNotice(t("首页资源加载失败，请检查安装包内的界面资源。","Could not load home page resources. Check the UI resources in the installation package."));
});
