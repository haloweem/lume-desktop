import {escapeHTML} from './dom.js';
import {t,getLanguage} from './localization.js';
import {expandedWidgets,islandCategoryIcon} from './expanded-widgets.js';
import {syncSettingsControlBusy} from './settings-frame.js';

// Thumbnails are bundled images rendered by the desktop component. Browsing
// styles never starts a live preview or changes the enabled widget layout.
const categories=[...([0,1,4,5,3,6].map(id=>expandedWidgets[id]))];
export const islandStyleWidgets=categories.map(([key])=>key);
const selectedWidget=state=>islandStyleWidgets.includes(state.islandStyleWidget)?state.islandStyleWidget:'clock';
function newYearPreview(){
  const now=new Date();
  const target=[now.getFullYear(),String(now.getMonth()+1).padStart(2,'0'),String(now.getDate()).padStart(2,'0')].join('-');
  const today=Date.UTC(now.getFullYear(),now.getMonth(),now.getDate());
  const digits='〇一二三四五六七八九';
  const chineseNumber=value=>value<10?digits[value]:`${value>=20?digits[Math.floor(value/10)]:''}十${value%10?digits[value%10]:''}`;
  const year=String(now.getFullYear()).replace(/\d/g,digit=>digits[Number(digit)]);
  const day=now.getDate()>20&&now.getDate()<30?`廿${digits[now.getDate()%10]}`:chineseNumber(now.getDate());
  return {remaining:Math.round((Date.UTC(now.getFullYear()+1,0,1)-today)/86400000),target,nextYear:now.getFullYear()+1,
    chineseDate:`${year}年${chineseNumber(now.getMonth()+1)}月${day}`};
}
const styles=widget=>widget==='clock'
  ? [['simple',t('简约时间','Simple clock'),'clock-simple'],['progress',t('时间进度','Time progress'),'clock-progress'],
    ['digital',t('数码时钟','Digital clock'),'clock-digital'],['date',t('大字日期','Time and date'),'clock-date'],
    ['flip',t('翻页时钟','Flip clock'),'clock-flip'],['pixel',t('像素时钟','Pixel clock'),'clock-pixel']]
  : widget==='calendar-weather'
    ? [['default',t('默认样式','Default style'),widget],['chinese-almanac',t('中华万年历','Chinese almanac'),'calendar-chinese-almanac']]
    : widget==='countdown'
      ? [['standard',t('默认样式','Default style')],['violet',t('纯色样式','Solid color style')]]
    : [['default',t('默认样式','Default style'),widget]];
function categoryLabel(key){
  const [,zh,en]=categories.find(([name])=>name===key);
  return t(zh,en);
}
const busy=state=>['loading','saving'].includes(state.dynamicIslandStatus);
const chosenStyle=(state,widget)=>widget==='clock'?(state.dynamicIsland.clockStyle??'simple'):
  widget==='calendar-weather'?(state.dynamicIsland.calendarStyle??'default'):
    widget==='countdown'?(state.dynamicIsland.countdownStyle??'standard'):'default';

function styleCards(state,widget) {
  return styles(widget).map(([id,name,asset])=>{
    const selected=chosenStyle(state,widget)===id;
    const action=widget==='clock'?`data-island-clock-style="${id}"`:
      widget==='calendar-weather'?`data-island-calendar-style="${id}"`:widget==='countdown'?`data-island-countdown-style="${id}"`:'';
    const imageClass=widget==='countdown'?'island-style-card__image island-style-card__image--countdown':'island-style-card__image';
    let image=`<img src="assets/island-styles/${asset}${getLanguage()==='en'?'-en':''}.png" alt="" draggable="false">`;
    if(widget==='countdown'){
      const preview=newYearPreview();
      if(id==='violet'){
        const dateLabel=preview.chineseDate;
        const intervalLabel=t('还有'+preview.remaining+'天',preview.remaining+' days remaining');
        image=`<span class="island-countdown-preview island-countdown-preview--violet" aria-hidden="true">
          <span class="island-countdown-preview__days"><strong>${preview.remaining}</strong><span>${t('天','days')}</span></span>
          <span class="island-countdown-preview__details"><span class="island-countdown-preview__event">${t('新年','New Year')}</span><span class="island-countdown-preview__date">${escapeHTML(dateLabel)}</span><span class="island-countdown-preview__interval">${escapeHTML(intervalLabel)}</span></span>
        </span>`;
      } else {
        const dateLabel=t('日期：','Date: ')+preview.target;
        image=`<span class="island-countdown-preview" aria-hidden="true">
          <span class="island-countdown-preview__title">${t('新年','New Year')}</span>
          <span class="island-countdown-preview__number">${preview.remaining}</span>
          <span class="island-countdown-preview__date">${escapeHTML(dateLabel)}</span></span>`;
      }
    }
    return `<button type="button" class="island-style-card" data-island-style-card="${id}" ${action} aria-pressed="${selected}" ${busy(state)?'disabled':''}>
      <span class="${imageClass}">${image}</span>
      <span class="island-style-card__caption"><span>${name}</span><span class="island-style-card__check" aria-hidden="true">✓</span></span>
    </button>`;
  }).join('');
}

export function islandStyles(state) {
  const selected=selectedWidget(state);
  return `<section class="island-styles" data-island-styles aria-labelledby="island-styles-title">
    <h2 id="island-styles-title">${t('组件样式','Widget styles')}</h2>
    <div class="island-styles__body">
      <div class="island-styles__categories" role="group" aria-label="${t('选择组件','Choose widget')}">
        ${categories.map(([key,zh,en])=>`<button type="button" data-island-style-widget="${key}" aria-label="${t(zh,en)}" title="${t(zh,en)}" aria-pressed="${selected===key}">${islandCategoryIcon(key)}</button>`).join('')}
      </div>
      <div class="island-styles__content">
        <h3 data-island-style-title>${categoryLabel(selected)}</h3>
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
    section.querySelector('[data-island-style-title]').textContent=categoryLabel(selected);
  }
  cards.querySelectorAll('[data-island-style-card]').forEach(button=>{
    const chosen=button.dataset.islandStyleCard===chosenStyle(state,selected);
    button.setAttribute('aria-pressed',String(chosen));
    syncSettingsControlBusy(button,busy(state));
    button.toggleAttribute('data-island-pending',state.dynamicIslandStatus==='saving');
  });
}
