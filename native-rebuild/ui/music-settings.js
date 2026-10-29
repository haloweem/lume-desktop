import {DEFAULT_MUSIC_PREFERENCES,DEFAULT_SONNET_COLORS,SONNET_COLOR_KEYS} from './music-preference-values.js';
import {MUSIC_ICONS} from './music-icons.js';
import {MUSIC_VISUALIZERS} from './music-visualizer.js';
import {MUSIC_BACKGROUND_SOURCE_ICONS} from './music-wallpaper-gallery.js';
import {isMusicImportedFontFamily,validMusicFontLabel} from './music-font-assets.js';
export {createMusicFontPicker} from './music-font-picker.js';

const escape=value=>String(value).replace(/[&<>"']/g,ch=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[ch]));
const FONTS=[['','默认字体'],['Microsoft YaHei','微软雅黑'],['DengXian','等线'],['SimSun','宋体'],['SimHei','黑体'],['KaiTi','楷体'],['Arial','Arial'],['Segoe UI','Segoe UI']];
const INTENSITY={calm:'平静',normal:'标准',chaotic:'混乱'};
const EFFECTS=[['none','关闭'],['rain-window','雨窗'],['snow','雪天'],['star-river','星河']];
const COLORS=[['lyricsFontColor','字体颜色'],['lyricsKaraokeFillColor','逐字颜色填充']];
const SONNET_LABELS={lyricsFontColor:'字体颜色',karaokeFillColor:'逐字颜色填充',glowColor:'辉光颜色'};
export const DEFAULT_MUSIC_BACKGROUND=Object.freeze({loaded:false,pending:false,media:null,error:''});
const options=(entries,value)=>entries.map(([id,label])=>`<option value="${escape(id)}" ${id===value?'selected':''}>${escape(label)}</option>`).join('');
// Keep the renderer and saved preferences; temporarily hide its entry point.
const VISIBLE_VISUALIZERS=MUSIC_VISUALIZERS;
const visualizerOption=(mode,value,disabled)=>`<button type="button" data-music-visualizer="${escape(mode.id)}" role="option" aria-selected="${mode.id===value}" ${disabled?'disabled':''}><span>${escape(mode.label)}</span></button>`;
const visualizerLabel=value=>MUSIC_VISUALIZERS.find(mode=>mode.id===value)?.label||MUSIC_VISUALIZERS[0].label;

function colorRow(key,label,value,loaded,sonnet=false) {
  const field=sonnet?'data-music-sonnet-color':'data-music-preference';
  const reset=sonnet?'data-music-sonnet-reset':'data-music-color-reset';
  const text=sonnet?'data-music-sonnet-value':'data-music-color-value';
  return `<div class="MusicSettingRow MusicColorRow"><span>${label}</span><div class="MusicColorField"><span class="MusicColorValue" ${text}="${key}">${value.toUpperCase()}</span><button type="button" ${reset}="${key}" aria-label="重置${label}" title="重置${label}" ${!loaded||value==='#ffffff'?'disabled':''}>${MUSIC_ICONS.reset}</button><label class="MusicColorSwatch" style="--swatch:${value}" title="选择${label}"><input type="color" ${field}="${key}" value="${value}" aria-label="选择${label}" ${loaded?'':'disabled'}></label></div></div>`;
}

export function musicSettings({preferences=DEFAULT_MUSIC_PREFERENCES,loaded=false,pending=false}={},background=DEFAULT_MUSIC_BACKGROUND,tab='globalSettings') {
  const disabled=loaded?'':'disabled';
  const fonts=FONTS.some(([font])=>font===preferences.fontFamily)?FONTS:
    [...FONTS,[preferences.fontFamily,isMusicImportedFontFamily(preferences.fontFamily)?'已导入字体':preferences.fontFamily]];
  const sonnetColors=preferences.sonnetColors??DEFAULT_SONNET_COLORS;
  return `<section class="MusicSettings" data-music-settings aria-label="\u97f3\u4e50\u8bbe\u7f6e" aria-busy="${pending}" ${['controls','globalSettings'].includes(tab)?'':'hidden'}>
    <div class="MusicGlobalSettings" data-music-panel-content="globalSettings" ${tab==='globalSettings'?'':'hidden'}>
      <div class="MusicSettingRow MusicFontRow"><label for="music-font-quick">字体设置</label><div class="MusicQuickControls MusicFontControls"><select id="music-font" class="MusicModeProxy" data-music-preference="fontFamily" aria-label="歌词字体" aria-hidden="true" tabindex="-1" ${disabled}>${options(fonts,preferences.fontFamily)}</select><div class="MusicFontQuick"><button id="music-font-quick" type="button" class="MusicVisualizerTrigger" data-music-font-trigger="quick" ${disabled} aria-haspopup="listbox" aria-expanded="false" aria-label="常用字体">${escape(fonts.find(([font])=>font===preferences.fontFamily)?.[1]||'默认字体')}</button></div><button type="button" class="MusicFontChange" data-music-font-trigger ${disabled} aria-haspopup="dialog" aria-expanded="false" aria-label="更换字体">更换</button></div></div>
      <div class="MusicSettingRow"><label for="music-mode">歌词样式</label><div class="MusicQuickControls"><select id="music-mode" class="MusicModeProxy" data-music-preference="mode" aria-label="歌词样式" aria-hidden="true" tabindex="-1" ${disabled}>${options(VISIBLE_VISUALIZERS.map(mode=>[mode.id,mode.label]),preferences.mode)}</select><div class="MusicVisualizerPicker" data-music-visualizer-picker><button id="music-mode-picker" type="button" class="MusicVisualizerTrigger" data-music-visualizer-trigger aria-label="歌词样式" aria-haspopup="listbox" aria-expanded="false" ${disabled}>${escape(visualizerLabel(preferences.mode))}</button><div class="MusicVisualizerMenu" data-music-visualizer-menu role="listbox" aria-label="歌词样式" hidden>${VISIBLE_VISUALIZERS.map(mode=>visualizerOption(mode,preferences.mode,!loaded)).join('')}</div></div><button type="button" data-music-setting="intensity" aria-label="动画强度" ${disabled}>${INTENSITY[preferences.animationIntensity]}</button></div></div>
      <div class="MusicSettingRow"><label for="music-effect-picker">特效</label><select id="music-effect" class="MusicModeProxy" data-music-preference="effectMode" aria-label="特效" aria-hidden="true" tabindex="-1" ${disabled}>${options(EFFECTS,preferences.effectMode||'none')}</select><div class="MusicVisualizerPicker" data-music-effect-picker><button id="music-effect-picker" type="button" class="MusicVisualizerTrigger MusicEffectTrigger" data-music-effect-trigger aria-label="特效" aria-haspopup="listbox" aria-expanded="false" ${disabled}>${escape(EFFECTS.find(([id])=>id===preferences.effectMode)?.[1]||'关闭')}</button><div class="MusicVisualizerMenu" data-music-effect-menu role="listbox" aria-label="特效" hidden>${EFFECTS.map(([id,label])=>`<button type="button" data-music-effect="${id}" role="option" aria-selected="${id===(preferences.effectMode||'none')}" ${disabled}><span>${label}</span></button>`).join('')}</div></div></div>
      <div class="MusicBackgroundSetting" data-music-background-picker><div class="MusicSettingRow"><span>背景设置</span><span data-music-background-name>${escape(background.media?.name||'')}</span></div><div class="MusicBackgroundActions"><button type="button" data-music-background-trigger aria-haspopup="menu" aria-expanded="false" ${!background.loaded||background.pending?'disabled':''}>自选背景</button></div>
        <div class="MusicBackgroundSourceMenu" data-music-background-menu role="menu" hidden>
          <button type="button" role="menuitem" data-music-background-action="choose" ${!background.loaded||background.pending?'disabled':''}>${MUSIC_BACKGROUND_SOURCE_ICONS.folder}文件夹</button>
          <button type="button" role="menuitem" data-music-background-action="gallery" ${!background.loaded||background.pending?'disabled':''}>${MUSIC_BACKGROUND_SOURCE_ICONS.local}Lume 本地</button>
          <button type="button" role="menuitem" data-music-background-action="clear" aria-label="移除背景" ${!background.loaded||background.pending||!background.media?'disabled':''} ${background.media?'':'hidden'}>${MUSIC_ICONS.remove}移除背景</button>
        </div><p data-music-background-error role="alert" ${background.error?'':'hidden'}>${escape(background.error)}</p></div>
    </div>
    <div class="MusicColorSettings" data-music-panel-content="controls" ${tab==='controls'?'':'hidden'}>
      <div class="MusicColorGroup" data-music-color-group="global" ${['sonnet','lumiere'].includes(preferences.mode)?'hidden':''}>${COLORS.map(([key,label])=>colorRow(key,label,preferences[key],loaded)).join('')}</div>
      <div class="MusicColorGroup" data-music-color-group="sonnet" ${preferences.mode==='sonnet'?'':'hidden'}>${SONNET_COLOR_KEYS.map(key=>colorRow(key,SONNET_LABELS[key],sonnetColors[key],loaded,true)).join('')}</div>
      <p data-music-lumiere-colors ${preferences.mode==='lumiere'?'': 'hidden'}>绘光使用原版配色，文字随光线自然点亮。</p>
    </div>
  </section>`;
}

function setQuickPickerOpen(root,kind,open) {
  const picker=root?.querySelector?.(`[data-music-${kind}-picker]`);
  const trigger=picker?.querySelector(`[data-music-${kind}-trigger]`);
  const menu=picker?.querySelector(`[data-music-${kind}-menu]`);
  if(!picker||!trigger||!menu)return false;
  const next=Boolean(open)&&!trigger.disabled;
  picker.dataset.open=String(next);
  trigger.setAttribute('aria-expanded',String(next));
  menu.hidden=!next;
  if(next && kind==='background')positionMusicBackgroundPicker(root);
  return next;
}

export function positionMusicBackgroundPicker(root) {
  const picker=root?.querySelector?.('[data-music-background-picker]');
  const menu=picker?.querySelector('[data-music-background-menu]');
  const trigger=picker?.querySelector('[data-music-background-trigger]');
  const panel=picker?.closest('.MusicPlayerPanel');
  if(!panel||!trigger||!menu||menu.hidden)return;
  if(!picker.getClientRects().length){setMusicBackgroundPickerOpen(root,false);return;}
  menu.style.maxHeight='';
  const height=menu.getBoundingClientRect().height;
  const bounds=panel.getBoundingClientRect(),button=trigger.getBoundingClientRect(),origin=picker.getBoundingClientRect();
  const window=panel.ownerDocument.defaultView;
  const above=Math.max(0,button.top-Math.max(bounds.top+1,0)-8);
  const below=Math.max(0,Math.min(bounds.bottom-1,window.innerHeight)-button.bottom-8);
  const up=height>below&&above>below,space=up?above:below;
  menu.dataset.placement=up?'above':'below';
  menu.style.maxHeight=`${Math.floor(space)}px`;
  menu.style.top=`${up?button.top-origin.top-Math.min(height,Math.floor(space))-8:button.bottom-origin.top+8}px`;
}

export const setMusicVisualizerPickerOpen=(root,open)=>setQuickPickerOpen(root,'visualizer',open);
export const setMusicEffectPickerOpen=(root,open)=>setQuickPickerOpen(root,'effect',open);
export const setMusicBackgroundPickerOpen=(root,open)=>setQuickPickerOpen(root,'background',open);

export function closeMusicVisualizerPicker(root) {
  return setMusicVisualizerPickerOpen(root,false);
}

export function focusMusicVisualizerOption(root,value) {
  const option=[...root?.querySelectorAll?.('[data-music-visualizer]')||[]]
    .find(node=>node.dataset.musicVisualizer===value);
  option?.focus({preventScroll:true});
}

export function updateMusicBackground(root,background=DEFAULT_MUSIC_BACKGROUND) {
  const trigger=root.querySelector('[data-music-background-trigger]');
  if(trigger)trigger.disabled=!background.loaded||background.pending;
  if(!background.loaded||background.pending)setMusicBackgroundPickerOpen(root,false);
  for(const button of root.querySelectorAll('[data-music-background-action]'))
    button.disabled=!background.loaded||background.pending||(button.dataset.musicBackgroundAction==='clear'&&!background.media);
  const clear=root.querySelector('[data-music-background-action="clear"]');
  if(clear)clear.hidden=!background.media;
  const name=root.querySelector('[data-music-background-name]');
  if(name){name.textContent=background.media?.name||'';name.title=name.textContent;}
  const error=root.querySelector('[data-music-background-error]');
  if(error){error.hidden=!background.error;error.textContent=background.error||'';}
  positionMusicBackgroundPicker(root);
}

export function updateMusicSettings(root,state,{getFontLabel}={}) {
  const preferences=state.preferences;
  for(const fontTrigger of root.querySelectorAll('[data-music-font-trigger]'))fontTrigger.disabled=!state.loaded;
  const picker=root.querySelector('[data-music-visualizer-picker]');
  const trigger=picker?.querySelector('[data-music-visualizer-trigger]');
  if(trigger) {
    trigger.disabled=!state.loaded;
    trigger.textContent=visualizerLabel(preferences.mode);
    for(const option of picker.querySelectorAll('[data-music-visualizer]')) {
      option.disabled=!state.loaded;
      option.setAttribute('aria-selected',String(option.dataset.musicVisualizer===preferences.mode));
    }
    if(!state.loaded)closeMusicVisualizerPicker(root);
  }
  const effectPicker=root.querySelector('[data-music-effect-picker]');
  const effectTrigger=effectPicker?.querySelector('[data-music-effect-trigger]');
  if(effectTrigger) {
    effectTrigger.disabled=!state.loaded;
    effectTrigger.textContent=EFFECTS.find(([id])=>id===preferences.effectMode)?.[1]||'关闭';
    for(const option of effectPicker.querySelectorAll('[data-music-effect]')) {
      option.disabled=!state.loaded;
      option.setAttribute('aria-selected',String(option.dataset.musicEffect===(preferences.effectMode||'none')));
    }
    if(!state.loaded)setMusicEffectPickerOpen(root,false);
  }
  for(const field of root.querySelectorAll('[data-music-preference]')) {
    field.disabled=!state.loaded;
    const value=preferences[field.dataset.musicPreference];
    if(field.tagName==='SELECT' && ![...field.options].some(option=>option.value===value)) {
      const option=field.ownerDocument.createElement('option');option.value=value;option.textContent=value;field.append(option);
    }
    if(field.value!==value)field.value=value;
    if(field.dataset.musicPreference==='fontFamily')for(const option of field.options) {
      if(!isMusicImportedFontFamily(option.value))continue;
      const label=getFontLabel?.(option.value);
      option.textContent=validMusicFontLabel(label)?label:'已导入字体';
    }
    if(field.type==='color') {
      field.closest('.MusicColorSwatch').style.setProperty('--swatch',value);
      root.querySelector(`[data-music-color-value="${field.dataset.musicPreference}"]`).textContent=value.toUpperCase();
    }
  }
  const fontQuick=root.querySelector('[data-music-font-trigger="quick"]');
  if(fontQuick) {
    const field=root.querySelector('#music-font');
    fontQuick.textContent=field?.selectedOptions[0]?.textContent||'默认字体';
    fontQuick.title=fontQuick.textContent;
  }
  const sonnetColors=preferences.sonnetColors??DEFAULT_SONNET_COLORS;
  for(const field of root.querySelectorAll('[data-music-sonnet-color]')) {
    const value=sonnetColors[field.dataset.musicSonnetColor];
    field.disabled=!state.loaded;
    if(field.value!==value)field.value=value;
    field.closest('.MusicColorSwatch').style.setProperty('--swatch',value);
    root.querySelector(`[data-music-sonnet-value="${field.dataset.musicSonnetColor}"]`).textContent=value.toUpperCase();
  }
  for(const group of root.querySelectorAll('[data-music-color-group]'))
    group.hidden=preferences.mode==='lumiere'||(group.dataset.musicColorGroup==='sonnet')!==(preferences.mode==='sonnet');
  for(const button of root.querySelectorAll('[data-music-setting]'))button.disabled=!state.loaded;
  for(const button of root.querySelectorAll('[data-music-color-reset]'))button.disabled=!state.loaded||preferences[button.dataset.musicColorReset]==='#ffffff';
  for(const button of root.querySelectorAll('[data-music-sonnet-reset]'))button.disabled=!state.loaded||sonnetColors[button.dataset.musicSonnetReset]==='#ffffff';
  const lumiereColors=root.querySelector('[data-music-lumiere-colors]');
  if(lumiereColors)lumiereColors.hidden=preferences.mode!=='lumiere';
  const intensity=root.querySelector('[data-music-setting="intensity"]');
  if(intensity)intensity.textContent=INTENSITY[preferences.animationIntensity];
  root.querySelector('[data-music-settings]')?.setAttribute('aria-busy',String(state.pending));
}
