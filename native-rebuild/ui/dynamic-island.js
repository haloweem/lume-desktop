import {toggle,escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsControlBusy} from './settings-frame.js';
import {t} from './localization.js';
import {compactWidgetPicker,compactSelectionLabel,syncCompactWidgetPicker} from './compact-widgets.js';

export function validIslandState(m) {
  const keys=['version','type','enabled','running','locked','expanded','focusing','finished','remaining',
    'hideFullscreen','topmost','hover','autoHide','size','material','loadStatus','error','result','saved'];
  const compact=Object.hasOwn(m||{},'leftWidget')&&Object.hasOwn(m||{},'rightWidget');
  const screens=Object.hasOwn(m||{},'screen')&&Array.isArray(m.screens);
  return !!m && typeof m==='object' && !Array.isArray(m) && Object.keys(m).length===keys.length+(Object.hasOwn(m,'shape')?1:0)+(compact?2:0)+(screens?2:0) &&
    keys.every(key=>Object.hasOwn(m,key)) && m.version===1 && m.type==='dynamicIsland-state' &&
    ['enabled','running','locked','expanded','focusing','finished','saved','hideFullscreen','topmost','hover','autoHide'].every(key=>typeof m[key]==='boolean') &&
    islandSizes.includes(m.size) && islandMaterials.includes(m.material) &&
    (!Object.hasOwn(m,'shape')||['standard','capsule'].includes(m.shape)) &&
    (!compact||(Number.isInteger(m.leftWidget)&&m.leftWidget>=0&&m.leftWidget<=30&&Number.isInteger(m.rightWidget)&&m.rightWidget>=0&&m.rightWidget<=30))&&
    (!screens||(Number.isInteger(m.screen)&&m.screen>=0&&m.screen<=64&&m.screens.length>=1&&m.screens.length<=64&&m.screens.every((id,index)=>id===index+1)))&&
    Number.isInteger(m.remaining) && m.remaining>=0 && m.remaining<=1500 &&
    ['fresh','loaded','recovered','failed'].includes(m.loadStatus) && ['','runtime','save','rollback','screen'].includes(m.error) &&
    ['snapshot','applied'].includes(m.result) && (!m.running || m.enabled) &&
    (!m.saved || (m.result==='applied' && !m.error && m.loadStatus==='loaded'));
}
export const islandSizes=['small','standard','large'];
export const islandMaterials=['none','liquid-glass','frosted-glass'];
export function validIslandPreview(m) {
  const keys=['version','type','mode','shape','material','size','width','height','image','error'];
  const compact=Object.hasOwn(m||{},'leftWidget')&&Object.hasOwn(m||{},'rightWidget')&&Array.isArray(m.readings);
  return !!m&&typeof m==='object'&&!Array.isArray(m)&&Object.keys(m).length===keys.length+(Object.hasOwn(m,'animating')?1:0)+(compact?3:0)&&keys.every(key=>Object.hasOwn(m,key))&&
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
  const expanded=state.dynamicIslandPreviewMode==='expanded';
  return `<div class="island-home" data-island-home>
    <aside class="island-appearance" aria-label="${t('外观','Appearance')}">
      <h1 class="settings-component-title">${t('灵动岛','Dynamic Island')}</h1>
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
    </aside>
    <div class="island-home__main">
    <section class="island-home__preview-shell" aria-label="${t('灵动岛实时预览','Dynamic Island live preview')}">
      <div class="island-home__preview-stage ${expanded?'is-expanded':'is-compact'}" data-island-preview-stage data-island-shape="${p.shape||'capsule'}" data-island-size="${p.size}" data-island-material="${p.material}">
        <img class="island-home__native-preview" data-island-native-preview alt="${t('灵动岛原生渲染预览','Native island preview')}" hidden>
        <span class="island-home__preview-wait" data-island-preview-wait>${t('正在读取桌面预览…','Loading desktop preview…')}</span>
        <div class="island-preview-sides" data-island-preview-sides role="group" aria-label="${t('选择组件位置','Choose widget side')}">
          <button type="button" data-island-compact-side="left" aria-label="${t('编辑左侧组件','Edit left widget')}"></button>
          <button type="button" data-island-compact-side="right" aria-label="${t('编辑右侧组件','Edit right widget')}"></button>
        </div>
        <span class="island-preview-selection" data-island-preview-selection>${compactSelectionLabel(state)}</span>
        <div class="island-home__preview-stage-modes" role="group" aria-label="${t('预览状态','Preview state')}">
          <button type="button" data-island-preview-mode="compact" aria-pressed="${!expanded}">${t('收缩','Collapse')}</button>
          <button type="button" data-island-preview-mode="expanded" aria-pressed="${expanded}">${t('展开','Expand')}</button>
        </div>
      </div>
    </section>
    ${compactWidgetPicker(state)}
    </div>
  </div>`;
}
export function islandPage(state,tab='settings') {
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
  const body=tab==='home'?islandHome(state):tab==='settings'?preferences:tab==='details'?details:
    `<div class="island-editor"><h2>${labels[tab]||labels.home}</h2><p class="island-editor__empty">${t('此模块的灵动岛参数将在后续接入。','Island options for this module will be added later.')}</p></div>`;
  return settingsFrame({title:t('灵动岛','Dynamic Island'),description:t('音乐播放信息与控制','Music playback information and controls'),
    tab,body,status:islandStatus(state),error:state.dynamicIslandStatus==='error',scope:'dynamicIsland',backLabel:t('返回桌面美化','Back to desktop styles'),variant:'island'});
}
const previewImages=new WeakMap();
function presentPreview(stage,frame,matches) {
  const image=stage.querySelector('[data-island-native-preview]'),wait=stage.querySelector('[data-island-preview-wait]');
  if(!image)return;
  let record=previewImages.get(image);
  if(!record){record={requested:'',shown:'',pending:null};previewImages.set(image,record);}
  // Keep the last decoded frame while settings are acknowledged and the new
  // native image is prepared. Invalidate any in-flight image from an old choice.
  if(frame&&!matches){record.pending=null;record.requested=record.shown;}
  if(!frame){
    record.requested='';record.shown='';record.pending=null;image.removeAttribute('src');image.hidden=true;
    stage.classList.remove('has-native-preview');
  }
  if(matches&&record.requested!==frame.image) {
    record.requested=frame.image;
    const next=new Image();record.pending=next;
    next.onload=()=>{
      if(!image.isConnected||record.pending!==next)return;
      image.src=next.src;record.shown=frame.image;record.pending=null;image.hidden=false;
      stage.classList.add('has-native-preview');if(wait)wait.hidden=true;
    };
    next.onerror=()=>{
      if(record.pending!==next)return;
      record.pending=null;record.requested='';
      if(wait&&!record.shown){wait.hidden=false;wait.textContent=t('预览暂不可用','Preview temporarily unavailable');}
    };
    next.src=frame.image;
  }
  if(wait){wait.hidden=!!record.shown;wait.textContent=frame?.error?t('预览暂不可用','Preview temporarily unavailable'):t('正在读取桌面预览…','Loading desktop preview…');}
}
export function syncIslandControls(root,state) {
  const p=state.dynamicIsland,disabled=busy(state);
  const home=root.querySelector('[data-island-home]');
  if(home) {
    const expanded=state.dynamicIslandPreviewMode==='expanded';
    const stage=home.querySelector('[data-island-preview-stage]');
    if(stage) {
      stage.classList.toggle('is-expanded',expanded);
      stage.classList.toggle('is-compact',!expanded);
      stage.dataset.islandShape=p.shape||'capsule';
      stage.dataset.islandSize=p.size;
      stage.dataset.islandMaterial=p.material;
      const frame=state.dynamicIslandPreview;
      const matches=frame&&!frame.error&&frame.mode===(expanded?'expanded':'compact')&&frame.shape===(p.shape||'capsule')&&frame.material===p.material&&frame.size===p.size&&
        (frame.leftWidget===undefined||frame.leftWidget===(p.leftWidget??0)&&frame.rightWidget===(p.rightWidget??0));
      presentPreview(stage,frame,matches);
      const scale=p.size==='small'?.86:p.size==='large'?1.16:1;
      const width=(expanded?372:(p.leftWidget||p.rightWidget)?360:236)*scale;
      const height=(expanded?156:(p.leftWidget||p.rightWidget)?36:34)*scale;
      const top=(p.shape==='standard'?0:8)*scale;
      const hit=stage.querySelector('[data-island-preview-sides]');
      hit.hidden=expanded;hit.style.width=`${width/960*100}%`;
      hit.style.top=`${top/300*100}%`;hit.style.height=`${height/300*100}%`;
      const label=stage.querySelector('[data-island-preview-selection]');
      label.hidden=expanded;label.textContent=compactSelectionLabel(state);
      label.style.top=`${(top+height+10)/300*100}%`;
    }
    home.querySelectorAll('[data-island-preview-mode]').forEach(button=>{
      button.setAttribute('aria-pressed',String(button.dataset.islandPreviewMode===(expanded?'expanded':'compact')));
    });
    home.querySelectorAll('button[data-island-shape]').forEach(button=>{
      button.setAttribute('aria-pressed',String(button.dataset.islandShape===(p.shape||'capsule')));
      syncSettingsControlBusy(button,disabled);
    });
    home.querySelectorAll('[data-island-preference]').forEach(button=>{
      button.setAttribute('aria-pressed',String(p[button.dataset.islandPreference]===button.dataset.value));
      syncSettingsControlBusy(button,disabled);
    });
    const screens=home.querySelector('[data-island-screen-options]');
    if(screens){
      const ids=screenIds(p).join(',');
      if(screens.dataset.islandScreenOptions!==ids){screens.dataset.islandScreenOptions=ids;screens.innerHTML=screenButtons(p,disabled);}
      screens.querySelectorAll('[data-island-screen]').forEach(button=>{
        button.setAttribute('aria-pressed',String(Number(button.dataset.islandScreen)===(p.screen??1)));
        syncSettingsControlBusy(button,disabled);
      });
    }
    const unavailable=home.querySelector('[data-island-screen-unavailable]');
    if(unavailable)unavailable.hidden=!(p.screen>0&&!screenIds(p).includes(p.screen));
    home.querySelectorAll('[data-island-expansion]').forEach(button=>{
      button.setAttribute('aria-pressed',String(p.hover===(button.dataset.islandExpansion==='hover')));
      syncSettingsControlBusy(button,disabled);
    });
    syncCompactWidgetPicker(home,state);
  }
  for(const [action,value] of [['toggle',p.enabled],...displayToggles().map(([a,key])=>[a,p[key]])]) {
    root.querySelectorAll(`[data-action="dynamicIsland-${action}"]`).forEach(button=>{
      button.classList.toggle('checked',value);button.setAttribute('aria-checked',String(value));
      syncSettingsControlBusy(button,disabled);
    });
  }
  const layer=root.querySelector('#dynamicIsland-layer');
  if(layer){
    syncSettingsControlBusy(layer,disabled);
    const value=p.topmost?'above':'desktop';
    const options=root.querySelector('#dynamicIsland-layer-options');
    options?.querySelectorAll('[data-select-value]').forEach(option=>{
      const selected=option.dataset.selectValue===value;option.setAttribute('aria-selected',String(selected));
      option.querySelector('span').textContent=selected?'✓':'';
      if(selected)layer.querySelector('span').textContent=option.childNodes[0].textContent;
      syncSettingsControlBusy(option,disabled);
    });
  }
  root.querySelectorAll('[data-action^="dynamicIsland-"]').forEach(button=>{
    const action=button.dataset.action.slice('dynamicIsland-'.length);
    if(['toggle','lock','hideFullscreen','topmost','hover','autoHide'].includes(action))return;
    syncSettingsControlBusy(button,disabled||(!['reset','resetAppearance'].includes(action)&&!p.enabled));
    if(['expand','collapse'].includes(action)) {
      button.dataset.action=`dynamicIsland-${p.expanded?'collapse':'expand'}`;
      button.textContent=p.expanded?t('收起','Collapse'):t('展开','Expand');
    }
  });
  const status=root.querySelector('[data-settings-scope="dynamicIsland"] .status');
  if(status){status.textContent=islandStatus(state);status.hidden=!status.textContent;status.setAttribute('role',state.dynamicIslandStatus==='error'?'alert':'status');}
}
