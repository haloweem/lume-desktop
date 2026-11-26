import {t} from './localization.js';

const selects={
  'clock-style':['clock','style'],
  'calendar-theme':['calendar','theme'],'calendar-material':['calendar','material'],
  'performance-material':['performance','material'],'spectrum-style':['spectrum','style'],
  'todayTodo-material':['todayTodo','material'],'almanac-material':['almanac','material'],
  'weather-material':['weather','material'],
  'organizer-material':['organizer','material'],'minimize-engine':['minimize','engine'],
  'taskbar-hide-behavior':['taskbar','hideBehavior'],'radial-material':['radial','material'],
  'dock-material':['dock','material'],'dynamicIsland-size':['dynamicIsland','size'],
  'dynamicIsland-material':['dynamicIsland','material'],
};
const dockRanges={
  'dock-icon-scale':'iconScale','dock-position':'position',
  'dock-position-offset':'positionOffset',
  'dock-bottom-offset':'bottomOffset','dock-icon-spacing':'iconSpacing',
};
const toggles={
  toggle:['clock','enabled','enabled'],
  ...Object.fromEntries(['calendar','performance','spectrum','todayTodo','almanac','weather',
    'organizer','icons','taskbar','minimize','radial','dock'].map(scope=>[`${scope}-toggle`,[scope,'enabled','enabled']])),
  'dock-reflection':['dock','reflection','reflection'],'dock-show-running':['dock','showRunning','showRunning'],
  ...Object.fromEntries(['animation','animateRestore','animateLaunch','animateClose'].map(key=>['dock-'+key,['dock',key,key]])),
  'dock-launchpad-enabled':['dock','launchpadEnabled','launchpadEnabled'],
  'dock-show-start-menu':['dock','showStartMenu','showStartMenu'],
  'dock-system-tray':['dock','systemTray','systemTray'],
  'dock-summon-enabled':['dock','summonEnabled','summonEnabled'],
  'icons-autoHide':['icons','autoHide','enableAutoHide'],'icons-restoreOnInput':['icons','restoreOnInput','showIconsOnAnyInput'],
  'taskbar-transparent':['taskbar','transparent','transparentTaskbar'],'taskbar-autoHide':['taskbar','autoHide','autoHideTaskbar'],
  ...Object.fromEntries(['restore','launch','multiMonitor','unhideTaskbar'].map(key=>[`minimize-${key}`,['minimize',key,key]])),
};
const resets=new Map(['calendar','performance','spectrum','todayTodo','almanac','weather',
  'organizer','icons','taskbar','minimize','radial','dock'].map(scope=>[`${scope}-reset`,scope]));

// Component intent only. The caller owns navigation, lifecycle and the request
// busy guard; the bridge remains authoritative for wire validation and ranges.
export function createComponentSettingsCommands({getState,request,send,showNotice}) {
  return {
    select(id,value) {
      if(Object.hasOwn(selects,id)){request(...selects[id],value);return true;}
      if(Object.hasOwn(dockRanges,id)){request('dock',dockRanges[id],Number(value));return true;}
      if(id==='dynamicIsland-layer') {
        if(['desktop','above'].includes(value))request('dynamicIsland','topmost',value==='above');
        return true;
      }
      return false;
    },
    action(action) {
      const state=getState();
      if(Object.hasOwn(toggles,action)){
        const [scope,key,property]=toggles[action];request(scope,key,!state[scope][property]);return true;
      }
      if(resets.has(action)){request(resets.get(action),'reset');return true;}
      if(action==='reset'){request('clock','reset');return true;}
      if(action==='calendar-textColor'||action==='calendar-highlightColor'){
        request('calendar',action.slice('calendar-'.length));return true;
      }
      if(action==='weather-location-settings'){send('weather','location-settings');return true;}
      if(action==='dock-summon-clear'){request('dock','summonShortcut',0);return true;}
      if(typeof action==='string'&&action.startsWith('dynamicIsland-')) {
        const key=action.slice('dynamicIsland-'.length);
        if(key==='toggle')request('dynamicIsland','enabled',!state.dynamicIsland.enabled);
        else if(key==='lock')request('dynamicIsland','locked',!state.dynamicIsland.locked);
        else if(['hideFullscreen','topmost','hover','autoHide'].includes(key))request('dynamicIsland',key,!state.dynamicIsland[key]);
        else if(['reset','resetAppearance','expand','collapse','focus','stopFocus'].includes(key))request('dynamicIsland',key);
        return true;
      }
      return false;
    },
    input(target) {
      if(target.matches('[data-setting-slider],[data-setting-number]'))return true;
      const range=target.closest?.('[data-dock-range],[data-organizer-range]');
      if(!range)return false;
      const value=range.valueAsNumber,output=range.closest('[data-setting-range]')?.querySelector('output');
      if(Number.isInteger(value)) {
        const unit=range.dataset.rangeUnit||'';
        if(output)output.textContent=`${value}${unit}`;
        range.setAttribute('aria-valuetext',`${value}${unit}`);
        const min=Number(range.min),max=Number(range.max);
        if(Number.isFinite(min)&&Number.isFinite(max)&&max>min)
          range.style.setProperty('--range-progress',`${((value-min)/(max-min))*100}%`);
      }
      return true;
    },
    change(target) {
      if(target.matches('[data-setting-slider],[data-setting-number]'))return true;
      const value=target.valueAsNumber,state=getState();
      if(target.matches('[data-organizer-range]')){
        if(Number.isInteger(value)&&value>=50&&value<=120)request('organizer','iconScale',value);
        return true;
      }
      if(target.matches('[data-dock-range]')) {
        const key=dockRanges[target.id];
        if(key&&Number.isInteger(value))request('dock',key,value);
        return true;
      }
      if(['minimize-durationMs','minimize-unhideDurationMs'].includes(target.id)) {
        const key=target.id.slice('minimize-'.length);
        const min=key==='durationMs'?50:0,max=key==='durationMs'?2000:5000;
        if(state.minimizeStatus==='saving'||state.minimizeStatus==='loading')return true;
        if(Number.isInteger(value)&&value>=min&&value<=max)request('minimize',key,value);
        else {
          target.value=String(state.minimize[key]);
          showNotice(t(`${key==='durationMs'?'动画时长':'任务栏显示时长'}需要填写 ${min} 到 ${max} 毫秒的整数`,
            `${key==='durationMs'?'Animation duration':'Taskbar display duration'} must be an integer between ${min} and ${max} milliseconds.`));
        }
        return true;
      }
      if(target.id==='icons-delay') {
        if(Number.isInteger(value)&&value>=3&&value<=60)request('icons','delay',value);
        else {
          target.value=String(state.icons.autoHideDelay);
          showNotice(t('自动隐藏延迟需要填写 3 到 60 秒的整数','Auto-hide delay must be an integer between 3 and 60 seconds.'));
        }
        return true;
      }
      if(target.id==='clock-style'){request('clock','style',target.value);return true;}
      return false;
    },
  };
}
