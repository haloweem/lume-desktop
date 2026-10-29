import {enterMusic, leaveMusic} from "./music-navigation.js";

const emptyWallpaperEngagement = (revision = -1) => ({
  revision,id:"",status:"idle",likes:null,downloads:null,liked:false,canLike:false,busy:false,error:"",
});
const leavingWallpaperEngagement = (state,view) => state.view==="wallpaperDetail" && view!=="wallpaperDetail"
  ? emptyWallpaperEngagement(state.wallpaperEngagement.revision) : state.wallpaperEngagement;

export const initialState = () => ({
  view: "home",
  history: [],
  musicReturn: null,
  slide: 0,
  query: "",
  clock: { enabled: false, style: "welfare" },
  notes: { enabled: false, pending: false },
  calendar: { enabled:false,theme:"spacious",material:"frosted-glass",textColor:"#ffffff",highlightColor:"#0a84ff",locked:false,running:false },
  calendarStatus: "loading",
  calendarError: "",
  calendarLoadStatus: "loaded",
  reminderAvailable: null,
  calendarMaterialStatus: "inactive",
  performance: {enabled:false,running:false,locked:false,material:"frosted-glass",width:340,height:216},
  performanceStatus:"loading",
  performanceError:"",
  performanceLoadStatus:"fresh",
  performanceMaterialStatus:"inactive",
  spectrum: {enabled:false,running:false,locked:false,width:880,height:120,style:"pulse",scale:100,
    pulse:{barWidth:9,barGap:1},blocks:{barWidth:4,barGap:1},waves:{barWidth:5,barGap:1}},
  spectrumStatus:"loading",
  spectrumError:"",
  spectrumLoadStatus:"fresh",
  resourceSnapshot:null,
  todayTodo:{enabled:false,running:false,locked:false,material:"frosted-glass",width:320,height:152},
  todayTodoStatus:"loading",todayTodoError:"",todayTodoLoadStatus:"fresh",todayTodoMaterialStatus:"inactive",
  almanac: {enabled:false,running:false,locked:false,material:"frosted-glass",width:320,height:152},
  almanacStatus:"loading",
  almanacError:"",
  almanacLoadStatus:"fresh",
  almanacMaterialStatus:"inactive",
  weather: {enabled:false,running:false,locked:false,material:"frosted-glass",city:"auto",width:320,height:152},
  weatherStatus:"loading",
  weatherError:"",
  weatherLoadStatus:"fresh",
  weatherMaterialStatus:"inactive",
  weatherData:{loading:false,error:"",hasWeather:false,cacheWriteFailed:false,systemLocation:"unknown"},
  organizer:{enabled:false,running:false,locked:false,material:"frosted-glass",width:420,height:392,title:"桌面收纳",count:0,mode:"expanded",layout:"icons",iconScale:70,
    collections:[{id:"default",title:"桌面收纳",enabled:false,running:false,count:0}]},
  organizerStatus:"loading",
  organizerPending:null,
  organizerError:"",
  organizerLoadStatus:"fresh",
  organizerMaterialStatus:"inactive",
  icons:{enabled:false,running:false,enableAutoHide:false,autoHideDelay:5,showIconsOnAnyInput:true,
    manuallyHidden:false,desktops:0,autoHidden:0,pendingRestores:0},
  iconsStatus:"loading",iconsError:"",iconsLoadStatus:"fresh",
  taskbar:{enabled:false,running:false,transparentTaskbar:true,autoHideTaskbar:false,hideBehavior:'auto-hide',
    applying:false,pendingRecovery:false,taskbars:0,mode:"unavailable"},
  taskbarStatus:"loading",taskbarError:"",taskbarLoadStatus:"fresh",taskbarRevision:0,taskbarRequestRevision:null,taskbarPending:null,
  minimize:{enabled:false,running:false,durationMs:450,restore:true,launch:true,multiMonitor:false,
    unhideTaskbar:true,unhideDurationMs:450,engine:"modern",x64:false,x86:false,hosts:0,hooks:0,workers:0,systemError:0},
  minimizeStatus:"loading",minimizeError:"",minimizeLoadStatus:"fresh",
  radial:{enabled:false,running:false,visible:false,material:"liquid-glass",shortcutCode:9},
  radialStatus:"loading",radialError:"",radialLoadStatus:"fresh",radialMaterialStatus:"inactive",
  dock:{enabled:false,running:false,material:"liquid-glass",reflection:false,iconScale:100,position:50,bottomOffset:0,iconSpacing:10,launchpadScale:100,launchpadIconScale:100,pinned:0,added:0},
  dockStatus:"loading",dockError:"",dockLoadStatus:"fresh",dockMaterialStatus:"inactive",
  dynamicIsland:{enabled:false,running:false,locked:false,expanded:false,focusing:false,finished:false,remaining:0,
    hideFullscreen:true,topmost:true,hover:false,autoHide:true,size:'standard',material:'none'},
  dynamicIslandStatus:"loading",dynamicIslandError:"",dynamicIslandLoadStatus:"fresh",
  wallpaper:{revision:-1,status:"idle",error:"",loadStatus:"fresh",playerAvailable:false,
    catalogStatus:"idle",catalogError:"",catalog:[],library:[],likedIds:[],displays:[],
    download:{id:"",status:"idle",received:0,total:0,error:""},pauseFullscreen:false},
  wallpaperStatus:"loading",wallpaperRequestRevision:null,wallpaperCatalogRequested:false,
  wallpaperEngagement:emptyWallpaperEngagement(),
  wallpaperAccount:{revision:-1,status:"signed-out",id:""},
  wallpaperBrowse:{local:{kind:"all",query:"",page:1},official:{kind:"video",query:"",page:1}},
  wallpaperSelection:null,wallpaperDisplay:"",wallpaperSearchPage:1,wallpaperPanel:null,wallpaperApplyTarget:"display",
  status: "loading",
  visible: true,
  fullscreen: false,
  visibilityGeneration: 0,
});
// A requested switch position is immediate feedback, not confirmation that
// Windows applied it. Keep the last confirmed host snapshot until settlement.
export function taskbarDisplayedPreferences(state) {
  return state.taskbarStatus==='saving' && state.taskbarPending
    ? {...state.taskbar,...state.taskbarPending} : state.taskbar;
}
export function organizerDisplayedEnabled(state) {
  return state.organizerStatus==='saving' && typeof state.organizerPending==='boolean'
    ? state.organizerPending : state.organizer.enabled;
}
const views = [
  "home",
  "wallpaperLocal",
  "wallpaperOfficial",
  "desktopBeauty",
  "widgets",
  "clock",
  "calendar",
  "performance",
  "spectrum",
  "almanac",
  "todayTodo",
  "weather",
  "organizer",
  "icons",
  "taskbar",
  "minimize",
  "radial",
  "dock",
  "dynamicIsland",
  "search",
  "wallpaperDetail",
  "music",
  "applicationSettings",
  "unavailable",
];
export function reduce(state, event) {
  switch (event.type) {
    case "wallpaper-catalog-requested":
      return {...state,wallpaperCatalogRequested:true};
    case "wallpaper-select":
      return ["local","official"].includes(event.source) && typeof event.id === "string"
        ? {...state,wallpaperSelection:{source:event.source,id:event.id},wallpaperPanel:null,
          wallpaperEngagement:state.wallpaperSelection?.source===event.source && state.wallpaperSelection?.id===event.id
            ? state.wallpaperEngagement : emptyWallpaperEngagement(state.wallpaperEngagement.revision)} : state;
    case "wallpaper-engagement-reset":
      return {...state,wallpaperEngagement:emptyWallpaperEngagement(state.wallpaperEngagement.revision)};
    case "wallpaper-like-request":
      return state.wallpaperEngagement.id===event.id && state.wallpaperEngagement.status==="ready" &&
        state.wallpaperEngagement.canLike && !state.wallpaperEngagement.busy
        ? {...state,wallpaperEngagement:{...state.wallpaperEngagement,busy:true}} : state;
    case "wallpaper-panel":
      return [null,"settings","targets"].includes(event.panel) ? {...state,wallpaperPanel:event.panel} : state;
    case "wallpaper-apply-target":
      return ["display","all"].includes(event.target) ? {...state,wallpaperApplyTarget:event.target} : state;
    case "wallpaper-display":
      return state.wallpaper.displays.some(display=>display.id === event.display)
        ? {...state,wallpaperDisplay:event.display} : state;
    case "wallpaper-filter": {
      const key=state.view === "wallpaperOfficial" ? "official" : "local";
      const current=state.wallpaperBrowse[key];
      const kind=["all","video","image"].includes(event.kind) ? event.kind : current.kind;
      const query=typeof event.query === "string" ? event.query : current.query;
      return {...state,wallpaperBrowse:{...state.wallpaperBrowse,[key]:{kind,query,page:1}}};
    }
    case "wallpaper-page": {
      if (!Number.isSafeInteger(event.page) || event.page < 1) return state;
      if (state.view === "search") return {...state,wallpaperSearchPage:event.page};
      const key=state.view === "wallpaperOfficial" ? "official" : "local";
      return {...state,wallpaperBrowse:{...state.wallpaperBrowse,[key]:{...state.wallpaperBrowse[key],page:event.page}}};
    }
    case "navigate":
      if (event.view === "music") return enterMusic(state.view==="wallpaperDetail"
        ? {...state,wallpaperEngagement:leavingWallpaperEngagement(state,"music")} : state);
      if (state.view === "music" && event.view !== "applicationSettings") {
        const panel={...leaveMusic(state),history:[...(state.musicReturn?.history || []),state.musicReturn?.view || "home"]};
        return views.includes(event.view)?{...panel,view:event.view,musicReturn:null,
          query:event.query ?? panel.query,wallpaperSearchPage:event.query!==undefined?1:panel.wallpaperSearchPage}:state;
      }
      return views.includes(event.view)
        ? {
            ...state,
            view: event.view,
            wallpaperEngagement:leavingWallpaperEngagement(state,event.view),
            history: [...state.history, state.view],
            musicReturn:state.view === "music" && event.view !== "applicationSettings" ? null : state.musicReturn,
            query: event.query ?? state.query,
            wallpaperSearchPage:event.query !== undefined ? 1 : state.wallpaperSearchPage,
          }
        : state;
    case "music-leave":
      return leaveMusic(state);
    case "back":
      {
        const view=state.history.at(-1) || "home";
        return {
        ...state,
        view,
        wallpaperEngagement:leavingWallpaperEngagement(state,view),
        history: state.history.slice(0, -1),
        musicReturn:view==="music"?state.musicReturn:null,
      };
      }
    case "carousel":
      return event.count > 0
        ? {
            ...state,
            slide:
              (((state.slide + event.delta) % event.count) + event.count) %
              event.count,
          }
        : state;
    case "request":
      if(event.scope==="dynamicIsland")return {...state,dynamicIslandStatus:"saving"};
      if(event.scope==="radial")return {...state,radialStatus:"saving"};
      if(event.scope==="dock")return {...state,dockStatus:"saving"};
      if(event.scope==="wallpaper") return {...state,wallpaperStatus:"saving",wallpaperRequestRevision:state.wallpaper.revision};
      if(event.scope==="taskbar") {
        const property={enabled:'enabled',transparent:'transparentTaskbar',autoHide:'autoHideTaskbar'}[event.key];
        const pending=event.key==='reset'?{transparentTaskbar:true,autoHideTaskbar:false,hideBehavior:'auto-hide'}:
          event.key==='hideBehavior'&&['auto-hide','fully-hidden'].includes(event.value)?{hideBehavior:event.value}:
          property&&typeof event.value==='boolean'?{[property]:event.value}:null;
        return {...state,taskbarStatus:"saving",taskbarRequestRevision:state.taskbarRevision,taskbarPending:pending};
      }
      if(event.scope==="minimize")return {...state,minimizeStatus:"saving"};
      if(event.scope==="icons")return {...state,iconsStatus:"saving"};
      if (event.scope === "weather") return {...state,weatherStatus:"saving"};
      if (event.scope === "organizer") return {...state,organizerStatus:"saving",
        organizerPending:event.key==='enabled'&&typeof event.value==='boolean'?event.value:null};
      if (event.scope === "almanac") return {...state,almanacStatus:"saving"};
      if (event.scope === "todayTodo") return {...state,todayTodoStatus:"saving"};
      if (event.scope === "performance") return {...state,performanceStatus:"saving"};
      if (event.scope === "spectrum") return {...state,spectrumStatus:"saving"};
      if (event.scope === "calendar") return { ...state, calendarStatus:"saving" };
      return { ...state, status: "saving" };
    case "unavailable":
      if(event.scope==="dynamicIsland")return {...state,dynamicIslandStatus:"unavailable"};
      if(event.scope==="radial")return {...state,radialStatus:"unavailable"};
      if(event.scope==="dock")return {...state,dockStatus:"unavailable"};
      if(event.scope==="wallpaper")return {...state,wallpaperStatus:"unavailable",wallpaperRequestRevision:null,
        wallpaperEngagement:emptyWallpaperEngagement(state.wallpaperEngagement.revision)};
      if(event.scope==="taskbar")return {...state,taskbarStatus:"unavailable",taskbarRequestRevision:null,taskbarPending:null};
      if(event.scope==="minimize")return {...state,minimizeStatus:"unavailable"};
      if(event.scope==="icons")return {...state,iconsStatus:"unavailable"};
      if (event.scope === "weather") return {...state,weatherStatus:"unavailable"};
      if (event.scope === "organizer") return {...state,organizerStatus:"unavailable",organizerPending:null};
      if (event.scope === "almanac") return {...state,almanacStatus:"unavailable"};
      if (event.scope === "todayTodo") return {...state,todayTodoStatus:"unavailable"};
      if (event.scope === "performance") return {...state,performanceStatus:"unavailable",resourceSnapshot:null};
      if (event.scope === "spectrum") return {...state,spectrumStatus:"unavailable"};
      if (event.scope === "calendar") return { ...state, calendarStatus:"unavailable" };
      if (event.scope === "all") return { ...state, status:"unavailable",dynamicIslandStatus:"unavailable",radialStatus:"unavailable",dockStatus:"unavailable",calendarStatus:"unavailable",performanceStatus:"unavailable",resourceSnapshot:null,spectrumStatus:"unavailable",almanacStatus:"unavailable",todayTodoStatus:"unavailable",weatherStatus:"unavailable",organizerStatus:"unavailable",iconsStatus:"unavailable",taskbarStatus:"unavailable",taskbarRequestRevision:null,taskbarPending:null,minimizeStatus:"unavailable",wallpaperStatus:"unavailable",wallpaperRequestRevision:null,
        organizerPending:null,wallpaperEngagement:emptyWallpaperEngagement(state.wallpaperEngagement.revision) };
      return { ...state, status: "unavailable" };
    case "host": {
      const m = event.message;
      if(m.type==='dynamicIsland-state') {
        const {enabled,running,locked,expanded,focusing,finished,remaining,hideFullscreen,topmost,hover,autoHide,size,material}=m;
        const failed=!!m.error||m.loadStatus==='failed';
        const pending=state.dynamicIslandStatus==='saving'&&m.result==='snapshot'&&!failed;
        return {...state,dynamicIsland:{enabled,running,locked,expanded,focusing,finished,remaining,hideFullscreen,topmost,hover,autoHide,size,material},
          dynamicIslandError:m.error,dynamicIslandLoadStatus:m.loadStatus,
          dynamicIslandStatus:pending?'saving':failed?'error':m.saved?'saved':m.loadStatus==='recovered'?'recovered':'idle'};
      }
      if(m.type==="dock-state") {
        const failed=!!m.error||m.loadStatus==="failed";
        const pending=state.dockStatus==="saving"&&m.result==="snapshot"&&!failed;
        const {enabled,running,material,reflection,iconScale,position,bottomOffset,iconSpacing,pinned,added}=m;
        return {...state,dock:{enabled,running,material,reflection,iconScale,position,bottomOffset,iconSpacing,launchpadScale:m.launchpadScale??100,launchpadIconScale:m.launchpadIconScale??100,pinned,added},
          dockError:m.error,dockLoadStatus:m.loadStatus,dockMaterialStatus:m.materialStatus,
          dockStatus:pending?"saving":failed?"error":m.saved?"saved":m.loadStatus==="recovered"?"recovered":"idle"};
      }
      if(m.type==="radial-state") {
        const failed=!!m.error||m.loadStatus==="failed";
        const pending=state.radialStatus==="saving"&&m.result==="snapshot"&&!failed;
        const {enabled,running,visible,material,shortcutCode}=m;
        return {...state,radial:{enabled,running,visible,material,shortcutCode},
          radialError:m.error,radialLoadStatus:m.loadStatus,radialMaterialStatus:m.materialStatus,
          radialStatus:pending?"saving":failed?"error":m.saved?"saved":m.loadStatus==="recovered"?"recovered":"idle"};
      }
      if(m.type==="wallpaper-engagement-state") {
        if(m.revision<=state.wallpaperEngagement.revision)return state;
        const {version,type,...wallpaperEngagement}=m;
        return {...state,wallpaperEngagement};
      }
      if(m.type==="account-state") {
        if(m.revision<state.wallpaperAccount.revision)return state;
        const id=m.status==="signed-in" ? m.profile.id : "";
        const changed=id!==state.wallpaperAccount.id || m.status!==state.wallpaperAccount.status;
        return {...state,wallpaperAccount:{revision:m.revision,status:m.status,id},
          wallpaperEngagement:changed ? {...state.wallpaperEngagement,liked:false,canLike:false,busy:false}
            : state.wallpaperEngagement};
      }
      if(m.type==="wallpaper-progress") {
        const current=state.wallpaper.download;
        if(current.status!=="running" || current.id!==m.download.id ||
          m.download.received<current.received)return state;
        return {...state,wallpaper:{...state.wallpaper,download:{...m.download}}};
      }
      if(m.type==="wallpaper-state") {
        if(m.revision < state.wallpaper.revision)return state;
        if(state.wallpaper.revision<0 && (!m.catalog || !m.library))return state;
        const failed=!!m.error || m.loadStatus==="failed";
        const pending=m.status==="busy" || (state.wallpaperRequestRevision!==null &&
          m.revision<=state.wallpaperRequestRevision && !failed);
        const {version,type,...wallpaper}=m;
        const selected=m.displays.find(display=>display.id===state.wallpaperDisplay) ||
          m.displays.find(display=>display.primary) || m.displays[0];
        return {...state,wallpaper:{...wallpaper,
          catalog:m.catalog ? m.catalog.map(item=>({...item})) : state.wallpaper.catalog,
          library:m.library ? m.library.map(item=>({...item})) : state.wallpaper.library,
          likedIds:m.likedIds ? [...m.likedIds] : state.wallpaper.likedIds,
          displays:m.displays.map(display=>({...display})),download:{...m.download}},
          wallpaperStatus:failed?"error":pending?"saving":"idle",
          wallpaperRequestRevision:pending?state.wallpaperRequestRevision:null,
          wallpaperDisplay:selected?.id || ""};
      }
      if(m.type==="taskbar-state") {
        if(m.revision<state.taskbarRevision)return state;
        const failed=!!m.error || m.loadStatus==="failed" || m.pendingRecovery || m.enabled!==m.running;
        const pending=m.applying || (state.taskbarRequestRevision!==null && m.revision<=state.taskbarRequestRevision && !failed);
        const completed=state.taskbarRequestRevision!==null && m.revision>state.taskbarRequestRevision && !pending && !failed && m.loadStatus==="loaded";
        const {enabled,running,transparentTaskbar,autoHideTaskbar,hideBehavior,applying,pendingRecovery,taskbars,mode}=m;
        // Hold the confirmed values only while the page's own request is in
        // flight. A host-side apply (startup Restore) reports the real
        // preferences; holding the page placeholder would show an enabled
        // feature as off until the apply completes.
        const preferences=m.applying && state.taskbarRequestRevision!==null ? {enabled:state.taskbar.enabled,transparentTaskbar:state.taskbar.transparentTaskbar,autoHideTaskbar:state.taskbar.autoHideTaskbar,hideBehavior:state.taskbar.hideBehavior} : {enabled,transparentTaskbar,autoHideTaskbar,hideBehavior};
        return {...state,taskbar:{...preferences,running,applying,pendingRecovery,taskbars,mode},
          taskbarRevision:m.revision,taskbarRequestRevision:pending?state.taskbarRequestRevision:null,
          taskbarPending:pending?state.taskbarPending:null,
          taskbarError:m.error,taskbarLoadStatus:m.loadStatus,
          taskbarStatus:pending?"saving":failed?"error":completed?"applied":"idle"};
      }
      if(m.type==="minimize-state") {
        const failed=!!m.error || m.loadStatus==="failed" || m.enabled!==m.running;
        const pending=state.minimizeStatus==="saving" && m.result==="snapshot" && !failed;
        const {enabled,running,durationMs,restore,launch,multiMonitor,unhideTaskbar,unhideDurationMs,engine,
          x64,x86,hosts,hooks,workers,systemError}=m;
        return {...state,minimize:{enabled,running,durationMs,restore,launch,multiMonitor,unhideTaskbar,unhideDurationMs,engine,x64,x86,hosts,hooks,workers,systemError},
          minimizeError:m.error,minimizeLoadStatus:m.loadStatus,
          minimizeStatus:pending?"saving":failed?"error":m.saved?"saved":m.loadStatus==="recovered"?"recovered":"idle"};
      }
      if(m.type==="icons-state") {
        const failed=!!m.error || m.loadStatus==="failed" || m.enabled!==m.running;
        const pending=state.iconsStatus==="saving"&&m.result==="snapshot"&&!failed;
        const {enabled,running,enableAutoHide,autoHideDelay,showIconsOnAnyInput,manuallyHidden,desktops,autoHidden,pendingRestores}=m;
        return {...state,icons:{enabled,running,enableAutoHide,autoHideDelay,showIconsOnAnyInput,manuallyHidden,desktops,autoHidden,pendingRestores},
          iconsError:m.error,iconsLoadStatus:m.loadStatus,
          iconsStatus:pending?"saving":failed?"error":m.saved?"saved":m.loadStatus==="recovered"?"recovered":"idle"};
      }
      if(m.type==="organizer-state") {
        const pending=state.organizerStatus==="saving"&&m.result==="snapshot";
        return {...state,organizer:{enabled:m.enabled,running:m.running,locked:m.locked,material:m.material,width:m.width,height:m.height,title:m.title,count:m.count,mode:m.mode,layout:m.layout,iconScale:m.iconScale??100,collections:m.collections.map(collection=>({...collection}))},
          organizerError:m.error,organizerLoadStatus:m.loadStatus,organizerMaterialStatus:m.materialStatus,
          organizerPending:pending?state.organizerPending:null,
          organizerStatus:pending?"saving":m.error||m.loadStatus==="failed"?"error":m.saved?"saved":m.loadStatus==="recovered"?"recovered":"idle"};
      }
      if(m.type==="weather-state") {
        const pending=state.weatherStatus==="saving"&&m.result==="snapshot";
        const {enabled,running,locked,material,width,height,city}=m;
        return {...state,weather:{enabled,running,locked,material,width,height,city},
          weatherError:m.error,weatherLoadStatus:m.loadStatus,weatherMaterialStatus:m.materialStatus,
          weatherStatus:pending?"saving":m.error||m.loadStatus==="failed"?"error":m.saved?"saved":m.loadStatus==="recovered"?"recovered":"idle",
          weatherData:{loading:m.loading,error:m.weatherError,cacheWriteFailed:m.cacheWriteFailed,hasWeather:m.hasWeather,systemLocation:m.systemLocation,
            source:m.source,fetchedAt:m.fetchedAt,sourceUpdatedAt:m.sourceUpdatedAt,validAt:m.validAt,remainingDay:m.remainingDay}};
      }
      if(["performance-state","almanac-state","todayTodo-state"].includes(m.type)) {
        const scope=m.type==="todayTodo-state"?"todayTodo":m.type==="almanac-state"?"almanac":"performance";
        const pending=state[`${scope}Status`]==="saving" && m.result==="snapshot";
        const {enabled,running,locked,material,width,height}=m;
        return {...state,[scope]:{enabled,running,locked,material,width,height},
          [`${scope}Error`]:m.error,[`${scope}LoadStatus`]:m.loadStatus,[`${scope}MaterialStatus`]:m.materialStatus,
          [`${scope}Status`]:pending?"saving":m.error||m.loadStatus==="failed"?"error":m.saved?"saved":m.loadStatus==="recovered"?"recovered":"idle"};
      }
      if (m.type === "spectrum-state") {
        const pending=state.spectrumStatus==="saving" && m.result==="snapshot";
        const {enabled,running,locked,width,height,style,scale,pulse,blocks,waves}=m;
        return {...state,spectrum:{enabled,running,locked,width,height,style,scale,pulse:{...pulse},blocks:{...blocks},waves:{...waves}},
          spectrumError:m.error,spectrumLoadStatus:m.loadStatus,
          spectrumStatus:pending?"saving":m.error||m.loadStatus==="failed"?"error":m.saved?"saved":m.loadStatus==="recovered"?"recovered":"idle"};
      }
      if (m.type === "resource-snapshot") return {...state,resourceSnapshot:m};
      if (m.type === "reminder-status") return {...state,reminderAvailable:m.available};
      if (m.type === "calendar-settings") {
        const result=m.result ?? (m.saved ? "applied" : "snapshot");
        const loadStatus=m.loadStatus ?? "loaded";
        const error=m.error || (result === "cancelled" ? state.calendarError : "");
        return { ...state, calendar:{...m.calendar},calendarError:error,calendarLoadStatus:loadStatus,calendarMaterialStatus:m.materialStatus,
          calendarStatus:error ? "error" : m.saved ? "saved" : loadStatus === "failed" ? "error" : loadStatus === "recovered-backup" ? "recovered" : "idle" };
      }
      if (m.type === "settings")
        return {
          ...state,
          clock: { ...m.clock },
          status: m.saved ? "saved" : "error",
          error: m.error || "",
        };
      if (m.type === "visibility") {
        if (Number.isSafeInteger(m.generation) && m.generation < state.visibilityGeneration) return state;
        return { ...state, visible: m.visible,
          visibilityGeneration: Number.isSafeInteger(m.generation) ? m.generation : state.visibilityGeneration };
      }
      if (m.type === "window") return { ...state, fullscreen: m.fullscreen };
      return state;
    }
    default:
      return state;
  }
}
export function searchCatalog(items, query) {
  const q = query.trim().toLocaleLowerCase();
  return items.filter((item) =>
    `${item.title} ${item.description || ""}`.toLocaleLowerCase().includes(q),
  );
}
export function createVisibilityTimer(callback, delay, clock = globalThis) {
  let handle = null;
  const stop = () => {
    if (handle !== null) {
      clock.clearInterval(handle);
      handle = null;
    }
  };
  return {
    update(nativeVisible, documentVisible) {
      if (nativeVisible && documentVisible) {
        if (handle === null) handle = clock.setInterval(callback, delay);
      } else stop();
    },
    dispose: stop,
  };
}

export function createVisibilityCleanup(cleanup, acknowledge) {
  let lastAcknowledged = 0;
  return {
    acknowledge(generation) {
      if (!Number.isSafeInteger(generation) || generation < 1 || generation <= lastAcknowledged) return false;
      cleanup(generation);
      const accepted = acknowledge(generation) === true;
      if (accepted) lastAcknowledged = generation;
      return accepted;
    },
  };
}

export function createClockPreviewTimer(callback, clock = globalThis) {
  let active = false;
  const timer = createVisibilityTimer(callback, 1000, clock);
  return {
    update({ nativeVisible, documentVisible, view, tab, previewOpen }) {
      const nextActive =
        nativeVisible &&
        documentVisible &&
        view === "clock" &&
        tab === "settings" &&
        previewOpen;
      if (nextActive && !active) callback();
      active = nextActive;
      timer.update(active, true);
    },
    dispose() {
      active = false;
      timer.dispose();
    },
  };
}

export function settingsNotice(
  status,
  { hasInlineStatus = false, previousStatus, error = "" } = {},
) {
  if (hasInlineStatus) return null;
  if (status === "error") return error === "runtime" ? "桌面时钟启动失败，请检查字体和安装文件" : error === "rollback" ? "时钟恢复失败，请重新启用组件" : "保存失败，已恢复此前设置，请重试";
  if (status === "unavailable") return "宿主未连接，设置未保存";
  if (status === "saved" && previousStatus === "saving")
    return "已应用并保存";
  return null;
}
