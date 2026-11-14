import {defaultIslandNotificationPreferences,normalizeIslandNotificationPreferences,validIslandNotificationState} from './island-notifications-protocol.js';

// This feature owns its defaults and confirmation rules. The flat keys preserve
// the page-state contract; unrelated state and DOM are never inspected here.
export const initialIslandState = () => ({
  dynamicIsland:{enabled:false,running:false,locked:false,expanded:false,focusing:false,finished:false,remaining:0,
    hideFullscreen:true,topmost:false,hover:false,hoverDelayMs:300,animationSpeed:'normal',bounce:'normal',autoHide:true,size:'standard',material:'none',shape:'capsule',leftWidget:0,rightWidget:0,screen:1,screens:[1],expandedLayout:1,clockStyle:'simple',calendarStyle:'default'},
  dynamicIslandStatus:"loading",dynamicIslandError:"",dynamicIslandLoadStatus:"fresh",
  islandNotifications:{preferences:defaultIslandNotificationPreferences(),revision:0,loaded:'fresh',result:'snapshot',saved:false,error:'',availability:'not-connected',receiving:false},
  islandNotificationsStatus:'loading',
  dynamicIslandPreview:null,
  islandCompactReadings:null,
  islandCompactCapturedAt:0,
  islandStyleWidget:'clock',
  islandCompactSide:'left',
});
export const unavailableIslandState = () => ({
  islandNotificationsStatus:'unavailable',dynamicIslandStatus:'unavailable',
});

// undefined means this feature does not own the event. Returning the input
// state consumes a stale/invalid message without changing object identity.
export function reduceIslandState(state,event) {
  if(event.type==='request') {
    if(event.scope==='island-notifications')return {...state,islandNotificationsStatus:event.key==='snapshot'?'loading':'saving'};
    if(event.scope==='dynamicIsland')return {...state,dynamicIslandStatus:'saving'};
  }
  if(event.type==='unavailable') {
    if(event.scope==='island-notifications')return {...state,islandNotificationsStatus:'unavailable'};
    if(event.scope==='dynamicIsland')return {...state,dynamicIslandStatus:'unavailable'};
  }
  if(event.type==='host') {
    const m=event.message;
    if(m.type==='island-notifications-state'){
      if(!validIslandNotificationState(m)||m.revision<state.islandNotifications.revision)return state;
      const failed=!!m.error||m.loaded==='failed';
      const pending=state.islandNotificationsStatus==='saving'&&m.result==='snapshot'&&!failed;
      return {...state,islandNotifications:{...m,preferences:normalizeIslandNotificationPreferences(m.preferences)},islandNotificationsStatus:pending?'saving':failed?'error':'ready'};
    }
    if(m.type==='dynamicIsland-preview')return {...state,dynamicIslandPreview:m,
      islandCompactReadings:state.islandCompactReadings??(m.readings?.length?m.readings:null)};
    if(m.type==='dynamicIsland-state') {
      const {enabled,running,locked,expanded,focusing,finished,remaining,hideFullscreen,topmost,hover,autoHide,size,material}=m;
      const failed=!!m.error||m.loadStatus==='failed';
      const pending=state.dynamicIslandStatus==='saving'&&m.result==='snapshot'&&!failed;
      return {...state,dynamicIsland:{enabled,running,locked,expanded,focusing,finished,remaining,hideFullscreen,topmost,hover,autoHide,size,material,shape:m.shape||'capsule',leftWidget:m.leftWidget??0,rightWidget:m.rightWidget??0,screen:m.screen??1,screens:m.screens??[1],hoverDelayMs:m.hoverDelayMs??300,animationSpeed:m.animationSpeed??'normal',bounce:m.bounce??'normal',expandedLayout:m.expandedLayout??1,clockStyle:m.clockStyle??'simple',calendarStyle:m.calendarStyle??'default'},
        dynamicIslandError:m.error,dynamicIslandLoadStatus:m.loadStatus,
        dynamicIslandStatus:pending?'saving':failed?'error':m.saved?'saved':m.loadStatus==='recovered'?'recovered':'idle'};
    }
  }
  return undefined;
}
