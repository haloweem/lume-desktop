import {toggle,escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsControlBusy} from './settings-frame.js';
import {t} from './localization.js';
import {expandedWidgetPicker,syncExpandedWidgetPicker,validExpandedLayout,expandedSelectionSummary,decodeExpandedLayout,encodeExpandedLayout} from './expanded-widgets.js';
import {todoBoardMarkup} from './todo-board.js';
import {islandNotificationsPage,syncIslandNotifications} from './island-notifications.js';
import {islandStyles,syncIslandStyles} from './island-styles.js';

// Focus remains hidden; music, the calendar, to-dos, clock and performance
// are offered. The host keeps any saved layout and projects
// it the same way, so a hidden-only layout falls back to music.
const availableExpandedWidgets=[0,1,4,5,3];
const releasedLayout=code=>{
  const ids=decodeExpandedLayout(code??1);if(!ids)return 1;
  const shown=ids.filter(id=>availableExpandedWidgets.includes(id));
  return ids.length&&!shown.length?1:encodeExpandedLayout(shown);
};
const releaseState=state=>({...state,dynamicIsland:{...state.dynamicIsland,
  expandedLayout:releasedLayout(state.dynamicIsland.expandedLayout)}});

export function validIslandState(m) {
  const keys=['version','type','enabled','running','locked','expanded','focusing','finished','remaining',
    'hideFullscreen','topmost','hover','autoHide','size','material','loadStatus','error','result','saved'];
  const compact=Object.hasOwn(m||{},'leftWidget')&&Object.hasOwn(m||{},'rightWidget');
  const screens=Object.hasOwn(m||{},'screen')&&Array.isArray(m.screens);
  const animation=['hoverDelayMs','animationSpeed','bounce'].some(key=>Object.hasOwn(m||{},key));
  return !!m && typeof m==='object' && !Array.isArray(m) && Object.keys(m).length===keys.length+(Object.hasOwn(m,'expandedLayout')?1:0)+(Object.hasOwn(m,'clockStyle')?1:0)+(Object.hasOwn(m,'calendarStyle')?1:0)+(Object.hasOwn(m,'shape')?1:0)+(compact?2:0)+(screens?2:0)+(animation?3:0) &&
    keys.every(key=>Object.hasOwn(m,key)) && m.version===1 && m.type==='dynamicIsland-state' &&
    ['enabled','running','locked','expanded','focusing','finished','saved','hideFullscreen','topmost','hover','autoHide'].every(key=>typeof m[key]==='boolean') &&
    islandSizes.includes(m.size) && islandMaterials.includes(m.material) &&
    (!Object.hasOwn(m,'shape')||['standard','capsule'].includes(m.shape)) &&
    (!Object.hasOwn(m,'expandedLayout')||validExpandedLayout(m.expandedLayout)) &&
    (!Object.hasOwn(m,'clockStyle')||['simple','progress','digital','date','flip','pixel'].includes(m.clockStyle)) &&
    (!Object.hasOwn(m,'calendarStyle')||['default','chinese-almanac'].includes(m.calendarStyle)) &&
    (!compact||(Number.isInteger(m.leftWidget)&&m.leftWidget>=0&&m.leftWidget<=30&&Number.isInteger(m.rightWidget)&&m.rightWidget>=0&&m.rightWidget<=30))&&
    (!screens||(Number.isInteger(m.screen)&&m.screen>=0&&m.screen<=64&&m.screens.length>=1&&m.screens.length<=64&&m.screens.every((id,index)=>id===index+1)))&&
    (!animation||(validIslandHoverDelay(m.hoverDelayMs)&&islandAnimationSpeeds.includes(m.animationSpeed)&&islandBounces.includes(m.bounce)))&&
    Number.isInteger(m.remaining) && m.remaining>=0 && m.remaining<=1500 &&
    ['fresh','loaded','recovered','failed'].includes(m.loadStatus) && ['','runtime','save','rollback','screen'].includes(m.error) &&
    ['snapshot','applied'].includes(m.result) && (!m.running || m.enabled) &&
    (!m.saved || (m.result==='applied' && !m.error && m.loadStatus==='loaded'));
}
export const islandSizes=['small','standard','large'];
export const islandMaterials=['none','liquid-glass','frosted-glass'];
export const islandAnimationSpeeds=['slow','normal','fast'];
export const islandBounces=['subtle','normal','bouncy'];
export const validIslandHoverDelay=value=>Number.isInteger(value)&&value>=100&&value<=1000;
const delayLabel=value=>`${value/1000}s`;
export function previewIslandHoverDelay(slider,value) {
  if(!validIslandHoverDelay(value))return false;
  slider.value=String(value);slider.setAttribute('aria-valuetext',delayLabel(value));
  const output=slider.closest('[data-island-delay-range]')?.querySelector('output');
  if(output)output.textContent=delayLabel(value);
  return true;
}
export function validIslandPreview(m) {
  const keys=['version','type','mode','shape','material','size','width','height','image','error'];
  const compact=Object.hasOwn(m||{},'leftWidget')&&Object.hasOwn(m||{},'rightWidget')&&Array.isArray(m.readings);
  const animation=Object.hasOwn(m||{},'animationSpeed')||Object.hasOwn(m||{},'bounce');
  return !!m&&typeof m==='object'&&!Array.isArray(m)&&Object.keys(m).length===keys.length+(Object.hasOwn(m,'expandedLayout')?1:0)+(Object.hasOwn(m,'animating')?1:0)+(Object.hasOwn(m,'clockStyle')?1:0)+(Object.hasOwn(m,'calendarStyle')?1:0)+(compact?3:0)+(animation?2:0)&&keys.every(key=>Object.hasOwn(m,key))&&
    (!Object.hasOwn(m,'expandedLayout')||validExpandedLayout(m.expandedLayout))&&
    (!Object.hasOwn(m,'clockStyle')||['simple','progress','digital','date','flip','pixel'].includes(m.clockStyle))&&
    (!Object.hasOwn(m,'calendarStyle')||['default','chinese-almanac'].includes(m.calendarStyle))&&
    (!animation||(islandAnimationSpeeds.includes(m.animationSpeed)&&islandBounces.includes(m.bounce)))&&
    (!compact||(Number.isInteger(m.leftWidget)&&m.leftWidget>=0&&m.leftWidget<=30&&Number.isInteger(m.rightWidget)&&m.rightWidget>=0&&m.rightWidget<=30&&
      m.readings.length<=30&&m.readings.every(row=>row&&typeof row==='object'&&Object.keys(row).length===5&&
        Number.isInteger(row.id)&&row.id>=1&&row.id<=30&&typeof row.text==='string'&&row.text.length<=80&&
        typeof row.secondary==='string'&&row.secondary.length<=80&&typeof row.available==='boolean'&&
        (row.progress===null||typeof row.progress==='number'&&row.progress>=0&&row.progress<=1))))&&
    (!Object.hasOwn(m,'animating')||typeof m.animating==='boolean')&&
    m.version===1&&m.type==='dynamicIsland-preview'&&['compact','expanded'].includes(m.mode)&&['standard','capsule'].includes(m.shape)&&
    islandMaterials.includes(m.material)&&islandSizes.includes(m.size)&&['','render'].includes(m.error)&&
    Number.isInteger(m.width)&&Number.isInteger(m.height)&&typeof m.image==='string'&&
    (m.error==='render'?m.image===''&&m.width===0&&m.height===0:
      m.width===960&&m.height===300&&m.image.length<1500000&&/^data:image\/(png|jpeg);base64,[A-Za-z0-9+/]+=*$/.test(m.image));
}
const materialOptions=()=>[['none',t('纯黑','Black')],['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
const sizeOptions=()=>[['small',t('小','Small')],['standard',t('标准','Standard')],['large',t('大','Large')]];
// Display switches: [action, state key, label, hint].
const displayToggles=()=>[
  ['hideFullscreen','hideFullscreen',t('全屏时隐藏','Hide in fullscreen'),t('全屏应用在前台时隐藏灵动岛；独占游戏和系统演示保护始终优先。','Hide the island while a fullscreen app is in front. Exclusive games and system presentation protection always take priority.')],
  ['autoHide','autoHide',t('空闲时隐藏','Hide when idle'),t('闲置几秒后收到屏幕最上方，只露出一条边；鼠标移上去即滑出，切歌或暂停时也会短暂出现','After a few idle seconds it slides up against the top of the screen, leaving a sliver; hover to bring it back. It also peeks out on track changes')],
];
export function islandStatus(state) {
  const status=state.dynamicIslandStatus;
  if(status==='loading')return t('正在读取设置…','Loading settings…');
  if(status==='saving')return '';
  if(status==='unavailable')return t('桌面宿主未连接，设置未保存','Desktop host disconnected. Settings were not saved.');
  if(status==='error'&&state.dynamicIslandError==='screen')return t('所选屏幕已断开，请选择其他屏幕','The selected screen was disconnected. Choose another screen.');
  if(status==='error')return state.dynamicIslandError==='runtime'
    ? t('灵动岛启动失败，请重试','Could not start the island. Please try again.')
    : t('设置读取或保存失败，原有配置已保留','Could not load or save settings. Your previous settings were preserved.');
  if(status==='recovered')return t('已从备份读取配置，下一次修改后保存','Settings restored from backup; changes will save them.');
  return '';
}
const busy=state=>['loading','saving','unavailable'].includes(state.dynamicIslandStatus);
const screenIds=p=>p.screens?.length?p.screens:[1];
function screenButtons(p,disabled) {
  return [...screenIds(p).map(id=>[id,`${t('屏幕','Screen')} ${id}`]),[0,t('全部屏幕','All screens')]]
    .map(([id,label])=>`<button type="button" data-island-screen="${id}" aria-pressed="${(p.screen??1)===id}" ${disabled?'disabled':''}>${label}</button>`).join('');
}
function islandHome(state) {
  const p=state.dynamicIsland;
  return `<div class="island-home" data-island-home>
    <aside class="island-appearance" aria-label="${t('外观','Appearance')}">
      <h1 class="settings-component-title">${t('灵动岛','Dynamic Island')}</h1>
      <div class="island-appearance__scroll">
      <div class="island-appearance__head"><h2>${t('外观','Appearance')}</h2><button type="button" data-action="dynamicIsland-resetAppearance" ${busy(state)?'disabled':''}>↺ ${t('重置','Reset')}</button></div>
      <div class="island-appearance__body">
        <div class="island-appearance__row island-appearance__row--inline">
          <div class="island-appearance__label" id="island-shape-label">${t('灵动岛形态','Island shape')}</div>
          <div class="island-appearance__segments" role="group" aria-labelledby="island-shape-label">
            ${[['standard',t('标准形态','Standard')],['capsule',t('胶囊形态','Capsule')]].map(([shape,label])=>`<button type="button" data-island-shape="${shape}" aria-pressed="${(p.shape||'capsule')===shape}" ${busy(state)?'disabled':''}>${label}</button>`).join('')}
          </div>
        </div>
        <div class="island-appearance__row">
          <div class="island-appearance__label" id="island-material-label">${t('材质','Material')}</div>
          <div class="island-appearance__segments" role="group" aria-labelledby="island-material-label">
            ${materialOptions().map(([value,label])=>`<button type="button" data-island-preference="material" data-value="${value}" aria-pressed="${p.material===value}" ${busy(state)?'disabled':''}>${label}</button>`).join('')}
          </div>
        </div>
        <div class="island-appearance__row">
          <div class="island-appearance__label" id="island-size-label">${t('大小','Size')}</div>
          <div class="island-appearance__segments" role="group" aria-labelledby="island-size-label">
            ${sizeOptions().map(([value,label])=>`<button type="button" data-island-preference="size" data-value="${value}" aria-pressed="${p.size===value}" ${busy(state)?'disabled':''}>${label}</button>`).join('')}
          </div>
        </div>
      </div>
      <section class="island-display" aria-label="${t('显示','Display')}">
        <div class="island-appearance__head"><h2>${t('显示','Display')}</h2></div>
        <div class="island-appearance__body">
          <div class="island-appearance__row">
            <div class="island-appearance__label" id="island-screen-label">${t('显示策略','Display policy')}</div>
            <div class="island-appearance__segments island-display__screens" role="group" aria-labelledby="island-screen-label" data-island-screen-options="${screenIds(p).join(',')}">${screenButtons(p,busy(state))}</div>
            <p class="island-display__hint" data-island-screen-unavailable ${p.screen>0&&!screenIds(p).includes(p.screen)?'':'hidden'}>${t('所选屏幕未连接，暂在主屏显示','The selected screen is disconnected. Showing on the primary screen for now.')}</p>
          </div>
        </div>
      </section>
      <section class="island-expansion" aria-labelledby="island-expansion-label">
        <div class="island-appearance__head"><h2 id="island-expansion-label">${t('展开模式','Expansion mode')}</h2></div>
        <div class="island-appearance__body">
          <div class="island-appearance__row">
            <div class="island-appearance__segments" role="group" aria-labelledby="island-expansion-label">
              ${[['click',t('点击展开','Click to expand')],['hover',t('悬停展开','Hover to expand')]].map(([mode,label])=>`<button type="button" data-island-expansion="${mode}" aria-pressed="${p.hover===(mode==='hover')}" ${busy(state)?'disabled':''}>${label}</button>`).join('')}
            </div>
          </div>
        </div>
      </section>
      <section class="island-animation" aria-labelledby="island-animation-label">
        <div class="island-appearance__head"><h2 id="island-animation-label">${t('动画设置','Animation settings')}</h2></div>
        <div class="island-appearance__body">
          <div class="island-appearance__row">
            <div class="setting-range" data-island-delay-range>
              <label class="island-appearance__label" for="island-hover-delay">${t('悬停延迟','Hover delay')}</label>
              <div class="island-animation__delay">
                <input id="island-hover-delay" data-island-hover-delay type="range" min="100" max="1000" step="50" value="${p.hoverDelayMs??300}" aria-valuetext="${delayLabel(p.hoverDelayMs??300)}" aria-describedby="island-hover-delay-hint" ${busy(state)?'disabled':''}>
                <output for="island-hover-delay">${delayLabel(p.hoverDelayMs??300)}</output>
              </div>
              <span class="island-animation__hint" id="island-hover-delay-hint">${t('鼠标悬停后自动展开的等待时间，0.1～1 秒','Wait before expanding on hover, from 0.1 to 1 second')}</span>
            </div>
          </div>
          <div class="island-appearance__row">
            <div class="island-appearance__label" id="island-speed-label">${t('展开 / 收缩速度','Expand / collapse speed')}</div>
            <div class="island-appearance__segments" role="group" aria-labelledby="island-speed-label">
              ${[['slow',t('非常慢','Very slow')],['normal',t('适中','Moderate')],['fast',t('非常快','Very fast')]].map(([value,label])=>`<button type="button" data-island-preference="animationSpeed" data-value="${value}" aria-pressed="${(p.animationSpeed??'normal')===value}" ${busy(state)?'disabled':''}>${label}</button>`).join('')}
            </div>
          </div>
          <div class="island-appearance__row">
            <div class="island-appearance__label" id="island-bounce-label">${t('弹动幅度','Bounce amount')}</div>
            <div class="island-appearance__segments" role="group" aria-labelledby="island-bounce-label">
              ${[['subtle',t('轻微','Subtle')],['normal',t('常规','Normal')],['bouncy',t('弹动','Bouncy')]].map(([value,label])=>`<button type="button" data-island-preference="bounce" data-value="${value}" aria-pressed="${(p.bounce??'normal')===value}" ${busy(state)?'disabled':''}>${label}</button>`).join('')}
            </div>
          </div>
        </div>
      </section>
      </div>
    </aside>
    <div class="island-home__main">
    <div class="island-home__components-scroll">
    <section class="island-component-workspace is-expanded" data-island-component-workspace>
    <div class="island-component-workspace__head">
      <div class="island-component-workspace__title"><h2 data-island-component-title>${t('展开态组件','Expanded widgets')}</h2><span class="island-component-summary" data-island-component-summary aria-label="${t('已启用组件','Enabled widgets')}">${expandedSelectionSummary(p.expandedLayout??1)}</span></div>
      <p data-island-component-hint>${t('点击开关启用音乐、日历、待办、时间或性能','Use the switches to enable music, calendar, to-dos, clock or performance.')}</p>
    </div>
    <div data-island-expanded-panel>${expandedWidgetPicker(state,availableExpandedWidgets)}</div>
    ${islandStyles(state)}
    </section>
    </div>
    </div>
  </div>`;
}
export function islandPage(state,tab='settings') {
  state=releaseState(state);
  const p=state.dynamicIsland,disabled=busy(state);
  const button=(action,label,inactive=false)=>`<button type="button" class="reset" data-action="dynamicIsland-${action}" ${inactive?'disabled':''}>${label}</button>`;
  const preferences=
    `<div class="setting-row island-option"><label>${t('显示层级','Display layer')}<small>${t('桌面层位于应用后方；应用上方可覆盖普通窗口，不抢占焦点。','The desktop layer stays behind apps. Above applications shows over ordinary windows without taking focus.')}</small></label>${settingsSelect('dynamicIsland-layer',t('显示层级','Display layer'),p.topmost?'above':'desktop',[
      ['desktop',t('桌面层','Desktop layer')],['above',t('应用上方','Above applications')]],disabled)}</div>`+
    displayToggles().map(([action,key,label,hint])=>`<div class="setting-row island-option"><label>${label}<small>${hint}</small></label>${toggle(p[key],label,`dynamicIsland-${action}`,disabled)}</div>`).join('')+
    `<div class="reset-row">${button('reset',t('恢复默认设置','Restore defaults'),disabled)}</div>`;
  const details=
    `<div class="settings-details"><h3>${t('功能说明','About the island')}</h3><p>${t('胶囊固定在顶部中央，点击展开或收起；展开后鼠标离开几秒会自动收起。','The capsule stays centered at the top and expands or collapses when clicked; it collapses a few seconds after the pointer leaves.')}</p><p>${t('默认在全屏程序和游戏前台时完全隐藏，不抢焦点。','By default it hides completely in front of fullscreen apps and games and never takes focus.')}</p><p>${t('展开后点击其他任意位置即收起；右键胶囊可切换材质。','When expanded, clicking anywhere else collapses it; right-click the capsule to change the material.')}</p><p>${t('播放音乐时显示封面、歌曲、歌手、进度和节奏频谱，并提供切歌与播放控制。','When music is playing, it shows the cover, track, artist, progress and beat spectrum with playback controls.')}</p></div>`;
  const labels={home:t('首页','Home'),music:t('音乐','Music'),todo:t('待办','To-do'),'quick-launch':t('快捷启动','Quick launch'),clock:t('时钟','Clock'),notifications:t('通知','Notifications'),settings:t('设置','Settings'),details:t('详情','Details')};
  const body=tab==='home'?islandHome(state):tab==='settings'?preferences:tab==='details'?details:tab==='todo'?todoBoardMarkup('island'):tab==='notifications'?islandNotificationsPage(state):
    `<div class="island-editor"><h2>${labels[tab]||labels.home}</h2><p class="island-editor__empty">${t('此模块的灵动岛参数将在后续接入。','Island options for this module will be added later.')}</p></div>`;
  return settingsFrame({title:t('灵动岛','Dynamic Island'),description:t('音乐播放信息与控制','Music playback information and controls'),
    tab,body,status:islandStatus(state),error:state.dynamicIslandStatus==='error',scope:'dynamicIsland',backLabel:t('返回桌面美化','Back to desktop styles'),variant:'island'});
}
export function syncIslandControls(root,state) {
  syncIslandNotifications(root,state);
  state=releaseState(state);
  const p=state.dynamicIsland,disabled=busy(state);
  const syncBusy=(control,inactive=disabled)=>{
    syncSettingsControlBusy(control,inactive);
    control.toggleAttribute('data-island-pending',state.dynamicIslandStatus==='saving'&&inactive);
  };
  const home=root.querySelector('[data-island-home]');
  if(home) {
    const summary=home.querySelector('[data-island-component-summary]');
    if(summary.dataset.layout!==String(p.expandedLayout??1)){
      summary.dataset.layout=String(p.expandedLayout??1);summary.innerHTML=expandedSelectionSummary(p.expandedLayout??1);
    }
    home.querySelectorAll('button[data-island-shape]').forEach(button=>{
      button.setAttribute('aria-pressed',String(button.dataset.islandShape===(p.shape||'capsule')));
      syncBusy(button);
    });
    home.querySelectorAll('[data-island-preference]').forEach(button=>{
      button.setAttribute('aria-pressed',String(p[button.dataset.islandPreference]===button.dataset.value));
      syncBusy(button);
    });
    const screens=home.querySelector('[data-island-screen-options]');
    if(screens){
      const ids=screenIds(p).join(',');
      if(screens.dataset.islandScreenOptions!==ids){screens.dataset.islandScreenOptions=ids;screens.innerHTML=screenButtons(p,disabled);}
      screens.querySelectorAll('[data-island-screen]').forEach(button=>{
        button.setAttribute('aria-pressed',String(Number(button.dataset.islandScreen)===(p.screen??1)));
        syncBusy(button);
      });
    }
    const unavailable=home.querySelector('[data-island-screen-unavailable]');
    if(unavailable)unavailable.hidden=!(p.screen>0&&!screenIds(p).includes(p.screen));
    home.querySelectorAll('[data-island-expansion]').forEach(button=>{
      button.setAttribute('aria-pressed',String(p.hover===(button.dataset.islandExpansion==='hover')));
      syncBusy(button);
    });
    const delay=home.querySelector('[data-island-hover-delay]');
    if(delay){
      const submitted=Number(delay.dataset.submitted);
      const pending=state.dynamicIslandStatus==='saving';
      previewIslandHoverDelay(delay,pending&&validIslandHoverDelay(submitted)?submitted:(p.hoverDelayMs??300));
      if(!pending)delete delay.dataset.submitted;
      syncBusy(delay);
    }
    syncIslandStyles(home,state);
    syncExpandedWidgetPicker(home,state);
  }
  for(const [action,value] of [['toggle',p.enabled],...displayToggles().map(([a,key])=>[a,p[key]])]) {
    root.querySelectorAll(`[data-action="dynamicIsland-${action}"]`).forEach(button=>{
      button.classList.toggle('checked',value);button.setAttribute('aria-checked',String(value));
      syncBusy(button);
    });
  }
  const layer=root.querySelector('#dynamicIsland-layer');
  if(layer){
    syncBusy(layer);
    const value=p.topmost?'above':'desktop';
    const options=root.querySelector('#dynamicIsland-layer-options');
    options?.querySelectorAll('[data-select-value]').forEach(option=>{
      const selected=option.dataset.selectValue===value;option.setAttribute('aria-selected',String(selected));
      option.querySelector('span').textContent=selected?'✓':'';
      if(selected)layer.querySelector('span').textContent=option.childNodes[0].textContent;
      syncBusy(option);
    });
  }
  root.querySelectorAll('[data-action^="dynamicIsland-"]').forEach(button=>{
    const action=button.dataset.action.slice('dynamicIsland-'.length);
    if(['toggle','lock','hideFullscreen','topmost','hover','autoHide'].includes(action))return;
    syncBusy(button,disabled||(!['reset','resetAppearance'].includes(action)&&!p.enabled));
    if(['expand','collapse'].includes(action)) {
      button.dataset.action=`dynamicIsland-${p.expanded?'collapse':'expand'}`;
      button.textContent=p.expanded?t('收起','Collapse'):t('展开','Expand');
    }
  });
  const status=root.querySelector('[data-settings-scope="dynamicIsland"] .status');
  if(status){status.textContent=islandStatus(state);status.hidden=!status.textContent;status.setAttribute('role',state.dynamicIslandStatus==='error'?'alert':'status');}
}
