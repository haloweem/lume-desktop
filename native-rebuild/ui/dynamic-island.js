import {toggle} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect,syncSettingsControlBusy} from './settings-frame.js';
import {t} from './localization.js';

export function validIslandState(m) {
  const keys=['version','type','enabled','running','locked','expanded','focusing','finished','remaining',
    'hideFullscreen','topmost','hover','autoHide','size','material','loadStatus','error','result','saved'];
  return !!m && typeof m==='object' && !Array.isArray(m) && Object.keys(m).length===keys.length &&
    keys.every(key=>Object.hasOwn(m,key)) && m.version===1 && m.type==='dynamicIsland-state' &&
    ['enabled','running','locked','expanded','focusing','finished','saved','hideFullscreen','topmost','hover','autoHide'].every(key=>typeof m[key]==='boolean') &&
    islandSizes.includes(m.size) && islandMaterials.includes(m.material) &&
    Number.isInteger(m.remaining) && m.remaining>=0 && m.remaining<=1500 &&
    ['fresh','loaded','recovered','failed'].includes(m.loadStatus) && ['','runtime','save','rollback'].includes(m.error) &&
    ['snapshot','applied'].includes(m.result) && (!m.running || m.enabled) &&
    (!m.saved || (m.result==='applied' && !m.error && m.loadStatus==='loaded'));
}
export const islandSizes=['small','standard','large'];
export const islandMaterials=['none','liquid-glass','frosted-glass'];
const materialOptions=()=>[['none',t('纯黑','Black')],['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
const sizeOptions=()=>[['small',t('小','Small')],['standard',t('标准','Standard')],['large',t('大','Large')]];
// Display switches: [action, state key, label, hint].
const displayToggles=()=>[
  ['autoHide','autoHide',t('无活动时自动隐藏','Auto-hide when idle'),t('闲置几秒后收到屏幕最上方，只露出一条边；鼠标移上去即滑出，切歌或暂停时也会短暂出现','After a few idle seconds it slides up against the top of the screen, leaving a sliver; hover to bring it back. It also peeks out on track changes')],
  ['hideFullscreen','hideFullscreen',t('全屏和游戏时隐藏','Hide for fullscreen apps and games'),t('全屏程序、游戏或演示在前台时立即让开，不遮挡画面','Steps aside at once while a fullscreen app, game or presentation is in front')],
  ['topmost','topmost',t('显示在窗口上方','Show above windows'),t('关闭后只在桌面上可见','When off, it is only visible on the desktop')],
  ['hover','hover',t('悬停自动展开','Expand on hover'),t('鼠标停留片刻即展开，移开后收起','Expands after a short rest and collapses when the pointer leaves')],
];
export function islandStatus(state) {
  const status=state.dynamicIslandStatus;
  if(status==='loading')return t('正在读取设置…','Loading settings…');
  if(status==='saving')return t('正在应用…','Applying…');
  if(status==='unavailable')return t('桌面宿主未连接，设置未保存','Desktop host disconnected. Settings were not saved.');
  if(status==='error')return state.dynamicIslandError==='runtime'
    ? t('灵动岛启动失败，请重试','Could not start the island. Please try again.')
    : t('设置读取或保存失败，原有配置已保留','Could not load or save settings. Your previous settings were preserved.');
  if(status==='recovered')return t('已从备份读取配置，下一次修改后保存','Settings restored from backup; changes will save them.');
  return '';
}
const busy=state=>['loading','saving','unavailable'].includes(state.dynamicIslandStatus);
export function islandPage(state,tab='settings') {
  const p=state.dynamicIsland,disabled=busy(state),off=disabled||!p.enabled;
  const button=(action,label,inactive=false)=>`<button type="button" class="reset" data-action="dynamicIsland-${action}" ${inactive?'disabled':''}>${label}</button>`;
  const body=tab==='settings' ?
    `<div class="island-preview" aria-hidden="true"><span class="island-cover"></span><span class="island-title">${t('正在播放','Now playing')}</span><span class="island-bars"><i></i><i></i><i></i><i></i></span></div>`+
    `<div class="setting-row"><label>${t('启用灵动岛','Enable Dynamic Island')}</label>${toggle(p.enabled,t('灵动岛','Dynamic Island'),'dynamicIsland-toggle',disabled)}</div>`+
    `<div class="setting-row"><label for="dynamicIsland-material">${t('材质','Material')}</label>${settingsSelect('dynamicIsland-material',t('材质','Material'),p.material,materialOptions(),disabled)}</div>`+
    `<div class="setting-row"><label for="dynamicIsland-size">${t('大小','Size')}</label>${settingsSelect('dynamicIsland-size',t('大小','Size'),p.size,sizeOptions(),disabled)}</div>`+
    displayToggles().map(([action,key,label,hint])=>`<div class="setting-row island-option"><label>${label}<small>${hint}</small></label>${toggle(p[key],label,`dynamicIsland-${action}`,disabled)}</div>`).join('')+
    `<div class="setting-row"><label>${t('展开面板','Expanded panel')}</label>${button(p.expanded?'collapse':'expand',p.expanded?t('收起','Collapse'):t('展开','Expand'),off)}</div>`+
    `<div class="reset-row">${button('reset',t('恢复默认设置','Restore defaults'),disabled)}</div>` :
    `<div class="settings-details"><h3>${t('功能说明','About the island')}</h3><p>${t('胶囊固定在顶部中央，点击展开或收起；展开后鼠标离开几秒会自动收起。','The capsule stays centered at the top and expands or collapses when clicked; it collapses a few seconds after the pointer leaves.')}</p><p>${t('默认在全屏程序和游戏前台时完全隐藏，不抢焦点。','By default it hides completely in front of fullscreen apps and games and never takes focus.')}</p><p>${t('展开后点击其他任意位置即收起；右键胶囊可切换材质。','When expanded, clicking anywhere else collapses it; right-click the capsule to change the material.')}</p><p>${t('播放音乐时显示封面、歌曲、歌手、进度和节奏频谱，并提供切歌与播放控制。','When music is playing, it shows the cover, track, artist, progress and beat spectrum with playback controls.')}</p></div>`;
  return settingsFrame({title:t('灵动岛','Dynamic Island'),description:t('音乐播放信息与控制','Music playback information and controls'),
    tab,body,status:islandStatus(state),error:state.dynamicIslandStatus==='error',scope:'dynamicIsland',backLabel:t('返回桌面美化','Back to desktop styles')});
}
export function syncIslandControls(root,state) {
  const p=state.dynamicIsland,disabled=busy(state);
  for(const [action,value] of [['toggle',p.enabled],...displayToggles().map(([a,key])=>[a,p[key]])]) {
    root.querySelectorAll(`[data-action="dynamicIsland-${action}"]`).forEach(button=>{
      button.classList.toggle('checked',value);button.setAttribute('aria-checked',String(value));
      syncSettingsControlBusy(button,disabled);
    });
  }
  root.querySelectorAll('[data-action^="dynamicIsland-"]').forEach(button=>{
    const action=button.dataset.action.slice('dynamicIsland-'.length);
    if(['toggle','lock','hideFullscreen','topmost','hover','autoHide'].includes(action))return;
    syncSettingsControlBusy(button,disabled||(action!=='reset'&&!p.enabled));
    if(['expand','collapse'].includes(action)) {
      button.dataset.action=`dynamicIsland-${p.expanded?'collapse':'expand'}`;
      button.textContent=p.expanded?t('收起','Collapse'):t('展开','Expand');
    }
  });
  syncSettingsSelect(root,'dynamicIsland-size',p.size,sizeOptions(),disabled,true);
  syncSettingsSelect(root,'dynamicIsland-material',p.material,materialOptions(),disabled,true);
  const status=root.querySelector('[data-settings-scope="dynamicIsland"] .status');
  if(status){status.textContent=islandStatus(state);status.hidden=!status.textContent;status.setAttribute('role',state.dynamicIslandStatus==='error'?'alert':'status');}
}
