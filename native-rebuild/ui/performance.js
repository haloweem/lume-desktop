import {escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect} from './settings-frame.js';
import {t} from './localization.js';

const materials=()=>[['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
export function performanceStatus(state) {
  if(state.performanceStatus==='saving')return '';
  if(state.performanceStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.performanceStatus==='unavailable')return t('桌面宿主未连接，性能设置未保存','Desktop host disconnected. Performance settings were not saved.');
  if(state.performanceStatus==='error')return {
    runtime:t('性能组件启动失败，请检查安装文件','Could not start the performance widget. Check the installation files.'),
    layer:t('无法把性能组件放到桌面层，可能有更高权限的覆盖窗口；请关闭后重试','Could not place the performance widget on the desktop layer. A higher-privilege overlay may block it; close it and try again.'),
    save:t('性能设置保存失败，已恢复此前设置，请重试','Could not save performance settings. Previous settings restored. Please try again.'),
    rollback:t('性能组件恢复失败，请重新启用组件','Could not restore the performance widget. Enable it again.'),
  }[state.performanceError] || t('性能配置读取失败，当前使用默认设置；尚未保存','Could not load performance settings. Using defaults; not yet saved.');
  if(state.performanceLoadStatus==='recovered')return t('已从备份恢复性能配置，尚未保存到主配置','Performance settings restored from backup; not yet saved to the main configuration.');
  if(state.performanceMaterialStatus==='transparent-fallback')return t('当前使用兼容材质','Using a compatible material.');
  return '';
}
export function performanceNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.performanceStatus))return performanceStatus(state);
  if(state.performanceStatus==='saved'&&previousStatus==='saving')return t('性能设置已应用并保存','Performance settings applied and saved');
  return null;
}
export function performancePage(state,tab='settings') {
  const description=t('在桌面查看 CPU、GPU、内存与网络状态。','View CPU, GPU, memory and network status on your desktop.');
  const disabled=state.performanceStatus==='saving',p=state.performance;
  const body=tab==='settings'?
    `<div class="setting-row"><label for="performance-material">${t('材质设置','Material')}</label>${settingsSelect('performance-material',t('材质设置','Material'),p.material,materials(),disabled)}</div>`+
    `<div class="reset-row"><button class="reset" data-action="performance-reset" ${disabled?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About this widget')}</h3><p>${h(description)}</p><p>${t('启用后每秒更新。组件可拖动、调整大小或锁定；关闭后停止采样。暂时无法获取的数据以 -- 显示。','Updates once per second while enabled. Drag, resize or lock the widget. Sampling stops when disabled. Unavailable readings are shown as --.')}</p></div>`;
  return settingsFrame({title:t('性能','Performance'),description,tab,
    body,status:performanceStatus(state),
    error:state.performanceStatus==='error',scope:'performance'});
}
export function syncPerformanceControls(root,state) {
  const disabled=state.performanceStatus==='saving';
  root.querySelectorAll('[data-action="performance-toggle"]').forEach(button=>{
    button.classList.toggle('checked',state.performance.enabled);
    button.setAttribute('aria-checked',String(state.performance.enabled));button.disabled=disabled;
  });
  syncSettingsSelect(root,'performance-material',state.performance.material,materials(),disabled);
  const reset=root.querySelector('[data-action="performance-reset"]');if(reset)reset.disabled=disabled;
  const status=root.querySelector('[data-settings-scope="performance"] .status');
  if(status) {
    status.textContent=performanceStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.performanceStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.performanceStatus==='error');
  }
}
