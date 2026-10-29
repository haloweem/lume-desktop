import {escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect} from './settings-frame.js';
import {t} from './localization.js';

const materials=()=>[['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
export function almanacStatus(state) {
  if(state.almanacStatus==='saving')return t('正在保存...','Saving...');
  if(state.almanacStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.almanacStatus==='unavailable')return t('桌面宿主未连接，黄历设置未保存','Desktop host disconnected. Almanac settings were not saved.');
  if(state.almanacStatus==='error')return {
    runtime:t('黄历组件启动失败，请检查安装文件','Could not start the almanac widget. Check the installation files.'),
    layer:t('无法把黄历组件放到桌面层，可能有更高权限的覆盖窗口；请关闭后重试','Could not place the almanac widget on the desktop layer. A higher-privilege overlay may block it; close it and try again.'),
    save:t('黄历设置保存失败，已恢复此前设置，请重试','Could not save almanac settings. Previous settings restored. Please try again.'),
    rollback:t('黄历组件恢复失败，请重新启用组件','Could not restore the almanac widget. Enable it again.'),
  }[state.almanacError] || t('黄历配置读取失败，当前使用默认设置；尚未保存','Could not load almanac settings. Using defaults; not yet saved.');
  if(state.almanacLoadStatus==='recovered')return t('已从备份恢复黄历配置，尚未保存到主配置','Almanac settings restored from backup; not yet saved to the main configuration.');
  if(state.almanacMaterialStatus==='transparent-fallback')return t('材质暂不可用，当前为透明显示','Material unavailable. Using a transparent fallback.');
  return '';
}
export function almanacNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.almanacStatus))return almanacStatus(state);
  if(state.almanacStatus==='saved'&&previousStatus==='saving')return t('黄历设置已应用并保存','Almanac settings applied and saved');
  return null;
}
export function almanacPage(state,tab='settings') {
  const description=t('在桌面查看农历、干支与每日宜忌。','View the lunar calendar, heavenly stems and earthly branches, and daily auspicious and inauspicious activities.');
  const disabled=state.almanacStatus==='saving',p=state.almanac;
  const body=tab==='settings'?
    `<div class="setting-row"><label for="almanac-material">${t('材质设置','Material')}</label>${settingsSelect('almanac-material',t('材质设置','Material'),p.material,materials(),disabled)}</div>`+
    `<div class="reset-row"><button class="reset" data-action="almanac-reset" ${disabled?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About this widget')}</h3><p>${h(description)}</p><p>${t('日期随本地时间跨日更新。宜忌内容可横向滚动，组件可拖动、调整大小或锁定。支持 1900—2100 年。','The date updates at local midnight. Scroll activities horizontally. Drag, resize or lock the widget. Supports 1900-2100.')}</p></div>`;
  return settingsFrame({title:t('黄历','Almanac'),description,tab,body,status:almanacStatus(state),error:state.almanacStatus==='error',scope:'almanac'});
}
export function syncAlmanacControls(root,state) {
  const disabled=state.almanacStatus==='saving';
  root.querySelectorAll('[data-action="almanac-toggle"]').forEach(button=>{
    button.classList.toggle('checked',state.almanac.enabled);
    button.setAttribute('aria-checked',String(state.almanac.enabled));button.disabled=disabled;
  });
  syncSettingsSelect(root,'almanac-material',state.almanac.material,materials(),disabled);
  const reset=root.querySelector('[data-action="almanac-reset"]');if(reset)reset.disabled=disabled;
  const status=root.querySelector('[data-settings-scope="almanac"] .status');
  if(status) {
    status.textContent=almanacStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.almanacStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.almanacStatus==='error');
  }
}
