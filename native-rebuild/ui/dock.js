import {escapeHTML as h,toggle} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect,syncSettingsControlBusy} from './settings-frame.js';
import {t} from './localization.js';
import {settingsRange,syncSettingsRange} from './settings-range.js';

const materials=()=>[['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')],['none',t('无','None')]];
export const dockIconScales=Array.from({length:71},(_,index)=>50+index);
export const dockPositions=Array.from({length:101},(_,index)=>index);
export const dockBottomOffsets=Array.from({length:161},(_,index)=>index);
export const dockIconSpacings=Array.from({length:21},(_,index)=>4+index);
const busy=state=>['loading','saving'].includes(state.dockStatus);
const count=value=>Number.isSafeInteger(value)&&value>=0&&value<=1000;
const rangeControl=(id,label,value,min,max,unit,disabled)=>{
  const key={'dock-icon-scale':'iconScale','dock-bottom-offset':'bottomOffset','dock-icon-spacing':'iconSpacing',
    'dock-launchpad-scale':'launchpadScale','dock-launchpad-icon-scale':'launchpadIconScale'}[id];
  return settingsRange(id,label,Number(value),min,max,unit,disabled,'dock',key);
};
const inRange=(value,min,max)=>Number.isInteger(value)&&value>=min&&value<=max;

export function validDockState(message) {
  const keys=['version','type','enabled','running','material','reflection','iconScale','position','bottomOffset','iconSpacing','pinned','added',
    'materialStatus','loadStatus','error','result','saved'];
  const optional=['visibleItems','layersVisible','missingIcons','iconCachePaths','launchpadScale','launchpadIconScale'];
  return !!message&&typeof message==='object'&&!Array.isArray(message)&&
    keys.every(key=>Object.hasOwn(message,key))&&Object.keys(message).every(key=>keys.includes(key)||optional.includes(key))&&
    message.version===1&&message.type==='dock-state'&&
    ['enabled','running','saved','reflection'].every(key=>typeof message[key]==='boolean')&&
    ['liquid-glass','frosted-glass','none'].includes(message.material)&&inRange(message.iconScale,50,120)&&
    (message.launchpadScale===undefined||inRange(message.launchpadScale,60,140))&&
    (message.launchpadIconScale===undefined||inRange(message.launchpadIconScale,50,120))&&
    inRange(message.position,0,100)&&inRange(message.bottomOffset,0,160)&&inRange(message.iconSpacing,4,24)&&
    count(message.pinned)&&count(message.added)&&
    (message.visibleItems===undefined||count(message.visibleItems)) &&
    (message.layersVisible===undefined||typeof message.layersVisible==='boolean') &&
    (message.missingIcons===undefined||count(message.missingIcons)) &&
    (message.iconCachePaths===undefined||inRange(message.iconCachePaths,0,256)) &&
    ['inactive','native-ready','native-starting','transparent-fallback'].includes(message.materialStatus)&&
    ['fresh','loaded','recovered','failed'].includes(message.loadStatus)&&
    ['','runtime','save','rollback','launchpad','collection-popup','limit'].includes(message.error)&&['snapshot','applied'].includes(message.result)&&
    (!message.saved||(message.result==='applied'&&message.error===''&&message.loadStatus==='loaded'));
}

export function dockStatus(state) {
  if(state.dockStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.dockStatus==='saving')return t('正在保存...','Saving...');
  if(state.dockStatus==='unavailable')return t('桌面宿主未连接，Dock 设置未保存','Desktop host disconnected. Dock settings were not saved.');
  if(state.dockStatus==='error')return {
    runtime:t('Dock 启动失败，请检查安装文件','Could not start the Dock. Check the installation files.'),
    save:t('Dock 设置保存失败，请重试','Could not save Dock settings. Please try again.'),
    rollback:t('Dock 恢复失败，请重新启用','Could not restore the Dock. Enable it again.'),
    launchpad:t('启动台打开失败，请确认屏幕空间足够','Could not open the launchpad. Make sure the screen has enough space.'),
    'collection-popup':t('收纳组打开失败，请重试','Could not open the group. Please try again.'),
    limit:t('数量已达上限：Dock 最多固定 32 项，启动台最多添加 200 项','Limit reached: up to 32 Dock items and 200 added launchpad items.'),
  }[state.dockError]||t('Dock 配置读取失败，当前使用默认设置；尚未保存','Could not load Dock settings. Using defaults; not yet saved.');
  if(state.dockLoadStatus==='recovered')return t('已从备份恢复 Dock 配置，尚未保存到主配置','Dock settings restored from backup; not yet saved to the main configuration.');
  if(state.dockMaterialStatus==='transparent-fallback')return t('材质暂不可用，当前为透明显示','Material unavailable. Using a transparent fallback.');
  if(state.dockMaterialStatus==='native-starting')return t('正在准备 Dock 材质...','Preparing the Dock material...');
  return '';
}

export function dockNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.dockStatus))return dockStatus(state);
  if(state.dockStatus==='saved'&&previousStatus==='saving')return t('Dock 设置已应用并保存','Dock settings applied and saved');
  return null;
}

export function dockPage(state,tab='settings') {
  const title=t('Dock栏美化','Dock');
  const description=t('启动台与常用应用','Launchpad and favourite apps');
  const disabled=busy(state),resetLabel=t('恢复默认','Restore defaults');
  const summary=t(`已固定 ${state.dock.pinned} 项，启动台已添加 ${state.dock.added} 项`,
    `${state.dock.pinned} pinned, ${state.dock.added} added to the launchpad`);
  const body=tab==='settings'?
    `<div class="setting-row"><label for="dock-material">${t('材质设置','Material')}</label>${settingsSelect('dock-material',t('材质设置','Material'),state.dock.material,materials(),disabled)}</div>`+
    `<div class="setting-row"><div><label>${t('图标大小','Icon size')}</label><p class="setting-help">${t('按 1% 微调 Dock 的整体大小','Adjust the Dock size in 1% steps')}</p></div>${rangeControl('dock-icon-scale',t('图标大小','Icon size'),state.dock.iconScale,50,120,'%',disabled)}</div>`+
    `<div class="setting-row"><div><label>${t('图标位置','Icon position')}</label><p class="setting-help">${t('调整 Dock 距屏幕底部的高度，每次 1 px','Raise the Dock from the bottom in 1 px steps')}</p></div>${rangeControl('dock-bottom-offset',t('距底部上移','Move up from the bottom'),state.dock.bottomOffset??0,0,160,' px',disabled)}</div>`+
    `<div class="setting-row"><div><label>${t('图标间距','Icon spacing')}</label><p class="setting-help">${t('按 1 px 微调图标之间的距离','Adjust the gap in 1 px steps')}</p></div>${rangeControl('dock-icon-spacing',t('图标间距','Icon spacing'),state.dock.iconSpacing??10,4,24,' px',disabled)}</div>`+
    `<div class="setting-row"><div><label>${t('图标倒影','Icon reflection')}</label><p class="setting-help">${t('在 Dock 图标下方显示镜面倒影','Show a mirrored reflection under the Dock icons')}</p></div>${toggle(state.dock.reflection,t('图标倒影','Icon reflection'),'dock-reflection',disabled)}</div>`+
    `<div class="setting-row">${rangeControl('dock-launchpad-scale',t('启动台大小','Launchpad size'),state.dock.launchpadScale??100,60,140,'%',disabled)}</div>`+
    `<div class="setting-row">${rangeControl('dock-launchpad-icon-scale',t('启动台图标大小','Launchpad icon size'),state.dock.launchpadIconScale??100,50,120,'%',disabled)}</div>`+
    `<div class="setting-row"><label>${t('项目','Items')}</label><span class="dock-summary" data-dock-summary>${h(summary)}</span></div>`+
    `<div class="reset-row"><button type="button" class="reset" data-action="dock-reset" title="${h(resetLabel)}" ${disabled?'disabled':''}>${resetLabel}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About the Dock')}</h3>`+
    `<p>${t('Dock 显示在屏幕底部，第一个是启动台，第二个是 Lume，之后是固定的应用和正在运行的程序（下方圆点表示正在运行）。','The Dock sits at the bottom of the screen: the launchpad, Lume, then pinned apps and running programs (a dot marks running ones).')}</p>`+
    `<p>${t('启动台显示桌面上的图标（桌面本身不变），可以搜索，也可以用“添加”加入文件和文件夹。在启动台或 Dock 上右键可以固定或取消固定。','The launchpad shows your desktop icons (the desktop itself is unchanged); search them or use Add for files and folders. Right-click to pin or unpin.')}</p></div>`;
  return settingsFrame({title,description,tab,body,status:dockStatus(state),error:state.dockStatus==='error',
    scope:'dock',backLabel:t('返回桌面美化','Back to desktop styles')});
}

export function syncDockControls(root,state) {
  const disabled=busy(state);
  root.querySelectorAll('[data-action="dock-toggle"]').forEach(button=>{
    button.classList.toggle('checked',state.dock.enabled);
    button.setAttribute('aria-checked',String(state.dock.enabled));button.disabled=disabled;
    button.title=t('Dock栏美化','Dock');
  });
  syncSettingsSelect(root,'dock-material',state.dock.material,materials(),disabled,true);
  const material=root.querySelector('#dock-material');
  if(material)material.title=t('材质设置','Material');
  const syncRange=(id,value,min,max,unit)=>{
    syncSettingsRange(root,id,Number(value),min,max,unit,disabled);
  };
  syncRange('dock-icon-scale',state.dock.iconScale,50,120,'%');
  syncRange('dock-bottom-offset',state.dock.bottomOffset??0,0,160,' px');
  syncRange('dock-icon-spacing',state.dock.iconSpacing??10,4,24,' px');
  syncRange('dock-launchpad-scale',state.dock.launchpadScale??100,60,140,'%');
  syncRange('dock-launchpad-icon-scale',state.dock.launchpadIconScale??100,50,120,'%');
  root.querySelectorAll('[data-action="dock-reflection"]').forEach(button=>{
    button.classList.toggle('checked',state.dock.reflection);
    button.setAttribute('aria-checked',String(state.dock.reflection));syncSettingsControlBusy(button,disabled);
  });
  const reset=root.querySelector('[data-action="dock-reset"]');if(reset)syncSettingsControlBusy(reset,disabled);
  const summary=root.querySelector('[data-dock-summary]');
  if(summary)summary.textContent=t(`已固定 ${state.dock.pinned} 项，启动台已添加 ${state.dock.added} 项`,
    `${state.dock.pinned} pinned, ${state.dock.added} added to the launchpad`);
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
