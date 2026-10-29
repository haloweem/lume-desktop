import {MUSIC_ICONS} from './music-icons.js';
import {musicSettings,updateMusicSettings,updateMusicBackground} from './music-settings.js';
import {musicProviderAccountPanel} from './music-provider-account.js';

export const MUSIC_PANEL_TABS=['cover','controls','globalSettings','account'];
const TABS=[['cover','封面','panelDisc'],['controls','控制','sliders'],['globalSettings','全局','settings'],['account','账户','user']];
const escape=value=>String(value??'').replace(/[&<>"']/g,ch=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[ch]));
const COPY_ATTRIBUTES={'data-music-cover-copy':'',role:'button',tabindex:'0',title:'复制歌曲信息','aria-label':'复制歌曲信息'};

// The four reachable UnifiedPanel tabs share one connected settings subtree.
export function musicPlayerPanel(state,view,preferences,background) {
  const tab=MUSIC_PANEL_TABS.includes(view.panelTab)?view.panelTab:'globalSettings';
  return `<aside class="MusicPlayerPanel" data-music-panel="lyrics" data-music-player-panel data-panel-tab="${tab}" aria-label="播放器面板">
    <div class="MusicPlayerPanelInner"><div class="MusicPanelCover">${MUSIC_ICONS.panelDisc}${state.current?.artwork?`<img data-music-image src="${escape(state.current.artwork)}" alt="专辑封面" decoding="async" draggable="false">`:''}</div>
      <div class="MusicPanelTabs" role="group" aria-label="播放器面板">${TABS.map(([id,label,icon])=>`<button type="button" data-music-panel-tab="${id}" aria-label="${label}" title="${label}" aria-pressed="${tab===id}">${MUSIC_ICONS[icon]}</button>`).join('')}</div>
      <div class="MusicPanelContent"><div class="MusicCoverDetails" data-music-panel-content="cover" ${tab==='cover'?'':'hidden'}><h2 data-music-cover-title ${state.current?Object.entries(COPY_ATTRIBUTES).map(([key,value])=>`${key}="${value}"`).join(' '):''}>${escape(state.current?.title||'暂无播放')}</h2><div class="MusicCoverMetadata"><p data-music-cover-artist>${escape(state.current?.artist)}</p><p data-music-cover-album>${escape(state.current?.album)}</p></div></div>
      ${musicSettings(preferences,background,tab)}
      <div class="MusicPanelAccount" data-music-panel-content="account" ${tab==='account'?'':'hidden'}>${musicProviderAccountPanel()}</div></div>
    </div></aside>`;
}

export function updateMusicPlayerPanel(root,{state,view,preferences,background,getFontLabel}) {
  const panel=root.matches?.('[data-music-player-panel]')?root:root.querySelector('[data-music-player-panel]');
  if(!panel)return;
  const tab=MUSIC_PANEL_TABS.includes(view.panelTab)?view.panelTab:'globalSettings';
  panel.dataset.panelTab=tab;
  for(const button of panel.querySelectorAll('[data-music-panel-tab]'))button.setAttribute('aria-pressed',String(button.dataset.musicPanelTab===tab));
  for(const content of panel.querySelectorAll('[data-music-panel-content]'))content.hidden=content.dataset.musicPanelContent!==tab;
  panel.querySelector('[data-music-settings]').hidden=!['controls','globalSettings'].includes(tab);
  for(const [key,value] of [['title',state.current?.title||'暂无播放'],['artist',state.current?.artist],['album',state.current?.album]])
    panel.querySelector(`[data-music-cover-${key}]`).textContent=value||'';
  const title=panel.querySelector('[data-music-cover-title]');
  for(const [key,value] of Object.entries(COPY_ATTRIBUTES)) {
    if(state.current)title.setAttribute(key,value);
    else title.removeAttribute(key);
  }
  const cover=panel.querySelector('.MusicPanelCover'),source=state.current?.artwork;
  let image=cover.querySelector('img');
  if(!source)image?.remove();
  else if(!image||image.getAttribute('src')!==source) {
    if(!image){image=panel.ownerDocument.createElement('img');image.dataset.musicImage='';image.alt='专辑封面';image.draggable=false;cover.append(image);}
    image.hidden=false;image.src=source;
  }
  updateMusicSettings(panel,preferences,{getFontLabel});
  updateMusicBackground(panel,background);
}
