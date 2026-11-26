import {escapeHTML as h,toggle} from './dom.js';
import {settingsFrame,settingsGroup,syncSettingsControlBusy} from './settings-frame.js';
import {t} from './localization.js';
import {settingsRange,syncSettingsRange} from './settings-range.js';
import {shortcutFromEvent,shortcutLabel,validRadialShortcut} from './radial.js';

// The control center at the Dock's right end (protocol key "systemTray"), paired with dock::SystemTrayAvailable.
const systemTrayAvailable=true;

const materials=()=>[['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')],['none',t('无','None')]];
export const dockAnimationDurationDefault=400;
export const dockReflectionDefaults={reflectionRatio:50,reflectionTransparency:50};
export const dockSummonShortcutDefault=2093;
export const dockIconScales=Array.from({length:71},(_,index)=>50+index);
export const dockPositions=Array.from({length:101},(_,index)=>index);
export const dockPositionOffsets=Array.from({length:51},(_,index)=>index);
export const dockBottomOffsets=Array.from({length:161},(_,index)=>index);
export const dockIconSpacings=Array.from({length:21},(_,index)=>4+index);
const busy=state=>['loading','saving'].includes(state.dockStatus);
const count=value=>Number.isSafeInteger(value)&&value>=0&&value<=1000;
const rangeControl=(id,label,value,min,max,unit,disabled)=>{
  const key={'dock-animation-duration':'animationDuration','dock-icon-scale':'iconScale','dock-bottom-offset':'bottomOffset','dock-icon-spacing':'iconSpacing',
    'dock-reflection-ratio':'reflectionRatio','dock-reflection-transparency':'reflectionTransparency',
    'dock-position-offset':'positionOffset','dock-launchpad-scale':'launchpadScale','dock-launchpad-icon-scale':'launchpadIconScale'}[id];
  return settingsRange(id,label,Number(value),min,max,unit,disabled,'dock',key);
};
const animationOptions=()=>[['animation',t('启用神灯动画','Enable Genie animation'),false],
  ['animateRestore',t('还原与最大化动画','Restore and maximize'),true],
  ['animateLaunch',t('应用打开动画','App opening'),true],['animateClose',t('窗口关闭动画','Window closing'),true]];
function animationGroup(dock,disabled){
  const row=([key,label,fallback])=>`<div class="setting-row"><label>${label}</label>${toggle(dock[key]??fallback,label,'dock-'+key,disabled||key!=='animation'&&!dock.animation)}</div>`;
  const [master,...options]=animationOptions();
  return settingsGroup(row(master)+`<div data-dock-animation-options${dock.animation?'':' hidden'}><div class="setting-row">${rangeControl('dock-animation-duration',t('动画时长','Duration'),dock.animationDuration??dockAnimationDurationDefault,50,2000,' ms',disabled||!dock.animation)}</div>${options.map(row).join('')}</div>`,t('动画','Animation'));
}
const inRange=(value,min,max)=>Number.isInteger(value)&&value>=min&&value<=max;

export function validDockState(message) {
  const keys=['version','type','enabled','running','material','reflection','systemTray',
    'iconScale','position','bottomOffset','iconSpacing','pinned','added',
    'materialStatus','loadStatus','error','result','saved'];
  const optional=['screen','screenCount','reflectionRatio','reflectionTransparency','showRunning','runningPreview','animation','animateRestore','animateLaunch','animateClose','animationDuration','showStartMenu','launchpadEnabled','edge','summonEnabled','summonShortcutCode','summonHotkeyRegistered','visibleItems','layersVisible','missingIcons','iconCachePaths','launchpadScale','launchpadIconScale','launchpadMaximized','launchpadRemoved','positionMode','positionOffset'];
  return !!message&&typeof message==='object'&&!Array.isArray(message)&&
    keys.every(key=>Object.hasOwn(message,key))&&Object.keys(message).every(key=>keys.includes(key)||optional.includes(key))&&
    message.version===1&&message.type==='dock-state'&&
    ['enabled','running','saved','reflection','systemTray'].every(key=>typeof message[key]==='boolean')&&
    (message.screen===undefined||inRange(message.screen,0,64))&&
    (message.screenCount===undefined||inRange(message.screenCount,0,64))&&
    ['reflectionRatio','reflectionTransparency'].every(key=>!Object.hasOwn(message,key)||inRange(message[key],0,100))&&
    (!Object.hasOwn(message,'summonEnabled')||typeof message.summonEnabled==='boolean')&&
    (!Object.hasOwn(message,'summonHotkeyRegistered')||typeof message.summonHotkeyRegistered==='boolean')&&
    (message.summonShortcutCode===undefined||validDockSummonShortcut(message.summonShortcutCode))&&
    (message.positionMode===undefined||['center','left','right'].includes(message.positionMode))&&
    (message.positionOffset===undefined||inRange(message.positionOffset,0,50))&&
    (!Object.hasOwn(message,'showRunning')||typeof message.showRunning==='boolean')&&
    ['animation','animateRestore','animateLaunch','animateClose'].every(key=>!Object.hasOwn(message,key)||typeof message[key]==='boolean')&&
    (!Object.hasOwn(message,'animationDuration')||inRange(message.animationDuration,50,2000))&&
    (!Object.hasOwn(message,'runningPreview')||typeof message.runningPreview==='boolean')&&
    (!Object.hasOwn(message,'launchpadEnabled')||typeof message.launchpadEnabled==='boolean')&&
    (!Object.hasOwn(message,'showStartMenu')||typeof message.showStartMenu==='boolean')&&
    (!Object.hasOwn(message,'edge')||['top','bottom','left','right'].includes(message.edge))&&
    ['liquid-glass','frosted-glass','none'].includes(message.material)&&inRange(message.iconScale,50,120)&&
    (message.launchpadScale===undefined||inRange(message.launchpadScale,60,140))&&
    (message.launchpadIconScale===undefined||inRange(message.launchpadIconScale,50,120))&&
    (message.launchpadMaximized===undefined||typeof message.launchpadMaximized==='boolean')&&
    (message.launchpadRemoved===undefined||count(message.launchpadRemoved))&&
    inRange(message.position,0,100)&&inRange(message.bottomOffset,0,160)&&inRange(message.iconSpacing,4,24)&&
    count(message.pinned)&&count(message.added)&&
    (message.visibleItems===undefined||count(message.visibleItems)) &&
    (message.layersVisible===undefined||typeof message.layersVisible==='boolean') &&
    (message.missingIcons===undefined||count(message.missingIcons)) &&
    (message.iconCachePaths===undefined||inRange(message.iconCachePaths,0,256)) &&
    ['inactive','native-ready','native-starting','transparent-fallback'].includes(message.materialStatus)&&
    ['fresh','loaded','recovered','failed'].includes(message.loadStatus)&&
    ['','screen','runtime','save','rollback','launchpad','collection-popup','limit'].includes(message.error)&&['snapshot','applied'].includes(message.result)&&
    (!message.saved||(message.result==='applied'&&message.error===''&&message.loadStatus==='loaded'));
}

export function validDockSummonShortcut(code) {
  if(code===0)return true;
  if(!validRadialShortcut(code))return false;
  const modifiers=Math.floor(code/256),control=!!(modifiers&2),alt=!!(modifiers&1),win=!!(modifiers&8);
  const key=code%256;
  return (control||alt||win)&&key!==123&&!(win&&[68,69,76,82].includes(key))&&
    !(win&&key===9)&&!(alt&&key===115)&&!(control&&!!(modifiers&4)&&key===27);
}

export function dockSummonShortcutFromEvent(event) {
  const code=shortcutFromEvent(event);
  return code!==null&&validDockSummonShortcut(code)?code:null;
}

const summonLabel=code=>shortcutLabel(code)||t('点击设置快捷键','Click to set shortcut');
const summonConflict=state=>state.dock.enabled&&state.dock.summonEnabled&&state.dock.summonShortcutCode&&
  !busy(state)&&state.dock.summonHotkeyRegistered===false?t('快捷键不可用，请换一组','Shortcut unavailable. Choose another.') : '';
function summonShortcutRow(state){
  const {summonEnabled,summonShortcutCode:code}=state.dock, disabled=busy(state)||!summonEnabled;
  const clear=t('清除快捷键','Clear shortcut'),error=summonConflict(state);
  return `<div class="setting-row dock-summon-shortcut-row"${summonEnabled?'':' hidden'}><label for="dock-summon-key">${t('唤出快捷键','Summon shortcut')}</label>`+
    `<div class="dock-shortcut-field"><button id="dock-summon-key" class="dock-shortcut-record" type="button" data-dock-summon-shortcut aria-pressed="false" aria-describedby="dock-key-error" ${code?'':'data-empty'} ${disabled?'disabled':''}>${h(summonLabel(code))}</button>`+
    `<button type="button" class="dock-shortcut-clear" data-action="dock-summon-clear" title="${h(clear)}" aria-label="${h(clear)}" ${code?'':'hidden'} ${disabled?'disabled':''}><span aria-hidden="true">×</span></button></div>`+
    `<span id="dock-key-error" class="dock-shortcut-error" data-dock-summon-status role="status" ${error?'':'hidden'}>${h(error)}</span></div>`;
}

export function mountDockSummonShortcut(root,{getState,onShortcut,onCaptureChange}) {
  let active=null,candidate=null;
  const feedback=text=>{
    const status=root.querySelector('[data-dock-summon-status]');
    if(status){status.textContent=text;status.hidden=!text;}
  };
  const cancel=()=>{
    const wasActive=active!==null;
    if(active){active.setAttribute('aria-pressed','false');active.textContent=summonLabel(getState().dock.summonShortcutCode);}
    active=null;candidate=null;feedback(summonConflict(getState()));
    if(wasActive)onCaptureChange?.(false);
  };
  const start=()=>{
    cancel();
    const recorder=root.querySelector('[data-dock-summon-shortcut]');
    if(!recorder||busy(getState())||!getState().dock.summonEnabled)return false;
    recorder.focus();
    if(root.ownerDocument.activeElement!==recorder)return false;
    active=recorder;recorder.setAttribute('aria-pressed','true');
    recorder.textContent=t('按下组合键','Press shortcut');feedback('');
    onCaptureChange?.(true);return true;
  };
  const click=event=>{if(event.target.closest('[data-dock-summon-shortcut]'))start();};
  const keydown=event=>{
    if(!active||event.target!==active||root.ownerDocument.activeElement!==active)return;
    if(busy(getState())){cancel();return;}
    event.preventDefault();event.stopPropagation();
    if(event.key==='Escape'){cancel();return;}
    if(event.repeat||['Alt','Control','Shift','Meta'].includes(event.key))return;
    const code=dockSummonShortcutFromEvent(event);
    candidate=code===null?null:{key:event.code,code};
    feedback(code===null?t('此组合键不可用','This key combination is unavailable'): '');
    active.textContent=code===null?t('按下组合键','Press shortcut'):shortcutLabel(code);
  };
  const keyup=event=>{
    if(!active||event.target!==active||root.ownerDocument.activeElement!==active)return;
    event.preventDefault();event.stopPropagation();
    if(!candidate||candidate.key!==event.code||busy(getState()))return;
    const code=candidate.code;cancel();onShortcut(code);
  };
  const blur=event=>{if(event.target===active)cancel();};
  const composition=event=>{if(event.target===active)cancel();};
  root.addEventListener('click',click);root.addEventListener('keydown',keydown);
  root.addEventListener('keyup',keyup);root.addEventListener('blur',blur,true);
  root.addEventListener('compositionstart',composition);
  root.ownerDocument.defaultView.addEventListener('blur',cancel);
  return {start,cancel,sync(){
    const state=getState(),recorder=root.querySelector('[data-dock-summon-shortcut]');
    if(recorder&&recorder!==active){
      recorder.textContent=summonLabel(state.dock.summonShortcutCode);
      recorder.toggleAttribute('data-empty',!state.dock.summonShortcutCode);
      syncSettingsControlBusy(recorder,busy(state)||!state.dock.summonEnabled);
    }
    const clear=root.querySelector('[data-action="dock-summon-clear"]');
    if(clear){clear.hidden=!state.dock.summonShortcutCode;clear.disabled=busy(state)||!state.dock.summonEnabled;}
    if(!active)feedback(summonConflict(state));
  },dispose(){cancel();root.removeEventListener('click',click);
    root.removeEventListener('keydown',keydown);root.removeEventListener('keyup',keyup);
    root.removeEventListener('blur',blur,true);root.removeEventListener('compositionstart',composition);
    root.ownerDocument.defaultView.removeEventListener('blur',cancel);}};
}

export function dockStatus(state) {
  if(state.dockStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.dockStatus==='saving')return '';
  if(state.dockStatus==='unavailable')return t('桌面宿主未连接，Dock 设置未保存','Desktop host disconnected. Dock settings were not saved.');
  if(state.dockStatus==='error')return {
    screen:t('所选屏幕未连接，请重新选择','The selected screen is disconnected. Choose another screen.'),
    runtime:t('Dock 启动失败，请检查安装文件','Could not start the Dock. Check the installation files.'),
    save:t('Dock 设置保存失败，请重试','Could not save Dock settings. Please try again.'),
    rollback:t('Dock 恢复失败，请重新启用','Could not restore the Dock. Enable it again.'),
    launchpad:t('启动台打开失败，请确认屏幕空间足够','Could not open the launchpad. Make sure the screen has enough space.'),
    'collection-popup':t('收纳组打开失败，请重试','Could not open the group. Please try again.'),
    limit:t('数量已达上限：Dock 最多固定 32 项，启动台最多添加 200 项','Limit reached: up to 32 Dock items and 200 added launchpad items.'),
  }[state.dockError]||t('Dock 配置读取失败，当前使用默认设置；尚未保存','Could not load Dock settings. Using defaults; not yet saved.');
  if(state.dockLoadStatus==='recovered')return t('已从备份恢复 Dock 配置，尚未保存到主配置','Dock settings restored from backup; not yet saved to the main configuration.');
  if(state.dockMaterialStatus==='transparent-fallback')return t('当前使用兼容材质','Using a compatible material.');
  if(state.dockMaterialStatus==='native-starting')return t('正在准备 Dock 材质...','Preparing the Dock material...');
  return '';
}

export function dockNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.dockStatus))return dockStatus(state);
  if(state.dockStatus==='saved'&&previousStatus==='saving')return t('Dock 设置已应用并保存','Dock settings applied and saved');
  return null;
}

function screenButtons(dock,disabled) {
  const ids=Array.from({length:dock.screenCount??1},(_,index)=>index+1);
  return [...ids,0].map(id=>`<button type="button" data-dock-screen="${id}" aria-pressed="${(dock.screen??1)===id}" ${disabled?'disabled':''}>${id?t('屏幕','Screen')+' '+id:t('全部屏幕','All screens')}</button>`).join('');
}
const disconnected=dock=>(dock.screen??1)>(dock.screenCount??1);
const dockEdges=()=>[['top',t('顶部','Top')],['bottom',t('底部','Bottom')],['left',t('左侧','Left')],['right',t('右侧','Right')]];
const verticalDock=edge=>edge==='left'||edge==='right';
const positionLabels=edge=>verticalDock(edge)?[t('垂直位置','Vertical position'),t('上偏移','Upward offset'),t('下偏移','Downward offset')]:
  [t('水平位置','Horizontal position'),t('左偏移','Left offset'),t('右偏移','Right offset')];
const edgeOffsetLabel=edge=>edge==='bottom'?t('距底部上移','Raise from bottom'):t('距屏幕边缘','Distance from edge');

export function dockPage(state,tab='dock') {
  if(tab==='launchpad')tab='settings';
  if(!['dock','settings','details'].includes(tab))tab='dock';
  const title=t('Dock栏美化','Dock');
  const description=t('启动台与常用应用','Launchpad and favourite apps');
  const disabled=busy(state),resetLabel=t('恢复默认','Restore defaults');
  const reflectionLabel=t('图标倒影','Icon reflection'),reflectionDisabled=disabled||!state.dock.reflection;
  const materialLabel=t('材质设置','Material settings');
  const launchpadLabel=t('启用启动台','Enable launchpad');
  const launchpadEnabled=state.dock.launchpadEnabled??true;
  const launchpadDisabled=disabled||!launchpadEnabled;
  const appearance=rows=>settingsGroup(rows,t('尺寸','Size'));
  const positionMode=state.dock.positionMode??'center';
  const edge=state.dock.edge??'bottom';
  const [positionLabel,firstOffset,lastOffset]=positionLabels(edge);
  const positionOptions=[['center',t('居中','Center')],['left',firstOffset],['right',lastOffset]];
  const body=tab==='dock'?
    appearance(`<div class="setting-row">${rangeControl('dock-icon-scale',t('图标大小','Icon size'),state.dock.iconScale,50,120,'%',disabled)}</div>`+
    `<div class="setting-row">${rangeControl('dock-icon-spacing',t('图标间距','Icon spacing'),state.dock.iconSpacing??10,4,24,' px',disabled)}</div>`)+
    settingsGroup(`<div class="setting-row"><label for="dock-edge">${t('Dock栏位置','Dock position')}</label><select id="dock-edge" aria-label="${t('Dock栏位置','Dock position')}" ${disabled?'disabled':''}>${dockEdges().map(([value,label])=>`<option value="${value}" ${edge===value?'selected':''}>${label}</option>`).join('')}</select></div>`+
    `<div class="setting-row dock-position-row"><span class="setting-row__label" id="dock-position-label">${positionLabel}</span><div class="island-appearance__segments" id="dock-position-mode" role="group" aria-labelledby="dock-position-label">${positionOptions.map(([value,label])=>
      `<button type="button" data-dock-position="${value}" aria-pressed="${positionMode===value}" ${disabled?'disabled':''}>${label}</button>`).join('')}</div></div>`+
    `<div class="setting-row" data-dock-position-offset-row${positionMode==='center'?' hidden':''}>${rangeControl('dock-position-offset',t('偏移量','Offset'),state.dock.positionOffset??0,0,50,'%',disabled)}</div>`+
    `<div class="setting-row">${rangeControl('dock-bottom-offset',edgeOffsetLabel(edge),state.dock.bottomOffset??0,0,160,' px',disabled)}</div>`,t('位置','Position'))+
    settingsGroup(`<div class="setting-row"><label>${reflectionLabel}</label>${toggle(state.dock.reflection,reflectionLabel,'dock-reflection',disabled)}</div>`+
    `<div class="setting-row">${rangeControl('dock-reflection-ratio',t('倒影比例','Reflection ratio'),state.dock.reflectionRatio??dockReflectionDefaults.reflectionRatio,0,100,'%',reflectionDisabled)}</div>`+
    `<div class="setting-row">${rangeControl('dock-reflection-transparency',t('透明度比例','Transparency'),state.dock.reflectionTransparency??dockReflectionDefaults.reflectionTransparency,0,100,'%',reflectionDisabled)}</div>`,reflectionLabel):
    tab==='settings'?
    settingsGroup(`<div class="setting-row dock-material-row"><span class="setting-row__label" id="dock-material-label">${materialLabel}</span><div class="island-appearance__segments" id="dock-material" role="group" aria-labelledby="dock-material-label">${materials().map(([value,label])=>
      `<button type="button" data-dock-material="${value}" aria-pressed="${state.dock.material===value}" ${disabled?'disabled':''}>${label}</button>`).join('')}</div></div>`,t('材质','Material'))+
    animationGroup(state.dock,disabled)+
    settingsGroup(`<div class="setting-row"><label>${launchpadLabel}</label>${toggle(launchpadEnabled,launchpadLabel,'dock-launchpad-enabled',disabled)}</div>`+
    `<div class="setting-row">${rangeControl('dock-launchpad-scale',t('启动台大小','Launchpad size'),state.dock.launchpadScale??100,60,140,'%',launchpadDisabled)}</div>`+
    `<div class="setting-row">${rangeControl('dock-launchpad-icon-scale',t('启动台图标大小','Launchpad icon size'),state.dock.launchpadIconScale??100,50,120,'%',launchpadDisabled)}</div>`,t('启动台','Launchpad'))+
    settingsGroup(`<div class="setting-row dock-display-row"><span id="dock-screen-label">${t('显示策略','Display policy')}</span><div class="island-appearance__segments" role="group" aria-labelledby="dock-screen-label" data-dock-screens="${state.dock.screenCount??1}">${screenButtons(state.dock,disabled)}</div></div>`+
    `<p data-dock-screen-unavailable ${disconnected(state.dock)?'':'hidden'}>${t('所选屏幕未连接，暂在主屏显示','The selected screen is disconnected. Showing on the primary screen for now.')}</p>`+
    `<div class="setting-row"><label>${t('开始菜单','Start menu')}</label>${toggle(state.dock.showStartMenu??true,t('开始菜单','Start menu'),'dock-show-start-menu',disabled)}</div>`+
    `<div class="setting-row"><label>${t('运行区','Running apps')}</label>${toggle(state.dock.showRunning??true,t('运行区','Running apps'),'dock-show-running',disabled)}</div>`+
    `<div class="setting-row dock-display-row" data-dock-running-style${state.dock.showRunning===false?' hidden':''}><span id="dock-running-style-label">${t('显示方式','Display style')}</span><div class="island-appearance__segments" role="group" aria-labelledby="dock-running-style-label">${[[false,t('图标','Icon')],[true,t('预览','Preview')]].map(([preview,label])=>
      `<button type="button" data-dock-running-preview="${preview}" aria-pressed="${preview===(state.dock.runningPreview??false)}" ${disabled?'disabled':''}>${label}</button>`).join('')}</div></div>`+
    (systemTrayAvailable?`<div class="setting-row"><label>${t('控制中心','Control center')}</label>${toggle(state.dock.systemTray,t('控制中心','Control center'),'dock-system-tray',disabled)}</div>`:''),t('显示','Display'))+
    settingsGroup(`<div class="setting-row"><label>${t('启用唤出','Enable summon')}</label>${toggle(state.dock.summonEnabled,t('Dock 栏唤出','Summon Dock'),'dock-summon-enabled',disabled)}</div>`+
    summonShortcutRow(state),t('Dock 栏唤出','Summon Dock'))+
    `<div class="reset-row"><button type="button" class="reset" data-action="dock-reset" title="${h(resetLabel)}" ${disabled?'disabled':''}>${resetLabel}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About the Dock')}</h3>`+
    `<p>${t('启动台显示桌面上的图标（桌面本身不变），可以搜索，也可以用“添加”加入文件和文件夹。将运行区图标拖过分隔线，松开即可固定到应用区的对应位置。也可以右键固定或移除图标。','The launchpad shows your desktop icons (the desktop itself is unchanged); search them or use Add for files and folders. Drag a running app across the divider and drop it where you want to pin it. You can also right-click to pin or unpin.')}</p></div>`;
  return settingsFrame({title,description,tab,body,status:dockStatus(state),error:state.dockStatus==='error',
    scope:'dock',backLabel:t('返回桌面美化','Back to desktop styles'),
    pageTitle:title,navigationPages:[['dock',t('Dock','Dock')],['settings',t('设置','Settings')],['details',t('详情','Details')]]});
}

export function syncDockControls(root,state) {
  const disabled=busy(state);
  root.querySelectorAll('[data-action="dock-toggle"]').forEach(button=>{
    button.classList.toggle('checked',state.dock.enabled);
    button.setAttribute('aria-checked',String(state.dock.enabled));button.disabled=disabled;
    button.title=t('Dock栏美化','Dock');
  });
  root.querySelectorAll('[data-dock-material]').forEach(button=>{
    button.setAttribute('aria-pressed',String(button.dataset.dockMaterial===state.dock.material));
    syncSettingsControlBusy(button,disabled);
  });
  const style=root.querySelector('[data-dock-running-style]');
  if(style){
    style.hidden=state.dock.showRunning===false;
    style.querySelectorAll('[data-dock-running-preview]').forEach(button=>{
      button.setAttribute('aria-pressed',String((button.dataset.dockRunningPreview==='true')===(state.dock.runningPreview??false)));
      syncSettingsControlBusy(button,disabled||style.hidden);
    });
  }
  const screens=root.querySelector('[data-dock-screens]');
  if(screens){
    const count=String(state.dock.screenCount??1);
    if(screens.dataset.dockScreens!==count){screens.dataset.dockScreens=count;screens.innerHTML=screenButtons(state.dock,disabled);}
    screens.querySelectorAll('[data-dock-screen]').forEach(button=>{
      button.setAttribute('aria-pressed',String(Number(button.dataset.dockScreen)===(state.dock.screen??1)));
      syncSettingsControlBusy(button,disabled);
    });
  }
  const hint=root.querySelector('[data-dock-screen-unavailable]');
  if(hint)hint.hidden=!disconnected(state.dock);
  const mode=state.dock.positionMode??'center';
  const edge=state.dock.edge??'bottom', edgeSelect=root.querySelector('#dock-edge');
  if(edgeSelect){edgeSelect.value=edge;edgeSelect.disabled=disabled;}
  const [positionLabel,firstOffset,lastOffset]=positionLabels(edge);
  const label=root.querySelector('#dock-position-label');
  if(label)label.textContent=positionLabel;
  const offsetLabel=root.querySelector('label[for="dock-bottom-offset"]');
  if(offsetLabel)offsetLabel.textContent=edgeOffsetLabel(edge);
  root.querySelectorAll('[data-dock-position]').forEach(button=>{
    if(button.dataset.dockPosition!=='center')button.textContent=button.dataset.dockPosition==='left'?firstOffset:lastOffset;
    button.setAttribute('aria-pressed',String(button.dataset.dockPosition===mode));
    syncSettingsControlBusy(button,disabled);
  });
  const offsetRow=root.querySelector('[data-dock-position-offset-row]');
  if(offsetRow)offsetRow.hidden=mode==='center';
  const syncRange=(id,value,min,max,unit,inactive=disabled)=>{
    syncSettingsRange(root,id,Number(value),min,max,unit,inactive);
  };
  const animationControls=root.querySelector('[data-dock-animation-options]');
  if(animationControls)animationControls.hidden=!state.dock.animation;
  for(const [key,,fallback] of animationOptions()){
    const checked=state.dock[key]??fallback;
    root.querySelectorAll('[data-action="dock-'+key+'"]').forEach(button=>{
      button.classList.toggle('checked',checked);button.setAttribute('aria-checked',String(checked));
      syncSettingsControlBusy(button,disabled||key!=='animation'&&!state.dock.animation);
    });
  }
  syncRange('dock-animation-duration',state.dock.animationDuration??dockAnimationDurationDefault,50,2000,' ms',disabled||!state.dock.animation);
  syncRange('dock-icon-scale',state.dock.iconScale,50,120,'%');
  syncRange('dock-position-offset',state.dock.positionOffset??0,0,50,'%');
  syncRange('dock-bottom-offset',state.dock.bottomOffset??0,0,160,' px');
  syncRange('dock-icon-spacing',state.dock.iconSpacing??10,4,24,' px');
  syncRange('dock-reflection-ratio',state.dock.reflectionRatio??dockReflectionDefaults.reflectionRatio,0,100,'%',disabled||!state.dock.reflection);
  syncRange('dock-reflection-transparency',state.dock.reflectionTransparency??dockReflectionDefaults.reflectionTransparency,0,100,'%',disabled||!state.dock.reflection);
  const launchpadEnabled=state.dock.launchpadEnabled??true;
  syncRange('dock-launchpad-scale',state.dock.launchpadScale??100,60,140,'%',disabled||!launchpadEnabled);
  syncRange('dock-launchpad-icon-scale',state.dock.launchpadIconScale??100,50,120,'%',disabled||!launchpadEnabled);
  root.querySelectorAll('[data-action="dock-launchpad-enabled"]').forEach(button=>{
    button.classList.toggle('checked',launchpadEnabled);
    button.setAttribute('aria-checked',String(launchpadEnabled));syncSettingsControlBusy(button,disabled);
  });
  root.querySelectorAll('[data-action="dock-reflection"]').forEach(button=>{
    button.classList.toggle('checked',state.dock.reflection);
    button.setAttribute('aria-checked',String(state.dock.reflection));syncSettingsControlBusy(button,disabled);
  });
  root.querySelectorAll('[data-action="dock-show-start-menu"]').forEach(button=>{
    const shown=state.dock.showStartMenu??true;
    button.classList.toggle('checked',shown);
    button.setAttribute('aria-checked',String(shown));syncSettingsControlBusy(button,disabled);
  });
  root.querySelectorAll('[data-action="dock-show-running"]').forEach(button=>{
    const shown=state.dock.showRunning??true;
    button.classList.toggle('checked',shown);
    button.setAttribute('aria-checked',String(shown));syncSettingsControlBusy(button,disabled);
  });
  root.querySelectorAll('[data-action="dock-system-tray"]').forEach(button=>{
    button.classList.toggle('checked',state.dock.systemTray);
    button.setAttribute('aria-checked',String(state.dock.systemTray));syncSettingsControlBusy(button,disabled);
  });
  const reset=root.querySelector('[data-action="dock-reset"]');if(reset)syncSettingsControlBusy(reset,disabled);
  root.querySelectorAll('[data-action="dock-summon-enabled"]').forEach(button=>{
    button.classList.toggle('checked',state.dock.summonEnabled);
    button.setAttribute('aria-checked',String(state.dock.summonEnabled));syncSettingsControlBusy(button,disabled);
  });
  const shortcutRow=root.querySelector('.dock-summon-shortcut-row');
  if(shortcutRow)shortcutRow.hidden=!state.dock.summonEnabled;
  const status=root.querySelector('[data-settings-scope="dock"] .status');
  if(status) {
    status.textContent=dockStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.dockStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.dockStatus==='error');
  }
  root.querySelectorAll('[data-settings-scope="dock"] button').forEach(button=>{
    if(!button.title)button.title=button.getAttribute('aria-label')||button.textContent.trim();
  });
}
