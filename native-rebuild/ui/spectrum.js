import {escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect} from './settings-frame.js';
import {settingsRange,syncSettingsRange} from './settings-range.js';
import {t} from './localization.js';

// Each style keeps its own bar settings; the sliders edit the style in use.
const styles=()=>[['pulse',t('样式一','Style 1')],['blocks',t('样式二','Style 2')],['waves',t('样式三','Style 3')]];
const px=t(' px',' px');
const ranges=s=>[
  ['spectrum-size',t('组件大小','Widget size'),s.scale,50,300,'%','size'],
  ['spectrum-bar-width',t('线条宽度','Bar width'),s[s.style].barWidth,2,24,px,`${s.style}.barWidth`],
  ['spectrum-bar-gap',t('线条间隔','Bar spacing'),s[s.style].barGap,0,12,px,`${s.style}.barGap`],
];

export function spectrumStatus(state) {
  if(state.spectrumStatus==='saving')return t('正在保存...','Saving...');
  if(state.spectrumStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.spectrumStatus==='unavailable')return t('桌面宿主未连接，频谱设置未保存','Desktop host disconnected. Spectrum settings were not saved.');
  if(state.spectrumStatus==='error')return {
    runtime:t('音频频谱启动失败，请检查声音输出设备','Could not start the audio spectrum. Check your sound output device.'),
    layer:t('无法把音频频谱放到桌面层，可能有更高权限的覆盖窗口；请关闭后重试','Could not place the audio spectrum on the desktop layer. A higher-privilege overlay may block it; close it and try again.'),
    save:t('频谱设置保存失败，已恢复此前设置，请重试','Could not save spectrum settings. Previous settings restored. Please try again.'),
    rollback:t('音频频谱恢复失败，请重新启用组件','Could not restore the audio spectrum. Enable it again.'),
  }[state.spectrumError] || t('频谱配置读取失败，当前使用默认设置；尚未保存','Could not load spectrum settings. Using defaults; not yet saved.');
  if(state.spectrumLoadStatus==='recovered')return t('已从备份恢复频谱配置，尚未保存到主配置','Spectrum settings restored from backup; not yet saved to the main configuration.');
  return '';
}
export function spectrumNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.spectrumStatus))return spectrumStatus(state);
  if(state.spectrumStatus==='saved'&&previousStatus==='saving')return t('频谱设置已应用并保存','Spectrum settings applied and saved');
  return null;
}
export function spectrumPage(state,tab='settings') {
  const description=t('跟随电脑正在播放的声音，在桌面显示彩色音频频谱。','Show a colourful audio spectrum of whatever your computer is playing.');
  const disabled=state.spectrumStatus==='saving',s=state.spectrum;
  const body=tab==='settings'?
    `<div class="setting-row"><label for="spectrum-style">${t('音频样式','Spectrum style')}</label>${settingsSelect('spectrum-style',t('音频样式','Spectrum style'),s.style,styles(),disabled)}</div>`+
    ranges(s).map(([id,label,value,min,max,unit,key])=>`<div class="setting-row">${settingsRange(id,label,value,min,max,unit,disabled,'spectrum',key)}</div>`).join('')+
    `<div class="reset-row"><button class="reset" data-action="spectrum-reset" ${disabled?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About this widget')}</h3><p>${h(description)}</p><p>${t('频谱读取系统正在输出的声音，任何播放器都能驱动；没有声音时样式一只剩一条线，样式二留一排短条，样式三留一排小圆点。拖动组件移动位置，拖动边缘或用「组件大小」调整大小；右键可切换音频样式、调整大小或锁定。每种样式的线条宽度和间隔分别保存。','The spectrum listens to the system sound output, so any player drives it. Drag to move; drag an edge or use Widget size to resize. Right-click to switch style, resize or lock. Each style keeps its own bar width and spacing.')}</p></div>`;
  return settingsFrame({title:t('音频频谱','Audio spectrum'),description,tab,body,status:spectrumStatus(state),
    error:state.spectrumStatus==='error',scope:'spectrum'});
}
export function syncSpectrumControls(root,state) {
  const disabled=state.spectrumStatus==='saving';
  root.querySelectorAll('[data-action="spectrum-toggle"]').forEach(button=>{
    button.classList.toggle('checked',state.spectrum.enabled);
    button.setAttribute('aria-checked',String(state.spectrum.enabled));button.disabled=disabled;
  });
  syncSettingsSelect(root,'spectrum-style',state.spectrum.style,styles(),disabled);
  for(const [id,,value,min,max,unit,key] of ranges(state.spectrum)) {
    // The width and spacing sliders follow the style in use.
    const slider=root.querySelector(`#${id}`);if(slider)slider.dataset.numericKey=key;
    syncSettingsRange(root,id,value,min,max,unit,disabled);
  }
  const reset=root.querySelector('[data-action="spectrum-reset"]');if(reset)reset.disabled=disabled;
  const status=root.querySelector('[data-settings-scope="spectrum"] .status');
  if(status) {
    status.textContent=spectrumStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.spectrumStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.spectrumStatus==='error');
  }
}
