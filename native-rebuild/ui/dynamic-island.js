import {toggle,escapeHTML as h} from './dom.js';
import {settingsFrame,syncSettingsControlBusy} from './settings-frame.js';
import {t} from './localization.js';

export function validIslandState(m) {
  const keys=['version','type','enabled','running','locked','expanded','focusing','finished','remaining',
    'hideFullscreen','topmost','hover','autoHide','size','material','loadStatus','error','result','saved'];
  return !!m && typeof m==='object' && !Array.isArray(m) && Object.keys(m).length===keys.length+(Object.hasOwn(m,'shape')?1:0) &&
    keys.every(key=>Object.hasOwn(m,key)) && m.version===1 && m.type==='dynamicIsland-state' &&
    ['enabled','running','locked','expanded','focusing','finished','saved','hideFullscreen','topmost','hover','autoHide'].every(key=>typeof m[key]==='boolean') &&
    islandSizes.includes(m.size) && islandMaterials.includes(m.material) &&
    (!Object.hasOwn(m,'shape')||['standard','capsule'].includes(m.shape)) &&
    Number.isInteger(m.remaining) && m.remaining>=0 && m.remaining<=1500 &&
    ['fresh','loaded','recovered','failed'].includes(m.loadStatus) && ['','runtime','save','rollback'].includes(m.error) &&
    ['snapshot','applied'].includes(m.result) && (!m.running || m.enabled) &&
    (!m.saved || (m.result==='applied' && !m.error && m.loadStatus==='loaded'));
}
export const islandSizes=['small','standard','large'];
export const islandMaterials=['none','liquid-glass','frosted-glass'];
export function validIslandPreview(m) {
  const keys=['version','type','mode','shape','material','size','width','height','image','error'];
  return !!m&&typeof m==='object'&&!Array.isArray(m)&&Object.keys(m).length===keys.length+(Object.hasOwn(m,'animating')?1:0)&&keys.every(key=>Object.hasOwn(m,key))&&
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
  ['autoHide','autoHide',t('无活动时自动隐藏','Auto-hide when idle'),t('闲置几秒后收到屏幕最上方，只露出一条边；鼠标移上去即滑出，切歌或暂停时也会短暂出现','After a few idle seconds it slides up against the top of the screen, leaving a sliver; hover to bring it back. It also peeks out on track changes')],
  ['hideFullscreen','hideFullscreen',t('全屏和游戏时隐藏','Hide for fullscreen apps and games'),t('全屏程序、游戏或演示在前台时立即让开，不遮挡画面','Steps aside at once while a fullscreen app, game or presentation is in front')],
  ['topmost','topmost',t('显示在窗口上方','Show above windows'),t('关闭后只在桌面上可见','When off, it is only visible on the desktop')],
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
function islandHome(state) {
  const p=state.dynamicIsland;
  const expanded=state.dynamicIslandPreviewMode==='expanded';
  return `<div class="island-home" data-island-home>
    <aside class="island-appearance" aria-label="${t('外观','Appearance')}">
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
    </aside>
    <div class="island-home__main">
    <section class="island-home__preview-shell" aria-label="${t('灵动岛实时预览','Dynamic Island live preview')}">
      <div class="island-home__preview-stage ${expanded?'is-expanded':'is-compact'}" data-island-preview-stage data-island-shape="${p.shape||'capsule'}" data-island-size="${p.size}" data-island-material="${p.material}">
        <img class="island-home__native-preview" data-island-native-preview alt="${t('灵动岛原生渲染预览','Native island preview')}" hidden>
        <span class="island-home__preview-wait" data-island-preview-wait>${t('正在读取桌面预览…','Loading desktop preview…')}</span>
        <div class="island-home__preview-stage-modes" role="group" aria-label="${t('预览状态','Preview state')}">
          <button type="button" data-island-preview-mode="compact" aria-pressed="${!expanded}">${t('收缩','Collapse')}</button>
          <button type="button" data-island-preview-mode="expanded" aria-pressed="${expanded}">${t('展开','Expand')}</button>
        </div>
      </div>
    </section>
    <div class="island-home__components-heading">
      <h3><svg viewBox="0 0 16 16" aria-hidden="true"><rect x="2" y="2" width="4" height="4" rx=".8"/><rect x="10" y="2" width="4" height="4" rx=".8"/><rect x="2" y="10" width="4" height="4" rx=".8"/><rect x="10" y="10" width="4" height="4" rx=".8"/></svg><span data-island-components-title>${expanded?t('展开组件','Expanded components'):t('收缩组件','Collapsed components')}</span></h3>
      <p>${t('点击标签加入或移出，拖动排序','Click a tag to add or remove; drag to reorder')}</p>
    </div>
    ${['expanded','compact'].map(mode=>`<div class="island-home__state-grid" data-island-layout="${mode}" role="group" aria-label="${mode==='expanded'?t('展开布局','Expanded layout'):t('收缩布局','Collapsed layout')}" ${(mode==='expanded')===expanded?'':'hidden'}>
      <section class="island-home__state-box" data-island-membership="inside" aria-label="${t('岛内','In the island')}">
        <h3>${t('岛内','In the island')}</h3>
      </section>
      <section class="island-home__state-box" data-island-membership="available" aria-label="${t('未加入','Not added')}">
        <h3>${t('未加入','Not added')}</h3>
      </section>
    </div>`).join('')}
    </div>
  </div>`;
}
export function islandPage(state,tab='settings') {
  const p=state.dynamicIsland,disabled=busy(state);
  const button=(action,label,inactive=false)=>`<button type="button" class="reset" data-action="dynamicIsland-${action}" ${inactive?'disabled':''}>${label}</button>`;
  const preferences=
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
      const matches=frame&&!frame.error&&frame.mode===(expanded?'expanded':'compact')&&frame.shape===(p.shape||'capsule')&&frame.material===p.material&&frame.size===p.size;
      presentPreview(stage,frame,matches);
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
    home.querySelectorAll('[data-island-layout]').forEach(layout=>{
      layout.hidden=layout.dataset.islandLayout!==(expanded?'expanded':'compact');
    });
    const componentsTitle=home.querySelector('[data-island-components-title]');
    if(componentsTitle)componentsTitle.textContent=expanded?t('展开组件','Expanded components'):t('收缩组件','Collapsed components');
  }
  for(const [action,value] of [['toggle',p.enabled],...displayToggles().map(([a,key])=>[a,p[key]])]) {
    root.querySelectorAll(`[data-action="dynamicIsland-${action}"]`).forEach(button=>{
      button.classList.toggle('checked',value);button.setAttribute('aria-checked',String(value));
      syncSettingsControlBusy(button,disabled);
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
