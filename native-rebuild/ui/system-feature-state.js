// Host-confirmed Windows feature state. Page requests only mark an operation
// pending; they never replace confirmed preferences with optimistic values.
export const initialSystemFeatureState = () => ({
  icons:{enabled:false,running:false,enableAutoHide:false,autoHideDelay:5,showIconsOnAnyInput:true,
    manuallyHidden:false,desktops:0,autoHidden:0,pendingRestores:0},
  iconsStatus:"loading",iconsError:"",iconsLoadStatus:"fresh",
  taskbar:{enabled:false,running:false,transparentTaskbar:true,autoHideTaskbar:false,hideBehavior:'auto-hide',
    applying:false,pendingRecovery:false,taskbars:0,mode:"unavailable",effectStatus:'inactive',taskbarType:'unknown',stage:'',hresult:0,
    registeredTaskbars:0,backgrounds:0,appliedTaskbars:0,hideApplied:false,effectiveAutoHide:false,fullyHidden:false,autoHideDemand:false},
  taskbarStatus:"loading",taskbarError:"",taskbarLoadStatus:"fresh",taskbarRevision:0,taskbarRequestRevision:null,taskbarPending:null,
  minimize:{enabled:false,running:false,durationMs:450,restore:true,launch:true,multiMonitor:false,
    unhideTaskbar:true,unhideDurationMs:450,engine:"modern",x64:false,x86:false,hosts:0,hooks:0,workers:0,systemError:0},
  minimizeStatus:"loading",minimizeError:"",minimizeLoadStatus:"fresh",
});
export const unavailableSystemFeatureState = () => ({
  taskbarStatus:'unavailable',taskbarRequestRevision:null,taskbarPending:null,
  iconsStatus:'unavailable',minimizeStatus:'unavailable',
});

// A requested switch position is immediate feedback, not confirmation that
// Windows applied it. Keep the last confirmed host snapshot until settlement.
export function taskbarDisplayedPreferences(state) {
  return state.taskbarStatus==='saving' && state.taskbarPending
    ? {...state.taskbar,...state.taskbarPending} : state.taskbar;
}

// undefined leaves routing to another feature; stale host revisions return the
// same object so callers can preserve their normal no-op behavior.
export function reduceSystemFeatureState(state,event) {
  if(event.type==='request') {
    if(event.scope==='taskbar') {
      const property={enabled:'enabled',transparent:'transparentTaskbar',autoHide:'autoHideTaskbar'}[event.key];
      const pending=event.key==='reset'?{transparentTaskbar:true,autoHideTaskbar:false,hideBehavior:'auto-hide'}:
        event.key==='hideBehavior'&&['auto-hide','fully-hidden'].includes(event.value)?{hideBehavior:event.value}:
        property&&typeof event.value==='boolean'?{[property]:event.value}:null;
      return {...state,taskbarStatus:'saving',taskbarRequestRevision:state.taskbarRevision,taskbarPending:pending};
    }
    if(event.scope==='minimize')return {...state,minimizeStatus:'saving'};
    if(event.scope==='icons')return {...state,iconsStatus:'saving'};
  }
  if(event.type==='unavailable') {
    if(event.scope==='taskbar')return {...state,taskbarStatus:'unavailable',taskbarRequestRevision:null,taskbarPending:null};
    if(event.scope==='minimize')return {...state,minimizeStatus:'unavailable'};
    if(event.scope==='icons')return {...state,iconsStatus:'unavailable'};
  }
  if(event.type==='host') {
    const m=event.message;
    if(m.type==="taskbar-state") {
      if(m.revision<state.taskbarRevision)return state;
      const failed=!!m.error || m.loadStatus==="failed" || m.pendingRecovery ||
        (m.enabled&&!m.running) || (!m.enabled&&m.running&&!m.autoHideDemand);
      const pending=m.applying || (state.taskbarRequestRevision!==null && m.revision<=state.taskbarRequestRevision && !failed);
      const confirmed=(!m.enabled||!m.transparentTaskbar||m.effectStatus==='applied') &&
        (!((m.enabled&&m.autoHideTaskbar)||m.autoHideDemand)||m.hideApplied===true);
      const completed=state.taskbarRequestRevision!==null && m.revision>state.taskbarRequestRevision && !pending && !failed && confirmed && m.loadStatus==="loaded";
      const {enabled,running,transparentTaskbar,autoHideTaskbar,hideBehavior,applying,pendingRecovery,taskbars,mode}=m;
      // Hold the confirmed values only while the page's own request is in
      // flight. A host-side apply (startup Restore) reports the real
      // preferences; holding the page placeholder would show an enabled
      // feature as off until the apply completes.
      const preferences=m.applying && state.taskbarRequestRevision!==null ? {enabled:state.taskbar.enabled,transparentTaskbar:state.taskbar.transparentTaskbar,autoHideTaskbar:state.taskbar.autoHideTaskbar,hideBehavior:state.taskbar.hideBehavior} : {enabled,transparentTaskbar,autoHideTaskbar,hideBehavior};
      return {...state,taskbar:{...preferences,running,applying,pendingRecovery,taskbars,mode,
        effectStatus:m.effectStatus || (enabled&&transparentTaskbar?'pending':'inactive'),taskbarType:m.taskbarType||'unknown',stage:m.stage||'',hresult:m.hresult||0,
        registeredTaskbars:m.registeredTaskbars||0,backgrounds:m.backgrounds||0,appliedTaskbars:m.appliedTaskbars||0,
        hideApplied:m.hideApplied===true,effectiveAutoHide:m.effectiveAutoHide===true,fullyHidden:m.fullyHidden===true,autoHideDemand:m.autoHideDemand===true},
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
  }
  return undefined;
}
