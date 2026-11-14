import {t,getLanguage} from './localization.js';
import {expandedWidgets,expandedWidgetIcon} from './expanded-widgets.js';
import {syncSettingsControlBusy} from './settings-frame.js';

// Thumbnails are bundled images rendered by the desktop component. Browsing
// styles never starts a live preview or changes the enabled widget layout.
const widgetIds=[0,1,4,5,3];
export const islandStyleWidgets=widgetIds.map(id=>expandedWidgets[id][0]);
const selectedWidget=state=>islandStyleWidgets.includes(state.islandStyleWidget)?state.islandStyleWidget:'clock';
const styles=widget=>widget==='clock'
  ? [['simple',t('简约时间','Simple clock'),'clock-simple'],['progress',t('时间进度','Time progress'),'clock-progress'],
    ['digital',t('数码时钟','Digital clock'),'clock-digital'],['date',t('大字日期','Time and date'),'clock-date'],
    ['flip',t('翻页时钟','Flip clock'),'clock-flip'],['pixel',t('像素时钟','Pixel clock'),'clock-pixel']]
  : widget==='calendar-weather'
    ? [['default',t('默认样式','Default style'),widget],['chinese-almanac',t('中华万年历','Chinese almanac'),'calendar-chinese-almanac']]
    : [['default',t('默认样式','Default style'),widget]];
const label=id=>t(expandedWidgets[id][1],expandedWidgets[id][2]);
const busy=state=>['loading','saving','unavailable'].includes(state.dynamicIslandStatus);
const chosenStyle=(state,widget)=>widget==='clock'?(state.dynamicIsland.clockStyle??'simple'):
  widget==='calendar-weather'?(state.dynamicIsland.calendarStyle??'default'):'default';

function styleCards(state,widget) {
  return styles(widget).map(([id,name,asset])=>{
    const selected=chosenStyle(state,widget)===id;
    const action=widget==='clock'?`data-island-clock-style="${id}"`:
      widget==='calendar-weather'?`data-island-calendar-style="${id}"`:'';
    return `<button type="button" class="island-style-card" data-island-style-card="${id}" ${action} aria-pressed="${selected}" ${busy(state)?'disabled':''}>
      <span class="island-style-card__image"><img src="assets/island-styles/${asset}${getLanguage()==='en'?'-en':''}.png" alt="" draggable="false"></span>
      <span class="island-style-card__caption"><span>${name}</span><span class="island-style-card__check" aria-hidden="true">✓</span></span>
    </button>`;
  }).join('');
}

export function islandStyles(state) {
  const selected=selectedWidget(state),id=widgetIds.find(id=>expandedWidgets[id][0]===selected);
  return `<section class="island-styles" data-island-styles aria-labelledby="island-styles-title">
    <h2 id="island-styles-title">${t('组件样式','Widget styles')}</h2>
    <div class="island-styles__body">
      <div class="island-styles__categories" role="group" aria-label="${t('选择组件','Choose widget')}">
        ${widgetIds.map(id=>`<button type="button" data-island-style-widget="${expandedWidgets[id][0]}" aria-label="${label(id)}" title="${label(id)}" aria-pressed="${selected===expandedWidgets[id][0]}">${expandedWidgetIcon(id)}</button>`).join('')}
      </div>
      <div class="island-styles__content">
        <h3 data-island-style-title>${label(id)}</h3>
        <div class="island-styles__cards" data-island-style-cards="${selected}">${styleCards(state,selected)}</div>
      </div>
    </div>
  </section>`;
}

export function syncIslandStyles(root,state) {
  const section=root.querySelector('[data-island-styles]');if(!section)return;
  const selected=selectedWidget(state),cards=section.querySelector('[data-island-style-cards]');
  section.querySelectorAll('[data-island-style-widget]').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.islandStyleWidget===selected)));
  if(cards.dataset.islandStyleCards!==selected){
    cards.dataset.islandStyleCards=selected;cards.innerHTML=styleCards(state,selected);
    section.querySelector('[data-island-style-title]').textContent=label(widgetIds.find(id=>expandedWidgets[id][0]===selected));
  }
  cards.querySelectorAll('[data-island-style-card]').forEach(button=>{
    const chosen=button.dataset.islandStyleCard===chosenStyle(state,selected);
    button.setAttribute('aria-pressed',String(chosen));
    syncSettingsControlBusy(button,busy(state));
    button.toggleAttribute('data-island-pending',state.dynamicIslandStatus==='saving');
  });
}
